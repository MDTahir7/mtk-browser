#include "browser_client.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <unordered_map>

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_callback.h"
#include "include/cef_frame.h"
#include "include/cef_menu_model.h"
#include "include/cef_request.h"
#include "include/wrapper/cef_closure_task.h"
#include "download_manager.h"
#include "filter_engine.h"
#include "inline_asset.h"
#include "policy.h"

namespace {

constexpr int kOpenLinkInNewTabId = MENU_ID_USER_FIRST;
constexpr int kCopyLinkAddressId = MENU_ID_USER_FIRST + 1;
constexpr int kSaveImageId = MENU_ID_USER_FIRST + 2;

std::atomic<unsigned long> g_blocked_count{0};
std::unordered_map<uint32_t, CefRefPtr<CefDownloadItemCallback>>
    g_download_callbacks;

void DestroyBrowserChild(CefWindowHandle window) {
  if (window && IsWindow(window)) DestroyWindow(window);
}

// Hostnames are already normalised to lowercase ASCII by the policy layer.
std::wstring AsciiToWide(const std::string& value) {
  return std::wstring(value.begin(), value.end());
}

// CEF's bundled PDF viewer is the only Chrome extension origin that MTK
// Browser permits. The trailing slash makes this an exact origin match (a
// lookalike host such as "...id.evil" cannot pass).
constexpr char kPdfViewerOrigin[] =
    "chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/";
// The viewer's own UI code lives in Chromium's shared resource bundle. Only
// this read-only asset path is permitted; chrome://settings, chrome://net-*
// and every other internal page stay unreachable, and main-frame navigation
// is still restricted to http/https by EvaluateNavigation.
constexpr char kSharedResourcesOrigin[] = "chrome://resources/";

bool IsTrustedPdfViewerUrl(const std::string& url) {
  return url.starts_with(kPdfViewerOrigin) ||
         url.starts_with(kSharedResourcesOrigin);
}

// Decorative loop rendered for the block page. The asset ships beside the
// executable and is inlined into the page, so the block page stays a single
// self-contained data: URL that needs no local file access. Loading and
// validation live in mtk::assets, which the Shields panel shares.
constexpr wchar_t kBlockAnimationFile[] = L"mtk_block_shield.webm";

// A data: URL carries its own bytes and reaches no network, so permitting this
// one media type keeps the block page self-contained without widening the
// fail-closed request policy. Everything else still goes through it.
bool IsInertBlockAnimation(const std::string& url,
                           cef_resource_type_t resource_type) {
  return resource_type == RT_MEDIA &&
         url.starts_with(mtk::assets::kWebmDataUrlPrefix);
}

std::string BlockPage(const std::wstring& reason) {
  const std::string text(reason.begin(), reason.end());
  const std::string& animation =
      mtk::assets::WebmDataUrl(kBlockAnimationFile);
  // The glyph stays in the markup as the fallback for a missing asset and for
  // readers who ask the platform to reduce motion.
  std::string art = "<div class=shield>&%239678;</div>";
  if (!animation.empty()) {
    art = "<video class=anim autoplay loop muted playsinline "
          "disablepictureinpicture aria-hidden=true src=" +
          animation + "></video>" + art;
  }
  return "data:text/html,<meta charset=utf-8><title>Blocked by MTK Browser</title><style>"
         "body{font-family:Segoe UI;background:%230b1020;color:%23f8fafc;"
         "display:grid;place-items:center;min-height:90vh;margin:0}"
         ".card{max-width:620px;padding:40px 48px 48px;border:1px solid %232c3856;"
         "border-radius:24px;text-align:center;background:%2311182b}"
         ".anim{width:152px;height:152px;display:block;margin:0 auto;"
         "pointer-events:none}.anim~.shield{display:none}"
         "@media (prefers-reduced-motion:reduce){.anim{display:none}"
         ".anim~.shield{display:block}}"
         ".shield{font-size:52px;color:%237c5cff}"
         "h1{margin:14px 0 8px;font-weight:600}"
         "p{color:%23aab7d1;line-height:1.7}"
         "</style><main class=card>" + art +
         "<h1>Navigation blocked</h1><p>" + text +
         "</p><p>MTK Browser stopped this request using your active safety policy."
         "</p></main>";
}

// Collapses the layout space that a blocked ad leaves behind, so the page looks
// like the ad slot was never there instead of showing an empty reserved box.
// Only containers with no remaining visible content are hidden; nothing that
// still renders text, media or controls is touched.
constexpr char kCollapseScript[] = R"JS(
(() => {
  if (window.__mtkCollapse) { window.__mtkCollapse(); return; }

  const AD_HINTS = [
    'ins.adsbygoogle', '[id^="div-gpt-ad"]', '[id^="google_ads"]',
    '[id^="google_ads_iframe"]', '[data-ad-slot]', '[data-ad-client]',
    '[data-google-query-id]', '[class*="adsbygoogle"]',
    'iframe[title*="advertisement" i]',
    'iframe[src*="doubleclick.net"]', 'iframe[src*="googlesyndication"]',
    'iframe[src*="adnxs.com"]', 'iframe[src*="amazon-adsystem"]',
    'iframe[src*="criteo"]', 'iframe[src*="taboola"]',
    'iframe[src*="outbrain"]'
  ].join(',');

  // Wrapper labels that sites print above a reserved ad box. AD_CHROME also
  // accepts the small share/opt-out controls some publishers attach to an ad;
  // it is used only while walking upward from an already-identified ad seed.
  const LABEL = /^(advertisement|advertisements|advertisment|advt|sponsored|sponsored\s+links?|ads?|\u0935\u093f\u091c\u094d\u091e\u093e\u092a\u0928)$/i;
  const AD_CHROME = /^(?:(?:advertisement|advertisements|advertisment|advt|sponsored|sponsored\s+links?|ads?|\u0935\u093f\u091c\u094d\u091e\u093e\u092a\u0928|\u0935\u093f\u091c\u094d\u091e\u093e\u092a\u0928\s+\u0939\u091f\u093e\u090f\u0902|\u0938\u093f\u0930\u094d\u092b\s+\u0916\u092c\u0930\s+\u092a\u0922\u093c\u0947\u0902|\u0936\u0947\u092f\u0930\s+\u0915\u0930\u0947\u0902|share)(?:\s+|$))+$/i;
  const KEEP = /^(body|html|head|main|nav|header|footer|article|form|table|tbody|tr)$/;
  const CONTENT =
    'img,video,canvas,picture,svg,input,select,textarea,button,a[href]';
  // Interactive surfaces that must never be collapsed. A hidden in-player ad
  // marker used to seed an upward walk that removed YouTube's seek bar, so
  // media players and widget roles are excluded from collapsing entirely.
  const PROTECT = [
    'video', 'audio', '.html5-video-player', '.ytp-chrome-bottom',
    '.ytp-chrome-top', '[role="slider"]', '[role="progressbar"]',
    '[role="toolbar"]', '[role="menu"]', '[role="menubar"]',
    '[role="tablist"]', '[role="dialog"]', '[role="navigation"]'
  ].join(',');

  const hide = element => {
    element.style.setProperty('display', 'none', 'important');
    element.setAttribute('data-mtk-collapsed', '');
  };

  // True when the element still shows something a reader would care about.
  // While collapseFrom walks upward from a known ad seed, publisher ad chrome
  // such as "ADVERTISEMENT · शेयर करें" is not meaningful reader content.
  // innerText is used deliberately: it ignores hidden subtrees.
  const meaningful = element => {
    const text = (element.innerText || '').replace(/\s+/g, ' ').trim();
    if (text.length > 2 && !AD_CHROME.test(text)) return true;
    for (const node of element.querySelectorAll(CONTENT)) {
      const box = node.getBoundingClientRect();
      if (box.width > 4 && box.height > 4) return true;
    }
    return false;
  };

  const collapseFrom = seed => {
    // An ad inside a player is an overlay: it reserves no page layout, so
    // there is nothing to collapse and its ancestors are real controls.
    if (seed.closest(PROTECT)) return;
    let node = seed;
    for (let depth = 0; depth < 5 && node; ++depth) {
      const parent = node.parentElement;
      if (!parent || KEEP.test(parent.tagName.toLowerCase())) break;
      if (parent.matches(PROTECT) || parent.closest(PROTECT)) break;
      if (parent.hasAttribute('data-mtk-collapsed')) break;
      if (meaningful(parent)) break;
      const box = parent.getBoundingClientRect();
      if (box.height === 0 && box.width === 0) break;
      hide(parent);
      node = parent;
    }
  };

  const run = () => {
    const seeds = [];
    for (const element of document.querySelectorAll(AD_HINTS)) {
      if (!element.hasAttribute('data-mtk-collapsed')) hide(element);
      seeds.push(element);
    }
    // Elements the cosmetic stylesheet already hid still reserve their
    // wrapper's height, so treat them as seeds too.
    for (const element of document.querySelectorAll('[data-mtk-hidden-ad]')) {
      seeds.push(element);
    }
    // Leaf "ADVERTISEMENT" captions: hiding the caption alone would leave the
    // gap, so it seeds an upward collapse as well.
    let scanned = 0;
    for (const element of document.querySelectorAll(
             'div,span,p,ins,section,aside,h2,h3,h4,h5,strong,small,font')) {
      if (++scanned > 4000) break;
      if (element.children.length > 0) continue;
      const text = (element.textContent || '').replace(/\s+/g, ' ').trim();
      if (!text || text.length > 24 || !LABEL.test(text)) continue;
      if (!element.hasAttribute('data-mtk-collapsed')) hide(element);
      seeds.push(element);
    }
    for (const seed of seeds) collapseFrom(seed);
  };

  window.__mtkCollapse = run;
  run();

  if (window === window.top) {
    // Bounded, finite follow-up passes for late/lazy ad slots. No permanent
    // observer runs here: a mutation observer on ad-heavy pages was measured
    // to starve the renderer.
    for (const delay of [400, 1200, 2500, 5000, 9000]) setTimeout(run, delay);
    let lastRun = 0;
    let remaining = 40;
    addEventListener('scroll', () => {
      const now = Date.now();
      if (remaining <= 0 || now - lastRun < 600) return;
      lastRun = now;
      --remaining;
      run();
    }, { passive: true });
  }
})();
)JS";

