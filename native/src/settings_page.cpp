#include "settings_page.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "download_manager.h"
#include "home_page.h"

namespace mtk::settings_page {
namespace {

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::string Lowered(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool DecodeFormValue(std::string_view raw, std::string* decoded) {
  if (raw.size() > 600) return false;
  std::string result;
  result.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '+') {
      result.push_back(' ');
      continue;
    }
    if (c == '%') {
      if (i + 2 >= raw.size()) return false;
      const int high = HexValue(raw[i + 1]);
      const int low = HexValue(raw[i + 2]);
      if (high < 0 || low < 0) return false;
      result.push_back(static_cast<char>(high * 16 + low));
      i += 2;
      continue;
    }
    result.push_back(c);
  }
  for (const unsigned char c : result) {
    if (c < 0x20 || c == 0x7f) return false;
  }
  *decoded = std::move(result);
  return true;
}

std::string ToggleRow(std::string_view title, std::string_view description,
                      std::string_view action_url, bool is_on,
                      std::string_view query) {
  if (!query.empty()) {
    const std::string lq = Lowered(query);
    if (Lowered(title).find(lq) == std::string::npos &&
        Lowered(description).find(lq) == std::string::npos) {
      return {};
    }
  }
  std::string row = "<div class=\"setting-row\">";
  row += "<div class=\"setting-info\"><b class=\"setting-title\">" +
         home::EscapeHtml(title) + "</b>";
  if (!description.empty()) {
    row += "<small class=\"setting-desc\">" + home::EscapeHtml(description) + "</small>";
  }
  row += "</div>";
  row += "<a class=\"toggle-btn " + std::string(is_on ? "on" : "off") +
         "\" href=\"" + std::string(action_url) + "\" role=\"switch\" aria-checked=\"" +
         (is_on ? "true" : "false") + "\"><span class=\"toggle-knob\"></span></a>";
  row += "</div>";
  return row;
}

}  // namespace

bool IsSettingsUrl(std::string_view url) {
  if (url == kUrl || url == "mtk://settings") return true;
  const std::string_view prefix(kUrl);
  if (!url.starts_with(prefix)) return false;
  std::string_view rest = url.substr(prefix.size());
  return rest.starts_with("?q=");
}

std::string QueryOf(std::string_view url) {
  const std::string_view prefix(kUrl);
  if (!url.starts_with(prefix)) return {};
  std::string_view rest = url.substr(prefix.size());
  if (!rest.starts_with("?q=")) return {};
  rest.remove_prefix(3);
  std::string decoded;
  if (!DecodeFormValue(rest, &decoded)) return {};
  return decoded;
}

Action ParseActionUrl(std::string_view url) {
  Action action;
  constexpr std::string_view kActionPrefix = "mtk://settings/action/";
  if (!url.starts_with(kActionPrefix)) return action;
  const std::string_view rest = url.substr(kActionPrefix.size());

  if (rest == "open-folder") {
    action.kind = ActionKind::kOpenDownloadFolder;
    return action;
  }
  if (rest == "clear-data") {
    action.kind = ActionKind::kClearBrowsingData;
    return action;
  }
  if (rest == "reset-settings") {
    action.kind = ActionKind::kResetSettings;
    return action;
  }

  constexpr std::string_view kTogglePrefix = "toggle?key=";
  if (rest.starts_with(kTogglePrefix)) {
    const std::string_view key = rest.substr(kTogglePrefix.size());
    if (key == "https_only") action.kind = ActionKind::kToggleHttpsOnly;
    else if (key == "fingerprint") action.kind = ActionKind::kToggleFingerprint;
    else if (key == "focus_mode") action.kind = ActionKind::kToggleFocusMode;
    else if (key == "memory_saver") action.kind = ActionKind::kToggleMemorySaver;
    else if (key == "vertical_tabs") action.kind = ActionKind::kToggleVerticalTabs;
    else if (key == "ask_download") action.kind = ActionKind::kToggleDownloadAskLocation;
    else if (key == "clear_on_exit") action.kind = ActionKind::kToggleClearOnExit;
    else if (key == "familiar_shortcuts") action.kind = ActionKind::kToggleFamiliarShortcuts;
    else if (key == "windows_hello") action.kind = ActionKind::kToggleWindowsHello;
    return action;
  }

  constexpr std::string_view kThemePrefix = "set-theme?val=";
  if (rest.starts_with(kThemePrefix)) {
    action.kind = ActionKind::kSetTheme;
    action.value = std::string(rest.substr(kThemePrefix.size()));
    return action;
  }

  constexpr std::string_view kSearchPrefix = "set-search?val=";
  if (rest.starts_with(kSearchPrefix)) {
    action.kind = ActionKind::kSetSearchEngine;
    action.value = std::string(rest.substr(kSearchPrefix.size()));
    return action;
  }

  constexpr std::string_view kStartupPrefix = "set-startup?val=";
  if (rest.starts_with(kStartupPrefix)) {
    action.kind = ActionKind::kSetStartupMode;
    action.value = std::string(rest.substr(kStartupPrefix.size()));
    return action;
  }

  constexpr std::string_view kDohPrefix = "set-doh?val=";
  if (rest.starts_with(kDohPrefix)) {
    action.kind = ActionKind::kSetDohProvider;
    action.value = std::string(rest.substr(kDohPrefix.size()));
    return action;
  }

  return action;
}

