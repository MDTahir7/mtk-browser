#pragma once

#include <cstddef>
#include <functional>

#if __has_include("include/cef_request_context.h")
#include "include/cef_request_context.h"
#define MTK_HAS_CEF 1
#endif

namespace mtk::private_session {

// Central manager for MTK Browser Private Window (Incognito) sessions.
// All concurrently open private windows share a single ephemeral in-memory
// browser context. When the last private window closes, the temporary session
// data (cookies, auth credentials, cache, private download records) is purged,
// and any subsequent private window begins with a brand new context.
class Manager final {
 public:
  static Manager& Get();

#if defined(MTK_HAS_CEF)
  // Returns the active shared in-memory CefRequestContext for private windows.
  // Initializes a new context and registers custom scheme handlers if one
  // is not currently active.
  CefRefPtr<CefRequestContext> AcquireContext();
  CefRefPtr<CefRequestContext> context() const { return context_; }
#endif

  // Window lifecycle tracking
  void OnWindowOpened();
  void OnWindowClosed();

  // Number of currently active private windows
  size_t ActiveWindowCount() const;

  // True if a private session is currently active
  bool HasActiveSession() const;

  // Purges all temporary session data (cookies, auth credentials, private downloads)
  // and tears down the private context. Called automatically when the last private
  // window closes.
  void PurgeSession();

  // Explicit reset (for unit testing and clean state reproduction)
  void Reset();

  using CleanupCallback = std::function<void()>;
  void SetCleanupCallback(CleanupCallback callback);

 private:
  Manager() = default;
  ~Manager() = default;

  size_t active_windows_ = 0;
  bool session_active_ = false;
  CleanupCallback cleanup_callback_;

#if defined(MTK_HAS_CEF)
  CefRefPtr<CefRequestContext> context_;
#endif
};

}  // namespace mtk::private_session