// Applies the cosmetic selectors that the filter engine resolved for this site.
// Selectors are injected as a stylesheet, so no page node is ever removed.
std::string CosmeticScript(const std::string& selectors) {
  std::string escaped;
  for (const char character : selectors) {
    if (character == '`' || character == '\\') escaped.push_back('\\');
    if (character == '\r' || character == '\n') continue;
    escaped.push_back(character);
  }

  std::string script;
  if (!escaped.empty()) {
    script =
        "(() => { const id='mtk-cosmetic-filter';"
        "let style=document.getElementById(id);"
        "if(!style){style=document.createElement('style');style.id=id;"
        "(document.head||document.documentElement).appendChild(style);}"
        "const rules=`" + escaped + "{display:none !important;}`;"
        "if(style.textContent!==rules){style.textContent=rules;}"
        "for(const node of document.querySelectorAll(`" + escaped + "`)){"
        "node.setAttribute('data-mtk-hidden-ad','');} })();";
  }
  return script + kCollapseScript;
}

mtk::policy::ResourceType MapResourceType(CefRequest::ResourceType type) {
  switch (type) {
    case RT_MAIN_FRAME:
    case RT_NAVIGATION_PRELOAD_MAIN_FRAME:
      return mtk::policy::ResourceType::kMainFrame;
    case RT_SUB_FRAME:
    case RT_NAVIGATION_PRELOAD_SUB_FRAME:
      return mtk::policy::ResourceType::kSubFrame;
    case RT_STYLESHEET: return mtk::policy::ResourceType::kStylesheet;
    case RT_SCRIPT: return mtk::policy::ResourceType::kScript;
    case RT_IMAGE: return mtk::policy::ResourceType::kImage;
    case RT_XHR: return mtk::policy::ResourceType::kXhr;
    case RT_FONT_RESOURCE: return mtk::policy::ResourceType::kFont;
    case RT_MEDIA: return mtk::policy::ResourceType::kMedia;
    case RT_OBJECT:
    case RT_PLUGIN_RESOURCE:
      return mtk::policy::ResourceType::kObject;
    case RT_PING:
    case RT_CSP_REPORT:
      return mtk::policy::ResourceType::kPing;
    case RT_WORKER:
    case RT_SHARED_WORKER:
    case RT_SERVICE_WORKER:
      return mtk::policy::ResourceType::kWorker;
    case RT_PREFETCH: return mtk::policy::ResourceType::kPrefetch;
    case RT_FAVICON: return mtk::policy::ResourceType::kFavicon;
    default: return mtk::policy::ResourceType::kOther;
  }
}

}  // namespace

