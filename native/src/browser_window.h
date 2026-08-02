#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "browser_client.h"
#include "icon_strip.h"

class BrowserWindow final : public BrowserClientDelegate {
 public:
  // Creates a top-level browser window that owns itself: it is registered on
  // creation and deleted once its window is destroyed. Returns nullptr when the
  // window could not be created. `startup_urls` empty means "use the configured
  // startup behaviour", which only the first window does.
  static BrowserWindow* Open(HINSTANCE instance, int show_command,
                             const std::vector<std::string>& startup_urls,
                             bool private_session = false);
  // Number of open top-level windows. The message loop ends at zero.
  static size_t WindowCount();

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
    // Pinned tabs are kept at the front of the strip in a narrow slot and have
    // no close button.
    bool pinned = false;
    bool muted = false;
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
  // `internal_page` marks a tab that opens one of the browser's own
  // `data:text/html` pages, which the navigation policy would otherwise refuse.
  bool CreateTab(const std::string& initial_url, bool internal_page = false);
  void ShowFindBar();
  void HideFindBar();
  void RunFind(bool forward, bool find_next);
  void ZoomActiveTab(cef_zoom_command_t command);
  // Opens Chromium's print preview, which also provides Save as PDF.
  void PrintActiveTab();
  // Per-site permissions, cookies, ad filtering and remembered zoom for the
  // site in the active tab.
  void ShowSiteSettingsDialog();
  // Saved sign-ins. Opening the list requires the parent PIN when one is set.
  void ShowPasswordsDialog();
  // Registers the browser with Windows and opens the Default apps page, which is
  // the only place the choice can actually be made on Windows 10 and 11.
  void MakeDefaultBrowser();
  // Editor for the user's own filter rules, behind the parent PIN because a rule
  // can allow as well as block.
  void ShowUserFiltersDialog();
  // Applies the site's remembered zoom to `tab`.
  void ApplySiteZoom(Tab& tab);
  // Opens the keyboard shortcut reference in a new tab. The shortcuts are not
  // the ones other browsers use, so they have to be discoverable in the product
  // rather than only in the repository.
  void ShowShortcutsPage();
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
  // Toolbar popup with the most recent downloads and inline actions.
  void ShowDownloadsPanel();
  void OpenLibraryEntry(bool bookmarks, size_t index);
  void ScheduleLibrarySave();
  void ScheduleSessionSave();
  void SetShieldHover(bool hovering);
  void SaveSessionSnapshot();
  void CreateStartupTabs();
  // Opens another top-level window, optionally starting on `url`. A private
  // window gets its own in-memory request context, so its cookies, cache and
  // storage never reach disk and are discarded when it closes.
  void OpenNewWindow(const std::string& url, bool private_session = false);
  // Points the shared DPI and bookmarks-bar metrics at this window. Those are
  // file-level state shared by the painting helpers, so every entry point that
  // can run for a different window has to set them first.
  void ActivateMetrics() const;
  void SelectTab(size_t index);
  void CloseTab(size_t index);
  // Tab management. Pinned tabs are always the leading run of the strip, so
  // pinning and reordering both keep that invariant.
  size_t PinnedCount() const;
  void MoveTab(size_t from, size_t to);
  void TogglePin(size_t index);
  void ToggleMute(size_t index);
  void DuplicateTab(size_t index);
  void CloseOtherTabs(size_t index);
  void CloseTabsToTheRight(size_t index);
  void ShowTabMenu(size_t index, POINT screen);
  // Drag-reorder. A press only becomes a drag after the mouse leaves a small
  // dead zone, so an ordinary click still selects.
  void BeginTabDrag(size_t index, POINT point);
  void UpdateTabDrag(POINT point);
  void EndTabDrag();
  void NavigateFromOmnibox();
  void Layout();
  void Paint(HDC dc);
  void PaintTabStrip(HDC dc, const RECT& client);
  // Bookmarks strip under the toolbar. Painted, not built from child windows,
  // so it scales with DPI the same way the rest of the chrome does.
  void PaintBookmarksBar(HDC dc, const RECT& client);
  void ToggleBookmarksBar();
  // Imports bookmarks from an installed Chromium-family browser.
  void ImportBookmarksFromBrowser();
  // Starts another process for `profile_id`. Profiles cannot share one process,
  // because the stores and the Chromium cache directory are process-wide.
  void LaunchProfile(const std::wstring& profile_id);
  // Asks for a name, creates the profile and opens it.
  void CreateProfile();
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
  // This window's DPI, so several windows on displays with different scaling
  // each paint at their own size.
  int dpi_ = 96;
  // Only one window writes the session snapshot, because the snapshot format
  // holds a single tab list. Ownership passes to a remaining window when the
  // owner closes.
  bool owns_session_ = false;
  // Urls this window opens with. Empty means "follow the startup preference",
  // which is only true of the first window.
  std::vector<std::string> startup_urls_;
  // Private window state. `request_context_` is only set for private windows;
  // an ordinary window passes nullptr and uses the global on-disk context.
  bool private_session_ = false;
  CefRefPtr<CefRequestContext> request_context_;
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
  // Tab being dragged, its press position, and whether the dead zone has been
  // left. `drag_candidate_` is set on press; `dragging_` only once it moves.
  size_t drag_candidate_ = static_cast<size_t>(-1);
  POINT drag_origin_{};
  bool dragging_ = false;
  // Clickable areas of the bookmarks bar, rebuilt on every paint so hit
  // testing can never disagree with what is on screen.
  struct BookmarkHit {
    RECT bounds;
    size_t index;
  };
  std::vector<BookmarkHit> bookmark_bar_hits_;
  size_t hovered_bookmark_ = static_cast<size_t>(-1);
  // Profile names behind the profile submenu entries, in menu order.
  std::vector<std::wstring> profile_menu_ids_;
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
