#include "browser_window.h"

#include <windows.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <commctrl.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "browser_client.h"
#include "download_manager.h"
#include "icon_strip.h"
#include "preferences.h"
#include "screenshot_panel.h"
#include "strings.h"
#include "include/cef_parser.h"

namespace {

static std::atomic<bool> g_fullpage_screenshot_busy{false};
static std::chrono::steady_clock::time_point g_last_screenshot_start{};

std::string BuildScrollAnimationScript(bool hindi) {
  const std::string badge_text =
      hindi ? reinterpret_cast<const char*>(u8"पूरा पेज कैप्चर हो रहा है")
            : "Capturing Full Page";

  std::string s = R"JS(
(async () => {
  try {
    const doc = document.documentElement;
    const body = document.body;
    const initialY = Math.round(window.scrollY || window.pageYOffset || 0);
    const vh = window.innerHeight || (doc ? doc.clientHeight : 800) || 800;
    const vw = window.innerWidth || (doc ? doc.clientWidth : 1280) || 1280;

    // Detect actual scrollable containers (support SPAs, overflow-y elements)
    const scrollTargets = [];
    if (document.scrollingElement) scrollTargets.push(document.scrollingElement);
    if (doc && !scrollTargets.includes(doc)) scrollTargets.push(doc);
    if (body && !scrollTargets.includes(body)) scrollTargets.push(body);

    const candidates = document.querySelectorAll('main, [role="main"], #root, #app, .content, #content, [style*="overflow"]');
    for (const el of candidates) {
      if (el && el.scrollHeight > el.clientHeight + 10) {
        const cs = window.getComputedStyle(el);
        if (cs.overflowY === 'auto' || cs.overflowY === 'scroll') {
          scrollTargets.push(el);
        }
      }
    }

    let maxContainerH = 0;
    for (const el of scrollTargets) {
      if (el && el.scrollHeight) {
        maxContainerH = Math.max(maxContainerH, el.scrollHeight);
      }
    }

    const docHeight = Math.max(
      doc ? doc.scrollHeight : 0,
      body ? body.scrollHeight : 0,
      doc ? doc.offsetHeight : 0,
      body ? body.offsetHeight : 0,
      maxContainerH,
      vh
    );

    const finalTargetH = docHeight;
    const maxScroll = Math.max(0, finalTargetH - vh);

    const docWidth = Math.max(
      doc ? doc.scrollWidth : 0,
      body ? body.scrollWidth : 0,
      doc ? doc.offsetWidth : 0,
      body ? body.offsetWidth : 0,
      vw
    );
    const finalTargetW = docWidth;

    const applyScroll = (y) => {
      window.scrollTo(0, y);
      if (document.scrollingElement) {
        try { document.scrollingElement.scrollTop = y; } catch (_) {}
      }
      if (doc) {
        try { doc.scrollTop = y; } catch (_) {}
      }
      if (body) {
        try { body.scrollTop = y; } catch (_) {}
      }
      for (const t of scrollTargets) {
        try {
          const tMax = t.scrollHeight - t.clientHeight;
          if (tMax > 0 && maxScroll > 0) {
            t.scrollTop = Math.round((y / maxScroll) * tMax);
          } else {
            t.scrollTop = y;
          }
        } catch (_) {}
      }
    };

    // Scroll to very top first and allow 1 frame for visual reset
    if (maxScroll > 0) {
      applyScroll(0);
      await new Promise((r) => requestAnimationFrame(r));
    }

    // High-tech scanning overlay
    const overlay = document.createElement('div');
    overlay.id = '__mtk_fullpage_scanner__';
    overlay.style.cssText = 'position:fixed!important;top:0!important;left:0!important;width:100vw!important;height:100vh!important;pointer-events:none!important;z-index:2147483647!important;overflow:hidden!important;margin:0!important;padding:0!important;border:none!important;background:transparent!important;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif!important;';

    const styleEl = document.createElement('style');
    styleEl.textContent = `
      @keyframes __mtk_dot_pulse {
        0% { transform: scale(0.85); opacity: 0.6; }
        100% { transform: scale(1.15); opacity: 1; }
      }
      @keyframes __mtk_trail_shimmer {
        0% { opacity: 0.7; }
        100% { opacity: 1; }
      }
    `;
    overlay.appendChild(styleEl);

    // Laser Beam Bar Container
    const beam = document.createElement('div');
    beam.style.cssText = 'position:absolute!important;top:0!important;left:0!important;right:0!important;width:100%!important;transform:translateY(0px)!important;will-change:transform!important;pointer-events:none!important;margin:0!important;padding:0!important;border:none!important;';

    // Upward glowing trail wash
    const trail = document.createElement('div');
    trail.style.cssText = 'width:100%!important;height:90px!important;margin-top:-90px!important;background:linear-gradient(180deg, rgba(56,189,248,0) 0%, rgba(244,114,182,0.08) 60%, rgba(244,114,182,0.22) 100%)!important;animation:__mtk_trail_shimmer 1s ease-in-out infinite alternate!important;pointer-events:none!important;';
    beam.appendChild(trail);

    // The radiant laser line
    const line = document.createElement('div');
    line.style.cssText = 'width:100%!important;height:3px!important;background:linear-gradient(90deg, rgba(56,189,248,0) 0%, rgba(56,189,248,0.85) 15%, #ec4899 50%, rgba(56,189,248,0.85) 85%, rgba(56,189,248,0) 100%)!important;box-shadow:0 0 14px 2px rgba(236,72,153,0.9),0 0 28px 6px rgba(56,189,248,0.55)!important;pointer-events:none!important;';
    beam.appendChild(line);

    // Downward soft lead glow
    const lead = document.createElement('div');
    lead.style.cssText = 'width:100%!important;height:24px!important;background:linear-gradient(180deg, rgba(56,189,248,0.16) 0%, transparent 100%)!important;pointer-events:none!important;';
    beam.appendChild(lead);

    overlay.appendChild(beam);

    // Vertical Document Minimap / Progress Rail on right edge
    const rail = document.createElement('div');
    rail.style.cssText = 'position:fixed!important;right:14px!important;top:80px!important;bottom:50px!important;width:4px!important;background:rgba(255,255,255,0.18)!important;border-radius:999px!important;pointer-events:none!important;z-index:2147483647!important;';

    const railFill = document.createElement('div');
    railFill.style.cssText = 'width:100%!important;height:0%!important;background:linear-gradient(180deg, #38bdf8, #ec4899)!important;border-radius:999px!important;box-shadow:0 0 8px #ec4899!important;transition:height 0.04s linear!important;';
    rail.appendChild(railFill);

    const railHead = document.createElement('div');
    railHead.style.cssText = 'position:absolute!important;left:-4px!important;top:0%!important;width:12px!important;height:12px!important;border-radius:50%!important;background:#ffffff!important;box-shadow:0 0 10px #ec4899,0 0 16px #38bdf8!important;transform:translateY(-50%)!important;transition:top 0.04s linear!important;';
    rail.appendChild(railHead);

    overlay.appendChild(rail);

    // Floating Glassmorphic Status Pill
    const pill = document.createElement('div');
    pill.style.cssText = 'position:fixed!important;top:24px!important;left:50%!important;transform:translateX(-50%)!important;display:inline-flex!important;align-items:center!important;gap:12px!important;background:rgba(15,23,42,0.92)!important;backdrop-filter:blur(16px)!important;-webkit-backdrop-filter:blur(16px)!important;border:1px solid rgba(255,255,255,0.2)!important;border-radius:9999px!important;padding:8px 20px!important;color:#ffffff!important;box-shadow:0 12px 32px rgba(0,0,0,0.5),0 0 20px rgba(236,72,153,0.3)!important;pointer-events:none!important;z-index:2147483647!important;user-select:none!important;';

    const dot = document.createElement('span');
    dot.style.cssText = 'width:8px!important;height:8px!important;border-radius:50%!important;background:#ec4899!important;box-shadow:0 0 8px #ec4899,0 0 16px #ec4899!important;animation:__mtk_dot_pulse 0.7s infinite alternate!important;display:inline-block!important;';
    pill.appendChild(dot);

    const label = document.createElement('span');
    label.textContent = )JS" + std::string("\"") + badge_text + std::string("\"") + R"JS(;
    label.style.cssText = 'font-size:13px!important;font-weight:600!important;letter-spacing:0.3px!important;color:#f1f5f9!important;line-height:1!important;';
    pill.appendChild(label);

    const pct = document.createElement('span');
    pct.textContent = '0%';
    pct.style.cssText = 'font-size:13px!important;font-weight:700!important;color:#38bdf8!important;font-variant-numeric:tabular-nums!important;min-width:38px!important;text-align:right!important;line-height:1!important;';
    pill.appendChild(pct);

    const pxLabel = document.createElement('span');
    pxLabel.textContent = `0 / ${finalTargetH}px`;
    pxLabel.style.cssText = 'font-size:11px!important;font-weight:500!important;color:#94a3b8!important;font-variant-numeric:tabular-nums!important;line-height:1!important;';
    pill.appendChild(pxLabel);

    overlay.appendChild(pill);
    (body || doc).appendChild(overlay);

    // Dynamic duration: brisk, responsive, yet gives full tactile feedback
    const viewports = maxScroll / Math.max(1, vh);
    const duration = maxScroll === 0
      ? 550
      : Math.min(1600, Math.max(800, Math.round(700 + viewports * 110)));

    const startTime = performance.now();

    await new Promise((resolve) => {
      let rafId = 0;
      let timeoutId = 0;

      const finish = () => {
        if (timeoutId) clearTimeout(timeoutId);
        if (rafId) cancelAnimationFrame(rafId);
        resolve();
      };

      timeoutId = setTimeout(finish, duration + 600);

      const frame = (now) => {
        const elapsed = now - startTime;
        const progress = Math.min(1, Math.max(0, elapsed / duration));

        // Smooth cubic ease-in-out
        const ease = progress < 0.5
          ? 4 * progress * progress * progress
          : 1 - Math.pow(-2 * progress + 2, 3) / 2;

        if (maxScroll > 0) {
          const currentY = Math.round(ease * maxScroll);
          applyScroll(currentY);
        }

        const beamY = ease * (vh - 6);
        beam.style.transform = `translateY(${beamY}px)`;

        const percentVal = Math.min(100, Math.round(ease * 100));
        pct.textContent = `${percentVal}%`;
        const currentPx = Math.min(finalTargetH, Math.round(ease * finalTargetH));
        pxLabel.textContent = `${currentPx} / ${finalTargetH}px`;

        railFill.style.height = `${percentVal}%`;
        railHead.style.top = `${percentVal}%`;

        if (progress < 1) {
          rafId = requestAnimationFrame(frame);
        } else {
          finish();
        }
      };

      rafId = requestAnimationFrame(frame);
    });

    if (maxScroll > 0) {
      applyScroll(maxScroll);
    }
    pct.textContent = '100%';
    pxLabel.textContent = `${finalTargetH} / ${finalTargetH}px`;
    railFill.style.height = '100%';
    railHead.style.top = '100%';

    // Strip overlay immediately so screenshot capture contains only page contents
    if (overlay && overlay.parentNode) {
      overlay.parentNode.removeChild(overlay);
    }

    // Wait 2 frames to flush compositor paint tree
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));