BrowserClient::BrowserClient(BrowserClientDelegate* delegate)
    : delegate_(delegate) {}

CefRefPtr<CefContextMenuHandler> BrowserClient::GetContextMenuHandler() {
  return this;
}
CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler() { return this; }
CefRefPtr<CefDownloadHandler> BrowserClient::GetDownloadHandler() { return this; }
CefRefPtr<CefFindHandler> BrowserClient::GetFindHandler() { return this; }
CefRefPtr<CefKeyboardHandler> BrowserClient::GetKeyboardHandler() { return this; }
CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() { return this; }
CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler() { return this; }
CefRefPtr<CefPermissionHandler> BrowserClient::GetPermissionHandler() {
  return this;
}
CefRefPtr<CefRequestHandler> BrowserClient::GetRequestHandler() { return this; }

void BrowserClient::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  if (!model || !params) return;
  model->Clear();

  if (browser) {
    model->AddItem(MENU_ID_BACK, L"Back");
    model->SetEnabled(MENU_ID_BACK, browser->CanGoBack());
    model->AddItem(MENU_ID_FORWARD, L"Forward");
    model->SetEnabled(MENU_ID_FORWARD, browser->CanGoForward());
    model->AddItem(MENU_ID_RELOAD, L"Reload");
  }

  const std::string link = params->GetLinkUrl();
  if (!link.empty()) {
    model->AddSeparator();
    model->AddItem(kOpenLinkInNewTabId, L"Open link in new tab");
    model->AddItem(kCopyLinkAddressId, L"Copy link address");
    const bool allowed = mtk::policy::EvaluateNavigation(link).allow;
    model->SetEnabled(kOpenLinkInNewTabId, allowed);
    model->SetEnabled(kCopyLinkAddressId, allowed);
  }

  if (params->HasImageContents() && !params->GetSourceUrl().empty()) {
    model->AddItem(kSaveImageId, L"Save image");
    model->SetEnabled(
        kSaveImageId,
        mtk::policy::EvaluateNavigation(params->GetSourceUrl().ToString()).allow);
  }

  const auto edit_flags = params->GetEditStateFlags();
  const bool can_copy = (edit_flags & CM_EDITFLAG_CAN_COPY) != 0;
  const bool can_paste = (edit_flags & CM_EDITFLAG_CAN_PASTE) != 0;
  const bool can_select_all = (edit_flags & CM_EDITFLAG_CAN_SELECT_ALL) != 0;
  if (can_copy || can_paste || can_select_all) {
    model->AddSeparator();
    if (can_copy) model->AddItem(MENU_ID_COPY, L"Copy");
    if (can_paste && params->IsEditable()) model->AddItem(MENU_ID_PASTE, L"Paste");
    if (can_select_all) model->AddItem(MENU_ID_SELECT_ALL, L"Select all");
  }
}

