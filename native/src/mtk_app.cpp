#include "mtk_app.h"

#include <string>

#include "browser_window.h"
#include "password_bridge.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_request_context.h"
#include "include/cef_v8.h"
#include "include/cef_values.h"

namespace {

// Executed from CefRenderProcessHandler::OnContextCreated, i.e. as soon as the
// page V8 context exists and before the page has produced its player data.
// This timing matters: YouTube embeds its ad payload in the initial player
// response, which a late OnLoadEnd script would see only after the ad player
// was already built. V8 extension registration is deliberately not used here;
// it stalled the renderer in this CEF build.
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

  // This intentionally examines only player-response shaped fields. Walking
  // all of ytInitialData is both unnecessary and expensive on long pages.
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

  // YouTube assigns these globals during startup and during every SPA
  // navigation. Guard the assignment itself instead of only inspecting it
  // after the ad player has already been created.
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

  // CSS from the filter engine hides known ad surfaces declaratively. Avoid a
  // MutationObserver or interval here: a busy YouTube page can produce enough
  // mutations to starve the renderer. Global assignment guards above sanitize
  // every player-data replacement; this is only a bounded recheck for SPA
  // navigation and an exact native skip control.
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

// Instagram has no network-enforceable safe mode, and its single-page
// navigation changes the URL without issuing a main-frame request, so the
// request policy alone cannot stop an in-app jump to Explore or Reels. This
// guard closes those two unfiltered discovery surfaces from inside the page.
// It is deliberately small: no DOM scanning and no mutation observer.
constexpr char kInstagramGuardScript[] = R"JS(
(() => {
  if (!/(^|\.)instagram\.com$/.test(location.hostname)) return;
  if (window.__mtkInstagramGuard) return;
  Object.defineProperty(window, '__mtkInstagramGuard', {
    value: true, configurable: false, enumerable: false
  });

  // On Instagram web, Search and Explore are the same nav entry (/explore/).
  // Hiding it would remove search, so the browse grid is swapped for the
  // keyword search page instead: the user keeps search, and the endless
  // unfiltered grid never renders. Hashtag and search routes are untouched;
  // the request policy still refuses adult terms on them.
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

  // Instagram delivers sponsored posts inside the same GraphQL feed payload as
  // organic posts, so no network rule can separate them. Measured on a real
  // feed, these units carry no "Sponsored" text in the DOM at all; what they
  // do carry is Meta's ad click-through link, which organic posts never use.
  // That link is the primary signal, and the visible label is kept as a
  // secondary one for the variants that still render it.
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
  // Cheap string check for route changes the app makes without history hooks.
  let previous = location.pathname;
  setInterval(() => {
    if (location.pathname !== previous) {
      previous = location.pathname;
      enforce();
    }
    hideSponsored();
  }, 1500);

  // The feed loads more posts on scroll, so sponsored checks run there too,
  // throttled so fast scrolling cannot flood the renderer.
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

// Turns on Chromium's local spellchecker and, just as importantly, keeps the
// server-backed "enhanced" spellcheck off. Enhanced spellcheck uploads what the
// user types to Google, which this browser must never do silently.
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

  // Hunspell dictionaries Chromium ships support for. Hindi has no Hunspell
  // dictionary in Chromium, so requesting it would only fail quietly.
  CefRefPtr<CefListValue> dictionaries = CefListValue::Create();
  dictionaries->SetString(0, "en-US");
  CefRefPtr<CefValue> languages = CefValue::Create();
  languages->SetList(dictionaries);
  set_preference("spellcheck.dictionaries", languages);
}

}  // namespace

MtkApp::MtkApp(HINSTANCE instance, int show_command)
    : instance_(instance), show_command_(show_command) {}

MtkApp::~MtkApp() = default;

CefRefPtr<CefBrowserProcessHandler> MtkApp::GetBrowserProcessHandler() {
  return this;
}

CefRefPtr<CefRenderProcessHandler> MtkApp::GetRenderProcessHandler() {
  return this;
}

void MtkApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  // Only the browser process line carries these; renderers inherit what they
  // need from it.
  if (!command_line || !process_type.empty()) return;

  // Media that starts playing on its own, with sound, is the single most
  // disruptive default on the web. Requiring a gesture matches what Chrome does
  // for sound and is stricter for muted video.
  if (!command_line->HasSwitch("autoplay-policy")) {
    command_line->AppendSwitchWithValue("autoplay-policy",
                                        "user-gesture-required");
  }

  // Picture-in-picture and the hardware media keys are Chromium features that
  // are compiled in but not enabled by default in this runtime.
  const char* features =
      "MediaSessionService,HardwareMediaKeyHandling,PictureInPictureWindow";
  if (command_line->HasSwitch("enable-features")) {
    const std::string existing = command_line->GetSwitchValue("enable-features");
    command_line->AppendSwitchWithValue("enable-features",
                                        existing + "," + features);
  } else {
    command_line->AppendSwitchWithValue("enable-features", features);
  }

  // Screen readers need the renderer's accessibility tree. "complete" is the
  // documented value; it costs a little memory per tab and is what makes the
  // page content reachable by assistive technology at all.
  if (!command_line->HasSwitch("force-renderer-accessibility")) {
    command_line->AppendSwitchWithValue("force-renderer-accessibility",
                                        "complete");
  }
}

void MtkApp::OnContextInitialized() {
  ConfigureSpellcheck();
  // Windows own themselves and end the message loop when the last one closes,
  // so nothing is held here.
  if (!BrowserWindow::Open(instance_, show_command_, {})) {
    CefQuitMessageLoop();
  }
}

void MtkApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefV8Context> context) {
  // The router has to see every context, including subframes, or its replies
  // have nowhere to land.
  mtk::password_bridge::RendererOnContextCreated(browser, frame, context);
  if (!frame || !frame->IsMain()) return;
  // Run in the freshly-created page context. All three scripts are intentionally
  // bounded and check the page themselves: no mutation observer and no periodic
  // page-data traversal.
  frame->ExecuteJavaScript(kShieldsPageScript, frame->GetURL(), 0);
  frame->ExecuteJavaScript(kInstagramGuardScript, frame->GetURL(), 0);
  frame->ExecuteJavaScript(mtk::password_bridge::PageScript(), frame->GetURL(), 0);
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
