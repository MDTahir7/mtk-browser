#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "browser_client.h"
#include "home_page.h"
#include "icon_strip.h"
#include "memory_saver.h"

namespace mtk::launch {
inline constexpr ULONG_PTR kOpenUrlCopyData = 0x4D544B55;  // "MTKU"
}

namespace mtk::session { struct Snapshot; struct Window; }

class BrowserWindow;
extern "C" void RealCaptureFullPageScreenshot(BrowserWindow* browser_window);

class BrowserWindow final : public BrowserClientDelegate {
 public:
  // Creates a top-level browser window that owns itself: it is registered on
  // creation and deleted once its window is destroyed. Returns nullptr when the
  // window could not be created. `startup_urls` empty means "use the configured
  // startup behaviour", which only the first window does.
  static BrowserWindow* Open(HINSTANCE instance, int show_command,
                             const std::vector<std::string>& startup_urls,
                             bool private_session = false,
                             bool defer_initial_tab = false,
                             bool app_mode = false);
  // Number of open top-level windows. The message loop ends at zero.
  static size_t WindowCount();

  bool app_mode() const { return app_mode_; }
  void set_app_mode(bool val) { app_mode_ = val; }
  bool private_session() const { return private_session_; }

  void ToggleScreenshotFlyout();
  void CaptureActivePageScreenshot();
  void StartSnipRegionScreenshot();
  void CaptureFullPageScreenshot();
  friend void RealCaptureFullPageScreenshot(BrowserWindow* browser_window);

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
  void OnBrowserFaviconUrlsChanged(
      BrowserClient* client, const std::vector<std::string>& icon_urls) override;
  static void ApplyDownloadedFavicon(const std::string& host,
                                     const uint32_t* src_pixels,
                                     int src_w, int src_h);
  void OnBrowserFindResult(BrowserClient* client, int identifier, int count,
                           int active_match_ordinal,
                           bool final_update) override;
  void OnBrowserPopupRequested(BrowserClient* client,
                               const std::string& url) override;
  void OnBrowserDownloadsChanged(BrowserClient* client) override;
  void OnBrowserMediaAccessChanged(BrowserClient* client, bool camera_active,
                                   bool microphone_active) override;
  void OnBrowserHomeAction(BrowserClient* client,
                           const mtk::home::Action& action) override;
  void OnBrowserHistoryAction(BrowserClient* client,
                              const mtk::history::Action& action) override;
  void OnBrowserPasskeyAction(BrowserClient* client,
                              mtk::passkeys::Action action) override;
  void OnBrowserDownloadsAction(
      BrowserClient* client, const mtk::downloads_page::Action& action) override;
  void OnBrowserSettingsAction(
      BrowserClient* client, const mtk::settings_page::Action& action) override;
  bool OnBrowserShortcut(BrowserClient* client, int virtual_key,
                         bool control, bool alt, bool shift) override;
  void OnBrowserFullscreenModeChange(BrowserClient* client,
                                     bool fullscreen) override;
  void OnBrowserGotFocus(BrowserClient* client) override;
  bool IsSplitViewActive() const override;
  void OpenLinkInOtherPane(const std::string& url) override;
  void OnBrowserRequestBlocked(BrowserClient* client) override;
  void TakeActivePageScreenshot() override;

  // Retries a failed/canceled download from the visible history. The original
  // row remains as an audit trail; CEF creates a fresh row for the new request.
  bool RetryDownload(uint64_t id);

  // Resumes an interrupted/paused download from a prior session using HTTP Range.
  bool ResumeInterruptedDownload(uint64_t id);

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
    std::wstring group;
    std::wstring workspace;
    bool muted = false;
    // Updated from CEF's live media-access callback, not from an Allow rule.
    bool camera_active = false;
    bool microphone_active = false;
    // Reader mode changes only the live document. A normal reload restores the
    // untouched page, and no extracted content is saved or sent anywhere.
    bool reader_mode = false;
    bool focus_mode = true;
    // Memory Saver / Tab Discarding: inactive background tabs release their CEF host.
    bool discarded = false;
    uint64_t last_active_tick = 0;
    HBITMAP favicon = nullptr;
    int favicon_w = 0;
    int favicon_h = 0;
    std::string favicon_host;

