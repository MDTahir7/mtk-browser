#pragma once

#include <windows.h>

#include <memory>

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_render_process_handler.h"

class BrowserWindow;

class MtkApp final : public CefApp,
                     public CefBrowserProcessHandler,
                     public CefRenderProcessHandler {
 public:
  MtkApp(HINSTANCE instance, int show_command);
  ~MtkApp() override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnContextInitialized() override;

  // Media and accessibility switches are applied here, before Chromium reads
  // its command line.
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  // Carries the fingerprint-protection preference into the renderer's command
  // line, since preferences are only loaded in the browser process.
  void OnBeforeChildProcessLaunch(
      CefRefPtr<CefCommandLine> command_line) override;

  // Runs MTK Shields as soon as CEF creates the page V8 context. This avoids
  // the renderer-stalling V8-extension registration path.
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  // Router replies for the password channel arrive in the render process here.
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  HINSTANCE instance_ = nullptr;
  int show_command_ = SW_SHOWDEFAULT;

  IMPLEMENT_REFCOUNTING(MtkApp);
};
