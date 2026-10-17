#pragma once

#include "browser_window.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_cookie.h"
#include "include/cef_frame.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_closure_task.h"
#include "bookmark_import.h"
#include "crash_guard.h"
#include "default_browser.h"
#include "dialog_text.h"
#include "download_manager.h"
#include "downloads_page.h"
#include "downloads_panel.h"
#include "focus_mode.h"
#include "history_page.h"
#include "home_page.h"
#include "home_scheme.h"
#include "library_store.h"
#include "ole_drag_drop.h"
#include "passkey_page.h"
#include "password_store.h"
#include "policy.h"
#include "preferences.h"
#include "profile.h"
#include "resource.h"
#include "session_store.h"
#include "settings_page.h"
#include "shields_panel.h"
#include "site_settings.h"
#include "strings.h"
#include "tab_preview.h"
#include "tab_search.h"
#include "task_manager_dialog.h"
#include "translation.h"
#include "updater.h"
#include "user_filters.h"
#include "web_app.h"

using Text = mtk::strings::Id;

inline constexpr wchar_t kWindowClass[] = L"MTKBrowserWindow";
inline constexpr char kDefaultHomeUrl[] = "mtk://home/";
extern std::string kHomeUrl;

inline constexpr int kBaseTabBarHeight = 44;
inline constexpr int kBaseToolbarHeight = 60;
inline constexpr int kBaseProgressHeight = 3;
inline constexpr int kBaseBookmarksBarHeight = 32;
extern int g_dpi;
extern bool g_bookmarks_bar;
extern std::vector<BrowserWindow*> g_windows;
extern mtk::update::Result g_update_result;
extern HWND g_downloads_dialog;

inline int Scaled(int value) { return MulDiv(value, g_dpi, 96); }
inline int TabBarHeight() { return Scaled(kBaseTabBarHeight); }
inline int ToolbarHeight() { return Scaled(kBaseToolbarHeight); }
inline int ProgressHeight() { return Scaled(kBaseProgressHeight); }
inline int BookmarksBarHeight() {
  return g_bookmarks_bar ? Scaled(kBaseBookmarksBarHeight) : 0;
}
inline int ChromeHeight() {
  return TabBarHeight() + ToolbarHeight() + BookmarksBarHeight();
}

inline constexpr UINT_PTR kProgressTimer = 1;
inline constexpr UINT_PTR kLibrarySaveTimer = 2;
inline constexpr UINT_PTR kDownloadSaveTimer = 3;
inline constexpr UINT_PTR kSessionSaveTimer = 4;
inline constexpr UINT_PTR kUpdateStartupTimer = 5;
inline constexpr UINT_PTR kUpdatePeriodicTimer = 6;
inline constexpr UINT_PTR kShieldAnimTimer = 7;
inline constexpr UINT_PTR kHomeAnimTimer = 8;
inline constexpr UINT_PTR kReloadAnimTimer = 9;
inline constexpr UINT_PTR kDownloadAnimTimer = 10;
inline constexpr UINT_PTR kMemorySaverTimer = 11;
inline constexpr UINT kShieldFrameMs = 33;
inline constexpr UINT kToolbarFrameMs = 33;
inline constexpr wchar_t kShieldStripFile[] = L"mtk_shield_hover.png";
inline constexpr UINT kUpdateStartupDelayMs = 45 * 1000;
inline constexpr UINT kUpdatePeriodMs = 12 * 60 * 60 * 1000;
inline constexpr UINT kFinalizeCloseMessage = WM_APP + 1;
inline constexpr UINT kUpdateResultMessage = WM_APP + 2;
inline constexpr UINT kShieldsManageSitesMessage = WM_APP + 3;

inline constexpr INT_PTR kBackId = 1001;
inline constexpr INT_PTR kForwardId = 1002;
inline constexpr INT_PTR kReloadId = 1003;
inline constexpr INT_PTR kHomeId = 1004;
inline constexpr INT_PTR kAddressId = 1005;
inline constexpr INT_PTR kSuggestionsId = 1006;
inline constexpr size_t kMaxSuggestions = 8;
inline constexpr size_t kMaxClosedTabs = 10;
inline constexpr INT_PTR kShieldId = 1007;
inline constexpr INT_PTR kMenuId = 1008;
inline constexpr INT_PTR kFindId = 1009;
inline constexpr INT_PTR kFindPreviousId = 1010;
inline constexpr INT_PTR kFindNextId = 1011;
inline constexpr INT_PTR kFindCloseId = 1012;
inline constexpr INT_PTR kDownloadsId = 1013;