bool BrowserClient::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    CefRefPtr<CefContextMenuParams> params, int command_id, EventFlags) {
  if (!params) return false;
  if (command_id == kOpenLinkInNewTabId) {
    const auto decision =
        mtk::policy::EvaluateNavigation(params->GetLinkUrl().ToString());
    if (decision.allow && delegate_) {
      delegate_->OnBrowserPopupRequested(this, decision.url);
    }
    return true;
  }
  if (command_id == kCopyLinkAddressId) {
    const auto decision =
        mtk::policy::EvaluateNavigation(params->GetLinkUrl().ToString());
    if (!decision.allow || !OpenClipboard(nullptr)) return true;
    EmptyClipboard();
    const std::wstring text = AsciiToWide(decision.url);
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
      void* target = GlobalLock(memory);
      if (target) {
        memcpy(target, text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
      } else {
        GlobalFree(memory);
      }
    }
    CloseClipboard();
    return true;
  }
  if (command_id == kSaveImageId) {
    const auto decision =
        mtk::policy::EvaluateNavigation(params->GetSourceUrl().ToString());
    if (decision.allow && browser) browser->GetHost()->StartDownload(decision.url);
    return true;
  }
  return false;
}

bool BrowserClient::CanDownload(CefRefPtr<CefBrowser>, const CefString& url,
                                const CefString& request_method) {
  // First gate: only user-initiated GET downloads from an allowed web address
  // reach the download manager. The file-type rules run in OnBeforeDownload,
  // where the real suggested name and MIME type are known.
  const std::string method = request_method.ToString();
  if (method != "GET" && method != "get" && !method.empty()) return false;
  const auto decision = mtk::policy::EvaluateNavigation(url.ToString());
  return decision.allow && (decision.url.starts_with("http://") ||
                            decision.url.starts_with("https://"));
}

