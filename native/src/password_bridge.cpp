#include "password_bridge.h"

#include <windows.h>

#include <string>

#include "browser_client.h"
#include "include/cef_parser.h"
#include "dialog_text.h"
#include "home_page.h"
#include "password_store.h"
#include "resource.h"
#include "site_settings.h"
#include "strings.h"

namespace mtk::password_bridge {
namespace {

// Bounds on what a page may send. The values also bound what will be stored.
constexpr size_t kMaxRequestBytes = 16 * 1024;

CefRefPtr<CefMessageRouterBrowserSide> g_browser_router;
CefRefPtr<CefMessageRouterRendererSide> g_renderer_router;

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

// Use CEF's JSON reader so whitespace, Unicode escapes and malformed requests
// have the same meaning as in the renderer. Bound decoded fields separately.
bool ReadField(const std::string& request, const char* key, size_t max_length,
               std::string* value) {
  value->clear();
  const auto parsed = CefParseJSON(request, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return false;
  const auto dictionary = parsed->GetDictionary();
  if (dictionary->GetType(key) != VTYPE_STRING) return false;
  *value = dictionary->GetString(key).ToString();
  return value->size() <= max_length;
}

std::string EscapeJsonString(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const unsigned char character : value) {
    switch (character) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (character < 0x20) {
          // Control characters cannot appear literally in a JSON string.
          static constexpr char kDigits[] = "0123456789abcdef";
          escaped += "\\u00";
          escaped.push_back(kDigits[character >> 4]);
          escaped.push_back(kDigits[character & 0x0F]);
        } else {
          escaped.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  return escaped;
}

// Owner window of the browser, so a save prompt is modal to the right window.
HWND OwnerWindow(CefRefPtr<CefBrowser> browser) {
  if (!browser) return nullptr;
  HWND view = browser->GetHost()->GetWindowHandle();
  if (!view) return nullptr;
  HWND parent = GetAncestor(view, GA_ROOT);
  return parent ? parent : view;
}

struct AccountPickerContext {
  const std::vector<std::wstring>* names = nullptr;
  // Set to the chosen row on IDOK; left at -1 otherwise.
  ptrdiff_t selected = -1;
};

INT_PTR CALLBACK AccountPickerDialogProc(HWND dialog, UINT message,
                                        WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_ACCOUNT_PICKER_INTRO, mtk::strings::Id::kAccountPickerIntro},
          {IDOK, mtk::strings::Id::kFill},
          {IDCANCEL, mtk::strings::Id::kCancel},
      };
      mtk::dialog_text::ApplyCaption(dialog,
                                     mtk::strings::Id::kAccountPickerTitle);
      mtk::dialog_text::Apply(dialog, kText);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      auto* context = reinterpret_cast<AccountPickerContext*>(l_param);
      if (context && context->names) {
        HWND list = GetDlgItem(dialog, IDC_ACCOUNT_PICKER_LIST);
        for (const auto& name : *context->names) {
          SendMessageW(list, LB_ADDSTRING, 0,
                      reinterpret_cast<LPARAM>(name.c_str()));
        }
        if (!context->names->empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
      }
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* context = reinterpret_cast<AccountPickerContext*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        if (context) {
          HWND list = GetDlgItem(dialog, IDC_ACCOUNT_PICKER_LIST);
          const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
          if (row != LB_ERR) context->selected = row;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

// Asks which account to fill when several are saved for one origin. Returns
// -1 when the dialog was cancelled, could not be shown, or nothing was
// selected, otherwise the index into `candidates` the person picked.
ptrdiff_t PickAccount(HWND owner,
                      const std::vector<mtk::passwords::Credential>& candidates) {
  std::vector<std::wstring> names;
  names.reserve(candidates.size());
  for (const auto& credential : candidates) names.push_back(credential.user_name);
  AccountPickerContext context{&names, -1};
  const INT_PTR result = DialogBoxParamW(
      reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE)),
      MAKEINTRESOURCEW(IDD_ACCOUNT_PICKER), owner,
      AccountPickerDialogProc, reinterpret_cast<LPARAM>(&context));
  if (result != IDOK) return -1;
  return context.selected;
}

class QueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
               int64_t /*query_id*/, const CefString& request,
               bool /*persistent*/,
               CefRefPtr<Callback> callback) override {
    if (!browser || !frame || !frame->IsValid() || !frame->IsMain() ||
        !callback) return false;
    const std::string body = request.ToString();
    if (body.size() > kMaxRequestBytes) return false;
    std::string channel;
    if (!ReadField(body, "mtk", 8, &channel) || channel != "password") return false;

    // The origin comes from the frame, never from the request, so a page cannot
    // ask about another site.
    const std::string page_url = frame->GetURL().ToString();
    const std::string origin = mtk::passwords::OriginOf(page_url);
    if (origin.empty() || !mtk::passwords::IsStorableUrl(page_url)) {
      callback->Failure(1, "unsupported origin");
      return true;
    }

    std::string action;
    if (!ReadField(body, "t", 8, &action)) {
      callback->Failure(2, "bad request");
      return true;
    }

    // Native modal dialogs pump messages. Do not allow another page query to
    // re-enter the handler and nest prompts while one is already open.
    if (handling_query_) {
      callback->Failure(5, "busy");
      return true;
    }
    struct Guard {
      bool& flag;
      explicit Guard(bool& value) : flag(value) { flag = true; }
      ~Guard() { flag = false; }
    } guard(handling_query_);
    if (action == "get") return HandleGet(browser, frame, origin, callback);
    if (action == "save") {
      if (!browser->GetHost()->GetRequestContext()->IsGlobal()) {
        callback->Failure(1, "private session");
        return true;
      }
      return HandleSave(browser, origin, body, callback);
    }
    callback->Failure(2, "bad request");
    return true;
  }