inline const wchar_t* ButtonGlyph(UINT control_id) {
  switch (control_id) {
    case kBackId: return L"\u2190";
    case kForwardId: return L"\u2192";
    case kReloadId: return L"\u21BB";
    case kHomeId: return L"\u2302";
    case kShieldId: return L"\u25C6";
    case kDownloadsId: return L"\u2193";
    case kMenuId: return L"\u22EE";
    case kFindPreviousId: return L"\u2039";
    case kFindNextId: return L"\u203A";
    case kFindCloseId: return L"\u00D7";
    default: return L"";
  }
}

inline constexpr mtk::preferences::DohProvider kDohOrder[] = {
    mtk::preferences::DohProvider::kCloudflareFamily,
    mtk::preferences::DohProvider::kCloudflare,
    mtk::preferences::DohProvider::kQuad9,
    mtk::preferences::DohProvider::kGoogle,
    mtk::preferences::DohProvider::kSystem,
};

inline constexpr size_t kMaxProfileMenuEntries = 20;
inline constexpr size_t kMaxFolderMenuEntries = 100;

inline constexpr int kTabMenuDuplicate = 1401;
inline constexpr int kTabMenuPin = 1402;
inline constexpr int kTabMenuMute = 1403;
inline constexpr int kTabMenuClose = 1404;
inline constexpr int kTabMenuCloseOthers = 1405;
inline constexpr int kTabMenuCloseRight = 1406;
inline constexpr int kTabMenuDetach = 1407;
inline constexpr int kTabMenuSleepWake = 1408;
inline constexpr size_t kNoTab = static_cast<size_t>(-1);

inline constexpr COLORREF kWindowColor = RGB(9, 12, 22);
inline constexpr COLORREF kTabBarTop = RGB(19, 23, 38);
inline constexpr COLORREF kTabBarBottom = RGB(14, 18, 30);
inline constexpr COLORREF kToolbarTop = RGB(27, 32, 50);
inline constexpr COLORREF kToolbarBottom = RGB(20, 25, 40);
inline constexpr COLORREF kPrivateBarTop = RGB(32, 24, 52);
inline constexpr COLORREF kPrivateBarBottom = RGB(22, 17, 38);
inline constexpr COLORREF kActiveTabTop = RGB(41, 48, 74);
inline constexpr COLORREF kActiveTabBottom = RGB(28, 34, 54);
inline constexpr COLORREF kHoverTabColor = RGB(28, 34, 53);
inline constexpr COLORREF kControlColor = RGB(31, 37, 58);
inline constexpr COLORREF kControlHoverColor = RGB(45, 53, 82);
inline constexpr COLORREF kBorderColor = RGB(52, 62, 95);
inline constexpr COLORREF kAccentColor = RGB(124, 92, 255);
inline constexpr COLORREF kAccentSoftColor = RGB(96, 165, 250);
inline constexpr COLORREF kShieldColor = RGB(74, 222, 128);
inline constexpr COLORREF kTextColor = RGB(240, 244, 255);
inline constexpr COLORREF kMutedColor = RGB(150, 162, 190);
inline constexpr COLORREF kDisabledColor = RGB(84, 94, 120);

inline void FillGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
  const int height = rect.bottom - rect.top;
  if (height <= 0) return;
  for (int offset = 0; offset < height; ++offset) {
    const double ratio = static_cast<double>(offset) / std::max(1, height - 1);
    const int red = static_cast<int>(GetRValue(top) +
                                     (GetRValue(bottom) - GetRValue(top)) * ratio);
    const int green = static_cast<int>(GetGValue(top) +
                                       (GetGValue(bottom) - GetGValue(top)) * ratio);
    const int blue = static_cast<int>(GetBValue(top) +
                                      (GetBValue(bottom) - GetBValue(top)) * ratio);
    RECT line{rect.left, rect.top + offset, rect.right, rect.top + offset + 1};
    HBRUSH brush = CreateSolidBrush(RGB(red, green, blue));
    FillRect(dc, &line, brush);
    DeleteObject(brush);
  }
}

inline void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
  HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1,
                                   rect.bottom + 1, radius, radius);
  HBRUSH brush = CreateSolidBrush(color);
  FillRgn(dc, region, brush);
  DeleteObject(brush);
  DeleteObject(region);
}