bool BrowserClient::OnBeforeDownload(
    CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!download_item || !download_item->IsValid() || !callback) return false;

  const std::string url = download_item->GetURL();
  const mtk::downloads::Mode mode = mtk::downloads::CurrentMode();
  const auto decision = mtk::downloads::Evaluate(
      url, suggested_name.ToWString(), download_item->GetMimeType().ToString(),
      mode);
  if (!decision.allow) {
    // Returning false cancels the download with the Alloy runtime style.
    MessageBoxW(nullptr, decision.reason.c_str(),
                L"MTK Browser blocked this download", MB_OK | MB_ICONWARNING);
    return false;
  }

  const int64_t total_bytes = download_item->GetTotalBytes();
  // Large files are never blocked. Safe mode only points the size out inside
  // the confirmation it already shows.
  const bool large_and_known =
      total_bytes > mtk::downloads::kLargeDownloadBytes;

  const std::wstring target = mtk::downloads::UniqueTargetPath(decision.file_name);
  if (target.empty()) {
    MessageBoxW(nullptr,
                L"MTK Browser could not prepare a download location.",
                L"MTK Browser", MB_OK | MB_ICONERROR);
    return false;
  }

  const std::string host = mtk::policy::HostOf(url);
  if (mode == mtk::downloads::Mode::kSafe) {
    // Safe mode never starts a download silently: the user confirms the file
    // name and destination first.
    std::wstring question = L"Save this file?\n\nName: " + decision.file_name;
    if (!host.empty()) question += L"\nFrom: " + AsciiToWide(host);
    question += L"\nTo: " + target;
    if (large_and_known) {
      question +=
          L"\n\nWarning: this file is larger than 2 GB and may take a long "
          L"time and a lot of disk space.";
    }
    if (MessageBoxW(nullptr, question.c_str(), L"MTK Browser download",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
      return false;
    }
  } else if (decision.risky) {
    // Standard mode saves without asking, except for files that can execute
    // code, where a mainstream browser also warns.
    std::wstring warning =
        L"This file can run programs on your computer.\n\nName: " +
        decision.file_name;
    if (!host.empty()) warning += L"\nFrom: " + AsciiToWide(host);
    warning += L"\n\nKeep this file?";
    if (MessageBoxW(nullptr, warning.c_str(), L"MTK Browser download warning",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
      return false;
    }
  }

  mtk::downloads::Item item;
  item.id = download_item->GetId();
  item.file_name = decision.file_name;
  item.full_path = target;
  item.url = url;
  item.state = mtk::downloads::State::kStarting;
  item.total_bytes = total_bytes;
  item.risky = decision.risky;
  // A size the user already acknowledged is not warned about again.
  item.size_warned = large_and_known && mode == mtk::downloads::Mode::kSafe;
  if (large_and_known) item.detail = L"large file, over 2 GB";
  mtk::downloads::Manager::Get().Upsert(item);
  if (delegate_) delegate_->OnBrowserDownloadsChanged(this);

  callback->Continue(target, /*show_dialog=*/false);
  return true;
}

void BrowserClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  if (!download_item || !download_item->IsValid()) return;

  auto& manager = mtk::downloads::Manager::Get();
  const uint32_t id = download_item->GetId();
  const auto& items = manager.items();
  const auto match = std::find_if(items.begin(), items.end(),
                                  [id](const mtk::downloads::Item& item) {
                                    return item.id == id;
                                  });
  // Downloads that were never accepted by OnBeforeDownload are not tracked and
  // must not retain a live CEF callback.
  if (match == items.end()) {
    g_download_callbacks.erase(id);
    return;
  }
  if (callback) g_download_callbacks[id] = callback;

  mtk::downloads::Item item = *match;
  const mtk::downloads::State previous_state = item.state;
  item.received_bytes = download_item->GetReceivedBytes();
  if (download_item->GetTotalBytes() > 0) {
    item.total_bytes = download_item->GetTotalBytes();
  }
  item.percent = download_item->GetPercentComplete();
  if (!download_item->GetFullPath().empty()) {
    item.full_path = download_item->GetFullPath();
  }

  // Crossing 2 GB only warns, and only once, when the size was not known while
  // the download was being confirmed. The transfer keeps running either way.
  const bool now_large =
      item.received_bytes > mtk::downloads::kLargeDownloadBytes ||
      item.total_bytes > mtk::downloads::kLargeDownloadBytes;
  const bool warn_now = now_large && !item.size_warned &&
                        mtk::downloads::CurrentMode() ==
                            mtk::downloads::Mode::kSafe;
  if (now_large) item.detail = L"large file, over 2 GB";
  if (now_large) item.size_warned = true;

  if (download_item->IsComplete()) {
    item.state = mtk::downloads::State::kComplete;
    item.percent = 100;
    // Mark the finished file as coming from the internet zone, exactly once.
    // Windows and Defender/SmartScreen rely on this marker to warn before the
    // file is executed, and Office uses it to open documents in Protected View.
    if (previous_state != mtk::downloads::State::kComplete &&
        !item.full_path.empty() &&
        !mtk::downloads::ApplyMarkOfTheWeb(item.full_path, item.url)) {
      item.detail = L"saved, but could not be marked as an internet download";
    }
  } else if (download_item->IsCanceled()) {
    item.state = mtk::downloads::State::kCanceled;
  } else if (download_item->IsInterrupted()) {
    item.state = mtk::downloads::State::kFailed;
  } else if (download_item->IsPaused()) {
    item.state = mtk::downloads::State::kPaused;
  } else if (download_item->IsInProgress()) {
    item.state = mtk::downloads::State::kInProgress;
  }

  manager.Upsert(item);
  if (item.state == mtk::downloads::State::kComplete ||
      item.state == mtk::downloads::State::kCanceled ||
      item.state == mtk::downloads::State::kFailed) {
    g_download_callbacks.erase(id);
  }
  if (delegate_) delegate_->OnBrowserDownloadsChanged(this);

  if (warn_now && item.state == mtk::downloads::State::kInProgress) {
    const std::wstring warning =
        L"This download has passed 2 GB and is still running.\n\n" +
        item.file_name +
        L"\n\nIt is not blocked. Use Cancel download in the Downloads window "
        L"(Ctrl+J) if you want to stop it.";
    MessageBoxW(nullptr, warning.c_str(), L"MTK Browser large download",
                MB_OK | MB_ICONWARNING);
  }
}

