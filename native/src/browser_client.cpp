#include "browser_client.h"

#include <atomic>
#include <string>

#include "include/cef_browser.h"
#include "include/cef_callback.h"
#include "include/cef_frame.h"
#include "include/cef_menu_model.h"
#include "include/cef_request.h"
#include "filter_engine.h"
#include "policy.h"

namespace {

std::atomic<unsigned long> g_blocked_count{0};

std::string BlockPage(const std::wstring& reason) {
  const std::string text(reason.begin(), reason.end());
  return "data:text/html,<meta charset=utf-8><title>Blocked by MTK Browser</title><style>"
         "body{font-family:Segoe UI;background:%230b1020;color:%23f8fafc;"
         "display:grid;place-items:center;min-height:90vh;margin:0}"
         ".card{max-width:620px;padding:48px;border:1px solid %232c3856;"
         "border-radius:24px;text-align:center;background:%2311182b}"
         ".shield{font-size:52px;color:%237c5cff}p{color:%23aab7d1;line-height:1.7}"
         "</style><main class=card><div class=shield>&%239678;</div>"
         "<h1>Navigation blocked</h1><p>" + text +
         "</p><p>MTK Browser stopped this request using your active safety policy."
         "</p></main>";
}

// Applies the cosmetic selectors that the filter engine resolved for this site.
// Selectors are injected as a stylesheet, so no page node is ever removed.
std::string CosmeticScript(const std::string& selectors) {
  if (selectors.empty()) return {};
  std::string escaped;
  for (const char character : selectors) {
    if (character == '`' || character == '\\') escaped.push_back('\\');
    if (character == '\r' || character == '\n') continue;
    escaped.push_back(character);
  }
  return
      "(() => { const id='mtk-cosmetic-filter';"
      "let style=document.getElementById(id);"
      "if(!style){style=document.createElement('style');style.id=id;"
      "(document.head||document.documentElement).appendChild(style);}"
      "const rules=`" + escaped + "{display:none !important;}`;"
      "if(style.textContent!==rules){style.textContent=rules;} })();";
}

mtk::policy::ResourceType MapResourceType(CefRequest::ResourceType type) {
  switch (type) {
    case RT_MAIN_FRAME:
    case RT_NAVIGATION_PRELOAD_MAIN_FRAME:
      return mtk::policy::ResourceType::kMainFrame;
    case RT_SUB_FRAME:
    case RT_NAVIGATION_PRELOAD_SUB_FRAME:
      return mtk::policy::ResourceType::kSubFrame;
    case RT_STYLESHEET: return mtk::policy::ResourceType::kStylesheet;
    case RT_SCRIPT: return mtk::policy::ResourceType::kScript;
    case RT_IMAGE: return mtk::policy::ResourceType::kImage;
    case RT_XHR: return mtk::policy::ResourceType::kXhr;
    case RT_FONT_RESOURCE: return mtk::policy::ResourceType::kFont;
    case RT_MEDIA: return mtk::policy::ResourceType::kMedia;
    case RT_OBJECT:
    case RT_PLUGIN_RESOURCE:
      return mtk::policy::ResourceType::kObject;
    case RT_PING:
    case RT_CSP_REPORT:
      return mtk::policy::ResourceType::kPing;
    case RT_WORKER:
    case RT_SHARED_WORKER:
    case RT_SERVICE_WORKER:
      return mtk::policy::ResourceType::kWorker;
    case RT_PREFETCH: return mtk::policy::ResourceType::kPrefetch;
    case RT_FAVICON: return mtk::policy::ResourceType::kFavicon;
    default: return mtk::policy::ResourceType::kOther;
  }
}

}  // namespace

BrowserClient::BrowserClient(BrowserClientDelegate* delegate)
    : delegate_(delegate) {}

CefRefPtr<CefContextMenuHandler> BrowserClient::GetContextMenuHandler() {
  return this;
}
CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler() { return this; }
CefRefPtr<CefDownloadHandler> BrowserClient::GetDownloadHandler() { return this; }
CefRefPtr<CefKeyboardHandler> BrowserClient::GetKeyboardHandler() { return this; }
CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() { return this; }
CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler() { return this; }
CefRefPtr<CefPermissionHandler> BrowserClient::GetPermissionHandler() {
  return this;
}
CefRefPtr<CefRequestHandler> BrowserClient::GetRequestHandler() { return this; }

void BrowserClient::OnBeforeContextMenu(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) {
  if (model) model->Clear();
}

bool BrowserClient::CanDownload(CefRefPtr<CefBrowser>, const CefString&,
                                const CefString&) {
  return false;
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (delegate_) delegate_->OnBrowserCreated(this);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser>) {
  if (delegate_) delegate_->OnBrowserCloseReady(this);
  // BrowserWindow performs the required Win32 hierarchy tear-down. Returning
  // true prevents CEF from sending a second ambiguous WM_CLOSE to the parent.
  return true;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (browser_ && browser_->IsSame(browser)) browser_ = nullptr;
  if (delegate_) delegate_->OnBrowserClosed(this);
}