inline void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color,
                          int width = 1) {
  HPEN pen = CreatePen(PS_SOLID, width, color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

inline void FillCircle(HDC dc, int center_x, int center_y, int radius, COLORREF color) {
  HRGN region = CreateEllipticRgn(center_x - radius, center_y - radius,
                                  center_x + radius + 1, center_y + radius + 1);
  HBRUSH brush = CreateSolidBrush(color);
  FillRgn(dc, region, brush);
  DeleteObject(brush);
  DeleteObject(region);
}

inline std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

inline std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

inline void RefreshHomeSnapshot() {
  const bool is_light = mtk::preferences::ShouldUseLightAppearance(
      mtk::preferences::Store::Get().values().theme_mode);
  mtk::home::RefreshSnapshot(mtk::library::Store::Get().history(),
                             mtk::library::Store::Get().bookmarks(),
                             mtk::downloads::Manager::Get().items(),
                             is_light);
  mtk::history::RefreshSnapshot(mtk::library::Store::Get().history());
}

inline void RefreshHomeUrl() {
  const auto& values = mtk::preferences::Store::Get().values();
  if (!values.home_page_custom) {
    kHomeUrl = mtk::home::kHomeUrl;
    return;
  }
  const auto decision = mtk::policy::ResolveInput(WideToUtf8(values.home_page));
  kHomeUrl = decision.allow ? decision.url : mtk::home::kHomeUrl;
}

inline constexpr int kBaseTabGap = 6;
inline constexpr int kBaseTabStripInset = 10;
inline constexpr int kBaseNewTabWidth = 38;
inline constexpr int kBasePinnedTabWidth = 54;

inline int TabStripReserve() { return Scaled(96); }
inline int PinnedSlotWidth() { return Scaled(kBasePinnedTabWidth); }

inline int TabSlotWidth(int width, size_t count, size_t pinned) {
  const size_t unpinned = count > pinned ? count - pinned : 0;
  const int caption_reserve = Scaled(46) * 3;
  const int available =
      std::max(Scaled(200),
               width - caption_reserve - TabStripReserve() - Scaled(kBaseTabStripInset) -
                   static_cast<int>(pinned) * PinnedSlotWidth());
  return std::clamp(available / static_cast<int>(std::max<size_t>(unpinned, 1)),
                    Scaled(150), Scaled(256));
}

inline int TabSlotLeft(size_t index, int width, size_t count, size_t pinned,
                       int scroll) {
  const int slot = TabSlotWidth(width, count, pinned);
  const size_t leading = std::min(index, pinned);
  const size_t trailing = index > pinned ? index - pinned : 0;
  return Scaled(kBaseTabStripInset) +
         static_cast<int>(leading) * PinnedSlotWidth() +
         static_cast<int>(trailing) * slot - scroll;
}

inline RECT TabRect(size_t index, int width, size_t count, size_t pinned,
                    int scroll = 0, bool maximized = false) {
  const int left = TabSlotLeft(index, width, count, pinned, scroll);
  const int right =
      TabSlotLeft(index + 1, width, count, pinned, scroll) - Scaled(kBaseTabGap);
  const int top = maximized ? Scaled(2) : Scaled(6);
  return {left, top, right, TabBarHeight()};
}

inline RECT NewTabRect(int width, size_t count, size_t pinned, int scroll = 0,
                       bool maximized = false) {
  const int left = TabSlotLeft(count, width, count, pinned, scroll);
  const int top = maximized ? Scaled(4) : Scaled(8);
  return {left + Scaled(2), top, left + Scaled(kBaseNewTabWidth),
          TabBarHeight() - (maximized ? 0 : Scaled(4))};
}

inline RECT TabCloseRect(const RECT& tab, bool pinned) {
  if (pinned) return RECT{0, 0, 0, 0};
  const int center_y = (tab.top + tab.bottom) / 2;
  const int half = Scaled(12);
  return {tab.right - Scaled(34), center_y - half, tab.right - Scaled(10),
          center_y + half};
}

inline RECT TabMuteRect(const RECT& tab, bool pinned) {
  const int center_y = (tab.top + tab.bottom) / 2;
  const int half = Scaled(11);
  const int right = pinned ? tab.right - Scaled(6) : tab.right - Scaled(38);
  return {right - Scaled(22), center_y - half, right, center_y + half};
}

bool PromptForText(HINSTANCE instance, HWND owner, const wchar_t* caption,
                   const wchar_t* label, std::wstring* value);
bool AuthorizeParentAction(HINSTANCE instance, HWND owner,
                           const wchar_t* message);
void PopulateDownloadList(HWND dialog);
void RevealInExplorer(HWND owner, const std::wstring& path);
void OpenDownloadedFile(HWND owner, const mtk::downloads::Item& item);