 private:
  bool handling_query_ = false;
  bool HandleGet(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                const std::string& origin,
                CefRefPtr<Callback> callback) {
    mtk::passwords::Credential credential;
    if (!mtk::passwords::Store::Get().SingleFor(origin, &credential)) {
      // Exactly one match would have succeeded above, so either nothing is
      // saved here, or several accounts are and the person has to choose.
      const auto candidates = mtk::passwords::Store::Get().AllFor(origin);
      if (candidates.size() < 2) {
        callback->Failure(3, "no credential");
        return true;
      }
      const ptrdiff_t chosen = PickAccount(OwnerWindow(browser), candidates);
      if (chosen < 0 || static_cast<size_t>(chosen) >= candidates.size()) {
        // Cancelled: declining to fill is not a failure worth reporting to the
        // page, which would otherwise be free to react to it.
        callback->Failure(3, "no credential");
        return true;
      }
      credential = candidates[static_cast<size_t>(chosen)];
    }
    if (!frame->IsValid() ||
        mtk::passwords::OriginOf(frame->GetURL().ToString()) != origin ||
        !mtk::passwords::IsStorableUrl(frame->GetURL().ToString())) {
      callback->Failure(3, "no credential");
      return true;
    }
    std::wstring password;
    if (!mtk::passwords::Store::Get().Reveal(origin, credential.user_name,
                                            &password)) {
      callback->Failure(4, "unavailable");
      return true;
    }
    const std::string response =
        std::string("{\"u\":\"") +
        EscapeJsonString(WideToUtf8(credential.user_name)) + "\",\"p\":\"" +
        EscapeJsonString(WideToUtf8(password)) + "\"}";
    mtk::passwords::Scrub(&password);
    callback->Success(response);
    return true;
  }

  bool HandleSave(CefRefPtr<CefBrowser> browser, const std::string& origin,
                  const std::string& body, CefRefPtr<Callback> callback) {
    std::string user_utf8;
    std::string password_utf8;
    if (!ReadField(body, "u", mtk::passwords::kMaxUserNameLength * 4, &user_utf8) ||
        !ReadField(body, "p", mtk::passwords::kMaxPasswordLength * 4,
                   &password_utf8)) {
      callback->Failure(2, "bad request");
      return true;
    }
    std::wstring user_name = Utf8ToWide(user_utf8);
    std::wstring password = Utf8ToWide(password_utf8);
    SecureZeroMemory(password_utf8.data(), password_utf8.size());
    if (user_name.empty() || password.empty()) {
      mtk::passwords::Scrub(&password);
      callback->Failure(2, "bad request");
      return true;
    }

    auto& store = mtk::passwords::Store::Get();
    using Disposition = mtk::passwords::SaveDisposition;
    const auto disposition = store.ClassifySave(origin, user_name, password);
    if (disposition == Disposition::kUnchanged) {
      mtk::passwords::Scrub(&password);
      callback->Success("{\"saved\":true,\"unchanged\":true}");
      return true;
    }
    if (disposition == Disposition::kInvalid ||
        disposition == Disposition::kUnavailable) {
      mtk::passwords::Scrub(&password);
      callback->Failure(4, "unavailable");
      return true;
    }
    using Text = mtk::strings::Id;
    const bool update = disposition == Disposition::kUpdate;
    const wchar_t* title = mtk::strings::Get(
        update ? Text::kUpdatePasswordTitle : Text::kSavePasswordTitle);
    const std::wstring question = std::wstring(mtk::strings::Get(
        update ? Text::kUpdatePasswordQuestion : Text::kSavePasswordQuestion)) +
        L"\n\n" + Utf8ToWide(origin) + L"\n" +
        mtk::strings::Get(Text::kPasswordAccountLabel) + user_name;
    const int answer =
        MessageBoxW(OwnerWindow(browser), question.c_str(),
                    title, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
    bool saved = false;
    if (answer == IDYES) {
      saved = store.SaveAndFlush(origin, user_name, password);
      if (!saved) {
        MessageBoxW(OwnerWindow(browser),
                    mtk::strings::Get(Text::kPasswordSaveFailed),
                    title, MB_OK | MB_ICONERROR);
      }
    }
    mtk::passwords::Scrub(&password);
    callback->Success(saved ? "{\"saved\":true}" : "{\"saved\":false}");
    return true;
  }
};

QueryHandler& Handler() {
  static QueryHandler handler;
  return handler;
}

CefMessageRouterConfig RouterConfig() {
  // Default names (cefQuery / cefQueryCancel). Nothing else in the product uses
  // the router, so there is no reason to rename them.
  return CefMessageRouterConfig();
}

}  // namespace

CefRefPtr<CefMessageRouterBrowserSide> Router() {
  if (!g_browser_router) {
    g_browser_router = CefMessageRouterBrowserSide::Create(RouterConfig());
    g_browser_router->AddHandler(&Handler(), false);
  }
  return g_browser_router;
}

bool OnProcessMessage(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefProcessId source_process,
                      CefRefPtr<CefProcessMessage> message) {
  return Router()->OnProcessMessageReceived(browser, frame, source_process,
                                            message);
}

void OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (g_browser_router) g_browser_router->OnBeforeClose(browser);
}

