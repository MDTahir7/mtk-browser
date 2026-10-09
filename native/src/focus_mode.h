#pragma once

namespace mtk::focus {

// MTK Focus Mode script for distraction-free YouTube:
// - Completely removes the suggested / recommended videos sidebar (#secondary, #related)
// - Removes player end-screen video recommendation grids (.ytp-endscreen-content, .ytp-ce-element)
// - Removes pause overlay recommendations
// - Prevents auto-navigation countdown to next recommended video
// - Centers the video player cleanly on watch pages
constexpr char kEnableScript[] = R"JS((() => {
  const STYLE_ID = 'mtk-focus-mode-style';
  if (!location.hostname.includes('youtube.com')) return;

  function applyFocus() {
    let style = document.getElementById(STYLE_ID);
    if (!style) {
      style = document.createElement('style');
      style.id = STYLE_ID;
      (document.head || document.documentElement).appendChild(style);
    }
    style.textContent = `
      #secondary,
      ytd-watch-flexy #secondary,
      ytd-watch-grid #secondary,
      #secondary.ytd-watch-flexy,
      #related,
      ytd-watch-next-secondary-results-renderer,
      yt-related-chip-cloud-renderer,
      ytd-item-section-renderer[watch-next-feed],
      #items.ytd-watch-next-secondary-results-renderer,
      .ytp-endscreen-content,
      .ytp-ce-element,
      .ytp-ce-video,
      .ytp-ce-playlist,
      .ytp-ce-channel,
      .ytp-ce-covering-overlay,
      .ytp-ce-element-show,
      .ytp-pause-overlay,
      .ytp-pause-overlay-container,
      .ytp-autonav-endscreen-countdown-overlay,
      .ytp-autonav-endscreen-button-container,
      .ytp-endscreen-paginate,
      .ytp-suggestion-set,
      ytd-reel-shelf-renderer {
        display: none !important;
      }
      ytd-watch-flexy:not([theater]):not([fullscreen]) #columns.ytd-watch-flexy {
        justify-content: center !important;
      }
      ytd-watch-flexy #primary.ytd-watch-flexy,
      ytd-watch-grid #primary {
        max-width: 1280px !important;
        margin: 0 auto !important;
      }
    `;
    try {
      const autonav = document.querySelector('.ytp-autonav-toggle-button[aria-checked="true"]');
      if (autonav) autonav.click();
    } catch (_) {}
  }

  applyFocus();
  if (!window._mtk_focus_listener_attached) {
    window._mtk_focus_listener_attached = true;
    window.addEventListener('yt-navigate-finish', applyFocus);
  }
})();)JS";

// Removes Focus Mode stylesheet so recommendations immediately reappear without reloading
constexpr char kDisableScript[] = R"JS((() => {
  const style = document.getElementById('mtk-focus-mode-style');
  if (style) style.remove();
})();)JS";

}  // namespace mtk::focus
