#include "shields_panel.h"

#include <dwmapi.h>

#include <string>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "inline_asset.h"

namespace mtk::shields {
namespace {

constexpr wchar_t kPanelClassName[] = L"MtkShieldsPanel";
constexpr wchar_t kBeaconFile[] = L"mtk_siren_alert.webm";
// Sized for the real markup with a little slack, so rounding the type up to whole
// pixels cannot clip the last row. Verified against the live panel.
constexpr int kPanelLogicalWidth = 344;
constexpr int kPanelLogicalHeight = 540;
constexpr int kAnchorGapLogical = 6;
// Kept as channels so both the GDI brush and CEF's background colour are built
// without casting a constant through GetRValue and friends.
constexpr BYTE kPanelRed = 0x11;
constexpr BYTE kPanelGreen = 0x18;
constexpr BYTE kPanelBlue = 0x2b;
constexpr COLORREF kPanelBackground = RGB(kPanelRed, kPanelGreen, kPanelBlue);
// Posted by the client once CEF has finished tearing the browser down, which is
// when the popup may safely be destroyed.
constexpr UINT kBrowserClosedMessage = WM_APP + 41;
// Posted once the hosted browser exists, which is when dismissal may be armed.
constexpr UINT kBrowserReadyMessage = WM_APP + 42;

// Only one panel exists at a time, so its window and client are tracked here
// rather than threaded through the caller.
HWND g_panel = nullptr;
CefRefPtr<CefClient> g_client;
// Activation arrives before CEF has finished creating the hosted browser, and an
// early WA_INACTIVE used to tear the popup down while the browser was still
// starting. That left a live browser with no window, which is why the panel
// could not be dismissed afterwards. Dismissal is therefore armed only once the
// browser exists.
bool g_dismiss_armed = false;
// A click on the shield button first deactivates the panel, so the watchdog
// closes it a moment before the button command arrives. Without this the command
// would immediately reopen what the user meant to close.
ULONGLONG g_closed_tick = 0;
constexpr ULONGLONG kReopenGuardMs = 450;
// Polling the foreground window is what makes "click outside" reliable: it works
// whether the click lands on our own window, another application, or the shell.
constexpr UINT_PTR kDismissTimer = 1;
constexpr UINT kDismissPollMs = 150;

// A data: URL ends at the first '#', and a stray '%' would be read as the start
// of an escape, so those two characters are encoded. Everything else is written
// as plain markup at the call site. HTML entities survive this: "&#10003;"
// becomes "&%2310003;" in the URL and decodes back before the parser sees it.
std::string EscapeForDataUrl(const std::string& markup) {
  std::string escaped;
  escaped.reserve(markup.size() + markup.size() / 8);
  for (const char character : markup) {
    if (character == '%') {
      escaped += "%25";
    } else if (character == '#') {
      escaped += "%23";
    } else {
      escaped += character;
    }
  }
  return escaped;
}

// Escapes the few characters that could break out of text content. The site is
// derived from the page's own address, so it is not arbitrary markup, but it is
// still page-controlled text going into a document.
std::string EscapeHtmlText(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char character : value) {
    switch (character) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      default: escaped.push_back(character); break;
    }
  }
  return escaped;
}

// One line naming the current site, what was blocked on it, and whether its own
// filtering switch is off. The panel stays read-only: the switch itself lives in
// the site settings dialog, and saying where it is beats a control this
// presentation-only surface would need a script bridge to operate.
std::string SiteLine(const SiteState& state) {
  if (state.site.empty()) {
    return "<div class=site>This page has no site settings of its own</div>";
  }
  std::string line = "<div class=site><b>" + EscapeHtmlText(state.site) + "</b> ";
  if (!state.site_filtering) {
    line += "&middot; <span class=off>ad and tracker filtering is off here</span>";
  } else {
    line += "&middot; " + std::to_string(state.site_blocked_count) +
            " blocked on this page";
  }
  if (state.custom_rule_count > 0) {
    line += " &middot; " + std::to_string(state.custom_rule_count) +
            " blocked by your own rules";
  }
  line += "</div>";
  return line;
}