void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser) {
  if (g_browser_router) g_browser_router->OnRenderProcessTerminated(browser);
}

void RendererOnContextCreated(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefV8Context> context) {
  if (!g_renderer_router) {
    g_renderer_router = CefMessageRouterRendererSide::Create(RouterConfig());
  }
  g_renderer_router->OnContextCreated(browser, frame, context);
}

void RendererOnContextReleased(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  if (g_renderer_router) {
    g_renderer_router->OnContextReleased(browser, frame, context);
  }
}

bool RendererOnProcessMessage(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefProcessId source_process,
                              CefRefPtr<CefProcessMessage> message) {
  if (!g_renderer_router) return false;
  return g_renderer_router->OnProcessMessageReceived(browser, frame,
                                                     source_process, message);
}

const char* PageScript() {
  // Deliberately small: no mutation observer, no periodic scanning, and it does
  // nothing at all outside https. The browser side ignores the origin the page
  // claims, so this script cannot ask about another site even if it is altered
  // by the page.
  return R"JS(
(() => {
  if (window.__mtkPasswords) return;
  if (location.protocol !== 'https:') return;
  Object.defineProperty(window, '__mtkPasswords', {
    value: true, configurable: false, enumerable: false
  });
  if (typeof window.cefQuery !== 'function') return;

  const ask = (payload, onSuccess) => {
    window.cefQuery({
      request: JSON.stringify(Object.assign({ mtk: 'password' }, payload)),
      persistent: false,
      onSuccess: response => { try { onSuccess(JSON.parse(response)); } catch (_) {} },
      onFailure: () => {}
    });
  };

  const passwordFields = () =>
    Array.from(document.querySelectorAll('input[type=password]'))
         .filter(field => !field.disabled && field.offsetParent !== null);

  // The user name field is the visible text-like input closest before the
  // password field, which is how these forms are built in practice.
  const userFieldFor = password => {
    const scope = password.form || document;
    const candidates = Array.from(
        scope.querySelectorAll('input:not([type=password])'))
        .filter(field => {
          const type = (field.type || 'text').toLowerCase();
          return ['text', 'email', 'tel', 'username', ''].includes(type) &&
                 !field.disabled && field.offsetParent !== null;
        });
    if (!candidates.length) return null;
    const before = candidates.filter(field =>
        field.compareDocumentPosition(password) &
        Node.DOCUMENT_POSITION_FOLLOWING);
    return (before.length ? before[before.length - 1] : candidates[0]);
  };

  const setValue = (field, value) => {
    if (!field) return;
    field.focus();
    field.value = value;
    field.dispatchEvent(new Event('input', { bubbles: true }));
    field.dispatchEvent(new Event('change', { bubbles: true }));
    field.blur();
  };

  const fill = () => {
    const password = passwordFields().find(field =>
        field.autocomplete !== 'new-password');
    if (!password || password.value) return;
    ask({ t: 'get' }, credential => {
      if (!credential || !credential.p) return;
      const user = userFieldFor(password);
      if (!password.isConnected || password.value ||
          (user && user.value && user.value !== credential.u)) return;
      if (user && !user.value) setValue(user, credential.u || '');
      setValue(password, credential.p);
    });
  };

  const offerToSave = event => {
    // Use the submitted form only. Account-change pages often contain a
    // separate login form, plus current/new/confirm password fields.
    const form = event.target;
    if (!form || form.tagName !== 'FORM') return;
    const fields = passwordFields().filter(field =>
        field.form === form && field.value);
    const newFields = fields.filter(field => field.autocomplete === 'new-password');
    if (newFields.length && newFields.some(field =>
        field.value !== newFields[0].value)) return;
    // Ambiguous unannotated multi-password forms are left alone.
    const password = newFields[0] || (fields.length === 1 ? fields[0] : null);
    if (!password) return;
    const user = userFieldFor(password);
    const name = user && user.value ? user.value : '';
    if (!name) return;
    ask({ t: 'save', u: name, p: password.value }, () => {});
  };

  addEventListener('submit', offerToSave, true);
  if (document.readyState === 'loading') {
    addEventListener('DOMContentLoaded', fill, { once: true });
  } else {
    fill();
  }
})();
)JS";
}

}  // namespace mtk::password_bridge