    return { success: true, initialY: initialY, domHeight: finalTargetH, domWidth: finalTargetW };
  } catch (err) {
    return { success: false, error: String(err) };
  }
})();
)JS";

  return s;
}

class FullPageDevToolsObserver : public CefDevToolsMessageObserver {
 public:
  FullPageDevToolsObserver() = default;

  void Start(CefRefPtr<CefBrowser> browser,
             BrowserWindow* browser_window,
             HWND owner_hwnd,
             std::wstring suggested_name,
             std::string page_url,
             bool is_private) {
    if (!browser || !browser->GetHost()) {
      FallbackAndCleanup(nullptr);
      return;
    }
    browser_window_ = browser_window;
    owner_hwnd_ = owner_hwnd;
    suggested_name_ = std::move(suggested_name);
    page_url_ = std::move(page_url);
    is_private_ = is_private;
    initial_scroll_y_ = -1;
    capture_w_ = 1280.0;
    capture_h_ = 800.0;
    msg_scroll_anim_ = 0;
    msg_capture_screenshot_ = 0;
    cleaned_up_ = false;

    // Immediately trigger top-to-bottom crawling animation with full DOM measurement
    const std::string anim_script =
        BuildScrollAnimationScript(mtk::strings::IsHindi());
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetString("expression", anim_script);
    params->SetBool("awaitPromise", true);
    params->SetBool("returnByValue", true);
    params->SetBool("userGesture", true);

    msg_scroll_anim_ =
        browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
    if (msg_scroll_anim_ == 0) {
      FallbackAndCleanup(browser);
    }
  }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser,
                              int message_id,
                              bool success,
                              const void* result,
                              size_t result_size) override {
    if (cleaned_up_) return;
    if (!browser || !browser->GetHost()) {
      FallbackAndCleanup(nullptr);
      return;
    }

    // STEP 1 RESULT: Runtime.evaluate (Scroll animation finished)
    if (msg_scroll_anim_ > 0 && message_id == msg_scroll_anim_) {
      msg_scroll_anim_ = 0;
      if (success && result && result_size > 0) {
        std::string json_str(static_cast<const char*>(result), result_size);
        CefRefPtr<CefValue> parsed = CefParseJSON(json_str, JSON_PARSER_RFC);
        if (parsed && parsed->GetType() == VTYPE_DICTIONARY) {
          CefRefPtr<CefDictionaryValue> root = parsed->GetDictionary();
          if (root->HasKey("result") && root->GetType("result") == VTYPE_DICTIONARY) {
            CefRefPtr<CefDictionaryValue> res = root->GetDictionary("result");
            if (res->HasKey("value") && res->GetType("value") == VTYPE_DICTIONARY) {
              CefRefPtr<CefDictionaryValue> val = res->GetDictionary("value");
              if (val->HasKey("initialY")) {
                if (val->GetType("initialY") == VTYPE_INT) {
                  initial_scroll_y_ = val->GetInt("initialY");
                } else if (val->GetType("initialY") == VTYPE_DOUBLE) {
                  initial_scroll_y_ = static_cast<int>(val->GetDouble("initialY"));
                }
              }
              if (val->HasKey("domHeight")) {
                double dom_h = 0.0;
                if (val->GetType("domHeight") == VTYPE_DOUBLE) dom_h = val->GetDouble("domHeight");
                else if (val->GetType("domHeight") == VTYPE_INT) dom_h = static_cast<double>(val->GetInt("domHeight"));
                if (dom_h > 0.0) {
                  capture_h_ = std::clamp(dom_h, 100.0, 16384.0);
                }
              }
              if (val->HasKey("domWidth")) {
                double dom_w = 0.0;
                if (val->GetType("domWidth") == VTYPE_DOUBLE) dom_w = val->GetDouble("domWidth");
                else if (val->GetType("domWidth") == VTYPE_INT) dom_w = static_cast<double>(val->GetInt("domWidth"));
                if (dom_w > 0.0) {
                  capture_w_ = std::clamp(dom_w, 100.0, 16384.0);
                }
              }
            }
          }
        }
      }

      // Step 2: Execute captureScreenshot
      TriggerCaptureScreenshot(browser);
      return;
    }

    // STEP 2 RESULT: Page.captureScreenshot
    if (msg_capture_screenshot_ > 0 && message_id == msg_capture_screenshot_) {
      msg_capture_screenshot_ = 0;
      HandleScreenshotResult(browser, success, result, result_size);
      return;
    }
  }

 private:
  void TriggerCaptureScreenshot(CefRefPtr<CefBrowser> browser) {
    if (!browser || !browser->GetHost()) {
      FallbackAndCleanup(nullptr);
      return;
    }
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetString("format", "png");
    params->SetBool("captureBeyondViewport", true);
    params->SetBool("fromSurface", true);

    CefRefPtr<CefDictionaryValue> clip = CefDictionaryValue::Create();
    clip->SetDouble("x", 0.0);
    clip->SetDouble("y", 0.0);
    clip->SetDouble("width", capture_w_);
    clip->SetDouble("height", capture_h_);
    clip->SetDouble("scale", 1.0);
    params->SetDictionary("clip", clip);

    msg_capture_screenshot_ =
        browser->GetHost()->ExecuteDevToolsMethod(0, "Page.captureScreenshot", params);
    if (msg_capture_screenshot_ == 0) {
      FallbackAndCleanup(browser);
    }
  }

  void HandleScreenshotResult(CefRefPtr<CefBrowser> browser, bool success,
                              const void* result, size_t result_size) {
    // Restore user scroll position smoothly
    if (initial_scroll_y_ >= 0 && browser && browser->GetMainFrame() && browser->GetMainFrame()->IsValid()) {
      std::string restore_js = "try { window.scrollTo({ top: " +
                               std::to_string(initial_scroll_y_) +
                               ", behavior: 'smooth' }); if (document.scrollingElement) { document.scrollingElement.scrollTop = " +
                               std::to_string(initial_scroll_y_) +
                               "; } } catch(e){}";
      browser->GetMainFrame()->ExecuteJavaScript(restore_js, "", 0);
    }

    if (!success || !result || result_size == 0) {
      FallbackAndCleanup(browser);
      return;
    }

    const char* str = static_cast<const char*>(result);
    std::string_view sv(str, result_size);
    size_t key_pos = sv.find("\"data\"");
    if (key_pos == std::string_view::npos) {
      FallbackAndCleanup(browser);
      return;
    }
    size_t colon_pos = sv.find(':', key_pos + 6);
    if (colon_pos == std::string_view::npos) {
      FallbackAndCleanup(browser);
      return;
    }
    size_t quote_start = sv.find('\"', colon_pos + 1);
    if (quote_start == std::string_view::npos) {
      FallbackAndCleanup(browser);
      return;
    }
    size_t quote_end = sv.find('\"', quote_start + 1);
    if (quote_end == std::string_view::npos) {
      FallbackAndCleanup(browser);
      return;
    }

    std::string_view base64_sv = sv.substr(quote_start + 1, quote_end - quote_start - 1);
    DWORD binary_len = 0;
    if (!CryptStringToBinaryA(base64_sv.data(), static_cast<DWORD>(base64_sv.size()),
                             CRYPT_STRING_BASE64, nullptr, &binary_len, nullptr, nullptr) ||
        binary_len == 0) {
      FallbackAndCleanup(browser);
      return;
    }
    std::vector<BYTE> binary(binary_len);
    if (!CryptStringToBinaryA(base64_sv.data(), static_cast<DWORD>(base64_sv.size()),
                             CRYPT_STRING_BASE64, binary.data(), &binary_len, nullptr, nullptr)) {
      FallbackAndCleanup(browser);
      return;
    }

    // Copy to clipboard via WIC (best-effort)
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
      Microsoft::WRL::ComPtr<IWICStream> stream;
      if (SUCCEEDED(factory->CreateStream(&stream))) {
        if (SUCCEEDED(stream->InitializeFromMemory(binary.data(), static_cast<DWORD>(binary.size())))) {
          Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
          if (SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                        WICDecodeMetadataCacheOnLoad, &decoder))) {
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
              Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
              if (SUCCEEDED(factory->CreateFormatConverter(&converter))) {
                if (SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR,
                                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                                    WICBitmapPaletteTypeCustom))) {
                  UINT w = 0, h = 0;
                  converter->GetSize(&w, &h);
                  if (w > 0 && h > 0 && (static_cast<uint64_t>(w) * h < 50000000ULL)) {
                    HDC screen_dc = GetDC(nullptr);
                    if (screen_dc) {
                      HBITMAP hbm = CreateCompatibleBitmap(screen_dc, w, h);
                      if (hbm) {
                        BITMAPINFO bmi{};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = w;
                        bmi.bmiHeader.biHeight = -static_cast<int>(h);
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        std::vector<BYTE> pixels(w * h * 4);
                        if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
                          SetDIBits(screen_dc, hbm, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);
                          mtk::downloads::CopyBitmapToClipboard(owner_hwnd_, hbm);
                        }
                        DeleteObject(hbm);
                      }
                      ReleaseDC(nullptr, screen_dc);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    std::wstring target_path;
    std::wstring final_file_name = suggested_name_;
    const auto& prefs = mtk::preferences::Store::Get().values();
    if (prefs.download_ask_location) {
      if (!mtk::downloads::PromptDownloadLocation(owner_hwnd_, suggested_name_,
                                                  &target_path, &final_file_name)) {
        Cleanup(browser);
        return;
      }
    } else {
      target_path = mtk::downloads::UniqueTargetPath(suggested_name_);
      if (target_path.empty()) {
        target_path = mtk::downloads::DownloadDirectory() + L"\\" + suggested_name_;
      }
    }

    HANDLE hfile = CreateFileW(target_path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hfile != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      WriteFile(hfile, binary.data(), static_cast<DWORD>(binary.size()), &written, nullptr);
      CloseHandle(hfile);
    }

    mtk::downloads::Item item;
    item.id = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) |
              static_cast<uint64_t>(GetTickCount64() & 0xFFFFFFFF);
    item.file_name = final_file_name;
    item.full_path = target_path;
    item.url = page_url_;
    item.state = mtk::downloads::State::kComplete;
    item.received_bytes = binary.size();
    item.total_bytes = item.received_bytes;
    item.percent = 100;
    item.is_private = is_private_;
    mtk::downloads::Manager::Get().Upsert(item);

    Cleanup(browser);
  }

  void Cleanup(CefRefPtr<CefBrowser> browser) {
    if (cleaned_up_) return;
    cleaned_up_ = true;

    msg_scroll_anim_ = 0;
    msg_capture_screenshot_ = 0;
    browser_window_ = nullptr;
    owner_hwnd_ = nullptr;
    suggested_name_.clear();
    page_url_.clear();
    g_fullpage_screenshot_busy.store(false);
  }

  void FallbackAndCleanup(CefRefPtr<CefBrowser> browser) {
    BrowserWindow* bw = browser_window_;
    Cleanup(browser);
    if (bw) {
      bw->CaptureActivePageScreenshot();
    }
  }

  BrowserWindow* browser_window_ = nullptr;
  HWND owner_hwnd_ = nullptr;
  std::wstring suggested_name_;
  std::string page_url_;
  bool is_private_ = false;
  int initial_scroll_y_ = -1;
  double capture_w_ = 1280.0;
  double capture_h_ = 800.0;
  int msg_scroll_anim_ = 0;
  int msg_capture_screenshot_ = 0;
  bool cleaned_up_ = false;
  IMPLEMENT_REFCOUNTING(FullPageDevToolsObserver);
};