std::string PanelUrl(unsigned long blocked_count, bool standard_downloads,
                     const SiteState& site_state) {
  const std::string& beacon = mtk::assets::WebmDataUrl(kBeaconFile);
  // The glyph stays in the markup as the fallback for a missing asset and for
  // readers who ask the platform to reduce motion.
  std::string art = "<div class=beacon>&#9678;</div>";
  if (!beacon.empty()) {
    // The asset URL is already percent-safe, so it is spliced in after escaping
    // rather than being run through EscapeForDataUrl with the markup.
    art = "<video class=anim autoplay loop muted playsinline "
          "disablepictureinpicture aria-hidden=true src=\x01></video>" + art;
  }

  const std::string downloads =
      standard_downloads
          ? "<li><i>&#10003;</i><div>Standard downloads<small>Any file type is "
            "saved</small></div></li>"
          : "<li><i>&#10003;</i><div>Safe downloads<small>Programs and scripts "
            "are blocked</small></div></li>";

  const std::string markup =
      "<meta charset=utf-8><title>MTK Shields</title><style>"
      ":root{color-scheme:dark}"
      "html,body{margin:0;height:100%;overflow:hidden}"
      "body{font-family:Segoe UI,sans-serif;background:#11182b;color:#f0f4ff;"
      "border:1px solid #2a3350;box-sizing:border-box;display:flex;"
      "flex-direction:column;-webkit-user-select:none;user-select:none}"
      ".hero{flex:0 0 auto;display:flex;flex-direction:column;"
      "align-items:center;padding:14px 18px 12px;"
      "background:radial-gradient(120px 74px at 50% 6px,#241a52,#11182b)}"
      ".anim{width:104px;height:104px;display:block;pointer-events:none}"
      ".anim~.beacon{display:none}"
      "@media (prefers-reduced-motion:reduce){.anim{display:none}"
      ".anim~.beacon{display:block}}"
      ".beacon{font-size:46px;line-height:104px;color:#7c5cff}"
      "h1{margin:2px 0 0;font-size:15px;font-weight:600}"
      ".pill{margin-top:7px;font-size:10px;font-weight:700;letter-spacing:1px;"
      "color:#4ade80;background:rgba(74,222,128,.14);"
      "border:1px solid rgba(74,222,128,.36);border-radius:99px;padding:3px 10px}"
      ".count{flex:0 0 auto;display:flex;align-items:baseline;"
      "justify-content:center;gap:8px;padding:11px 18px;background:#161f38;"
      "border-top:1px solid #2a3350;border-bottom:1px solid #2a3350}"
      ".count b{font-size:26px;font-weight:700;color:#60a5fa}"
      ".count span{font-size:12px;color:#96a2be}"
      "ul{flex:1 1 auto;list-style:none;margin:0;padding:9px 18px 12px;"
      "overflow:hidden}"
      // Every length here is a whole CSS pixel on purpose. Half pixels land
      // between device pixels at 125% and 150% scaling, and the resulting
      // antialiasing is what made this panel look soft next to the page.
      "li{display:flex;gap:9px;align-items:flex-start;padding:5px 0;"
      "font-size:12px;line-height:1.4}"
      "li i{flex:0 0 12px;color:#4ade80;font-style:normal;font-weight:700;"
      "font-size:11px}"
      "li small{display:block;color:#96a2be;font-size:11px}"
      ".hint{flex:0 0 auto;padding:0 18px 11px;font-size:11px;color:#6d7ba0;"
      "text-align:center}"
      ".site{flex:0 0 auto;padding:9px 18px;background:#131b31;"
      "border-bottom:1px solid #2a3350;font-size:11px;color:#96a2be;"
      "text-align:center;overflow:hidden;text-overflow:ellipsis;"
      "white-space:nowrap}"
      ".site b{color:#f0f4ff;font-weight:600}"
      ".site .off{color:#fbbf24}"
      "</style><div class=hero>" + art +
      "<h1>MTK Shields are active</h1><div class=pill>PROTECTING</div></div>"
      "<div class=count><b>" + std::to_string(blocked_count) +
      "</b><span>requests blocked this session</span></div>" +
      SiteLine(site_state) + "<ul>"
      "<li><i>&#10003;</i><div>Ad, tracker and telemetry requests blocked"
      "</div></li>"
      "<li><i>&#10003;</i><div>Adult and piracy domains blocked</div></li>"
      "<li><i>&#10003;</i><div>Adult search terms blocked</div></li>"
      "<li><i>&#10003;</i><div>SafeSearch and HTTPS upgrade enforced</div></li>" +
      downloads +
      "<li><i>&#10003;</i><div>Popups and risky permissions denied</div></li>"
      "</ul><div class=hint>Alt+S for this site &middot; Alt+Y for your own "
      "filters &middot; Esc to close</div>";

  std::string url = "data:text/html," + EscapeForDataUrl(markup);
  // Restore the placeholder with the already-encoded asset URL.
  const size_t slot = url.find('\x01');
  if (slot != std::string::npos) url.replace(slot, 1, beacon);
  return url;
}

