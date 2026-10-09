#include "shields_panel.h"

#include <dwmapi.h>

#include <string>
#include <utility>

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

constexpr int kPanelLogicalWidth = 354;
constexpr int kPanelLogicalHeight = 525;
constexpr int kAnchorGapLogical = 6;

constexpr BYTE kPanelRed = 0x0f;
constexpr BYTE kPanelGreen = 0x17;
constexpr BYTE kPanelBlue = 0x2a;
constexpr COLORREF kPanelBackground = RGB(kPanelRed, kPanelGreen, kPanelBlue);

constexpr UINT kBrowserClosedMessage = WM_APP + 41;
constexpr UINT kBrowserReadyMessage = WM_APP + 42;

HWND g_panel = nullptr;
CefRefPtr<CefClient> g_client;
bool g_dismiss_armed = false;
ULONGLONG g_closed_tick = 0;
constexpr ULONGLONG kReopenGuardMs = 450;
constexpr UINT_PTR kDismissTimer = 1;
constexpr UINT kDismissPollMs = 150;

Actions g_actions;

// Action URLs intercepted by OnBeforeBrowse
constexpr char kToggleShieldActionUrl[] = "mtk://shields/action/toggle-shield";
constexpr char kToggleCookiesActionUrl[] = "mtk://shields/action/toggle-cookies";
constexpr char kToggleDownloadsActionUrl[] = "mtk://shields/action/toggle-downloads";
constexpr char kOpenSiteSettingsActionUrl[] = "mtk://shields/action/open-site-settings";
constexpr char kOpenUserFiltersActionUrl[] = "mtk://shields/action/open-user-filters";
constexpr char kManageSitesActionUrl[] = "mtk://shields/action/manage-sites";

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