void BrowserClient::OnFindResult(CefRefPtr<CefBrowser> browser, int identifier,
                                 int count, const CefRect&, int active_match_ordinal,
                                 bool final_update) {
  if (delegate_ && browser_ && browser && browser_->IsSame(browser)) {
    delegate_->OnBrowserFindResult(this, identifier, count,
                                   active_match_ordinal, final_update);
  }
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (delegate_) delegate_->OnBrowserCreated(this);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  // Alloy is hosted as a Win32 child, so MTK owns hierarchy tear-down. Defer
  // DestroyWindow until after this callback returns to avoid re-entering CEF's
  // close stack; OnBeforeClose remains the authoritative final notification.
  if (browser) {
    CefPostTask(TID_UI,
                base::BindOnce(&DestroyBrowserChild,
                               browser->GetHost()->GetWindowHandle()));
  }
  return true;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (browser_ && browser_->IsSame(browser)) browser_ = nullptr;
  if (delegate_) delegate_->OnBrowserClosed(this);
}

bool BrowserClient::OnBeforePopup(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
    const CefString& target_url, const CefString&,
    cef_window_open_disposition_t, bool, const CefPopupFeatures&,
    CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>&, bool*) {
  const auto decision = mtk::policy::EvaluateNavigation(target_url.ToString());
  if (decision.allow && delegate_) {
    delegate_->OnBrowserPopupRequested(this, decision.url);
  }
  return true;
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool is_loading, bool can_go_back,
                                         bool can_go_forward) {
  if (!delegate_) return;
  delegate_->OnBrowserLoadingChanged(this, is_loading, can_go_back,
                                     can_go_forward);
  if (!is_loading && browser && browser->GetMainFrame()) {
    delegate_->OnBrowserNavigationCompleted(
        this, browser->GetMainFrame()->GetURL().ToString());
  }
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                              int) {
  if (!frame) return;
  const std::string url = frame->GetURL();
  const std::string host = mtk::policy::HostOf(url);
  if (host.empty()) return;

  const std::string selectors =
      mtk::filters::Engine::Get().CssSelectorsForHost(host);
  const std::string script = CosmeticScript(selectors);
  if (!script.empty()) frame->ExecuteJavaScript(script, url, 0);
}

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser>,
                                  const CefKeyEvent& event, CefEventHandle,
                                  bool*) {
  if (event.type != KEYEVENT_RAWKEYDOWN || !delegate_) return false;
  return delegate_->OnBrowserShortcut(
      this, event.windows_key_code,
      (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
      (event.modifiers & EVENTFLAG_ALT_DOWN) != 0,
      (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0);
}

bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser>,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request, bool, bool) {
  if (!frame || !frame->IsMain() || !request) return false;
  const std::string requested_url = request->GetURL();
  if (block_page_pending_ && requested_url.starts_with("data:text/html")) {
    block_page_pending_ = false;
    return false;
  }

  // Some browse surfaces have a safe equivalent, so substituting it keeps the
  // feature reachable instead of showing a dead end.
  if (const std::string alternative =
          mtk::policy::SafeSurfaceRedirect(requested_url);
      !alternative.empty() && alternative != requested_url) {
    frame->LoadURL(alternative);
    return true;
  }

  const auto decision = mtk::policy::EvaluateNavigation(requested_url);
  if (!decision.allow) {
    block_page_pending_ = true;
    frame->LoadURL(BlockPage(decision.reason));
    return true;
  }
  if (decision.url != requested_url) {
    frame->LoadURL(decision.url);
    return true;
  }
  return false;
}

bool BrowserClient::OnOpenURLFromTab(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& target_url,
    cef_window_open_disposition_t disposition, bool) {
  if (disposition == CEF_WOD_CURRENT_TAB) return false;
  const auto decision = mtk::policy::EvaluateNavigation(target_url.ToString());
  if (decision.allow && delegate_) {
    delegate_->OnBrowserPopupRequested(this, decision.url);
  }
  return true;
}

CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    bool, bool, const CefString& request_initiator,
    bool& disable_default_handling) {
  if (!request) return this;
  const std::string url = request->GetURL();
  if (request->GetResourceType() == RT_MAIN_FRAME &&
      url.starts_with("data:text/html")) {
    return this;
  }
  // The Chromium PDF renderer is packaged as one internal extension. Permit
  // only that exact origin; all other extension and custom-scheme requests
  // still go through the fail-closed policy below.
  if (IsTrustedPdfViewerUrl(url)) return this;
  if (IsInertBlockAnimation(url, request->GetResourceType())) return this;

  const auto decision = mtk::policy::EvaluateRequest(
      url, MapResourceType(request->GetResourceType()),
      request_initiator.ToString());
  if (!decision.allow) {
    g_blocked_count.fetch_add(1, std::memory_order_relaxed);
    disable_default_handling = true;
    return nullptr;
  }
  return this;
}