void ShowFallbackSummary(HWND owner, unsigned long blocked_count,
                         bool standard_downloads) {
  const std::wstring summary =
      L"MTK Shields are active.\n\nBlocked on this session: " +
      std::to_wstring(blocked_count) +
      L"\n\n"
      L"\u2022 Ad, tracker and telemetry requests blocked\n"
      L"\u2022 Adult and piracy domains blocked\n"
      L"\u2022 Adult search terms blocked\n"
      L"\u2022 SafeSearch and HTTPS upgrade enforced\n" +
      (standard_downloads
           ? std::wstring(L"\u2022 Downloads: standard mode, any file type is "
                          L"saved\n")
           : std::wstring(L"\u2022 Downloads: safe mode, programs and scripts "
                          L"blocked\n")) +
      L"\u2022 Popups and risky permissions denied";
  MessageBoxW(owner, summary.c_str(), L"MTK Shields",
              MB_OK | MB_ICONINFORMATION);
}

// Presentation only: no script bridge, no downloads, no navigation. Everything
// the panel needs is already inside the one data: URL it is given, so every
// other request is refused.
class PanelClient final : public CefClient,
                          public CefLifeSpanHandler,
                          public CefKeyboardHandler,
                          public CefContextMenuHandler,
                          public CefRequestHandler,
                          public CefResourceRequestHandler {
 public:
  explicit PanelClient(HWND panel) : panel_(panel) {}

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    // The popup was already dismissed while this browser was starting, so it has
    // no host window to live in and is closed at once instead of being orphaned.
    if (!panel_) {
      browser->GetHost()->CloseBrowser(true);
      return;
    }
    PostMessageW(panel_, kBrowserReadyMessage, 0, 0);
  }

  // Returning false lets CEF continue the close, and CEF then sends WM_CLOSE to
  // this browser's host window. That second WM_CLOSE must not call CloseBrowser
  // again, or the popup and CEF hand the same close back and forth forever and
  // the panel never goes away, so the state is recorded here.
  bool DoClose(CefRefPtr<CefBrowser>) override {
    closing_ = true;
    return false;
  }

  bool closing() const { return closing_; }

  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    if (panel_) PostMessageW(panel_, kBrowserClosedMessage, 0, 0);
  }

  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& event,
                     CefEventHandle, bool*) override {
    if (event.type == KEYEVENT_RAWKEYDOWN &&
        event.windows_key_code == VK_ESCAPE) {
      if (panel_) PostMessageW(panel_, WM_CLOSE, 0, 0);
      return true;
    }
    return false;
  }

  // A summary panel has nothing to inspect, copy or reload.
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefContextMenuParams>,
                           CefRefPtr<CefMenuModel> model) override {
    if (model) model->Clear();
  }

  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                      CefRefPtr<CefRequest> request, bool, bool) override {
    // Returning true cancels. Only the panel's own document may ever load.
    return !IsPanelDocument(request->GetURL().ToString());
  }

  bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                        const CefString&, cef_window_open_disposition_t,
                        bool) override {
    return true;  // handled, meaning discarded
  }

  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool,
      bool, const CefString&, bool&) override {
    return this;
  }

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback>) override {
    const std::string url = request->GetURL().ToString();
    // The document and its inlined loop both carry their own bytes and reach no
    // network. Anything else is refused, so the panel cannot be made to fetch.
    if (IsPanelDocument(url) ||
        url.starts_with(mtk::assets::kWebmDataUrlPrefix)) {
      return RV_CONTINUE;
    }
    return RV_CANCEL;
  }

  CefRefPtr<CefBrowser> browser() const { return browser_; }
  void Detach() { panel_ = nullptr; }

 private:
  static bool IsPanelDocument(const std::string& url) {
    return url.starts_with("data:text/html,");
  }

  HWND panel_ = nullptr;
  bool closing_ = false;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(PanelClient);
};

