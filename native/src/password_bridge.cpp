#include "password_bridge.h"

#include <windows.h>

#include <string>

#include "browser_client.h"
#include "home_page.h"
#include "password_store.h"
#include "site_settings.h"

namespace mtk::password_bridge {
namespace {

// Bounds on what a page may send. The values also bound what will be stored.
constexpr size_t kMaxRequestBytes = 2048;

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

// Minimal reader for the two flat requests this channel accepts. A full JSON
// parser is not needed and would be more surface than the feature deserves.
// Returns the value of "key" as a JSON string, unescaping only the sequences the
// page script can produce.
bool ReadField(const std::string& request, const char* key, size_t max_length,
               std::string* value) {
  value->clear();
  const std::string needle = std::string("\"") + key + "\":\"";
  const size_t start = request.find(needle);
  if (start == std::string::npos) return false;
  size_t index = start + needle.size();
  while (index < request.size()) {
    const char character = request[index];
    if (character == '"') return true;
    if (value->size() >= max_length) return false;
    if (character == '\\' && index + 1 < request.size()) {
      const char escape = request[index + 1];
      index += 2;
      switch (escape) {
        case '"': value->push_back('"'); break;
        case '\\': value->push_back('\\'); break;
        case 'n': value->push_back('\n'); break;
        case 'r': value->push_back('\r'); break;
        case 't': value->push_back('\t'); break;
        default: return false;
      }
      continue;
    }
    value->push_back(character);
    ++index;
  }
  return false;
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

class QueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
               int64_t /*query_id*/, const CefString& request,
               bool /*persistent*/,
               CefRefPtr<Callback> callback) override {
    if (!frame || !frame->IsMain() || !callback) return false;
    const std::string body = request.ToString();
    if (body.size() > kMaxRequestBytes) return false;
    if (body.find("\"mtk\":\"password\"") == std::string::npos) return false;

    // The origin comes from the frame, never from the request, so a page cannot
    // ask about another site.
    const std::string page_url = frame->GetURL().ToString();
    const std::string origin = mtk::sites::OriginOf(page_url);
    if (origin.empty() || !mtk::passwords::IsStorableUrl(page_url)) {
      callback->Failure(1, "unsupported origin");
      return true;
    }

    std::string action;
    if (!ReadField(body, "t", 8, &action)) {
      callback->Failure(2, "bad request");
      return true;
    }

    if (action == "get") return HandleGet(origin, callback);
    if (action == "save") return HandleSave(browser, origin, body, callback);
    callback->Failure(2, "bad request");
    return true;
  }

 private:
  bool HandleGet(const std::string& origin, CefRefPtr<Callback> callback) {
    mtk::passwords::Credential credential;
    if (!mtk::passwords::Store::Get().SingleFor(origin, &credential)) {
      // Nothing saved, or more than one account: filling would be a guess.
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

    // A password is never stored silently: capture proposes, the person decides.
    const std::wstring question =
        L"Save the password for " + Utf8ToWide(origin) + L"?\n\nSigned in as: " +
        user_name +
        L"\n\nSaved passwords are encrypted for this Windows account and this "
        L"profile.";
    const int answer =
        MessageBoxW(OwnerWindow(browser), question.c_str(),
                    L"Save password", MB_YESNO | MB_ICONQUESTION);
    bool saved = false;
    if (answer == IDYES) {
      auto& store = mtk::passwords::Store::Get();
      saved = store.Save(origin, user_name, password) && store.Flush();
      if (!saved) {
        MessageBoxW(OwnerWindow(browser),
                    L"MTK Browser could not save that password.",
                    L"Save password", MB_OK | MB_ICONERROR);
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
    const password = passwordFields()[0];
    if (!password || password.value) return;
    ask({ t: 'get' }, credential => {
      if (!credential || !credential.p) return;
      const user = userFieldFor(password);
      if (user && !user.value) setValue(user, credential.u || '');
      setValue(password, credential.p);
    });
  };

  const offerToSave = () => {
    const password = passwordFields().find(field => field.value);
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
