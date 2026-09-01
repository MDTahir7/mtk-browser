#pragma once

#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "history_page.h"
#include "home_page.h"

class BrowserClient;

class BrowserClientDelegate {
 public:
  virtual void OnBrowserCreated(BrowserClient* client) = 0;
  virtual void OnBrowserClosed(BrowserClient* client) = 0;
  virtual void OnBrowserAddressChanged(BrowserClient* client,
                                       const std::wstring& url) = 0;
  virtual void OnBrowserTitleChanged(BrowserClient* client,
                                     const std::wstring& title) = 0;
  virtual void OnBrowserLoadingChanged(BrowserClient* client, bool loading,
                                       bool can_go_back,
                                       bool can_go_forward) = 0;
  virtual void OnBrowserNavigationCompleted(BrowserClient* client,
                                            const std::string& url) = 0;
  virtual void OnBrowserFindResult(BrowserClient* client, int identifier,
                                   int count, int active_match_ordinal,
                                   bool final_update) = 0;
  virtual void OnBrowserPopupRequested(BrowserClient* client,
                                       const std::string& url) = 0;
  virtual void OnBrowserDownloadsChanged(BrowserClient* client) = 0;
  // Dispatches only allowlisted actions clicked from the exact built-in home
  // document. No arbitrary URL or script payload crosses this boundary.
  virtual void OnBrowserHomeAction(BrowserClient* client,
                                   mtk::home::Action action) = 0;
  // The same arrangement for the built-in history document. Deleting anything
  // asks for confirmation on the browser side, not on the page.
  virtual void OnBrowserHistoryAction(BrowserClient* client,
                                      const mtk::history::Action& action) = 0;
  virtual bool OnBrowserShortcut(BrowserClient* client, int virtual_key,
                                 bool control, bool alt, bool shift) = 0;

 protected:
  virtual ~BrowserClientDelegate() = default;
};

class BrowserClient final : public CefClient,
                            public CefCookieAccessFilter,
                            public CefContextMenuHandler,
                            public CefDisplayHandler,
                            public CefDownloadHandler,
                            public CefFindHandler,
                            public CefKeyboardHandler,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefRequestHandler,
                            public CefResourceRequestHandler,
                            public CefPermissionHandler {
 public:
  explicit BrowserClient(BrowserClientDelegate* delegate);

  // Renderer queries for the password channel arrive here.
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;

  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;
  CefRefPtr<CefFindHandler> GetFindHandler() override;
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override;
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override;
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;

  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;
  bool CanDownload(CefRefPtr<CefBrowser> browser,
                   const CefString& url,
                   const CefString& request_method) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;
  void OnFindResult(CefRefPtr<CefBrowser> browser, int identifier, int count,
                    const CefRect& selection_rect, int active_match_ordinal,
                    bool final_update) override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     cef_window_open_disposition_t target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;

  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        cef_window_open_disposition_t target_disposition,
                        bool user_gesture) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;
  bool GetAuthCredentials(CefRefPtr<CefBrowser> browser,
                          const CefString& origin_url,
                          bool is_proxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override;
  bool OnCertificateError(CefRefPtr<CefBrowser> browser,
                          cef_errorcode_t cert_error,
                          const CefString& request_url,
                          CefRefPtr<CefSSLInfo> ssl_info,
                          CefRefPtr<CefCallback> callback) override;
  bool OnSelectClientCertificate(
      CefRefPtr<CefBrowser> browser,
      bool is_proxy,
      const CefString& host,
      int port,
      const X509CertificateList& certificates,
      CefRefPtr<CefSelectClientCertificateCallback> callback) override;

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;
  // Installed only for sites whose cookies the user switched off.
  CefRefPtr<CefCookieAccessFilter> GetCookieAccessFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request) override;
  bool CanSendCookie(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request,
                     const CefCookie& cookie) override;
  bool CanSaveCookie(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     CefRefPtr<CefRequest> request,
                     CefRefPtr<CefResponse> response,
                     const CefCookie& cookie) override;
  void OnProtocolExecution(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefRequest> request,
                           bool& allow_os_execution) override;

  bool OnShowPermissionPrompt(
      CefRefPtr<CefBrowser> browser,
      uint64_t prompt_id,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefPermissionPromptCallback> callback) override;

  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;

  CefRefPtr<CefBrowser> browser() const { return browser_; }

  // Allows exactly one upcoming `data:text/html` main-frame navigation, which is
  // how the browser shows its own pages (the block page and the keyboard
  // shortcut reference). The flag is consumed by that navigation, so it cannot
  // be used to open a second page, and it is only ever set by this application.
  void ExpectInternalPage();

  // Number of ad/tracker/unsafe requests blocked since launch, shown in the
  // MTK Shields UI.
  static unsigned long blocked_count();
  // Requests blocked while the given page origin was showing, for the Shields
  // panel. A display counter only.
  static unsigned long blocked_count_for_origin(const std::string& origin);
  // Same, but only the subset of those blocks that matched a rule the user
  // wrote themselves, so the panel can say how many of *those* fired here.
  static unsigned long user_rule_blocked_count_for_origin(
      const std::string& origin);
  static bool CanControlDownload(uint32_t id);
  static bool PauseDownload(uint32_t id);
  static bool ResumeDownload(uint32_t id);
  static bool CancelDownload(uint32_t id);

 private:
  // True unless the page currently shown has ad and tracker filtering switched
  // off in its site settings.
  bool AdFilteringAllowedForCurrentPage() const;
  // Records one blocked request, in the session total and per page origin.
  // `from_user_rule` is true when the block came from user_filters rather than
  // the built-in policy, which is tracked separately for the same reason.
  void CountBlockedRequest(bool from_user_rule) const;

  BrowserClientDelegate* delegate_ = nullptr;
  bool block_page_pending_ = false;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};
