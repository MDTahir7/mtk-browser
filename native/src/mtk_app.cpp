#include "mtk_app.h"

#include <string>

#include "browser_window.h"
#include "fingerprint_script.h"
#include "home_scheme.h"
#include "password_bridge.h"
#include "preferences.h"
#include "site_settings.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_request_context.h"
#include "include/cef_v8.h"
#include "include/cef_values.h"

namespace {

constexpr char kBotChallengeCompatScript[] = R"JS(
(() => {
  if (window.__mtkBotCompat) return;
  Object.defineProperty(window, '__mtkBotCompat', {
    value: true, configurable: false, enumerable: false
  });

  const originalGetById = Document.prototype.getElementById;
  Document.prototype.getElementById = function(id) {
    const element = originalGetById.call(this, id);
    if (element) return element;
    if (id === 'progress' || id === 'status') {
      const path = (location && location.pathname) || '';
      const host = (location && location.hostname) || '';
      if (path.includes('.within.website') || host.includes('.within.website') ||
          path.includes('anubis') || host.includes('anubis') ||
          originalGetById.call(this, 'anubis_challenge') ||
          originalGetById.call(this, 'anubis_version') ||
          originalGetById.call(this, 'anubis-main')) {
        if (id === 'progress') {
          const fallback = document.createElement('div');
          fallback.id = 'progress';
          fallback.setAttribute('role', 'progressbar');
          const inner = document.createElement('div');
          inner.className = 'bar-inner';
          fallback.appendChild(inner);
          return fallback;
        }
        if (id === 'status') {
          const fallback = document.createElement('p');
          fallback.id = 'status';
          return fallback;
        }
      }
    }
    return null;
  };
  try {
    Document.prototype.getElementById.toString = () =>
        'function getElementById() { [native code] }';
  } catch (_) {}
})();
)JS";

constexpr char kShieldsPageScript[] = R"JS(
(() => {
  if (window.__mtkShields) return;
  Object.defineProperty(window, '__mtkShields', {
    value: true, configurable: false, enumerable: false
  });

  const youtube = /(^|\.)youtube(?:-nocookie)?\.com$/.test(location.hostname);
  if (!youtube) return;

  const AD_KEYS = new Set([
    'adPlacements', 'playerAds', 'adSlots', 'adBreakParams',
    'adBreakHeartbeatParams', 'adBreakServiceResponse', 'importantForAds',
    'playerAdParams', 'adParams', 'adRequestParams', 'adSignalsInfo'
  ]);

  const prune = value => {
    if (!value || typeof value !== 'object') return value;
    for (const key of AD_KEYS) {
      try { delete value[key]; } catch (_) {}
    }
    for (const key of ['playerResponse', 'player_response', 'playerConfig',
                       'config', 'args', 'PLAYER_VARS']) {
      const child = value[key];
      if (!child) continue;
      if ((key === 'player_response' || key === 'playerResponse') &&
          typeof child === 'string' && child.length > 1) {
        try {
          const parsed = nativeJsonParse(child);
          prune(parsed);
          value[key] = JSON.stringify(parsed);
        } catch (_) {}
      } else if (typeof child === 'object') {
        for (const adKey of AD_KEYS) {
          try { delete child[adKey]; } catch (_) {}
        }
        const nested = child.playerResponse || child.player_response;
        if (nested && typeof nested === 'object') prune(nested);
      }
    }
    return value;
  };

  const nativeJsonParse = JSON.parse;
  JSON.parse = function(text, reviver) {
    const parsed = nativeJsonParse.call(this, text, reviver);
    try { return prune(parsed); } catch (_) { return parsed; }
  };

  const nativeResponseJson = Response.prototype.json;
  Response.prototype.json = function() {
    return nativeResponseJson.call(this).then(value => {
      try { return prune(value); } catch (_) { return value; }
    });
  };

  const guardGlobal = name => {
    const descriptor = Object.getOwnPropertyDescriptor(window, name);
    if (descriptor && !descriptor.configurable) {
      try { prune(window[name]); } catch (_) {}
      return;
    }
    let stored = descriptor && 'value' in descriptor ? descriptor.value : undefined;
    const getter = descriptor && descriptor.get;
    const setter = descriptor && descriptor.set;
    try {
      Object.defineProperty(window, name, {
        configurable: true,
        enumerable: !descriptor || descriptor.enumerable,
        get() {
          const value = getter ? getter.call(this) : stored;
          try { return prune(value); } catch (_) { return value; }
        },
        set(value) {
          const clean = prune(value);
          stored = clean;
          if (setter) setter.call(this, clean);
        }
      });
      if (stored) stored = prune(stored);
    } catch (_) {}
  };
  ['ytInitialPlayerResponse', 'ytInitialData', 'ytplayer'].forEach(guardGlobal);

  const skipAd = () => {
    for (const button of document.querySelectorAll(
        'button.ytp-ad-skip-button-modern,button.ytp-ad-skip-button,' +
        '.ytp-ad-skip-button-container button')) {
      if (!button.disabled && button.getClientRects().length) button.click();
    }
  };

  const refreshPlayerData = () => {
    try {
      prune(window.ytInitialPlayerResponse);
      prune(window.ytplayer);
    } catch (_) {}
    skipAd();
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', refreshPlayerData,
                              { once: true });
  } else {
    refreshPlayerData();
  }
  window.addEventListener('yt-navigate-finish', refreshPlayerData, true);
})();
)JS";