struct BrowserCaptureSession {
  CefRefPtr<FullPageDevToolsObserver> observer;
  CefRefPtr<CefRegistration> registration;
};

static std::map<int, BrowserCaptureSession> g_capture_sessions;
static std::mutex g_capture_sessions_mutex;

}  // namespace

extern "C" void RealCaptureFullPageScreenshot(BrowserWindow* browser_window) {
  if (!browser_window) return;

  const auto now = std::chrono::steady_clock::now();
  if (g_fullpage_screenshot_busy.load()) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - g_last_screenshot_start).count();
    if (elapsed > 12) {
      g_fullpage_screenshot_busy.store(false);
    } else {
      return;
    }
  }

  const auto* tab = browser_window->ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return;
  const auto browser = tab->client->browser();

  const int browser_id = browser->GetIdentifier();
  CefRefPtr<FullPageDevToolsObserver> observer;
  {
    std::lock_guard<std::mutex> lock(g_capture_sessions_mutex);
    auto it = g_capture_sessions.find(browser_id);
    if (it != g_capture_sessions.end() && it->second.observer && it->second.registration) {
      observer = it->second.observer;
    } else {
      observer = new FullPageDevToolsObserver();
      CefRefPtr<CefRegistration> reg = browser->GetHost()->AddDevToolsMessageObserver(observer);
      g_capture_sessions[browser_id] = {observer, reg};
    }
  }

  g_fullpage_screenshot_busy.store(true);
  g_last_screenshot_start = now;

  const std::wstring suggested_name =
      L"FullPage_" + mtk::downloads::SuggestScreenshotFileName(tab->title);

  const std::string page_url =
      (browser->GetMainFrame() && browser->GetMainFrame()->IsValid())
          ? browser->GetMainFrame()->GetURL().ToString()
          : "";

  observer->Start(browser, browser_window, browser_window->window_,
                  suggested_name, page_url,
                  browser_window->private_session_);
}

