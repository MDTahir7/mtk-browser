#pragma once

#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_message_router.h"

// Channel between a page and the password store.
//
// Everything crossing this boundary is treated as untrusted: the page decides
// what to send, so the browser side bounds every field and derives the origin
// from the frame's own URL rather than from anything the page provided. See
// docs/PASSWORD_MANAGER.md.
namespace mtk::password_bridge {

// --- Browser process ---

// Router shared by every browser client, created on first use with the password
// query handler already registered.
CefRefPtr<CefMessageRouterBrowserSide> Router();

bool OnProcessMessage(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefProcessId source_process,
                      CefRefPtr<CefProcessMessage> message);
void OnBeforeClose(CefRefPtr<CefBrowser> browser);
void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser);

// --- Renderer process ---

void RendererOnContextCreated(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefV8Context> context);
void RendererOnContextReleased(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context);
bool RendererOnProcessMessage(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefProcessId source_process,
                              CefRefPtr<CefProcessMessage> message);

// Script injected into every https page: offers to save a submitted sign-in and
// fills a saved one.
const char* PageScript();

}  // namespace mtk::password_bridge