constexpr char kInstagramGuardScript[] = R"JS(
(() => {
  if (!/(^|\.)instagram\.com$/.test(location.hostname)) return;
  if (window.__mtkInstagramGuard) return;
  Object.defineProperty(window, '__mtkInstagramGuard', {
    value: true, configurable: false, enumerable: false
  });

  const SEARCH_PATH = '/explore/search/keyword/?q=';
  const GRID = /^\/explore\/?$|^\/explore\/(?:people|locations)/i;
  const REELS = /^\/reels\/?$/i;
  const currentPath = () => {
    try { return decodeURIComponent(location.pathname); }
    catch (_) { return location.pathname; }
  };
  const enforce = () => {
    const path = currentPath();
    if (GRID.test(path)) {
      location.replace(location.origin + SEARCH_PATH);
      return;
    }
    if (REELS.test(path) && path !== '/') location.replace(location.origin + '/');
  };

  const AD_LINK = 'a[href*="/ads/ig_redirect"],a[href*="ig_redirect"]';
  const AD_LABEL = /(?:^|\n)[ \t]*(?:sponsored|paid partnership[^\n]*|\u092a\u094d\u0930\u093e\u092f\u094b\u091c\u093f\u0924)[ \t]*(?:\n|$)/i;
  const hideSponsored = () => {
    let scanned = 0;
    for (const post of document.querySelectorAll('article')) {
      if (++scanned > 80) break;
      if (post.hasAttribute('data-mtk-sponsored')) continue;
      const sponsored = !!post.querySelector(AD_LINK) ||
                        AD_LABEL.test((post.innerText || '').slice(0, 240));
      if (!sponsored) continue;
      post.setAttribute('data-mtk-sponsored', '');
      post.style.setProperty('display', 'none', 'important');
    }
  };

  for (const name of ['pushState', 'replaceState']) {
    const original = history[name];
    if (typeof original !== 'function') continue;
    history[name] = function(...args) {
      const result = original.apply(this, args);
      enforce();
      hideSponsored();
      return result;
    };
  }
  addEventListener('popstate', enforce, true);
  addEventListener('hashchange', enforce, true);
  let previous = location.pathname;
  setInterval(() => {
    if (location.pathname !== previous) {
      previous = location.pathname;
      enforce();
    }
    hideSponsored();
  }, 1500);

  let lastRun = 0;
  addEventListener('scroll', () => {
    const now = Date.now();
    if (now - lastRun < 400) return;
    lastRun = now;
    hideSponsored();
  }, { passive: true });

  enforce();
  hideSponsored();
})();
)JS";

void ConfigureSpellcheck() {
  CefRefPtr<CefRequestContext> context = CefRequestContext::GetGlobalContext();
  if (!context) return;

  const auto set_preference = [&context](const char* name,
                                         CefRefPtr<CefValue> value) {
    if (!context->CanSetPreference(name)) return;
    CefString error;
    context->SetPreference(name, value, error);
  };

  CefRefPtr<CefValue> enabled = CefValue::Create();
  enabled->SetBool(true);
  set_preference("browser.enable_spellchecking", enabled);

  CefRefPtr<CefValue> disabled = CefValue::Create();
  disabled->SetBool(false);
  set_preference("spellcheck.use_spelling_service", disabled);

  CefRefPtr<CefListValue> dictionaries = CefListValue::Create();
  dictionaries->SetString(0, "en-US");
  CefRefPtr<CefValue> languages = CefValue::Create();
  languages->SetList(dictionaries);
  set_preference("spellcheck.dictionaries", languages);
}

void ConfigureSecureDns() {
  CefRefPtr<CefRequestContext> context = CefRequestContext::GetGlobalContext();
  if (!context) return;
  const auto& values = mtk::preferences::Store::Get().values();
  const std::string templates =
      mtk::preferences::DohTemplate(values.doh_provider);

  const auto set_preference = [&context](const char* name,
                                         CefRefPtr<CefValue> value) {
    if (!context->CanSetPreference(name)) return;
    CefString error;
    context->SetPreference(name, value, error);
  };

  CefRefPtr<CefValue> mode = CefValue::Create();
  mode->SetString(templates.empty() ? "off" : "automatic");
  set_preference("dns_over_https.mode", mode);

  CefRefPtr<CefValue> uri = CefValue::Create();
  uri->SetString(templates);
  set_preference("dns_over_https.templates", uri);
}

}  // namespace