unsigned long BrowserClient::blocked_count() {
  return g_blocked_count.load(std::memory_order_relaxed);
}

bool BrowserClient::CanControlDownload(uint32_t id) {
  const auto found = g_download_callbacks.find(id);
  return found != g_download_callbacks.end() && found->second;
}

bool BrowserClient::PauseDownload(uint32_t id) {
  const auto found = g_download_callbacks.find(id);
  if (found == g_download_callbacks.end() || !found->second) return false;
  found->second->Pause();
  return true;
}

bool BrowserClient::ResumeDownload(uint32_t id) {
  const auto found = g_download_callbacks.find(id);
  if (found == g_download_callbacks.end() || !found->second) return false;
  found->second->Resume();
  return true;
}

bool BrowserClient::CancelDownload(uint32_t id) {
  const auto found = g_download_callbacks.find(id);
  if (found == g_download_callbacks.end() || !found->second) return false;
  found->second->Cancel();
  return true;
}

bool BrowserClient::GetAuthCredentials(
    CefRefPtr<CefBrowser>, const CefString&, bool, const CefString&, int,
    const CefString&, const CefString&, CefRefPtr<CefAuthCallback>) {
  return false;
}

bool BrowserClient::OnCertificateError(
    CefRefPtr<CefBrowser>, cef_errorcode_t, const CefString&,
    CefRefPtr<CefSSLInfo>, CefRefPtr<CefCallback>) {
  return false;
}

