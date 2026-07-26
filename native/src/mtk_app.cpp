#include "mtk_app.h"

#include <string>

#include "browser_window.h"
#include "include/cef_app.h"
#include "include/cef_frame.h"
#include "include/cef_v8.h"

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

void MtkApp::OnContextInitialized() {
  browser_window_ = std::make_unique<BrowserWindow>(instance_);
  if (!browser_window_->Create(show_command_)) {
    browser_window_.reset();
    CefQuitMessageLoop();
  }
}

void MtkApp::OnContextCreated(CefRefPtr<CefBrowser>,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefV8Context>) {
  if (!frame || !frame->IsMain()) return;
  // Run in the freshly-created page context. The script is intentionally
  // bounded: no mutation observer and no periodic page-data traversal.
  frame->ExecuteJavaScript(kShieldsPageScript, frame->GetURL(), 0);
}
