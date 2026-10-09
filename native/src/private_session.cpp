#include "private_session.h"

#include "download_manager.h"

#if defined(MTK_HAS_CEF)
#include "home_scheme.h"
#include "include/cef_cookie.h"
#include "include/cef_request_context_handler.h"
#endif

namespace mtk::private_session {

Manager& Manager::Get() {
  static Manager manager;
  return manager;
}

#if defined(MTK_HAS_CEF)
CefRefPtr<CefRequestContext> Manager::AcquireContext() {
  if (!context_) {
    // An empty cache_path configures Chromium for in-memory, off-the-record
    // operation: cookies, HTTP cache, and HTML5 web storage remain ephemeral.
    CefRequestContextSettings settings;
    context_ = CefRequestContext::CreateContext(settings, nullptr);
    if (context_) {
      mtk::home::RegisterSchemeHandler(context_, /*private_session=*/true);
    }
  }
  session_active_ = (context_ != nullptr);
  return context_;
}
#endif

void Manager::OnWindowOpened() {
  ++active_windows_;
  session_active_ = true;
}

void Manager::OnWindowClosed() {
  if (active_windows_ > 0) {
    --active_windows_;
  }
  // When the LAST private window closes, terminate the private session.
  if (active_windows_ == 0) {
    PurgeSession();
  }
}

size_t Manager::ActiveWindowCount() const {
  return active_windows_;
}

bool Manager::HasActiveSession() const {
  return session_active_ || active_windows_ > 0;
}

void Manager::PurgeSession() {
#if defined(MTK_HAS_CEF)
  if (context_) {
    // 1. Purge all in-memory cookies
    if (auto cookie_mgr = context_->GetCookieManager(nullptr)) {
      cookie_mgr->DeleteCookies("", "", nullptr);
    }
    // 2. Clear authentication credentials and certificate exceptions
    context_->ClearHttpAuthCredentials(nullptr);
    context_->ClearCertificateExceptions(nullptr);
    // 3. Release our context reference so CEF frees the ephemeral context
    context_ = nullptr;
  }
#endif

  // 5. Evict ephemeral private download metadata from memory
  mtk::downloads::Manager::Get().ClearPrivateDownloads();

  // 6. Notify registered cleanup callback if any
  if (cleanup_callback_) {
    cleanup_callback_();
  }

  active_windows_ = 0;
  session_active_ = false;
}

void Manager::Reset() {
  PurgeSession();
}

void Manager::SetCleanupCallback(CleanupCallback callback) {
  cleanup_callback_ = std::move(callback);
}

}  // namespace mtk::private_session