MtkApp::MtkApp(HINSTANCE instance, int show_command,
               std::vector<std::string> startup_urls,
               bool app_mode)
    : instance_(instance), show_command_(SW_SHOWMAXIMIZED),
      startup_urls_(std::move(startup_urls)),
      app_mode_(app_mode) {
  (void)show_command;
}

MtkApp::~MtkApp() = default;

CefRefPtr<CefBrowserProcessHandler> MtkApp::GetBrowserProcessHandler() {
  return this;
}

CefRefPtr<CefRenderProcessHandler> MtkApp::GetRenderProcessHandler() {
  return this;
}

void MtkApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  mtk::home::RegisterCustomScheme(registrar);
}

void MtkApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  if (!command_line || !process_type.empty()) return;

  if (!command_line->HasSwitch("autoplay-policy")) {
    command_line->AppendSwitchWithValue("autoplay-policy",
                                        "no-user-gesture-required");
  }

  const char* features =
      "MediaSessionService,HardwareMediaKeyHandling,PictureInPictureWindow";
  if (command_line->HasSwitch("enable-features")) {
    const std::string existing = command_line->GetSwitchValue("enable-features");
    command_line->AppendSwitchWithValue("enable-features",
                                        existing + "," + features);
  } else {
    command_line->AppendSwitchWithValue("enable-features", features);
  }

  if (!command_line->HasSwitch("force-renderer-accessibility")) {
    command_line->AppendSwitchWithValue("force-renderer-accessibility",
                                        "complete");
  }
}

void MtkApp::OnBeforeChildProcessLaunch(
    CefRefPtr<CefCommandLine> command_line) {
  if (!command_line) return;
  static const uint64_t seed = []() {
    unsigned char bytes[8]{};
    const ULONGLONG tick = GetTickCount64();
    const uintptr_t address = reinterpret_cast<uintptr_t>(&bytes);
    memcpy(bytes, &tick, 4);
    memcpy(bytes + 4, &address, 4);
    uint64_t value = 0;
    memcpy(&value, bytes, 8);
    return value;
  }();
  command_line->AppendSwitchWithValue(
      mtk::fingerprint::kSwitchName,
      mtk::fingerprint::EncodeSwitchValue(
          mtk::preferences::Store::Get().values().fingerprint_protection,
          seed,
          mtk::sites::Store::Get().FingerprintingExemptOrigins()));
}

void MtkApp::OnContextInitialized() {
  ConfigureSpellcheck();
  ConfigureSecureDns();
  if (!mtk::home::RegisterSchemeHandler()) {
    CefQuitMessageLoop();
    return;
  }
  if (!BrowserWindow::Open(instance_, show_command_, startup_urls_, false, false,
                           app_mode_)) {
    CefQuitMessageLoop();
  }
}

void MtkApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefV8Context> context) {
  mtk::password_bridge::RendererOnContextCreated(browser, frame, context);
  if (!frame || !frame->IsMain()) return;
  frame->ExecuteJavaScript(kBotChallengeCompatScript, frame->GetURL(), 0);
  frame->ExecuteJavaScript(kShieldsPageScript, frame->GetURL(), 0);
  frame->ExecuteJavaScript(kInstagramGuardScript, frame->GetURL(), 0);
  frame->ExecuteJavaScript(mtk::password_bridge::PageScript(), frame->GetURL(), 0);

  static const std::string kFingerprintScript = []() {
    auto command_line = CefCommandLine::GetGlobalCommandLine();
    if (!command_line ||
        !command_line->HasSwitch(mtk::fingerprint::kSwitchName)) {
      return std::string();
    }
    const std::string value =
        command_line->GetSwitchValue(mtk::fingerprint::kSwitchName).ToString();
    return mtk::fingerprint::BuildScript(
        mtk::fingerprint::IsEnabledSwitchValue(value),
        mtk::fingerprint::DecodeSeed(value),
        mtk::fingerprint::DecodeExemptOrigins(value));
  }();
  if (!kFingerprintScript.empty()) {
    frame->ExecuteJavaScript(kFingerprintScript, frame->GetURL(), 0);
  }
}

void MtkApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  mtk::password_bridge::RendererOnContextReleased(browser, frame, context);
}

bool MtkApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefProcessId source_process,
                                      CefRefPtr<CefProcessMessage> message) {
  return mtk::password_bridge::RendererOnProcessMessage(browser, frame,
                                                        source_process, message);
}