PanelClient* CurrentClient() {
  return static_cast<PanelClient*>(g_client.get());
}

void DestroyPanel() {
  if (auto* client = CurrentClient()) client->Detach();
  g_client = nullptr;
  g_dismiss_armed = false;
  g_closed_tick = GetTickCount64();
  if (g_panel) {
    HWND panel = g_panel;
    g_panel = nullptr;
    KillTimer(panel, kDismissTimer);
    DestroyWindow(panel);
  }
}

// True while the panel, or the CEF child inside it, owns the foreground.
bool PanelHoldsForeground(HWND panel) {
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return false;
  return foreground == panel || GetAncestor(foreground, GA_ROOT) == panel;
}

LRESULT CALLBACK PanelProc(HWND window, UINT message, WPARAM w_param,
                           LPARAM l_param) {
  switch (message) {
    case kBrowserReadyMessage:
      // The hosted browser exists, so losing activation now genuinely means the
      // user clicked away.
      g_dismiss_armed = true;
      SetTimer(window, kDismissTimer, kDismissPollMs, nullptr);
      return 0;
    case WM_ACTIVATE:
      // Clicking anywhere outside activates another window, which is the
      // dismissal gesture users expect from a toolbar popup.
      if (g_dismiss_armed && LOWORD(w_param) == WA_INACTIVE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_TIMER:
      // Activation messages alone are not dependable here: the click may land on
      // the CEF child of another window, or on the shell, and some of those
      // paths never deliver WA_INACTIVE to this popup.
      if (w_param == kDismissTimer && g_dismiss_armed &&
          !PanelHoldsForeground(window)) {
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_KEYDOWN:
      // Esc reaches the popup itself when focus has not moved into the browser.
      if (w_param == VK_ESCAPE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_CLOSE: {
      // First close: ask CEF to unwind and wait. Once DoClose has run, CEF sends
      // WM_CLOSE here again, and that is the pass that tears the popup down.
      auto* client = CurrentClient();
      if (client && client->browser() && !client->closing()) {
        client->browser()->GetHost()->CloseBrowser(true);
        return 0;
      }
      DestroyPanel();
      return 0;
    }
    case kBrowserClosedMessage:
      DestroyPanel();
      return 0;
    case WM_ERASEBKGND:
      return 1;  // the hosted browser paints the whole client area
    case WM_NCDESTROY:
      if (g_panel == window) g_panel = nullptr;
      break;
    default:
      break;
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

bool EnsureClassRegistered(HINSTANCE instance) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW description{};
  description.cbSize = sizeof(description);
  description.lpfnWndProc = PanelProc;
  description.hInstance = instance;
  description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  description.hbrBackground = CreateSolidBrush(kPanelBackground);
  description.lpszClassName = kPanelClassName;
  registered = RegisterClassExW(&description) != 0;
  return registered;
}

}  // namespace

void ClosePanel() {
  if (g_panel) PostMessageW(g_panel, WM_CLOSE, 0, 0);
}

void ShowPanel(HINSTANCE instance, HWND owner, HWND anchor,
               unsigned long blocked_count, bool standard_downloads,
               const SiteState& site_state) {
  // A second click on the shield button should dismiss the open panel rather
  // than stack another one.
  if (g_panel) {
    ClosePanel();
    return;
  }
  // The same click that reaches this command is what deactivated an open panel a
  // moment ago, so treat it as the closing half of the toggle.
  if (GetTickCount64() - g_closed_tick < kReopenGuardMs) return;
  if (!EnsureClassRegistered(instance)) {
    ShowFallbackSummary(owner, blocked_count, standard_downloads);
    return;
  }

  const UINT dpi = GetDpiForWindow(owner) ? GetDpiForWindow(owner) : 96;
  const int width = MulDiv(kPanelLogicalWidth, static_cast<int>(dpi), 96);
  const int height = MulDiv(kPanelLogicalHeight, static_cast<int>(dpi), 96);

  RECT anchor_rect{};
  if (!anchor || !GetWindowRect(anchor, &anchor_rect)) {
    GetWindowRect(owner, &anchor_rect);
  }
  // Right-aligning under the button is the usual toolbar-popup placement, but the
  // shield sits near the left edge, where that would push most of the panel off
  // screen and the clamp would drop it in the corner away from its button. Left
  // alignment is used whenever the right-aligned edge would not fit.
  MONITORINFO monitor{sizeof(monitor)};
  const bool have_monitor = GetMonitorInfoW(
      MonitorFromRect(&anchor_rect, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;
  int x = anchor_rect.right - width;
  if (have_monitor && x < monitor.rcWork.left) x = anchor_rect.left;
  int y = anchor_rect.bottom + MulDiv(kAnchorGapLogical, static_cast<int>(dpi), 96);

  // Keep the whole panel on the monitor the button lives on.
  if (have_monitor) {
    x = std::max<int>(monitor.rcWork.left, std::min<int>(x, monitor.rcWork.right - width));
    y = std::max<int>(monitor.rcWork.top, std::min<int>(y, monitor.rcWork.bottom - height));
  }

  g_panel = CreateWindowExW(WS_EX_TOOLWINDOW, kPanelClassName, L"MTK Shields",
                            WS_POPUP, x, y, width, height, owner, nullptr,
                            instance, nullptr);
  if (!g_panel) {
    ShowFallbackSummary(owner, blocked_count, standard_downloads);
    return;
  }
  // Win11 rounds the popup; on Win10 this attribute is ignored.
  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(g_panel, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                        sizeof(corner));

  CefRefPtr<PanelClient> client = new PanelClient(g_panel);
  g_client = client;

  CefWindowInfo info;
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  info.SetAsChild(g_panel, CefRect(0, 0, width, height));
  CefBrowserSettings settings;
  // Matches the page background so the panel never flashes white while loading.
  settings.background_color =
      CefColorSetARGB(255, kPanelRed, kPanelGreen, kPanelBlue);
  if (!CefBrowserHost::CreateBrowser(info, client,
                                     PanelUrl(blocked_count, standard_downloads,
                                              site_state),
                                     settings, nullptr, nullptr)) {
    client->Detach();
    g_client = nullptr;
    HWND panel = g_panel;
    g_panel = nullptr;
    DestroyWindow(panel);
    ShowFallbackSummary(owner, blocked_count, standard_downloads);
    return;
  }

  ShowWindow(g_panel, SW_SHOW);
  SetForegroundWindow(g_panel);
}

}  // namespace mtk::shields
