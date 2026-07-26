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
  virtual bool OnBrowserShortcut(BrowserClient* client, int virtual_key,
                                 bool control, bool alt, bool shift) = 0;

 protected:
  virtual ~BrowserClientDelegate() = default;
};

class BrowserClient final : public CefClient,
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

  // Number of ad/tracker/unsafe requests blocked since launch, shown in the
  // MTK Shields UI.
  static unsigned long blocked_count();
  static bool CanControlDownload(uint32_t id);
  static bool PauseDownload(uint32_t id);
  static bool ResumeDownload(uint32_t id);
  static bool CancelDownload(uint32_t id);

 private:
  BrowserClientDelegate* delegate_ = nullptr;
  bool block_page_pending_ = false;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};