    mtk::memory::TabState ToMemoryState(bool is_active, bool is_split) const {
      mtk::memory::TabState state;
      state.active = is_active;
      state.split = is_split;
      state.pinned = pinned;
      state.loading = loading;
      state.camera_active = camera_active;
      state.microphone_active = microphone_active;
      state.discarded = discarded;
      state.last_active_tick = last_active_tick;
      return state;
    }
  };

  struct BookmarkHit {
    RECT bounds{};
    size_t index = 0;
    bool folder = false;
    bool overflow = false;
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
  // be observed on the control itself to drive the shield, home, and reload animations.
  static LRESULT CALLBACK ReloadProc(HWND window, UINT message,
                                     WPARAM w_param, LPARAM l_param,
                                     UINT_PTR subclass_id,
                                     DWORD_PTR reference_data);
  static LRESULT CALLBACK ShieldProc(HWND window, UINT message,
                                     WPARAM w_param, LPARAM l_param,
                                     UINT_PTR subclass_id,
                                     DWORD_PTR reference_data);
  static LRESULT CALLBACK DownloadsProc(HWND window, UINT message,
                                        WPARAM w_param, LPARAM l_param,
                                        UINT_PTR subclass_id,
                                        DWORD_PTR reference_data);
  static LRESULT CALLBACK HomeProc(HWND window, UINT message,
                                   WPARAM w_param, LPARAM l_param,
                                   UINT_PTR subclass_id,
                                   DWORD_PTR reference_data);
  static LRESULT CALLBACK FocusProc(HWND window, UINT message,
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
  // Opens CEF DevTools for the active tab in an associated window.
  void ShowDeveloperTools();
  // Per-site permissions, cookies, ad filtering and remembered zoom for the
  // site in the active tab.
  void ShowSiteSettingsDialog();
  // Same dialog, for an origin chosen from the site list rather than the
  // active tab (so a stored rule can be reviewed without navigating there).
  void ShowSiteSettingsDialogFor(const std::string& origin);
  void ShowSiteListDialog();
  void ToggleShieldForCurrentSite();
  void ToggleCookiesForCurrentSite();
  void ToggleDownloadProtection();
  void UpdateShieldsPanel();
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
  void ShowPasskeyCheckPage();
  void ShowTabGroupsMenu();
  void ShowWorkspacesMenu();
  void SetActiveWorkspace(const std::wstring& name);
  // Shows the active tab beside one other existing tab. Both pages keep their
  // normal CEF state; this only changes native child-window layout.
  void ToggleSplitView();
  void SaveActivePagePdf();
  void ShowDiagnostics();
  void ToggleReaderMode();
  void ToggleFocusMode();
  void ApplyFocusMode(Tab& tab);
  void SetFocusHover(bool hovering);
  void StartFocusAnimation(bool turning_on);
  void ToggleReadingList();
  void ShowReadingListDialog();
  // Opens an approved Google Translate web flow in a separate tab. Original
  // content stays in place and translation always asks before data leaves MTK.
  void TranslateActivePage(const std::string& target_language);
  void OpenActiveSiteAsWebApp();
  void ShowBrowserMenu();
  void ShowSettingsDialog();
  // Live CPU and memory details from CEF's process/task manager.
  void ShowTaskManager();
  void ShowClearDataDialog();
  // Signed update check. The network work runs on a worker thread and reports
  // back through kUpdateResultMessage.
  void StartUpdateCheck(bool user_initiated);
  void HandleUpdateResult();
  // Borderless full-screen page view. The chrome is hidden, not destroyed.
  void ToggleFullScreen();
  void SetFullScreen(bool enable);
  bool IsWindowMaximized() const;
  void MaximizeWindow();
  void RestoreWindow();
  // Restores and records the normal window rectangle in profile preferences.
  void ApplySavedWindowPlacement();
  void SaveWindowPlacement();
  // Reopens the most recently closed tab, like Ctrl+Shift+T elsewhere.
  bool ReopenClosedTab();
  // Reopens an entry shown in the recent-closed list, zero being the newest.
  bool ReopenClosedTabAt(size_t recent_index);
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
  void ShowTabSearch();
  // Saved bookmarks. History is not shown here; it has its own document.
  void ShowLibraryDialog();
  // Browsable history document. It replaces the history list dialog, so the
  // search, the day grouping and the delete actions all live on one page.
  void OpenHistoryPage();
  void OpenDownloadsPage();
  void OpenSettingsPage();
  void ShowDownloadsDialog();
  // Toolbar popup with the most recent downloads and inline actions.
  void ShowDownloadsPanel();
  void OpenLibraryEntry(size_t index);
  void ScheduleLibrarySave();
  void ScheduleSessionSave();
  void SetReloadHover(bool hovering);
  void SetReloadAnimating(bool animating);
  void SetShieldHover(bool hovering);
  void SetDownloadHover(bool hovering);
  void SetHomeHover(bool hovering);
  void SaveSessionSnapshot();
  void CreateStartupTabs();
  void RestoreAdditionalSessionWindows(const mtk::session::Snapshot& snapshot);
  void ApplyRestoredWindowState(const mtk::session::Window& saved);
  // Opens another top-level window, optionally starting on `url`. A private
  // window gets its own in-memory request context, so its cookies, cache and
  // storage never reach disk and are discarded when it closes.
  void OpenNewWindow(const std::string& url, bool private_session = false);
  bool DetachTabToNewWindow(size_t index);
  // Points the shared DPI and bookmarks-bar metrics at this window. Those are
  // file-level state shared by the painting helpers, so every entry point that
  // can run for a different window has to set them first.
  void ActivateMetrics() const;
  void SelectTab(size_t index);
  void SyncTabVisibility();
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
  bool DiscardTab(size_t index);
  bool ReviveTab(size_t index);
  void CheckMemorySaver();
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
  // Popup listing one folder's bookmarks. Folders are a shallow grouping, so the
  // menu never nests.
  void ShowBookmarkFolderMenu(size_t folder_index, POINT screen);
  // Right-click menu of the bar: folder management, and moving or removing the
  // bookmark that was clicked.
  void ShowBookmarksBarMenu(const BookmarkHit* hit, POINT screen);
  // Popup listing every folder and top-level bookmark that did not fit on the
  // bar.
  void ShowBookmarksOverflowMenu(POINT screen);
  void ToggleBookmarksBar();
  // Returns false when the name was rejected, after telling the user why.
  bool PromptForFolderName(const wchar_t* caption, const wchar_t* message,
                           std::wstring* name);
  void ToggleVerticalTabs();
  void ToggleVerticalTabsCollapsed();
  bool InTabStrip(POINT point) const;
  RECT TabStripRect() const;
  // Imports bookmarks from an installed Chromium-family browser.
  void ImportBookmarksFromBrowser();
  // Starts another process for `profile_id`. Profiles cannot share one process,
  // because the stores and the Chromium cache directory are process-wide.
  void LaunchProfile(const std::wstring& profile_id);
  // Asks for a name, creates the profile and opens it.
  void CreateProfile();
  // Deletes an inactive non-default profile after parent authorization and a
  // direct confirmation. The underlying directory goes to the Recycle Bin.
  void DeleteProfile();
  void RenameProfile();
  void PaintOmniboxFrame(HDC dc);
  void PaintProgress(HDC dc, const RECT& client);
  void PaintSplitDivider(HDC dc);
  enum class CaptionButton {
    kNone,
    kMinimize,
    kMaximize,
    kClose
  };
  int CaptionButtonWidth() const;
  int CaptionButtonHeight() const;
  RECT WindowControlsRect() const;
  RECT MinimizeButtonRect() const;
  RECT MaximizeButtonRect() const;
  RECT CloseButtonRect() const;
  CaptionButton HitTestCaptionButton(POINT pt) const;
  void PaintCaptionButtons(HDC dc, const RECT& client);
  bool IsLightAppearance() const;
  void DrawButton(const DRAWITEMSTRUCT& item);
  void TrackHover(POINT point);
  void ClearHover();
  void UpdateTabTooltip(POINT screen_point);
  void UpdateControls();
  // Sends CEF the real foreground/background state. A pinned tab is the
  // built-in exception and remains eligible to keep running in the background.
  void SetTabMemoryState(Tab& tab, bool hidden);
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
  // A destination created while detaching a live CEF child must not create a
  // temporary home tab that would flash or waste a renderer process.
  bool defer_initial_tab_ = false;
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
  HWND focus_ = nullptr;
  HWND screenshot_ = nullptr;
  HWND split_ = nullptr;
  HWND downloads_ = nullptr;
  HWND menu_ = nullptr;
  HWND tab_tooltip_ = nullptr;
  HFONT ui_font_ = nullptr;
  HFONT tab_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT badge_font_ = nullptr;
  HBRUSH edit_brush_ = nullptr;
  HBRUSH edit_brush_light_ = nullptr;
  std::vector<Tab> tabs_;
  // URLs behind the visible suggestion rows, in the same order.
  std::vector<std::string> suggestion_urls_;
  // Recently closed tabs, newest last, bounded so it cannot grow unchecked.
  std::vector<std::string> closed_tab_urls_;
  // Horizontal scroll offset of the tab strip, in pixels.
  int tab_scroll_ = 0;
  // The second visible tab in side-by-side mode, or kNoTab's value when split
  // view is off.
  size_t split_tab_ = static_cast<size_t>(-1);
  float split_ratio_ = 0.5f;
  int split_focus_pane_ = 0;  // 0 = primary/left, 1 = secondary/right
  bool split_dragging_ = false;
  bool split_hovered_ = false;
  RECT SplitDividerRect() const;
  bool IsOverSplitDivider(POINT pt) const;
  // Tab being dragged, its press position, and whether the dead zone has been
  // left. `drag_candidate_` is set on press; `dragging_` only once it moves.
  size_t drag_candidate_ = static_cast<size_t>(-1);
  POINT drag_origin_{};
  bool dragging_ = false;
  std::vector<BookmarkHit> bookmark_bar_hits_;
  size_t hovered_bookmark_ = static_cast<size_t>(-1);
  std::vector<std::pair<size_t, bool>> bookmark_overflow_entries_;
  std::vector<std::string> folder_menu_urls_;
  std::string context_bookmark_url_;
  std::wstring context_folder_name_;
  std::wstring active_workspace_;
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
  RECT omnibox_star_rect_{};
  bool hovered_omnibox_star_ = false;
  RECT omnibox_install_rect_{};
  bool hovered_omnibox_install_ = false;
  bool mouse_tracking_ = false;
  std::wstring tab_tooltip_text_;
  bool find_visible_ = false;
  bool find_active_ = false;
  int active_find_identifier_ = -1;
  bool progress_active_ = false;
  int progress_phase_ = 0;
  bool closing_ = false;
  // Rendered 3D iridescent circular arrow frames for the toolbar refresh button.
  mtk::icons::Strip reload_strip_;
  bool reload_hovering_ = false;
  bool reload_animating_ = false;
  int reload_frame_ = 0;
  // Rendered shield frames for the toolbar button, plus the hover playback
  // state. An unavailable strip leaves the static glyph in place.
  mtk::icons::Strip shield_strip_;
  bool shield_hovering_ = false;
  int shield_frame_ = 0;
  // Rendered 3D download frames for the toolbar button, with animated hover playback.
  mtk::icons::Strip download_strip_;
  bool download_hovering_ = false;
  int download_frame_ = 0;
  // Rendered 3D home frames for the toolbar button, with smoke billowing on hover.
  mtk::icons::Strip home_strip_;
  bool home_hovering_ = false;
  int home_frame_ = 0;
  // Rendered 3D target frames for the toolbar focus button, with bullseye lock-on.
  mtk::icons::Strip focus_strip_;
  bool focus_hovering_ = false;
  bool focus_animating_ = false;
  bool focus_disengaging_ = false;
  int focus_frame_ = 0;
  // A single update check at a time; the worker owns the pending result until
  // the UI thread collects it.
  bool update_check_running_ = false;
  bool update_check_user_initiated_ = false;
  bool app_mode_ = false;
  bool hovered_rail_toggle_ = false;
  bool shields_update_pending_ = false;
  CaptionButton hovered_caption_button_ = CaptionButton::kNone;
  CaptionButton pressed_caption_button_ = CaptionButton::kNone;
};
