#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "browser_client.h"
#include "icon_strip.h"

class BrowserWindow final : public BrowserClientDelegate {
 public:
  explicit BrowserWindow(HINSTANCE instance);
  ~BrowserWindow();

  bool Create(int show_command);

  void OnBrowserCreated(BrowserClient* client) override;
  void OnBrowserClosed(BrowserClient* client) override;
  void OnBrowserAddressChanged(BrowserClient* client,
                               const std::wstring& url) override;
  void OnBrowserTitleChanged(BrowserClient* client,
                             const std::wstring& title) override;
  void OnBrowserLoadingChanged(BrowserClient* client, bool loading,
                               bool can_go_back,
                               bool can_go_forward) override;
  void OnBrowserNavigationCompleted(BrowserClient* client,
                                    const std::string& url) override;
  void OnBrowserFindResult(BrowserClient* client, int identifier, int count,
                           int active_match_ordinal,
                           bool final_update) override;
  void OnBrowserPopupRequested(BrowserClient* client,
                               const std::string& url) override;
  void OnBrowserDownloadsChanged(BrowserClient* client) override;
  bool OnBrowserShortcut(BrowserClient* client, int virtual_key,
                         bool control, bool alt, bool shift) override;

 private:
  struct Tab {
    CefRefPtr<BrowserClient> client;
    std::wstring title = L"New Tab";
    std::wstring url;
    bool loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
  };

  void RememberClosedTab(const Tab& tab);

  static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                     WPARAM w_param, LPARAM l_param);
  static LRESULT CALLBACK AddressProc(HWND window, UINT message,
                                      WPARAM w_param, LPARAM l_param,
                                      UINT_PTR subclass_id,
                                      DWORD_PTR reference_data);
  static LRESULT CALLBACK FindProc(HWND window, UINT message,
                                   WPARAM w_param, LPARAM l_param,
                                   UINT_PTR subclass_id,
                                   DWORD_PTR reference_data);
  // Owner-draw buttons only repaint when Windows asks them to, so hover has to
  // be observed on the control itself to drive the shield animation.
  static LRESULT CALLBACK ShieldProc(HWND window, UINT message,
                                     WPARAM w_param, LPARAM l_param,
                                     UINT_PTR subclass_id,
                                     DWORD_PTR reference_data);
  LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
  bool HandleShortcut(int virtual_key, bool control, bool alt, bool shift);
  void CreateControls();
  void CreateFonts();
  void ApplyDpi(UINT dpi);
  bool CreateTab(const std::string& initial_url);
  void ShowFindBar();
  void HideFindBar();
  void RunFind(bool forward, bool find_next);
  void ZoomActiveTab(cef_zoom_command_t command);
  void ShowBrowserMenu();
  void ShowSettingsDialog();
  void ShowClearDataDialog();
  // Signed update check. The network work runs on a worker thread and reports
  // back through kUpdateResultMessage.
  void StartUpdateCheck(bool user_initiated);
  void HandleUpdateResult();
  // Borderless full-screen page view. The chrome is hidden, not destroyed.
  void ToggleFullScreen();
  // Reopens the most recently closed tab, like Ctrl+Shift+T elsewhere.
  bool ReopenClosedTab();
  // Horizontal tab-strip scrolling for when the tabs no longer fit.
  int MaxTabScroll(int width) const;
  void ScrollTabs(int delta);
  void EnsureActiveTabVisible();
  // Page area top: the chrome height normally, zero in full screen.
  int PageTop() const;
  // Omnibox autocomplete drawn from history and bookmarks.
  void RefreshSuggestions();
  void HideSuggestions();
  void MoveSuggestion(int delta);
  // True when a suggestion was highlighted and has been navigated to.
  bool AcceptSuggestion();
  void LayoutSuggestions();
  void ToggleBookmark();
  void ShowLibraryDialog(bool bookmarks);
  void ShowDownloadsDialog();
  void OpenLibraryEntry(bool bookmarks, size_t index);
  void ScheduleLibrarySave();
  void ScheduleSessionSave();
  void SetShieldHover(bool hovering);
  void SaveSessionSnapshot();
  void CreateStartupTabs();
  void SelectTab(size_t index);
  void CloseTab(size_t index);
  void NavigateFromOmnibox();
  void Layout();
  void Paint(HDC dc);
  void PaintTabStrip(HDC dc, const RECT& client);
  void PaintOmniboxFrame(HDC dc);
  void PaintProgress(HDC dc, const RECT& client);
  void DrawButton(const DRAWITEMSTRUCT& item);
  void TrackHover(POINT point);
  void ClearHover();
  void UpdateControls();
  void ResizeBrowser(Tab& tab);
  size_t FindTab(BrowserClient* client) const;
  Tab* ActiveTab();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND address_ = nullptr;
  HWND suggestions_ = nullptr;
  HWND find_ = nullptr;
  HWND find_status_ = nullptr;
  HWND find_previous_ = nullptr;
  HWND find_next_ = nullptr;
  HWND find_close_ = nullptr;
  HWND back_ = nullptr;
  HWND forward_ = nullptr;
  HWND reload_ = nullptr;
  HWND home_ = nullptr;
  HWND shield_ = nullptr;
  HWND downloads_ = nullptr;
  HWND menu_ = nullptr;
  HFONT ui_font_ = nullptr;
  HFONT tab_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT badge_font_ = nullptr;
  HBRUSH edit_brush_ = nullptr;
  std::vector<Tab> tabs_;
  // URLs behind the visible suggestion rows, in the same order.
  std::vector<std::string> suggestion_urls_;
  // Recently closed tabs, newest last, bounded so it cannot grow unchecked.
  std::vector<std::string> closed_tab_urls_;
  // Horizontal scroll offset of the tab strip, in pixels.
  int tab_scroll_ = 0;
  bool full_screen_ = false;
  // Window placement and style captured before entering full screen.
  WINDOWPLACEMENT saved_placement_{};
  LONG_PTR saved_style_ = 0;
  // True while the omnibox text is being replaced programmatically, so the
  // resulting EN_CHANGE does not rebuild the list.
  bool suppress_suggestions_ = false;
  // Tabs disappear from the strip immediately, but their clients stay alive
  // here until CEF delivers OnBeforeClose.
  std::vector<CefRefPtr<BrowserClient>> closing_clients_;
  size_t active_tab_ = 0;
  size_t hovered_tab_ = static_cast<size_t>(-1);
  bool hovered_tab_close_ = false;
  bool hovered_new_tab_ = false;
  bool mouse_tracking_ = false;
  bool find_visible_ = false;
  bool find_active_ = false;
  int active_find_identifier_ = -1;
  bool progress_active_ = false;
  int progress_phase_ = 0;
  bool closing_ = false;
  // Rendered shield frames for the toolbar button, plus the hover playback
  // state. An unavailable strip leaves the static glyph in place.
  mtk::icons::Strip shield_strip_;
  bool shield_hovering_ = false;
  int shield_frame_ = 0;
  // A single update check at a time; the worker owns the pending result until
  // the UI thread collects it.
  bool update_check_running_ = false;
  bool update_check_user_initiated_ = false;
};