std::string BuildHtml(const preferences::Values& prefs,
                      std::string_view query, bool light_appearance,
                      bool /*hindi*/) {
  static constexpr char kHead[] = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><meta name=referrer content=no-referrer>
<meta http-equiv=Content-Security-Policy content="default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; object-src 'none'; frame-src 'none'; connect-src 'none'; img-src data:; font-src 'none'; script-src 'none'">
<title>Settings — MTK Browser</title><style>
:root{
  color-scheme: dark;
  --bg: #18181b;
  --panel: #222226;
  --panel-hover: #27272b;
  --border: rgba(255, 255, 255, 0.08);
  --border-focus: #0078d4;
  --text: #f4f4f5;
  --text-secondary: #a1a1aa;
  --text-muted: #71717a;
  --accent: #0078d4;
  --accent-hover: #1084d8;
  --danger: #ef4444;
}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%}
body{background:var(--bg);color:var(--text);font:13.5px/1.4 -apple-system,BlinkMacSystemFont,'Segoe UI Variable Text','Segoe UI',Roboto,Helvetica,Arial,sans-serif;-webkit-font-smoothing:antialiased}
a{color:inherit;text-decoration:none}
.wrap{max-width:920px;margin:0 auto;padding:28px 24px 60px}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:20px}
.header-title-box{display:flex;align-items:center;gap:12px}
.header-title-box h1{margin:0;font-size:20px;font-weight:600;letter-spacing:-0.01em}
.back-link{width:28px;height:28px;border-radius:6px;display:grid;place-items:center;color:var(--text-secondary);transition:all .15s}
.back-link:hover{background:rgba(255,255,255,.08);color:var(--text)}
.header-right{display:flex;align-items:center;gap:10px;flex:1;max-width:440px;justify-content:flex-end}
form.find{position:relative;flex:1}
form.find svg{position:absolute;left:11px;top:9px;color:var(--text-muted);pointer-events:none}
form.find input{width:100%;height:32px;border:1px solid var(--border);border-radius:6px;background:var(--panel);color:var(--text);padding:0 12px 0 34px;font-size:12.5px;outline:none;transition:border-color .15s,box-shadow .15s}
form.find input:focus{border-color:var(--border-focus);box-shadow:0 0 0 2px rgba(0,120,212,.2)}
.btn-nav{height:32px;padding:0 12px;display:inline-flex;align-items:center;border:1px solid var(--border);border-radius:6px;background:var(--panel);color:var(--text-secondary);font-size:12px;font-weight:500;cursor:pointer;white-space:nowrap;transition:all .15s}
.btn-nav:hover{background:var(--panel-hover);color:var(--text);border-color:rgba(255,255,255,.15)}
.card{background:var(--panel);border:1px solid var(--border);border-radius:8px;padding:16px 18px;margin-bottom:14px}
.card h2{font-size:13.5px;font-weight:600;margin:0 0 12px;display:flex;align-items:center;gap:8px;color:var(--text)}
.card h2 .sym{color:var(--accent);font-size:14px}
.setting-row{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:10px 0;border-bottom:1px solid var(--border)}
.setting-row:last-child{border-bottom:none;padding-bottom:0}
.setting-info{min-width:0;flex:1}
.setting-title{font-size:13px;font-weight:550;color:var(--text);display:block}
.setting-desc{font-size:11.5px;color:var(--text-muted);margin-top:2px;display:block}
.toggle-btn{width:40px;height:22px;border-radius:11px;display:inline-flex;align-items:center;padding:2px;cursor:pointer;transition:background-color .15s,border-color .15s;flex-shrink:0}
.toggle-btn.off{background:transparent;border:2px solid #71717a}
.toggle-btn.on{background:var(--accent);border:2px solid var(--accent)}
.toggle-knob{width:14px;height:14px;border-radius:50%;background:#a1a1aa;transition:transform .15s ease,background-color .15s ease}
.toggle-btn.on .toggle-knob{transform:translateX(18px);background:#fff}
.toggle-btn.off .toggle-knob{transform:translateX(0)}
.chips{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}
.chip{padding:5px 12px;border-radius:6px;background:rgba(255,255,255,.05);border:1px solid var(--border);color:var(--text-secondary);font-size:11.5px;font-weight:500;cursor:pointer;transition:all .12s}
.chip:hover{border-color:rgba(255,255,255,.2);color:var(--text)}
.chip.active{background:var(--accent);border-color:var(--accent);color:#fff;font-weight:600}
.action-btn{display:inline-flex;align-items:center;padding:5px 12px;border-radius:6px;font-size:11.5px;font-weight:500;border:1px solid var(--border);background:rgba(255,255,255,.05);color:var(--text);cursor:pointer;transition:all .12s}
.action-btn:hover{background:rgba(255,255,255,.1);border-color:rgba(255,255,255,.18)}
.action-btn.danger{border-color:rgba(239,68,68,.3);color:#fca5a5}
.action-btn.danger:hover{background:rgba(239,68,68,.15);color:#ef4444;border-color:#ef4444}
</style>)HTML";

  std::string html = kHead;
  if (light_appearance) {
    html += R"HTML(<style>
:root{
  color-scheme: light;
  --bg: #f4f4f6;
  --panel: #ffffff;
  --panel-hover: #f9f9fb;
  --border: #e4e4e7;
  --border-focus: #0067b8;
  --text: #18181b;
  --text-secondary: #52525b;
  --text-muted: #a1a1aa;
  --accent: #0067b8;
  --accent-hover: #005a9e;
  --danger: #dc2626;
}
body{background:var(--bg)}
form.find input{background:#fff;border-color:#d4d4d8}
.btn-nav{background:#fff;border-color:#d4d4d8}
.card{background:#fff;border-color:#e4e4e7;box-shadow:0 1px 2px rgba(0,0,0,.03)}
.toggle-btn.off{border-color:#a1a1aa}
.toggle-btn.off .toggle-knob{background:#71717a}
.chip{background:#f4f4f5;border-color:#e4e4e7;color:var(--text-secondary)}
.chip:hover{border-color:#d4d4d8;color:var(--text)}
.chip.active{background:var(--accent);color:#fff;border-color:var(--accent)}
.action-btn{background:#f4f4f5;border-color:#e4e4e7;color:var(--text)}
.action-btn:hover{background:#ebebed}
</style>)HTML";
  }

  html += "</head><body><div class=wrap>";
  html += "<header><div class=\"header-title-box\">";
  html += "<a class=\"back-link\" href=\"" + std::string(home::kUrl) +
          "\" title=\"Back to Home\"><svg width=16 height=16 viewBox=\"0 0 24 24\" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round stroke-linejoin=round><path d=\"M19 12H5M12 19l-7-7 7-7\"/></svg></a>";
  html += "<h1>Settings</h1></div>";

  html += "<div class=\"header-right\">";
  html += "<form class=find method=get action=\"" + std::string(kUrl) + "\">";
  html += "<svg width=14 height=14 viewBox=\"0 0 24 24\" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round stroke-linejoin=round><circle cx=11 cy=11 r=8/><line x1=21 y1=21 x2=16.65 y2=16.65/></svg>";
  html += "<input name=q type=search value=\"" + home::EscapeHtml(query) +
          "\" placeholder=\"Find a setting...\" autocomplete=off spellcheck=false maxlength=200 aria-label=\"Search settings\">";
  html += "</form>";
  if (!query.empty()) {
    html += "<a class=\"btn-nav\" href=\"" + std::string(kUrl) + "\">Clear search</a>";
  }
  html += "</div></header>";

  // Section 1: General
  std::string sec_general;
  // Startup
  sec_general += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">On Startup</b><small class=\"setting-desc\">Choose what opens when MTK Browser starts</small><div class=\"chips\">";
  const bool start_home = (prefs.startup_mode == preferences::StartupMode::kHomePage);
  sec_general += "<a class=\"chip " + std::string(start_home ? "active" : "") + "\" href=\"mtk://settings/action/set-startup?val=home\">Open Home Dashboard</a>";
  sec_general += "<a class=\"chip " + std::string(!start_home ? "active" : "") + "\" href=\"mtk://settings/action/set-startup?val=restore\">Restore Previous Tabs</a>";
  sec_general += "</div></div></div>";

  // Search Engine
  sec_general += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">Default Search Engine</b><small class=\"setting-desc\">Used for searches typed into the omnibox address bar</small><div class=\"chips\">";
  const std::string& se = prefs.search_engine;
  sec_general += "<a class=\"chip " + std::string(se == "google" ? "active" : "") + "\" href=\"mtk://settings/action/set-search?val=google\">Google</a>";
  sec_general += "<a class=\"chip " + std::string(se == "bing" ? "active" : "") + "\" href=\"mtk://settings/action/set-search?val=bing\">Bing</a>";
  sec_general += "<a class=\"chip " + std::string(se == "duckduckgo" ? "active" : "") + "\" href=\"mtk://settings/action/set-search?val=duckduckgo\">DuckDuckGo</a>";
  sec_general += "</div></div></div>";

  // Familiar shortcuts
  sec_general += ToggleRow("Familiar Keyboard Shortcuts",
      "Enable Ctrl+Shift+T, Ctrl+H, Ctrl+J and common browser key combinations",
      "mtk://settings/action/toggle?key=familiar_shortcuts",
      prefs.familiar_shortcuts, query);

  if (!sec_general.empty()) {
    html += "<div class=card><h2><span class=sym>⚙</span>General</h2>" + sec_general + "</div>";
  }

  // Section 2: Privacy & Security
  std::string sec_privacy;
  sec_privacy += ToggleRow("HTTPS-Only Mode",
      "Upgrade all navigations to HTTPS and strictly refuse unencrypted plain HTTP subresources",
      "mtk://settings/action/toggle?key=https_only",
      prefs.https_only, query);

  sec_privacy += ToggleRow("Advanced Anti-Fingerprinting",
      "Farble canvas/audio readback and strip high-entropy User-Agent Client Hints to prevent tracking",
      "mtk://settings/action/toggle?key=fingerprint",
      prefs.fingerprint_protection, query);

  sec_privacy += ToggleRow("Focus Mode",
      "Hide distraction algorithms, recommended videos, and rabbit-hole loops on video platforms",
      "mtk://settings/action/toggle?key=focus_mode",
      prefs.focus_mode, query);

  sec_privacy += ToggleRow("Clear Activity on Exit",
      "Automatically remove browsing history and finished downloads when closing MTK Browser",
      "mtk://settings/action/toggle?key=clear_on_exit",
      prefs.clear_activity_on_exit, query);

  sec_privacy += ToggleRow("Windows Hello Security",
      "Require Windows biometric or PIN authentication before viewing saved passwords",
      "mtk://settings/action/toggle?key=windows_hello",
      prefs.windows_hello_unlock, query);

  // Encrypted DNS
  sec_privacy += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">Secure DNS (DNS-over-HTTPS)</b><small class=\"setting-desc\">Encrypt domain lookups to protect against ISP eavesdropping</small><div class=\"chips\">";
  auto doh_chip = [&](preferences::DohProvider prov, const char* id, const char* label) {
    const bool is_act = (prefs.doh_provider == prov);
    return "<a class=\"chip " + std::string(is_act ? "active" : "") +
           "\" href=\"mtk://settings/action/set-doh?val=" + id + "\">" + label + "</a>";
  };
  sec_privacy += doh_chip(preferences::DohProvider::kCloudflareFamily, "family", "Cloudflare Family");
  sec_privacy += doh_chip(preferences::DohProvider::kCloudflare, "cloudflare", "Cloudflare Standard");
  sec_privacy += doh_chip(preferences::DohProvider::kGoogle, "google", "Google Public DNS");
  sec_privacy += doh_chip(preferences::DohProvider::kQuad9, "quad9", "Quad9 (Malware blocking)");
  sec_privacy += doh_chip(preferences::DohProvider::kSystem, "system", "System Default");
  sec_privacy += "</div></div></div>";

  // Clear data action
  sec_privacy += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">Clear Browsing Data</b><small class=\"setting-desc\">Purge cookies, cached files, history, and active sessions</small></div>";
  sec_privacy += "<a class=\"action-btn danger\" href=\"mtk://settings/action/clear-data\">Clear data...</a></div>";

  if (!sec_privacy.empty()) {
    html += "<div class=card><h2><span class=sym>🛡</span>Privacy &amp; Security</h2>" + sec_privacy + "</div>";
  }

  // Section 3: Downloads
  std::string sec_downloads;
  sec_downloads += ToggleRow("Ask Where to Save Each File Before Downloading",
      "Prompt with a Save As dialog to choose the destination folder and filename for every download",
      "mtk://settings/action/toggle?key=ask_download",
      prefs.download_ask_location, query);

  const std::wstring dl_dir = downloads::DownloadDirectory();
  sec_downloads += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">Download Location</b><small class=\"setting-desc\">Default folder: " +
                   home::EscapeHtml(WideToUtf8(dl_dir)) + "</small></div>";
  sec_downloads += "<a class=\"action-btn\" href=\"mtk://settings/action/open-folder\">Open folder</a></div>";

  if (!sec_downloads.empty()) {
    html += "<div class=card><h2><span class=sym>⇩</span>Downloads</h2>" + sec_downloads + "</div>";
  }

  // Section 4: Appearance
  std::string sec_appearance;
  sec_appearance += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">Theme Mode</b><small class=\"setting-desc\">Customize the browser appearance</small><div class=\"chips\">";
  const auto tm = prefs.theme_mode;
  sec_appearance += "<a class=\"chip " + std::string(tm == preferences::ThemeMode::kSystem ? "active" : "") + "\" href=\"mtk://settings/action/set-theme?val=system\">Follow Windows</a>";
  sec_appearance += "<a class=\"chip " + std::string(tm == preferences::ThemeMode::kDark ? "active" : "") + "\" href=\"mtk://settings/action/set-theme?val=dark\">Dark Theme</a>";
  sec_appearance += "<a class=\"chip " + std::string(tm == preferences::ThemeMode::kLight ? "active" : "") + "\" href=\"mtk://settings/action/set-theme?val=light\">Light Theme</a>";
  sec_appearance += "</div></div></div>";

  sec_appearance += ToggleRow("Vertical Tabs",
      "Arrange open tabs in an expandable, compact left sidebar instead of the top strip",
      "mtk://settings/action/toggle?key=vertical_tabs",
      prefs.vertical_tabs, query);

  sec_appearance += ToggleRow("Memory Saver (Tab Sleeping)",
      "Suspend background unpinned tabs to free up RAM when memory pressure rises",
      "mtk://settings/action/toggle?key=memory_saver",
      prefs.memory_saver, query);

  if (!sec_appearance.empty()) {
    html += "<div class=card><h2><span class=sym>🎨</span>Appearance &amp; Performance</h2>" + sec_appearance + "</div>";
  }

  // Section 5: Reset
  html += "<div class=card><h2><span class=sym>↺</span>Reset Settings</h2>";
  html += "<div class=\"setting-row\"><div class=\"setting-info\"><b class=\"setting-title\">Restore Settings to Safe Defaults</b><small class=\"setting-desc\">Resets startup, appearance, search engine, and DoH back to defaults. Does not touch your bookmarks, history, or saved passwords.</small></div>";
  html += "<a class=\"action-btn danger\" href=\"mtk://settings/action/reset-settings\">Reset settings...</a></div>";
  html += "</div>";

  html += "</div></body></html>";
  return html;
}

}  // namespace mtk::settings_page