// ---------------------------------------------------------------------------
// 3D Animated Screenshot Toolbar Icon
// ---------------------------------------------------------------------------
static mtk::icons::Strip g_screenshot_strip;
static bool g_screenshot_strip_loaded = false;
static int g_screenshot_frame = 0;
static bool g_screenshot_hovering = false;
static HWND g_screenshot_button_hwnd = nullptr;

static constexpr UINT_PTR kScreenshotAnimTimer = 0x5343;  // 'SC'
static constexpr UINT kScreenshotFrameMs = 33;            // ~30 fps
static constexpr UINT_PTR kScreenshotSubclassId = 0x5343;

static VOID CALLBACK ScreenshotAnimTimerProc(HWND hwnd, UINT /*msg*/,
                                             UINT_PTR timer_id, DWORD /*time*/) {
  if (timer_id != kScreenshotAnimTimer) return;
  if (!g_screenshot_hovering && !mtk::screenshot_panel::IsOpen()) {
    KillTimer(hwnd, kScreenshotAnimTimer);
    return;
  }
  if (g_screenshot_strip.valid() && g_screenshot_strip.frame_count() > 1) {
    g_screenshot_frame = (g_screenshot_frame + 1) % g_screenshot_strip.frame_count();
    if (hwnd && IsWindow(hwnd)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }
}

static LRESULT CALLBACK ScreenshotButtonSubclassProc(HWND hwnd, UINT msg,
                                                     WPARAM wp, LPARAM lp,
                                                     UINT_PTR id, DWORD_PTR data) {
  if (msg == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    if (!g_screenshot_hovering) {
      g_screenshot_hovering = true;
      SetTimer(hwnd, kScreenshotAnimTimer, kScreenshotFrameMs, ScreenshotAnimTimerProc);
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  } else if (msg == WM_MOUSELEAVE) {
    g_screenshot_hovering = false;
    g_screenshot_frame = 0;
    if (!mtk::screenshot_panel::IsOpen()) {
      KillTimer(hwnd, kScreenshotAnimTimer);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  } else if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, ScreenshotButtonSubclassProc, id);
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

extern "C" void RealDrawScreenshotButton(HDC dc, const RECT* bounds,
                                         UINT item_state, HWND button_hwnd) {
  if (!dc || !bounds) return;

  if (!g_screenshot_strip_loaded) {
    g_screenshot_strip_loaded = true;
    g_screenshot_strip.Load(L"mtk_screenshot_hover.png");
    if (!g_screenshot_strip.valid()) {
      g_screenshot_strip.Load(L"mtk_screenshot_strip.png");
    }
  }

  const bool disabled = (item_state & ODS_DISABLED) != 0;
  const bool pressed = (item_state & ODS_SELECTED) != 0;
  const bool hot = (item_state & ODS_HOTLIGHT) != 0;
  const bool panel_open = mtk::screenshot_panel::IsOpen();

  if (button_hwnd && IsWindow(button_hwnd)) {
    g_screenshot_button_hwnd = button_hwnd;
    static HWND s_last_subclassed = nullptr;
    if (s_last_subclassed != button_hwnd) {
      s_last_subclassed = button_hwnd;
      SetWindowSubclass(button_hwnd, ScreenshotButtonSubclassProc,
                        kScreenshotSubclassId, 0);
    }
  }

  // Instant direct cursor check
  bool mouse_over = false;
  if (button_hwnd && IsWindow(button_hwnd)) {
    POINT pt{};
    GetCursorPos(&pt);
    RECT rc{};
    GetWindowRect(button_hwnd, &rc);
    mouse_over = PtInRect(&rc, pt);
  }

  if ((mouse_over || panel_open) && !g_screenshot_hovering && button_hwnd && IsWindow(button_hwnd)) {
    g_screenshot_hovering = true;
    SetTimer(button_hwnd, kScreenshotAnimTimer, kScreenshotFrameMs, ScreenshotAnimTimerProc);
  }

  const bool active = (g_screenshot_hovering || mouse_over || hot || panel_open) && !disabled;

  if (g_screenshot_strip.valid()) {
    RECT icon = *bounds;
    int dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
      auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
          GetProcAddress(user32, "GetDpiForWindow"));
      if (get_dpi && button_hwnd) {
        dpi = get_dpi(button_hwnd);
      }
    }
    const int inset = MulDiv(3, dpi ? dpi : 96, 96);
    InflateRect(&icon, -inset, -inset);

    int frame = 0;
    if (pressed) {
      // Shutter snap frame on click!
      frame = 11;
    } else if (active) {
      frame = g_screenshot_frame;
    } else {
      frame = 0;
    }
    g_screenshot_strip.Draw(dc, icon, frame);
  }
}

// 3D Animated Download Button
static mtk::icons::Strip g_download_strip;
static bool g_download_strip_loaded = false;
static int g_download_frame = 0;
static bool g_download_hovering = false;

static constexpr UINT_PTR kDownloadAnimTimer = 0x444C;  // 'DL'
static constexpr UINT kDownloadFrameMs = 33;            // ~30 fps
static constexpr UINT_PTR kDownloadSubclassId = 0x444C;

static VOID CALLBACK DownloadAnimTimerProc(HWND hwnd, UINT /*msg*/,
                                           UINT_PTR timer_id, DWORD /*time*/) {
  if (timer_id != kDownloadAnimTimer) return;
  if (!g_download_hovering) {
    KillTimer(hwnd, kDownloadAnimTimer);
    return;
  }
  if (g_download_strip.valid() && g_download_strip.frame_count() > 1) {
    g_download_frame = (g_download_frame + 1) % g_download_strip.frame_count();
    if (hwnd && IsWindow(hwnd)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }
}

static LRESULT CALLBACK DownloadButtonSubclassProc(HWND hwnd, UINT msg,
                                                   WPARAM wp, LPARAM lp,
                                                   UINT_PTR id, DWORD_PTR data) {
  if (msg == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    if (!g_download_hovering) {
      g_download_hovering = true;
      SetTimer(hwnd, kDownloadAnimTimer, kDownloadFrameMs, DownloadAnimTimerProc);
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  } else if (msg == WM_MOUSELEAVE) {
    g_download_hovering = false;
    g_download_frame = 0;
    KillTimer(hwnd, kDownloadAnimTimer);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, DownloadButtonSubclassProc, id);
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

extern "C" void RealDrawDownloadButton(HDC dc, const RECT* bounds,
                                       UINT item_state, HWND button_hwnd) {
  if (!dc || !bounds) return;

  if (!g_download_strip_loaded) {
    g_download_strip_loaded = true;
    g_download_strip.Load(L"mtk_download_hover.png");
  }

  const bool disabled = (item_state & ODS_DISABLED) != 0;
  const bool hot = (item_state & ODS_HOTLIGHT) != 0;

  if (button_hwnd && IsWindow(button_hwnd)) {
    static HWND s_last_subclassed_dl = nullptr;
    if (s_last_subclassed_dl != button_hwnd) {
      s_last_subclassed_dl = button_hwnd;
      SetWindowSubclass(button_hwnd, DownloadButtonSubclassProc,
                        kDownloadSubclassId, 0);
    }
  }

  // Instant direct cursor check
  bool mouse_over = false;
  if (button_hwnd && IsWindow(button_hwnd)) {
    POINT pt{};
    GetCursorPos(&pt);
    RECT rc{};
    GetWindowRect(button_hwnd, &rc);
    mouse_over = PtInRect(&rc, pt);
  }

  if (mouse_over && !g_download_hovering && button_hwnd && IsWindow(button_hwnd)) {
    g_download_hovering = true;
    SetTimer(button_hwnd, kDownloadAnimTimer, kDownloadFrameMs, DownloadAnimTimerProc);
  }

  const bool active = (g_download_hovering || mouse_over || hot) && !disabled;

  if (g_download_strip.valid()) {
    RECT icon = *bounds;
    int dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
      auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
          GetProcAddress(user32, "GetDpiForWindow"));
      if (get_dpi && button_hwnd) {
        dpi = get_dpi(button_hwnd);
      }
    }
    const int inset = MulDiv(4, dpi ? dpi : 96, 96);
    InflateRect(&icon, -inset, -inset);

    int frame = active ? g_download_frame : 0;
    g_download_strip.Draw(dc, icon, frame);
  }
}