std::string PanelUrl(unsigned long blocked_count, bool standard_downloads,
                     const SiteState& site_state) {
  const bool is_protecting = site_state.site_filtering || site_state.site.empty();

  std::string art_class = is_protecting ? "shield-art" : "shield-art paused";
  std::string art =
      "<svg id=mtk-shield-art class=\"" + art_class + "\" viewBox=\"0 0 64 64\" fill=none>"
      "<defs>"
      "<linearGradient id=sg x1=0% y1=0% x2=100% y2=100%>"
      "<stop offset=0% stop-color=#a855f7/>"
      "<stop offset=50% stop-color=#6366f1/>"
      "<stop offset=100% stop-color=#10b981/>"
      "</linearGradient>"
      "<filter id=glow x=-20% y=-20% width=140% height=140%>"
      "<feDropShadow dx=0 dy=3 stdDeviation=4 flood-color=#6366f1 flood-opacity=0.55/>"
      "</filter>"
      "</defs>"
      "<path d=\"M32 6 L52 14 C52 36 32 54 32 54 C32 54 12 36 12 14 Z\" fill=\"url(#sg)\" filter=\"url(#glow)\"/>"
      "<path d=\"M32 10 L48 16.5 C48 34.5 32 49 32 49 C32 49 16 34.5 16 16.5 Z\" fill=#0f172a opacity=0.45/>"
      "<path d=\"M24 32 L29.5 37.5 L40.5 24\" stroke=#ffffff stroke-width=3.6 stroke-linecap=round stroke-linejoin=round/>"
      "</svg>";

  std::string pill_html;
  if (site_state.site.empty() || site_state.site_filtering) {
    pill_html = "<div id=mtk-pill class=\"pill on\"><span class=dot></span>SHIELDS UP</div>";
  } else {
    pill_html = "<div id=mtk-pill class=\"pill off\"><span class=dot></span>SHIELDS DOWN</div>";
  }

  std::string site_line_html;
  if (site_state.site.empty()) {
    site_line_html = "<div id=mtk-site-line class=site>This page has no site settings of its own</div>";
  } else {
    site_line_html = "<div id=mtk-site-line class=site><b>" + EscapeHtmlText(site_state.site) + "</b> ";
    if (!site_state.site_filtering) {
      site_line_html += "&middot; <span class=off>blocking paused on this site</span>";
    } else {
      site_line_html += "&middot; " + std::to_string(site_state.site_blocked_count) + " blocked here";
    }
    if (site_state.custom_rule_count > 0) {
      site_line_html += " &middot; " + std::to_string(site_state.custom_rule_count) + " by your rules";
    }
    site_line_html += "</div>";
  }

  // Row 1: Ad & tracker blocking
  std::string ad_ctrl;
  std::string ad_sub;
  if (!site_state.site.empty()) {
    const std::string sw_class = site_state.site_filtering ? "switch on" : "switch off";
    ad_ctrl = "<a id=mtk-ad-switch class=\"" + sw_class + "\" href=\"" + kToggleShieldActionUrl +
              "\" title=\"Toggle ad & tracker blocking for this site\"><span class=knob></span></a>";
    ad_sub = site_state.site_filtering ? "Blocking ads, tracking &amp; telemetry"
                                       : "Paused on this site";
  } else {
    ad_ctrl = "<span class=\"badge blue\">ACTIVE</span>";
    ad_sub = "Global filters active";
  }

  // Row 2: Cookies & site storage
  std::string cookie_ctrl;
  std::string cookie_sub;
  if (!site_state.site.empty()) {
    const std::string sw_class = site_state.cookies ? "switch on" : "switch off";
    cookie_ctrl = "<a id=mtk-cookie-switch class=\"" + sw_class + "\" href=\"" + kToggleCookiesActionUrl +
                  "\" title=\"Toggle cookies for this site\"><span class=knob></span></a>";
    cookie_sub = site_state.cookies ? "Cookies &amp; site data allowed"
                                    : "Cookies blocked on this site";
  } else {
    cookie_ctrl = "<span class=\"badge blue\">ALLOWED</span>";
    cookie_sub = "Standard browser storage";
  }

  // Row 3: Download protection
  std::string dl_ctrl =
      standard_downloads
          ? "<a id=mtk-dl-btn class=\"badge-btn warn\" href=\"" + std::string(kToggleDownloadsActionUrl) +
            "\" title=\"Standard mode: click to switch to Safe mode\">STANDARD</a>"
          : "<a id=mtk-dl-btn class=\"badge-btn safe\" href=\"" + std::string(kToggleDownloadsActionUrl) +
            "\" title=\"Safe mode: click to switch to Standard mode\">SAFE</a>";

  std::string dl_desc =
      standard_downloads
          ? "Any file saved (prompts on risky)"
          : "Programs &amp; scripts blocked";

  const std::string markup =
      "<meta charset=utf-8><title>MTK Shields</title><style>"
      ":root{color-scheme:dark}"
      "html,body{margin:0;height:100%;overflow:hidden}"
      "body{font-family:Segoe UI,-apple-system,BlinkMacSystemFont,Roboto,sans-serif;background:#0f172a;color:#f8fafc;"
      "border:1px solid rgba(148,163,184,0.22);box-sizing:border-box;display:flex;"
      "flex-direction:column;-webkit-user-select:none;user-select:none}"
      ".hero{flex:0 0 auto;display:flex;flex-direction:column;align-items:center;"
      "padding:12px 16px 8px;background:radial-gradient(160px 90px at 50% 8px,rgba(99,102,241,0.22),transparent 95%)}"
      ".shield-art{width:56px;height:56px;display:block;transition:all 0.25s ease}"
      ".shield-art.paused{filter:grayscale(0.9) opacity(0.45)}"
      "h1{margin:6px 0 0;font-size:16px;font-weight:700;letter-spacing:0.2px;color:#f8fafc}"
      ".pill{margin-top:6px;font-size:10px;font-weight:700;letter-spacing:1px;"
      "border-radius:99px;padding:3px 10px;display:inline-flex;align-items:center;gap:6px}"
      ".pill.on{color:#34d399;background:rgba(16,185,129,.16);border:1px solid rgba(16,185,129,.38)}"
      ".pill.off{color:#fbbf24;background:rgba(245,158,11,.16);border:1px solid rgba(245,158,11,.38)}"
      ".pill .dot{width:6px;height:6px;border-radius:50%;background:currentColor;box-shadow:0 0 6px currentColor}"
      ".count{flex:0 0 auto;display:flex;align-items:baseline;justify-content:center;"
      "gap:8px;padding:8px 16px;background:rgba(30,41,59,0.7);border-top:1px solid rgba(148,163,184,0.15);border-bottom:1px solid rgba(148,163,184,0.15)}"
      ".count b{font-size:22px;font-weight:800;color:#38bdf8}"
      ".count span{font-size:11px;color:#94a3b8;font-weight:500}"
      ".site{flex:0 0 auto;padding:7px 16px;background:rgba(15,23,42,0.6);border-bottom:1px solid rgba(148,163,184,0.15);"
      "font-size:11px;color:#94a3b8;text-align:center;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
      ".site b{color:#f8fafc;font-weight:600}"
      ".site .off{color:#fbbf24;font-weight:600}"
      ".rows{flex:1 1 auto;display:flex;flex-direction:column;gap:1px;padding:6px 14px;overflow:hidden}"
      ".row{display:flex;align-items:center;justify-content:space-between;padding:5px 0;"
      "border-bottom:1px solid rgba(148,163,184,0.12)}"
      ".row:last-child{border-bottom:none}"
      ".row-main{flex:1 1 auto;min-width:0;padding-right:8px}"
      ".row-title{font-size:12px;font-weight:600;color:#f8fafc;line-height:1.25}"
      ".row-sub{font-size:10px;color:#94a3b8;line-height:1.25;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
      ".row-ctrl{flex:0 0 auto;display:flex;align-items:center}"
      ".switch{display:inline-flex;align-items:center;width:36px;height:20px;background:#334155;"
      "border-radius:99px;padding:2px;box-sizing:border-box;text-decoration:none;cursor:pointer;transition:background 0.2s ease}"
      ".switch.on{background:#10b981}"
      ".switch .knob{width:16px;height:16px;background:#ffffff;border-radius:50%;"
      "box-shadow:0 1px 3px rgba(0,0,0,0.4);transform:translateX(0);transition:transform 0.18s cubic-bezier(0.4, 0, 0.2, 1)}"
      ".switch.on .knob{transform:translateX(16px)}"
      ".badge{display:inline-block;padding:2px 7px;font-size:9px;font-weight:700;"
      "letter-spacing:0.5px;border-radius:4px;text-transform:uppercase}"
      ".badge.blue{background:rgba(56,189,248,.14);color:#38bdf8;border:1px solid rgba(56,189,248,.3)}"
      ".badge.cyan{background:rgba(6,182,212,.14);color:#22d3ee;border:1px solid rgba(6,182,212,.3)}"
      ".badge.purple{background:rgba(168,85,247,.14);color:#c084fc;border:1px solid rgba(168,85,247,.3)}"
      ".badge.slate{background:rgba(148,163,184,.14);color:#94a3b8;border:1px solid rgba(148,163,184,.3)}"
      ".badge-btn{display:inline-block;padding:2px 7px;font-size:9px;font-weight:700;"
      "letter-spacing:0.5px;border-radius:4px;text-transform:uppercase;text-decoration:none;cursor:pointer;transition:all 0.15s ease}"
      ".badge-btn.safe{background:rgba(16,185,129,.16);color:#34d399;border:1px solid rgba(16,185,129,.4)}"
      ".badge-btn.safe:hover{background:rgba(16,185,129,.28)}"
      ".badge-btn.warn{background:rgba(245,158,11,.16);color:#fbbf24;border:1px solid rgba(245,158,11,.4)}"
      ".badge-btn.warn:hover{background:rgba(245,158,11,.28)}"
      ".footer-btns{flex:0 0 auto;display:flex;gap:8px;padding:7px 14px;background:rgba(15,23,42,0.8);border-top:1px solid rgba(148,163,184,0.15)}"
      ".fbtn{flex:1 1 50%;display:flex;align-items:center;justify-content:space-between;"
      "padding:6px 10px;background:#1e293b;border:1px solid rgba(148,163,184,0.22);border-radius:6px;"
      "color:#cbd5e1;text-decoration:none;font-size:11px;font-weight:500;box-sizing:border-box;transition:all 0.15s ease}"
      ".fbtn:hover{background:#334155;color:#ffffff;border-color:rgba(148,163,184,0.4)}"
      ".fbtn kbd{background:#0f172a;border:1px solid #334155;border-radius:3px;padding:1px 5px;font-size:9px;color:#94a3b8}"
      ".manage{flex:0 0 auto;display:block;padding:8px 14px;font-size:11px;"
      "text-align:center;color:#60a5fa;text-decoration:none;border-top:1px solid rgba(148,163,184,0.15);transition:color 0.15s ease}"
      ".manage:hover{color:#93c5fd;text-decoration:underline}"
      "</style>"
      "<div class=hero>" + art + "<h1>MTK Shields</h1>" + pill_html + "</div>"
      "<div class=count><b id=mtk-total-count>" + std::to_string(blocked_count) + "</b><span>trackers &amp; ads blocked this session</span></div>" +
      site_line_html +
      "<div class=rows>"
      "<div class=row><div class=row-main><div class=row-title>Trackers &amp; ads blocking</div><div id=mtk-ad-sub class=row-sub>" + ad_sub + "</div></div><div class=row-ctrl>" + ad_ctrl + "</div></div>"
      "<div class=row><div class=row-main><div class=row-title>Cookies &amp; storage</div><div id=mtk-cookie-sub class=row-sub>" + cookie_sub + "</div></div><div class=row-ctrl>" + cookie_ctrl + "</div></div>"
      "<div class=row><div class=row-main><div class=row-title>Download protection</div><div id=mtk-dl-sub class=row-sub>" + dl_desc + "</div></div><div class=row-ctrl>" + dl_ctrl + "</div></div>"
      "<div class=row><div class=row-main><div class=row-title>SafeSearch &amp; HTTPS</div><div class=row-sub>Strict search &amp; upgraded connection</div></div><div class=row-ctrl><span class=\"badge cyan\">ENFORCED</span></div></div>"
      "<div class=row><div class=row-main><div class=row-title>Adult &amp; piracy domains</div><div class=row-sub>Restricted domains &amp; terms blocked</div></div><div class=row-ctrl><span class=\"badge purple\">BLOCKED</span></div></div>"
      "<div class=row><div class=row-main><div class=row-title>Popups &amp; permissions</div><div class=row-sub>Popups in tabs; risky permissions denied</div></div><div class=row-ctrl><span class=\"badge slate\">PROTECTED</span></div></div>"
      "</div>"
      "<div class=footer-btns>"
      "<a class=fbtn href=\"" + std::string(kOpenSiteSettingsActionUrl) + "\" title=\"Site Settings (Alt+S)\"><span>Site Settings</span><kbd>Alt+S</kbd></a>"
      "<a class=fbtn href=\"" + std::string(kOpenUserFiltersActionUrl) + "\" title=\"Your Filters (Alt+Y)\"><span>Your Filters</span><kbd>Alt+Y</kbd></a>"
      "</div>"
      "<a class=manage href=\"" + std::string(kManageSitesActionUrl) + "\">Manage all sites with stored settings</a>"
      "<script>"
      "window.__mtkUpdate=function(total,dlStd,site,siteBlocked,siteFilter,cookies,customRules){"
      "var t=document.getElementById('mtk-total-count');if(t)t.textContent=total;"
      "var p=document.getElementById('mtk-pill');if(p){if(!site||siteFilter){p.className='pill on';p.innerHTML='<span class=dot></span>SHIELDS UP';}else{p.className='pill off';p.innerHTML='<span class=dot></span>SHIELDS DOWN';}}"
      "var s=document.getElementById('mtk-site-line');if(s){if(!site){s.textContent='This page has no site settings of its own';}else{var h='<b>'+site+'</b> ';if(!siteFilter){h+='&middot; <span class=off>blocking paused on this site</span>';}else{h+='&middot; '+siteBlocked+' blocked here';}if(customRules>0){h+=' &middot; '+customRules+' by your rules';}s.innerHTML=h;}}"
      "var as=document.getElementById('mtk-ad-switch');if(as)as.className=siteFilter?'switch on':'switch off';"
      "var asub=document.getElementById('mtk-ad-sub');if(asub)asub.textContent=siteFilter?'Blocking ads, tracking & telemetry':'Paused on this site';"
      "var cs=document.getElementById('mtk-cookie-switch');if(cs)cs.className=cookies?'switch on':'switch off';"
      "var csub=document.getElementById('mtk-cookie-sub');if(csub)csub.textContent=cookies?'Cookies & site data allowed':'Cookies blocked on this site';"
      "var db=document.getElementById('mtk-dl-btn');if(db){db.className=dlStd?'badge-btn warn':'badge-btn safe';db.textContent=dlStd?'STANDARD':'SAFE';}"
      "var dsub=document.getElementById('mtk-dl-sub');if(dsub)dsub.textContent=dlStd?'Any file saved (prompts on risky)':'Programs & scripts blocked';"
      "var art=document.getElementById('mtk-shield-art');if(art){art.className=(!site||siteFilter)?'shield-art':'shield-art paused';}"
      "};"
      "</script>";

  return "data:text/html," + EscapeForDataUrl(markup);
}