bool BrowserClient::OnBeforePopup(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
    const CefString& target_url, const CefString&,
    cef_window_open_disposition_t, bool, const CefPopupFeatures&,
    CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>&, bool*) {
  const auto decision = mtk::policy::EvaluateNavigation(target_url.ToString());
  if (decision.allow && delegate_) {
    delegate_->OnBrowserPopupRequested(this, decision.url);
  }
  return true;
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser>, bool is_loading,
                                         bool can_go_back,
                                         bool can_go_forward) {
  if (delegate_) {
    delegate_->OnBrowserLoadingChanged(this, is_loading, can_go_back,
                                       can_go_forward);
  }
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                              int) {
  if (!frame) return;
  const std::string url = frame->GetURL();
  const std::string host = mtk::policy::HostOf(url);
  if (host.empty()) return;

  const std::string selectors =
      mtk::filters::Engine::Get().CssSelectorsForHost(host);
  const std::string script = CosmeticScript(selectors);
  if (!script.empty()) frame->ExecuteJavaScript(script, url, 0);
}

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser>,
                                  const CefKeyEvent& event, CefEventHandle,
                                  bool*) {
  if (event.type != KEYEVENT_RAWKEYDOWN || !delegate_) return false;
  return delegate_->OnBrowserShortcut(
      this, event.windows_key_code,
      (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
      (event.modifiers & EVENTFLAG_ALT_DOWN) != 0,
      (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0);
}

bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser>,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request, bool, bool) {
  if (!frame || !frame->IsMain() || !request) return false;
  const std::string requested_url = request->GetURL();
  if (block_page_pending_ && requested_url.starts_with("data:text/html")) {
    block_page_pending_ = false;
    return false;
  }

  const auto decision = mtk::policy::EvaluateNavigation(requested_url);
  if (!decision.allow) {
    block_page_pending_ = true;
    frame->LoadURL(BlockPage(decision.reason));
    return true;
  }
  if (decision.url != requested_url) {
    frame->LoadURL(decision.url);
    return true;
  }
  return false;
}

bool BrowserClient::OnOpenURLFromTab(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& target_url,
    cef_window_open_disposition_t disposition, bool) {
  if (disposition == CEF_WOD_CURRENT_TAB) return false;
  const auto decision = mtk::policy::EvaluateNavigation(target_url.ToString());
  if (decision.allow && delegate_) {
    delegate_->OnBrowserPopupRequested(this, decision.url);
  }
  return true;
}

CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    bool, bool, const CefString& request_initiator,
    bool& disable_default_handling) {
  if (!request) return this;
  const std::string url = request->GetURL();
  if (request->GetResourceType() == RT_MAIN_FRAME &&
      url.starts_with("data:text/html")) {
    return this;
  }

  const auto decision = mtk::policy::EvaluateRequest(
      url, MapResourceType(request->GetResourceType()),
      request_initiator.ToString());
  if (!decision.allow) {
    g_blocked_count.fetch_add(1, std::memory_order_relaxed);
    disable_default_handling = true;
    return nullptr;
  }
  return this;
}

unsigned long BrowserClient::blocked_count() {
  return g_blocked_count.load(std::memory_order_relaxed);
}

bool BrowserClient::GetAuthCredentials(
    CefRefPtr<CefBrowser>, const CefString&, bool, const CefString&, int,
    const CefString&, const CefString&, CefRefPtr<CefAuthCallback>) {
  return false;
}

bool BrowserClient::OnCertificateError(
    CefRefPtr<CefBrowser>, cef_errorcode_t, const CefString&,
    CefRefPtr<CefSSLInfo>, CefRefPtr<CefCallback>) {
  return false;
}

bool BrowserClient::OnSelectClientCertificate(
    CefRefPtr<CefBrowser>, bool, const CefString&, int,
    const X509CertificateList&,
    CefRefPtr<CefSelectClientCertificateCallback> callback) {
  if (callback) callback->Select(nullptr);
  return true;
}

CefResourceRequestHandler::ReturnValue BrowserClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>) {
  if (!request) return RV_CONTINUE;
  const std::string url = request->GetURL();
  if (request->GetResourceType() == RT_MAIN_FRAME &&
      url.starts_with("data:text/html")) {
    return RV_CONTINUE;
  }

  CefRequest::HeaderMap headers;
  request->GetHeaderMap(headers);
  for (const auto& [name, value] : mtk::policy::PrivacyRequestHeaders(url)) {
    headers.erase(name);
    headers.insert({name, value});
  }
  request->SetHeaderMap(headers);
  return RV_CONTINUE;
}

void BrowserClient::OnProtocolExecution(CefRefPtr<CefBrowser>,
                                        CefRefPtr<CefFrame>,
                                        CefRefPtr<CefRequest>,
                                        bool& allow_os_execution) {
  allow_os_execution = false;
}

bool BrowserClient::OnShowPermissionPrompt(
    CefRefPtr<CefBrowser>, uint64_t, const CefString&, uint32_t,
    CefRefPtr<CefPermissionPromptCallback> callback) {
  if (callback) callback->Continue(CEF_PERMISSION_RESULT_DENY);
  return true;
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser>,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (frame && frame->IsMain() && delegate_) {
    delegate_->OnBrowserAddressChanged(this, url.ToWString());
  }
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser>,
                                  const CefString& title) {
  if (delegate_) delegate_->OnBrowserTitleChanged(this, title.ToWString());
}