bool BrowserClient::OnSelectClientCertificate(
    CefRefPtr<CefBrowser>, bool, const CefString&, int,
    const X509CertificateList&,
    CefRefPtr<CefSelectClientCertificateCallback> callback) {
  if (callback) callback->Select(nullptr);
  return true;
}

CefResourceRequestHandler::ReturnValue BrowserClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>) {
  if (!request) return RV_CONTINUE;
  const std::string url = request->GetURL();
  if ((request->GetResourceType() == RT_MAIN_FRAME &&
       url.starts_with("data:text/html")) ||
      IsTrustedPdfViewerUrl(url) ||
      IsInertBlockAnimation(url, request->GetResourceType())) {
    return RV_CONTINUE;
  }

  CefRequest::HeaderMap headers;
  request->GetHeaderMap(headers);
  for (const auto& [name, value] : mtk::policy::PrivacyRequestHeaders(url)) {
    headers.erase(name);
    headers.insert({name, value});
  }
  request->SetHeaderMap(headers);
  return RV_CONTINUE;
}

void BrowserClient::OnProtocolExecution(CefRefPtr<CefBrowser>,
                                        CefRefPtr<CefFrame>,
                                        CefRefPtr<CefRequest>,
                                        bool& allow_os_execution) {
  allow_os_execution = false;
}

bool BrowserClient::OnShowPermissionPrompt(
    CefRefPtr<CefBrowser>, uint64_t, const CefString&, uint32_t,
    CefRefPtr<CefPermissionPromptCallback> callback) {
  if (callback) callback->Continue(CEF_PERMISSION_RESULT_DENY);
  return true;
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser>,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (frame && frame->IsMain() && delegate_) {
    delegate_->OnBrowserAddressChanged(this, url.ToWString());
  }
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser>,
                                  const CefString& title) {
  if (delegate_) delegate_->OnBrowserTitleChanged(this, title.ToWString());
}