void ShowFallbackSummary(HWND owner, unsigned long blocked_count,
                         bool standard_downloads) {
  const std::wstring summary =
      L"MTK Shields are active.\n\nBlocked on this session: " +
      std::to_wstring(blocked_count) +
      L"\n\n"
      L"\u2022 Ad, tracker and telemetry requests blocked\n"
      L"\u2022 Adult and piracy domains blocked\n"
      L"\u2022 SafeSearch and HTTPS upgrade enforced\n" +
      (standard_downloads
           ? std::wstring(L"\u2022 Downloads: standard mode, any file type is saved\n")
           : std::wstring(L"\u2022 Downloads: safe mode, programs and scripts blocked\n")) +
      L"\u2022 Popups isolated to tabs; risky permissions denied";
  MessageBoxW(owner, summary.c_str(), L"MTK Shields", MB_OK | MB_ICONINFORMATION);
}

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
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    if (!panel_) {
      browser->GetHost()->CloseBrowser(true);
      return;
    }
    PostMessageW(panel_, kBrowserReadyMessage, 0, 0);
  }

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
    if (event.type == KEYEVENT_RAWKEYDOWN) {
      if (event.windows_key_code == VK_ESCAPE) {
        if (panel_) PostMessageW(panel_, WM_CLOSE, 0, 0);
        return true;
      }
      if ((event.modifiers & EVENTFLAG_ALT_DOWN) != 0) {
        if (event.windows_key_code == 'S') {
          if (panel_) PostMessageW(panel_, WM_CLOSE, 0, 0);
          if (g_actions.on_open_site_settings) g_actions.on_open_site_settings();
          return true;
        }
        if (event.windows_key_code == 'Y') {
          if (panel_) PostMessageW(panel_, WM_CLOSE, 0, 0);
          if (g_actions.on_open_user_filters) g_actions.on_open_user_filters();
          return true;
        }
      }
    }
    return false;
  }

  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefContextMenuParams>,
                           CefRefPtr<CefMenuModel> model) override {
    if (model) model->Clear();
  }

  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                      CefRefPtr<CefRequest> request, bool, bool) override {
    const std::string url = request->GetURL().ToString();

    if (url == kToggleShieldActionUrl) {
      if (g_actions.on_toggle_shield) g_actions.on_toggle_shield();
      return true;
    }
    if (url == kToggleCookiesActionUrl) {
      if (g_actions.on_toggle_cookies) g_actions.on_toggle_cookies();
      return true;
    }
    if (url == kToggleDownloadsActionUrl) {
      if (g_actions.on_toggle_downloads) g_actions.on_toggle_downloads();
      return true;
    }
    if (url == kOpenSiteSettingsActionUrl) {
      if (g_actions.on_open_site_settings) g_actions.on_open_site_settings();
      return true;
    }
    if (url == kOpenUserFiltersActionUrl) {
      if (g_actions.on_open_user_filters) g_actions.on_open_user_filters();
      return true;
    }
    if (url == kManageSitesActionUrl) {
      if (g_actions.on_manage_sites) g_actions.on_manage_sites();
      return true;
    }

    return !IsPanelDocument(url);
  }

  bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                        const CefString&, cef_window_open_disposition_t,
                        bool) override {
    return true;
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
    if (IsPanelDocument(url) || url.starts_with(mtk::assets::kWebmDataUrlPrefix)) {
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
  g_actions = {};
  g_dismiss_armed = false;
  g_closed_tick = GetTickCount64();
  if (g_panel) {
    HWND panel = g_panel;
    g_panel = nullptr;
    KillTimer(panel, kDismissTimer);
    DestroyWindow(panel);
  }
}

bool PanelHoldsForeground(HWND panel) {
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return false;
  return foreground == panel || GetAncestor(foreground, GA_ROOT) == panel;
}

LRESULT CALLBACK PanelProc(HWND window, UINT message, WPARAM w_param,
                           LPARAM l_param) {
  switch (message) {
    case kBrowserReadyMessage:
      g_dismiss_armed = true;
      SetTimer(window, kDismissTimer, kDismissPollMs, nullptr);
      return 0;
    case WM_ACTIVATE:
      if (g_dismiss_armed && LOWORD(w_param) == WA_INACTIVE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_TIMER:
      if (w_param == kDismissTimer && g_dismiss_armed &&
          !PanelHoldsForeground(window)) {
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_KEYDOWN:
      if (w_param == VK_ESCAPE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_CLOSE: {
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
      return 1;
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

bool IsPanelOpen() {
  return g_panel != nullptr;
}

void UpdatePanel(unsigned long blocked_count, bool standard_downloads,
                 const SiteState& site_state) {
  if (!g_panel) return;
  auto* client = CurrentClient();
  if (client && client->browser() && client->browser()->GetMainFrame()) {
    std::string js =
        "if(window.__mtkUpdate){window.__mtkUpdate(" +
        std::to_string(blocked_count) + "," +
        (standard_downloads ? "true" : "false") + ",\"" +
        EscapeHtmlText(site_state.site) + "\"," +
        std::to_string(site_state.site_blocked_count) + "," +
        (site_state.site_filtering ? "true" : "false") + "," +
        (site_state.cookies ? "true" : "false") + "," +
        std::to_string(site_state.custom_rule_count) +
        ");}else{location.href=\"" +
        PanelUrl(blocked_count, standard_downloads, site_state) + "\";}";
    client->browser()->GetMainFrame()->ExecuteJavaScript(
        js, "mtk://shields/update", 0);
  }
}

void ShowPanel(HINSTANCE instance, HWND owner, HWND anchor,
               unsigned long blocked_count, bool standard_downloads,
               const SiteState& site_state, Actions actions) {
  if (g_panel) {
    ClosePanel();
    return;
  }
  if (GetTickCount64() - g_closed_tick < kReopenGuardMs) return;
  if (!EnsureClassRegistered(instance)) {
    ShowFallbackSummary(owner, blocked_count, standard_downloads);
    return;
  }

  g_actions = std::move(actions);

  const UINT dpi = GetDpiForWindow(owner) ? GetDpiForWindow(owner) : 96;
  const int width = MulDiv(kPanelLogicalWidth, static_cast<int>(dpi), 96);
  const int height = MulDiv(kPanelLogicalHeight, static_cast<int>(dpi), 96);

  RECT anchor_rect{};
  if (!anchor || !GetWindowRect(anchor, &anchor_rect)) {
    GetWindowRect(owner, &anchor_rect);
  }

  MONITORINFO monitor{sizeof(monitor)};
  const bool have_monitor = GetMonitorInfoW(
      MonitorFromRect(&anchor_rect, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;
  int x = anchor_rect.right - width;
  if (have_monitor && x < monitor.rcWork.left) x = anchor_rect.left;
  int y = anchor_rect.bottom + MulDiv(kAnchorGapLogical, static_cast<int>(dpi), 96);

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

  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(g_panel, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                        sizeof(corner));

  CefRefPtr<PanelClient> client = new PanelClient(g_panel);
  g_client = client;

  CefWindowInfo info;
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  info.SetAsChild(g_panel, CefRect(0, 0, width, height));
  CefBrowserSettings settings;
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
