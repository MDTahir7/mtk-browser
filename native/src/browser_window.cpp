#include "browser_window_internal.h"

std::string kHomeUrl = kDefaultHomeUrl;
int g_dpi = 96;
bool g_bookmarks_bar = false;
std::vector<BrowserWindow*> g_windows;
mtk::update::Result g_update_result;

BrowserWindow::BrowserWindow(HINSTANCE instance) : instance_(instance) {
  RefreshHomeUrl();
  RefreshHomeSnapshot();
  g_bookmarks_bar =
      mtk::preferences::Store::Get().values().show_bookmarks_bar;
}

void BrowserWindow::ShowDownloadsPanel() {
  mtk::downloads_panel::Callbacks callbacks;
  // The CEF download callbacks live behind BrowserClient, so they are handed to
  // the panel rather than reached from it.
  callbacks.pause = [](uint64_t id) {
    return id <= UINT32_MAX &&
           BrowserClient::PauseDownload(static_cast<uint32_t>(id));
  };
  callbacks.resume = [](uint64_t id) {
    return id <= UINT32_MAX &&
           BrowserClient::ResumeDownload(static_cast<uint32_t>(id));
  };
  callbacks.cancel = [](uint64_t id) {
    return id <= UINT32_MAX &&
           BrowserClient::CancelDownload(static_cast<uint32_t>(id));
  };
  callbacks.controllable = [](uint64_t id) {
    return id <= UINT32_MAX &&
           BrowserClient::CanControlDownload(static_cast<uint32_t>(id));
  };
  callbacks.show_all = [this]() { ShowDownloadsDialog(); };
  mtk::downloads_panel::TogglePanel(instance_, window_, downloads_,
                                    std::move(callbacks));
}

void BrowserWindow::OnBrowserDownloadsChanged(BrowserClient*) {
  // The download handler runs on this same UI thread, so the open dialog can be
  // refreshed directly.
  if (g_downloads_dialog) PopulateDownloadList(g_downloads_dialog);
  mtk::downloads_panel::Refresh();
  if (downloads_) InvalidateRect(downloads_, nullptr, FALSE);
  RefreshHomeSnapshot();
  if (window_) SetTimer(window_, kDownloadSaveTimer, 1500, nullptr);
}

void BrowserWindow::OnBrowserHomeAction(BrowserClient* client,
                                        const mtk::home::Action& action) {
  const auto* active = ActiveTab();
  if (!active || active->client.get() != client) return;

  switch (action.kind) {
    case mtk::home::ActionKind::kDownloads:
      ShowDownloadsPanel();
      break;
    case mtk::home::ActionKind::kHistory:
      OpenHistoryPage();
      break;
    case mtk::home::ActionKind::kBookmarks:
      ShowLibraryDialog();
      break;
    case mtk::home::ActionKind::kSettings:
      ShowSettingsDialog();
      break;
    case mtk::home::ActionKind::kDownloadOpen: {
      if (const auto* item = mtk::downloads::Manager::Get().FindItem(action.download_id)) {
        OpenDownloadedFile(window_, *item);
      }
      break;
    }
    case mtk::home::ActionKind::kDownloadShow: {
      if (const auto* item = mtk::downloads::Manager::Get().FindItem(action.download_id)) {
        RevealInExplorer(window_, item->full_path);
      }
      break;
    }
    case mtk::home::ActionKind::kNone:
      break;
  }
}

void BrowserWindow::OnBrowserHistoryAction(BrowserClient* client,
                                           const mtk::history::Action& action) {
  auto* active = ActiveTab();
  if (!active || active->client.get() != client) return;
  auto& store = mtk::library::Store::Get();
  bool changed = false;

  switch (action.kind) {
    case mtk::history::ActionKind::kRemoveEntry: {
      const auto& history = store.history();
      // The page was rendered from a copy, so the row is only removed when the
      // stored timestamp still matches. A list that shifted in between cannot
      // make this delete a different page.
      if (action.index >= history.size() ||
          history[action.index].visited != action.visited) {
        break;
      }
      changed = store.RemoveHistoryAt(action.index);
      break;
    }
    case mtk::history::ActionKind::kClearDay: {
      std::vector<size_t> matches;
      const auto& history = store.history();
      for (size_t index = 0; index < history.size(); ++index) {
        if (mtk::history::DayKeyOf(history[index].visited) == action.day) {
          matches.push_back(index);
        }
      }
      if (matches.empty()) break;
      const std::wstring question =
          L"Remove " + std::to_wstring(matches.size()) +
          (matches.size() == 1 ? L" page from this day?" : L" pages from this day?") +
          L"\n\nThis cannot be undone.";
      if (MessageBoxW(window_, question.c_str(), L"Remove history",
                      MB_YESNO | MB_ICONQUESTION) != IDYES) {
        break;
      }
      // Highest index first, so each removal leaves the earlier ones in place.
      for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        if (store.RemoveHistoryAt(*it)) changed = true;
      }
      break;
    }
    case mtk::history::ActionKind::kClearAll: {
      if (store.history().empty()) break;
      const std::wstring question =
          L"Remove all " + std::to_wstring(store.history().size()) +
          L" pages from your history?\n\nThis cannot be undone. Bookmarks and "
          L"saved passwords are not affected.";
      if (MessageBoxW(window_, question.c_str(), L"Remove history",
                      MB_YESNO | MB_ICONWARNING) != IDYES) {
        break;
      }
      changed = store.ClearHistory();
      break;
    }
    case mtk::history::ActionKind::kNone:
      break;
  }

  // The snapshot the page renders from is republished by ScheduleLibrarySave, so
  // reloading afterwards shows the change rather than the stale list.
  if (changed) ScheduleLibrarySave();
  if (active->client->browser()) active->client->browser()->Reload();
}

void BrowserWindow::ScheduleLibrarySave() {
  // History and bookmarks feed the built-in home page, so the served snapshot is
  // rebuilt as soon as either changes.
  RefreshHomeSnapshot();
  SetTimer(window_, kLibrarySaveTimer, 1500, nullptr);
}

void BrowserWindow::ScheduleSessionSave() {
  if (closing_ || !window_ || !owns_session_ || private_session_) return;
  // Snapshots are taken in both startup modes; see SaveSessionSnapshot().
  SetTimer(window_, kSessionSaveTimer, 1200, nullptr);
}



bool BrowserWindow::IsWindowMaximized() const {
  return window_ && IsZoomed(window_) != FALSE;
}

void BrowserWindow::MaximizeWindow() {
  if (window_) ShowWindow(window_, SW_MAXIMIZE);
}

void BrowserWindow::RestoreWindow() {
  if (window_) ShowWindow(window_, SW_RESTORE);
}

void BrowserWindow::ApplySavedWindowPlacement() {
  if (!window_ || private_session_ || app_mode_) return;
  const auto& prefs = mtk::preferences::Store::Get().values();
  if (!mtk::preferences::IsValidWindowBounds(prefs.window_bounds)) return;

  RECT desired{prefs.window_bounds.left, prefs.window_bounds.top,
               prefs.window_bounds.left + prefs.window_bounds.width,
               prefs.window_bounds.top + prefs.window_bounds.height};
  HMONITOR monitor = MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(MONITORINFO)};
  if (GetMonitorInfoW(monitor, &info)) {
    const int max_w = info.rcWork.right - info.rcWork.left;
    const int max_h = info.rcWork.bottom - info.rcWork.top;
    const int w = std::clamp(prefs.window_bounds.width, 480, std::max(480, max_w));
    const int h = std::clamp(prefs.window_bounds.height, 320, std::max(320, max_h));
    const int x = std::clamp<int>(prefs.window_bounds.left, info.rcWork.left,
                                  std::max<int>(info.rcWork.left, info.rcWork.right - w));
    const int y = std::clamp<int>(prefs.window_bounds.top, info.rcWork.top,
                                  std::max<int>(info.rcWork.top, info.rcWork.bottom - h));
    SetWindowPos(window_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void BrowserWindow::SaveWindowPlacement() {
  if (!window_ || full_screen_ || private_session_ || app_mode_) return;
  WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
  if (!GetWindowPlacement(window_, &wp)) return;

  auto& store = mtk::preferences::Store::Get();
  mtk::preferences::Values values = store.values();
  const RECT& normal = wp.rcNormalPosition;
  mtk::preferences::WindowBounds bounds{
      normal.left, normal.top, normal.right - normal.left,
      normal.bottom - normal.top};
  if (mtk::preferences::IsValidWindowBounds(bounds)) {
    values.window_bounds = bounds;
  }
  values.window_maximized = IsZoomed(window_) != FALSE;
  store.Save(values);
}

void BrowserWindow::SaveSessionSnapshot() {
  if (!owns_session_ || private_session_) return;

  std::vector<mtk::session::Window> session_windows;
  for (BrowserWindow* win : g_windows) {
    if (!win || win->private_session_ || win->app_mode_ || win->tabs_.empty()) {
      continue;
    }
    mtk::session::Window saved;
    if (win->window_) {
      WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
      if (GetWindowPlacement(win->window_, &wp)) {
        saved.left = wp.rcNormalPosition.left;
        saved.top = wp.rcNormalPosition.top;
        saved.width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        saved.height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
      }
      saved.maximized = IsZoomed(win->window_) != FALSE;
    }
    saved.active_index = win->active_tab_;
    saved.split_index = win->split_tab_;
    saved.split_ratio = static_cast<int>(std::lround(win->split_ratio_ * 100.0f));
    for (const auto& tab : win->tabs_) {
      saved.urls.push_back(WideToUtf8(tab.url));
      saved.pinned.push_back(tab.pinned);
      saved.groups.push_back(WideToUtf8(tab.group));
      saved.workspaces.push_back(WideToUtf8(tab.workspace));
    }
    session_windows.push_back(std::move(saved));
  }
  mtk::session::SaveWindows(session_windows);
}

void BrowserWindow::ApplyRestoredWindowState(const mtk::session::Window& saved) {
  if (window_ && saved.width >= 480 && saved.height >= 320) {
    RECT desired{saved.left, saved.top, saved.left + saved.width,
                 saved.top + saved.height};
    HMONITOR monitor = MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(monitor, &info)) {
      const int max_w = info.rcWork.right - info.rcWork.left;
      const int max_h = info.rcWork.bottom - info.rcWork.top;
      const int w = std::clamp(saved.width, 480, std::max(480, max_w));
      const int h = std::clamp(saved.height, 320, std::max(320, max_h));
      const int x = std::clamp<int>(saved.left, info.rcWork.left,
                                    std::max<int>(info.rcWork.left, info.rcWork.right - w));
      const int y = std::clamp<int>(saved.top, info.rcWork.top,
                                    std::max<int>(info.rcWork.top, info.rcWork.bottom - h));
      SetWindowPos(window_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (saved.maximized) {
      ShowWindow(window_, SW_MAXIMIZE);
    }
  }

  for (size_t index = 0; index < saved.urls.size(); ++index) {
    if (CreateTab(saved.urls[index])) {
      Tab& tab = tabs_.back();
      tab.pinned = index < saved.pinned.size() && saved.pinned[index];
      if (index < saved.groups.size()) {
        tab.group = Utf8ToWide(saved.groups[index]);
      }
      if (index < saved.workspaces.size()) {
        tab.workspace = Utf8ToWide(saved.workspaces[index]);
      }
    }
  }
  if (!tabs_.empty()) {
    active_tab_ = std::min(saved.active_index, tabs_.size() - 1);
    if (saved.split_index < tabs_.size() && saved.split_index != active_tab_) {
      split_tab_ = saved.split_index;
      split_ratio_ = std::clamp(saved.split_ratio, 20, 80) / 100.0f;
    }
  }
}

void BrowserWindow::RestoreAdditionalSessionWindows(
    const mtk::session::Snapshot& snapshot) {
  for (size_t win_idx = 1; win_idx < snapshot.windows.size(); ++win_idx) {
    const auto& saved_win = snapshot.windows[win_idx];
    if (saved_win.urls.empty()) continue;
    BrowserWindow* extra = Open(instance_,
                                saved_win.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL,
                                {}, false, true, false);
    if (extra) {
      extra->ApplyRestoredWindowState(saved_win);
      extra->Layout();
      extra->SyncTabVisibility();
    }
  }
}

void BrowserWindow::CreateStartupTabs() {
  RefreshHomeSnapshot();
  if (!startup_urls_.empty()) {
    for (const auto& url : startup_urls_) CreateTab(url);
    if (!tabs_.empty()) return;
  }
  if (owns_session_ && mtk::crash::LastExitWasUnclean() &&
      mtk::preferences::Store::Get().values().startup_mode !=
          mtk::preferences::StartupMode::kContinueSession) {
    const auto snapshot = mtk::session::Load();
    if (!snapshot.windows.empty()) {
      const std::wstring question =
          L"MTK Browser did not close normally last time.\n\nRestore the " +
          std::to_wstring(snapshot.urls.size()) +
          (snapshot.urls.size() == 1 ? L" tab that was open?"
                                     : L" tabs that were open?");
      if (MessageBoxW(window_, question.c_str(), L"Restore tabs",
                      MB_YESNO | MB_ICONQUESTION) == IDYES) {
        ApplyRestoredWindowState(snapshot.windows.front());
        RestoreAdditionalSessionWindows(snapshot);
        if (!tabs_.empty()) return;
      }
    }
  }

  if (mtk::preferences::Store::Get().values().startup_mode ==
          mtk::preferences::StartupMode::kContinueSession &&
      owns_session_) {
    const auto snapshot = mtk::session::Load();
    if (!snapshot.windows.empty()) {
      ApplyRestoredWindowState(snapshot.windows.front());
      RestoreAdditionalSessionWindows(snapshot);
      if (!tabs_.empty()) return;
    }
  }
  if (!CreateTab(kHomeUrl)) {
    MessageBoxW(window_, L"MTK Browser could not create its first browser tab.",
                L"MTK Browser startup error", MB_OK | MB_ICONERROR);
    PostMessageW(window_, WM_CLOSE, 0, 0);
  }
}

BrowserWindow::~BrowserWindow() {
  for (HFONT font : {ui_font_, tab_font_, glyph_font_, badge_font_}) {
    if (font) DeleteObject(font);
  }
  if (edit_brush_) DeleteObject(edit_brush_);
  if (edit_brush_light_) DeleteObject(edit_brush_light_);
}

BrowserWindow* BrowserWindow::Open(HINSTANCE instance, int show_command,
                                   const std::vector<std::string>& startup_urls,
                                   bool private_session,
                                   bool defer_initial_tab,
                                   bool app_mode) {
  auto* window = new BrowserWindow(instance);
  window->startup_urls_ = startup_urls;
  window->private_session_ = private_session;
  window->defer_initial_tab_ = defer_initial_tab;
  window->app_mode_ = app_mode;
  if (private_session) {
    // An empty cache_path is what makes the context in-memory: cookies, cache
    // and storage live only as long as this context does.
    CefRequestContextSettings settings;
    window->request_context_ = CefRequestContext::CreateContext(settings, nullptr);
    if (!window->request_context_ ||
        !mtk::home::RegisterSchemeHandler(window->request_context_, true)) {
      delete window;
      return nullptr;
    }
  }
  // The first window owns the session snapshot; later windows do not, because
  // the snapshot format holds one tab list. A private window never owns it.
  window->owns_session_ = g_windows.empty() && !private_session;
  g_windows.push_back(window);
  if (!window->Create(show_command)) {
    g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), window),
                    g_windows.end());
    delete window;
    return nullptr;
  }
  return window;
}

size_t BrowserWindow::WindowCount() { return g_windows.size(); }

void BrowserWindow::ActivateMetrics() const {
  g_dpi = dpi_ <= 0 ? 96 : dpi_;
  g_bookmarks_bar =
      mtk::preferences::Store::Get().values().show_bookmarks_bar;
}

void BrowserWindow::OpenNewWindow(const std::string& url, bool private_session) {
  std::vector<std::string> urls;
  urls.push_back(url.empty() ? kHomeUrl : url);
  if (!Open(instance_, SW_SHOWNORMAL, urls, private_session)) {
    MessageBoxW(window_, L"MTK Browser could not open another window.",
                L"MTK Browser", MB_OK | MB_ICONERROR);
  }
}

bool BrowserWindow::Create(int show_command) {
  // The class is process-wide, so it is registered once no matter how many
  // windows are opened.
  static bool class_registered = false;
  if (!class_registered) {
    WNDCLASSW window_class{};
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = CreateSolidBrush(kWindowColor);
    HMODULE dll_mod = GetModuleHandleW(L"MTKBrowser.dll");
    HINSTANCE res_inst = dll_mod ? dll_mod : instance_;
    window_class.hIcon = LoadIconW(res_inst, MAKEINTRESOURCEW(IDI_MTK_BROWSER));
    class_registered = RegisterClassW(&window_class) != 0 ||
                       GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    if (!class_registered) return false;
  }

  // Extra windows are offset so a new one does not land exactly on its opener.
  const int offset = static_cast<int>(g_windows.size() % 6) * 28;
  // The profile is named in the title when it is not the default one. The
  // single-instance check also uses that marker to tell one profile's windows
  // from another's.
  std::wstring title = private_session_ ? L"Private — MTK Browser" : L"MTK Browser";
  if (!mtk::profile::ActiveIsDefault()) {
    title += L" [" + mtk::profile::ActiveId() + L"]";
  }
  const DWORD window_style = WS_OVERLAPPED | WS_THICKFRAME | WS_SYSMENU |
                             WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
  window_ = CreateWindowExW(0, kWindowClass, title.c_str(),
                            window_style,
                            offset == 0 ? CW_USEDEFAULT : 60 + offset,
                            offset == 0 ? CW_USEDEFAULT : 40 + offset,
                            1360, 860, nullptr, nullptr, instance_, this);
  if (!window_) return false;

  HMODULE dll_mod = GetModuleHandleW(L"MTKBrowser.dll");
  HINSTANCE res_inst = dll_mod ? dll_mod : instance_;
  HICON icon_big = reinterpret_cast<HICON>(
      LoadImageW(res_inst, MAKEINTRESOURCEW(IDI_MTK_BROWSER), IMAGE_ICON,
                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                 LR_DEFAULTCOLOR));
  HICON icon_sm = reinterpret_cast<HICON>(
      LoadImageW(res_inst, MAKEINTRESOURCEW(IDI_MTK_BROWSER), IMAGE_ICON,
                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                 LR_DEFAULTCOLOR));
  if (!icon_big || !icon_sm) {
    wchar_t exe_path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) {
      wchar_t* last_slash = wcsrchr(exe_path, L'\\');
      if (last_slash) {
        *last_slash = L'\0';
        std::wstring ico_file = std::wstring(exe_path) + L"\\mtk_browser_globe.ico";
        if (GetFileAttributesW(ico_file.c_str()) == INVALID_FILE_ATTRIBUTES) {
          ico_file = std::wstring(exe_path) + L"\\mtk_browser.ico";
        }
        if (!icon_big) {
          icon_big = reinterpret_cast<HICON>(
              LoadImageW(nullptr, ico_file.c_str(), IMAGE_ICON,
                         GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                         LR_LOADFROMFILE));
        }
        if (!icon_sm) {
          icon_sm = reinterpret_cast<HICON>(
              LoadImageW(nullptr, ico_file.c_str(), IMAGE_ICON,
                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                         LR_LOADFROMFILE));
        }
      }
    }
  }
  if (icon_big) SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_big));
  if (icon_sm) SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_sm));

  LONG_PTR current_style = GetWindowLongPtrW(window_, GWL_STYLE);
  current_style &= ~WS_CAPTION;
  SetWindowLongPtrW(window_, GWL_STYLE, current_style);

  const MARGINS margins{0, 0, 0, 0};
  DwmExtendFrameIntoClientArea(window_, &margins);
  SetWindowPos(window_, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

  const BOOL dark = TRUE;
  DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
  ApplySavedWindowPlacement();
  const auto& pref_vals = mtk::preferences::Store::Get().values();
  const int effective_show = (!private_session_ && !app_mode_ && mtk::preferences::IsValidWindowBounds(pref_vals.window_bounds))
      ? (pref_vals.window_maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL)
      : show_command;
  ShowWindow(window_, effective_show);
  UpdateWindow(window_);
  // Update checks belong to the process, not to each window, so only the first
  // window schedules them. Otherwise three windows would mean three checks.
  if (owns_session_) {
    // Any installer left over from a previous update is removed before a new
    // check can download another one.
    mtk::update::CleanupDownloads();
    SetTimer(window_, kUpdateStartupTimer, kUpdateStartupDelayMs, nullptr);
    SetTimer(window_, kUpdatePeriodicTimer, kUpdatePeriodMs, nullptr);
  }
  return true;
}

LRESULT CALLBACK BrowserWindow::WindowProc(HWND window, UINT message,
                                            WPARAM w_param, LPARAM l_param) {
  BrowserWindow* self = reinterpret_cast<BrowserWindow*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<BrowserWindow*>(create->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(window, message, w_param, l_param);
  // Metrics are shared file-level state, so they are pointed at this window
  // before any of its messages are handled.
  self->ActivateMetrics();
  if (message == WM_NCDESTROY) {
    // Last message a window receives, so the instance can be released here
    // without any later message reaching freed memory.
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    const LRESULT result = DefWindowProcW(window, message, w_param, l_param);
    delete self;
    return result;
  }
  return self->HandleMessage(message, w_param, l_param);
}

LRESULT CALLBACK BrowserWindow::AddressProc(HWND window, UINT message,
                                             WPARAM w_param, LPARAM l_param,
                                             UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  // Focus moving into the suggestion list is part of picking a row, so the list
  // must survive that; any other focus loss closes it.
  if (message == WM_KILLFOCUS && self &&
      reinterpret_cast<HWND>(w_param) != self->suggestions_) {
    self->HideSuggestions();
  }
  if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && self) {
    if (w_param == VK_RETURN) {
      // A highlighted suggestion wins over the typed text.
      if (!self->AcceptSuggestion()) self->NavigateFromOmnibox();
      return 0;
    }
    if (w_param == VK_DOWN || w_param == VK_UP) {
      self->MoveSuggestion(w_param == VK_DOWN ? 1 : -1);
      return 0;
    }
    if (w_param == VK_ESCAPE) {
      self->HideSuggestions();
      return 0;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (self->HandleShortcut(static_cast<int>(w_param), control, alt, shift)) {
      return 0;
    }
  }
  // Same reason as in FindProc: an unmatched Alt+letter in an edit control
  // would beep, and Alt is where every browser action lives.
  if (message == WM_SYSCHAR) return 0;
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, AddressProc, 1);
  return DefSubclassProc(window, message, w_param, l_param);
}

LRESULT CALLBACK BrowserWindow::ShieldProc(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param,
                                           UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  if (self) {
    if (message == WM_MOUSEMOVE) {
      TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      self->SetShieldHover(true);
    } else if (message == WM_MOUSELEAVE) {
      self->SetShieldHover(false);
    }
  }
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ShieldProc, 3);
  return DefSubclassProc(window, message, w_param, l_param);
}

void BrowserWindow::SetShieldHover(bool hovering) {
  if (shield_hovering_ == hovering) return;
  shield_hovering_ = hovering;
  if (!shield_strip_.valid()) return;
  if (hovering) {
    shield_frame_ = 0;
    SetTimer(window_, kShieldAnimTimer, kShieldFrameMs, nullptr);
  } else {
    KillTimer(window_, kShieldAnimTimer);
    shield_frame_ = 0;
  }
  if (shield_) InvalidateRect(shield_, nullptr, FALSE);
}

LRESULT CALLBACK BrowserWindow::HomeProc(HWND window, UINT message,
                                         WPARAM w_param, LPARAM l_param,
                                         UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  if (self) {
    if (message == WM_MOUSEMOVE) {
      TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      self->SetHomeHover(true);
    } else if (message == WM_MOUSELEAVE) {
      self->SetHomeHover(false);
    }
  }
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, HomeProc, 4);
  return DefSubclassProc(window, message, w_param, l_param);
}

void BrowserWindow::SetHomeHover(bool hovering) {
  if (home_hovering_ == hovering) return;
  home_hovering_ = hovering;
  if (!home_strip_.valid()) return;
  if (hovering) {
    home_frame_ = 1;
    SetTimer(window_, kHomeAnimTimer, kToolbarFrameMs, nullptr);
  } else {
    KillTimer(window_, kHomeAnimTimer);
    home_frame_ = 0;
  }
  if (home_) InvalidateRect(home_, nullptr, FALSE);
}

LRESULT CALLBACK BrowserWindow::ReloadProc(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param,
                                           UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  if (self) {
    if (message == WM_MOUSEMOVE) {
      TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      self->SetReloadHover(true);
    } else if (message == WM_MOUSELEAVE) {
      self->SetReloadHover(false);
    }
  }
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ReloadProc, 5);
  return DefSubclassProc(window, message, w_param, l_param);
}

void BrowserWindow::SetReloadHover(bool hovering) {
  if (reload_hovering_ == hovering) return;
  reload_hovering_ = hovering;
  if (!reload_strip_.valid()) return;
  if (hovering) {
    reload_frame_ = 1;
    SetTimer(window_, kReloadAnimTimer, kToolbarFrameMs, nullptr);
  } else {
    if (!reload_animating_) {
      KillTimer(window_, kReloadAnimTimer);
      reload_frame_ = 0;
    }
  }
  if (reload_) InvalidateRect(reload_, nullptr, FALSE);
}

void BrowserWindow::SetReloadAnimating(bool animating) {
  if (reload_animating_ == animating) return;
  reload_animating_ = animating;
  if (!reload_strip_.valid()) return;
  if (animating) {
    SetTimer(window_, kReloadAnimTimer, kToolbarFrameMs, nullptr);
  } else if (!reload_hovering_) {
    KillTimer(window_, kReloadAnimTimer);
    reload_frame_ = 0;
  }
  if (reload_) InvalidateRect(reload_, nullptr, FALSE);
}

LRESULT CALLBACK BrowserWindow::DownloadsProc(HWND window, UINT message,
                                              WPARAM w_param, LPARAM l_param,
                                              UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  if (self) {
    if (message == WM_MOUSEMOVE) {
      TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      self->SetDownloadHover(true);
    } else if (message == WM_MOUSELEAVE) {
      self->SetDownloadHover(false);
    }
  }
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, DownloadsProc, 6);
  return DefSubclassProc(window, message, w_param, l_param);
}

void BrowserWindow::SetDownloadHover(bool hovering) {
  if (download_hovering_ == hovering) return;
  download_hovering_ = hovering;
  if (!download_strip_.valid()) return;
  if (hovering) {
    download_frame_ = 1;
    SetTimer(window_, kDownloadAnimTimer, kToolbarFrameMs, nullptr);
  } else {
    KillTimer(window_, kDownloadAnimTimer);
    download_frame_ = 0;
  }
  if (downloads_) InvalidateRect(downloads_, nullptr, FALSE);
}

LRESULT CALLBACK BrowserWindow::FindProc(HWND window, UINT message,
                                         WPARAM w_param, LPARAM l_param,
                                         UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && self) {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (w_param == VK_RETURN) {
      self->RunFind(!shift, true);
      return 0;
    }
    if (w_param == VK_ESCAPE) {
      self->HideFindBar();
      return 0;
    }
    if (self->HandleShortcut(static_cast<int>(w_param), control, alt, shift)) {
      return 0;
    }
  }
  // An edit control answers an unmatched Alt+letter with a beep, and every
  // browser action lives on Alt, so it is swallowed here.
  if (message == WM_SYSCHAR) return 0;
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, FindProc, 2);
  return DefSubclassProc(window, message, w_param, l_param);
}

int BrowserWindow::PageTop() const { return full_screen_ ? 0 : ChromeHeight(); }


void BrowserWindow::ToggleFullScreen() {
  if (!window_) return;
  if (!full_screen_) {
    saved_placement_ = {};
    saved_placement_.length = sizeof(saved_placement_);
    if (!GetWindowPlacement(window_, &saved_placement_)) return;
    HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    saved_style_ = GetWindowLongPtrW(window_, GWL_STYLE);
    full_screen_ = true;
    // The chrome is only hidden, so every tab, download and find state stays
    // exactly as it was when full screen is left again.
    for (HWND control : {back_, forward_, reload_, home_, shield_, downloads_,
                         menu_, address_, find_, find_status_, find_previous_,
                         find_next_, find_close_, suggestions_}) {
      if (control) ShowWindow(control, SW_HIDE);
    }
    SetWindowLongPtrW(window_, GWL_STYLE,
                      (saved_style_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
                          static_cast<LONG_PTR>(WS_POPUP));
    SetWindowPos(window_, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left,
                 info.rcMonitor.bottom - info.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
  } else {
    full_screen_ = false;
    SetWindowLongPtrW(window_, GWL_STYLE, saved_style_);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER);
    SetWindowPlacement(window_, &saved_placement_);
    for (HWND control : {back_, forward_, reload_, home_, shield_, downloads_,
                         menu_, address_}) {
      if (control) ShowWindow(control, SW_SHOW);
    }
    if (find_visible_) {
      ShowWindow(address_, SW_HIDE);
      for (HWND control : {find_, find_status_, find_previous_, find_next_,
                           find_close_}) {
        if (control) ShowWindow(control, SW_SHOW);
      }
    }
  }
  Layout();
  InvalidateRect(window_, nullptr, TRUE);
}


bool BrowserWindow::HandleShortcut(int virtual_key, bool control, bool alt,
                                   bool shift) {
  auto* tab = ActiveTab();

  if (!control && !alt && virtual_key == VK_ESCAPE) {
    if (full_screen_) {
      ToggleFullScreen();
      return true;
    }
    if (find_visible_) {
      HideFindBar();
      return true;
    }
    return false;
  }

  // Everything below is Alt-only. A Ctrl+Alt combination is left to the page,
  // because AltGr on many keyboard layouts arrives as Ctrl+Alt.
  if (!alt || control) return false;

  switch (virtual_key) {
    // Navigation
    case VK_OEM_4:  // Alt+[
      if (tab && tab->client->browser() && tab->can_go_back) {
        tab->client->browser()->GoBack();
      }
      return true;
    case VK_OEM_6:  // Alt+]
      if (tab && tab->client->browser() && tab->can_go_forward) {
        tab->client->browser()->GoForward();
      }
      return true;
    case 'R':
      if (tab && tab->client->browser()) {
        if (tab->loading) tab->client->browser()->StopLoad();
        else tab->client->browser()->Reload();
      }
      return true;

    // Tabs
    case 'T':
      if (shift) ReopenClosedTab();
      else CreateTab(kHomeUrl);
      return true;
    case 'X':
      if (!tabs_.empty()) CloseTab(active_tab_);
      return true;
    case 'U':
      if (!tabs_.empty()) DuplicateTab(active_tab_);
      return true;
    case 'M':
      if (!tabs_.empty()) ToggleMute(active_tab_);
      return true;
    case 'P':
      if (shift) {
        if (!tabs_.empty()) TogglePin(active_tab_);
      } else {
        PrintActiveTab();
      }
      return true;
    case VK_OEM_PERIOD:  // Alt+. next tab
      if (!tabs_.empty()) SelectTab((active_tab_ + 1) % tabs_.size());
      return true;
    case VK_OEM_COMMA:  // Alt+, previous tab
      if (!tabs_.empty()) {
        SelectTab((active_tab_ + tabs_.size() - 1) % tabs_.size());
      }
      return true;

    // Windows
    case 'W':
      OpenNewWindow({}, shift);
      return true;

    // Page
    case 'F':
      ShowFindBar();
      return true;
    case 'G':
      if (find_visible_) RunFind(!shift, true);
      else ShowFindBar();
      return true;
    case VK_RETURN:
      ToggleFullScreen();
      return true;
    case VK_OEM_PLUS:
    case VK_ADD:
      ZoomActiveTab(CEF_ZOOM_COMMAND_IN);
      return true;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
      ZoomActiveTab(CEF_ZOOM_COMMAND_OUT);
      return true;
    case '0':
      ZoomActiveTab(CEF_ZOOM_COMMAND_RESET);
      return true;

    // Omnibox and library
    case 'A':
      SetFocus(address_);
      SendMessageW(address_, EM_SETSEL, 0, -1);
      return true;
    case 'B':
      if (shift) ToggleBookmarksBar();
      else ToggleBookmark();
      return true;
    case 'O':
      ShowLibraryDialog();
      return true;
    case 'H':
      OpenHistoryPage();
      return true;
    case 'J':
      ShowDownloadsDialog();
      return true;
    case 'S':
      ShowSiteSettingsDialog();
      return true;
    case 'K':
      ShowPasswordsDialog();
      return true;
    case 'Y':
      ShowUserFiltersDialog();
      return true;
    case VK_OEM_2:  // Alt+/
      ShowShortcutsPage();
      return true;
    default:
      break;
  }

  // Alt+1..8 select that tab, Alt+9 the last one.
  if (!shift && virtual_key >= '1' && virtual_key <= '9' && !tabs_.empty()) {
    if (virtual_key == '9') {
      SelectTab(tabs_.size() - 1);
    } else {
      const size_t index = static_cast<size_t>(virtual_key - '1');
      if (index < tabs_.size()) SelectTab(index);
    }
    return true;
  }
  return false;
}

void BrowserWindow::CreateFonts() {
  const auto make_font = [](int height, int weight, const wchar_t* face,
                            DWORD quality = CLEARTYPE_QUALITY) {
    return CreateFontW(Scaled(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       quality, DEFAULT_PITCH, face);
  };
  for (HFONT* font : {&ui_font_, &tab_font_, &glyph_font_, &badge_font_}) {
    if (*font) {
      DeleteObject(*font);
      *font = nullptr;
    }
  }
  ui_font_ = make_font(-15, FW_NORMAL, L"Segoe UI", CLEARTYPE_QUALITY);
  tab_font_ = make_font(-14, FW_SEMIBOLD, L"Segoe UI", CLEARTYPE_QUALITY);
  glyph_font_ = make_font(-18, FW_NORMAL, L"Segoe UI Symbol", CLEARTYPE_QUALITY);
  badge_font_ = make_font(-11, FW_BOLD, L"Segoe UI", CLEARTYPE_QUALITY);
}

void BrowserWindow::ApplyDpi(UINT dpi) {
  dpi_ = static_cast<int>(dpi == 0 ? 96 : dpi);
  g_dpi = dpi_;
  CreateFonts();
  for (HWND control : {back_, forward_, reload_, home_, shield_, downloads_,
                       menu_, address_, suggestions_,
                       find_, find_status_, find_previous_, find_next_,
                       find_close_}) {
    if (control) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    }
  }
  if (address_) {
    SendMessageW(address_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(Scaled(6), Scaled(10)));
  }
  Layout();
  InvalidateRect(window_, nullptr, TRUE);
}

void BrowserWindow::CreateControls() {
  dpi_ = static_cast<int>(GetDpiForWindow(window_));
  if (dpi_ <= 0) dpi_ = 96;
  g_dpi = dpi_;
  CreateFonts();
  edit_brush_ = CreateSolidBrush(kControlColor);
  edit_brush_light_ = CreateSolidBrush(RGB(255, 255, 255));
  const DWORD button_style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
  // Button text is the accessible name, not the drawn glyph: an owner-draw
  // button is announced by its window text, and "←" or "⋮" tells a screen
  // reader user nothing. ButtonGlyph() supplies what is painted instead.
  using Text = mtk::strings::Id;
  back_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kBack), button_style,
                        0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kBackId), instance_, nullptr);
  forward_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kForward),
                           button_style, 0, 0, 0, 0, window_,
                           reinterpret_cast<HMENU>(kForwardId), instance_, nullptr);
  reload_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kReload),
                          button_style, 0, 0, 0, 0, window_,
                          reinterpret_cast<HMENU>(kReloadId), instance_, nullptr);
  home_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kHomePage),
                        button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kHomeId), instance_, nullptr);
  shield_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kShields),
                          button_style, 0, 0, 0, 0, window_,
                          reinterpret_cast<HMENU>(kShieldId), instance_, nullptr);
  downloads_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kDownloads),
                             button_style, 0, 0, 0, 0, window_,
                             reinterpret_cast<HMENU>(kDownloadsId), instance_,
                             nullptr);
  menu_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kBrowserMenu),
                        button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kMenuId), instance_, nullptr);
  address_ = CreateWindowW(L"EDIT", mtk::strings::Get(Text::kAddressPlaceholder),
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           0, 0, 0, 0, window_,
                           reinterpret_cast<HMENU>(kAddressId), instance_, nullptr);
  // Autocomplete list. It is a child of the toolbar window and is only shown
  // while the omnibox has matches, so it never sits over the page otherwise.
  suggestions_ = CreateWindowW(
      L"LISTBOX", L"",
      WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
      0, 0, 0, 0, window_, reinterpret_cast<HMENU>(kSuggestionsId), instance_,
      nullptr);
  find_ = CreateWindowW(L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                        0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kFindId), instance_, nullptr);
  find_status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_RIGHT,
                               0, 0, 0, 0, window_, nullptr, instance_, nullptr);
  find_previous_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kFindPrevious),
                                 button_style & ~WS_VISIBLE, 0, 0, 0, 0, window_,
                                 reinterpret_cast<HMENU>(kFindPreviousId),
                                 instance_, nullptr);
  find_next_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kFindNext),
                             button_style & ~WS_VISIBLE, 0, 0, 0, 0, window_,
                             reinterpret_cast<HMENU>(kFindNextId), instance_,
                             nullptr);
  find_close_ = CreateWindowW(L"BUTTON", mtk::strings::Get(Text::kCloseFindBar),
                              button_style & ~WS_VISIBLE, 0, 0, 0, 0, window_,
                              reinterpret_cast<HMENU>(kFindCloseId), instance_,
                              nullptr);
  for (HWND control : {back_, forward_, reload_, home_, shield_, downloads_,
                       menu_, address_, suggestions_,
                       find_, find_status_, find_previous_, find_next_, find_close_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
  }
  SetWindowTheme(suggestions_, L"Explorer", nullptr);
  SendMessageW(address_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELPARAM(Scaled(6), Scaled(10)));
  if (!IsLightAppearance()) {
    SetWindowTheme(address_, L"DarkMode_CFD", nullptr);
    SetWindowTheme(find_, L"DarkMode_CFD", nullptr);
  }
  SetWindowSubclass(address_, AddressProc, 1,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(find_, FindProc, 2,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(shield_, ShieldProc, 3,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(home_, HomeProc, 4,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(reload_, ReloadProc, 5,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(downloads_, DownloadsProc, 6,
                    reinterpret_cast<DWORD_PTR>(this));
  // Load animated / 3D strips for toolbar buttons
  shield_strip_.Load(kShieldStripFile);
  home_strip_.Load(L"mtk_home_hover.png");
  reload_strip_.Load(L"mtk_refresh_strip.png");
  download_strip_.Load(L"mtk_download_hover.png");
  focus_strip_.Load(L"mtk_focus_strip.png");
}

bool BrowserWindow::CreateTab(const std::string& initial_url,
                              bool internal_page) {
  if (!tabs_.empty()) {
    if (auto* active = ActiveTab(); active && active->client->browser()) {
      ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_HIDE);
    }
  }
  Tab tab;
  tab.client = new BrowserClient(this);
  // One of the browser's own pages has to be announced before the browser is
  // created, because the policy check runs on that first navigation.
  if (internal_page) tab.client->ExpectInternalPage();
  // Keep the URL snapshot-safe before CEF delivers its asynchronous address
  // callback. Session restore can then survive an immediate window close.
  tab.url = Utf8ToWide(initial_url);
  tabs_.push_back(tab);
  active_tab_ = tabs_.size() - 1;

  RECT client{};
  GetClientRect(window_, &client);
  CefWindowInfo info;
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  info.SetAsChild(
      window_, CefRect(0, PageTop(), static_cast<int>(client.right),
                       std::max(1, static_cast<int>(client.bottom) -
                                       PageTop())));
  CefBrowserSettings settings;
  // A private window passes its own in-memory context; an ordinary window
  // passes nullptr and gets the shared on-disk one.
  if (!CefBrowserHost::CreateBrowser(info, tabs_.back().client, initial_url,
                                     settings, nullptr, request_context_)) {
    tabs_.pop_back();
    active_tab_ = tabs_.empty() ? 0 : tabs_.size() - 1;
    if (auto* active = ActiveTab(); active && active->client->browser()) {
      ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
    }
    return false;
  }
  // A tab created while the strip is already full must be scrolled into view.
  EnsureActiveTabVisible();
  InvalidateRect(window_, nullptr, FALSE);
  return true;
}

void BrowserWindow::ShowFindBar() {
  if (!find_visible_) {
    find_visible_ = true;
    find_active_ = false;
    active_find_identifier_ = -1;
    SetWindowTextW(find_status_, L"Type to find");
    ShowWindow(address_, SW_HIDE);
    for (HWND control : {find_, find_status_, find_previous_, find_next_, find_close_}) {
      ShowWindow(control, SW_SHOW);
    }
    Layout();
  }
  SetFocus(find_);
  SendMessageW(find_, EM_SETSEL, 0, -1);
}

void BrowserWindow::HideFindBar() {
  if (!find_visible_) return;
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    tab->client->browser()->GetHost()->StopFinding(true);
  }
  find_visible_ = false;
  find_active_ = false;
  active_find_identifier_ = -1;
  SetWindowTextW(find_, L"");
  for (HWND control : {find_, find_status_, find_previous_, find_next_, find_close_}) {
    ShowWindow(control, SW_HIDE);
  }
  ShowWindow(address_, SW_SHOW);
  Layout();
}

void BrowserWindow::RunFind(bool forward, bool find_next) {
  if (!find_visible_) return;
  auto* tab = ActiveTab();
  if (!tab || !tab->client->browser()) return;
  const int length = GetWindowTextLengthW(find_);
  std::wstring query(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(find_, query.data(), length + 1);
  query.resize(static_cast<size_t>(length));
  if (query.empty()) {
    tab->client->browser()->GetHost()->StopFinding(true);
    find_active_ = false;
    active_find_identifier_ = -1;
    SetWindowTextW(find_status_, L"Type to find");
    return;
  }
  const bool repeat = find_next && find_active_;
  if (!repeat) active_find_identifier_ = -1;
  find_active_ = true;
  SetWindowTextW(find_status_, L"Searching…");
  tab->client->browser()->GetHost()->Find(query, forward, false, repeat);
}

void BrowserWindow::ShowShortcutsPage() {
  // Rows are {keys, action}. Keeping them in one table means the page and this
  // list cannot drift apart the way a hand-written page would.
  static constexpr std::pair<const char*, const char*> kRows[] = {
      {"Alt+[", "Back"},
      {"Alt+]", "Forward"},
      {"Alt+R", "Reload, or stop while loading"},
      {"Alt+T", "New tab"},
      {"Alt+Shift+T", "Reopen the tab you just closed"},
      {"Alt+X", "Close tab"},
      {"Alt+U", "Duplicate tab"},
      {"Alt+, / Alt+.", "Previous / next tab"},
      {"Alt+1 ... Alt+8", "Go to that tab"},
      {"Alt+9", "Go to the last tab"},
      {"Alt+Shift+P", "Pin or unpin tab"},
      {"Alt+M", "Mute or unmute tab"},
      {"Alt+W", "New window"},
      {"Alt+Shift+W", "New private window"},
      {"Alt+A", "Select the address bar"},
      {"Alt+F", "Find on page"},
      {"Alt+G / Alt+Shift+G", "Next / previous match"},
      {"Alt+P", "Print"},
      {"Alt+Enter", "Full screen"},
      {"Alt+= / Alt+-", "Zoom in / out"},
      {"Alt+0", "Reset zoom"},
      {"Alt+B", "Save this page as a bookmark"},
      {"Alt+Shift+B", "Show or hide the bookmarks bar"},
      {"Alt+O", "Bookmarks"},
      {"Alt+H", "History"},
      {"Alt+J", "Downloads"},
      {"Alt+K", "Saved passwords"},
      {"Alt+S", "Settings for this site"},
      {"Alt+Y", "Your filter rules"},
      {"Alt+/", "This page"},
      {"Esc", "Leave full screen, or close the find bar"},
  };

  std::string rows;
  for (const auto& [keys, action] : kRows) {
    rows += "<tr><td class=k>";
    rows += keys;
    rows += "</td><td>";
    rows += action;
    rows += "</td></tr>";
  }

  const std::string html =
      "<!doctype html><html lang=en><head><meta charset=utf-8>"
      "<title>Keyboard shortcuts</title><style>"
      ":root{color-scheme:dark}"
      "body{margin:0;background:#090c16;color:#f0f4ff;"
      "font:15px/1.5 'Segoe UI Variable','Segoe UI',system-ui,sans-serif;"
      "display:flex;justify-content:center;padding:48px 20px}"
      "main{width:100%;max-width:720px}"
      "h1{font-size:24px;margin:0 0 6px}"
      "p{color:#96a2be;margin:0 0 28px}"
      "table{width:100%;border-collapse:collapse}"
      "td{padding:9px 12px;border-bottom:1px solid #1b2136;vertical-align:top}"
      "td.k{width:210px;color:#cdb8ff;font-family:Consolas,monospace;"
      "white-space:nowrap}"
      "</style></head><body><main>"
      "<h1>Keyboard shortcuts</h1>"
      "<p>MTK Browser puts its own actions on the Alt key, so they never "
      "collide with what a web page or Windows already uses. Copy, paste, "
      "select all and undo keep their usual keys.</p>"
      "<table>" + rows + "</table></main></body></html>";

  // A data: URL ends at the first '#', and a bare '%' would start an escape, so
  // only those two characters need encoding; the rest of the markup is plain
  // ASCII written here.
  std::string escaped;
  escaped.reserve(html.size() + 16);
  for (const char character : html) {
    if (character == '%') escaped += "%25";
    else if (character == '#') escaped += "%23";
    else escaped.push_back(character);
  }
  CreateTab("data:text/html;charset=utf-8," + escaped, true);
}

void BrowserWindow::PrintActiveTab() {
  // Chromium's own print preview is used, which is also where "Save as PDF"
  // lives, so there is no separate export path to keep in sync.
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    tab->client->browser()->GetHost()->Print();
  }
}

void BrowserWindow::ZoomActiveTab(cef_zoom_command_t command) {
  auto* tab = ActiveTab();
  if (!tab || !tab->client->browser()) return;
  const std::string origin = mtk::sites::OriginOf(WideToUtf8(tab->url));

  // Zoom moves along Chromium's own ladder of steps, so the levels match what
  // other browsers offer, and the result is stored per site rather than only
  // applied to this tab.
  static constexpr int kSteps[] = {25,  33,  50,  67,  75,  80,  90, 100,
                                   110, 125, 150, 175, 200, 250, 300, 400, 500};
  const int current =
      origin.empty()
          ? mtk::sites::ZoomPercentForLevel(
                tab->client->browser()->GetHost()->GetZoomLevel())
          : mtk::sites::Store::Get().For(origin).zoom_percent;

  int target = mtk::sites::kDefaultZoomPercent;
  if (command != CEF_ZOOM_COMMAND_RESET) {
    const bool zoom_in = command == CEF_ZOOM_COMMAND_IN;
    target = current;
    if (zoom_in) {
      for (const int step : kSteps) {
        if (step > current) {
          target = step;
          break;
        }
      }
    } else {
      for (int index = static_cast<int>(std::size(kSteps)) - 1; index >= 0;
           --index) {
        if (kSteps[index] < current) {
          target = kSteps[index];
          break;
        }
      }
    }
  }

  tab->client->browser()->GetHost()->SetZoomLevel(
      mtk::sites::ZoomLevelForPercent(target));

  if (origin.empty()) return;
  auto& store = mtk::sites::Store::Get();
  mtk::sites::Rule rule = store.For(origin);
  rule.zoom_percent = target;
  if (store.Set(rule)) store.Flush();
}

void BrowserWindow::ApplySiteZoom(Tab& tab) {
  if (!tab.client || !tab.client->browser()) return;
  const std::string origin = mtk::sites::OriginOf(WideToUtf8(tab.url));
  const int percent = origin.empty()
                          ? mtk::sites::kDefaultZoomPercent
                          : mtk::sites::Store::Get().For(origin).zoom_percent;
  tab.client->browser()->GetHost()->SetZoomLevel(
      mtk::sites::ZoomLevelForPercent(percent));
}


void BrowserWindow::Layout() {
  RECT client{};
  GetClientRect(window_, &client);
  // In full screen the chrome is hidden, so only the page is laid out.
  if (full_screen_) {
    for (auto& tab : tabs_) ResizeBrowser(tab);
    return;
  }
  const int width = client.right;
  const int button = Scaled(38);
  const int gap = Scaled(2);
  const int edge = Scaled(10);
  const int y = TabBarHeight() +
                (ToolbarHeight() - ProgressHeight() - button) / 2;
  int x = edge;
  for (HWND control : {back_, forward_, reload_, home_}) {
    MoveWindow(control, x, y, button, button, TRUE);
    x += button + gap;
  }
  MoveWindow(shield_, x + Scaled(4), y, button + Scaled(8), button, TRUE);
  x += button + Scaled(20);

  // Trailing toolbar actions, laid out from the right edge.
  const int menu_x = width - edge - button;
  const int downloads_x = menu_x - button - gap;
  MoveWindow(downloads_, downloads_x, y, button, button, TRUE);
  MoveWindow(menu_, menu_x, y, button, button, TRUE);

  // The editable field sits inside the painted omnibox pill.
  const int field_height = Scaled(22);
  const int field_y = y + (button - field_height) / 2;
  const int field_inset = Scaled(30);
  if (find_visible_) {
    const int find_close_x = downloads_x - button - Scaled(8);
    const int find_next_x = find_close_x - button - gap;
    const int find_previous_x = find_next_x - button - gap;
    const int status_width = Scaled(84);
    const int status_x = find_previous_x - status_width - Scaled(10);
    MoveWindow(find_, x + field_inset, field_y,
               std::max(Scaled(120), status_x - x - field_inset - Scaled(10)),
               field_height, TRUE);
    MoveWindow(find_status_, status_x, field_y, status_width, field_height, TRUE);
    MoveWindow(find_previous_, find_previous_x, y, button, button, TRUE);
    MoveWindow(find_next_, find_next_x, y, button, button, TRUE);
    MoveWindow(find_close_, find_close_x, y, button, button, TRUE);
  } else {
    // No Go button: pressing Enter navigates, like every mainstream browser.
    const int omnibox_right = downloads_x - Scaled(12);
    const int omnibox_width =
        std::max(Scaled(140), omnibox_right - x - field_inset);
    MoveWindow(address_, x + field_inset, field_y, omnibox_width, field_height,
               TRUE);
  }
  LayoutSuggestions();
  for (auto& tab : tabs_) ResizeBrowser(tab);
}

void BrowserWindow::ResizeBrowser(Tab& tab) {
  if (!tab.client || !tab.client->browser()) return;
  RECT client{};
  GetClientRect(window_, &client);
  HWND browser = tab.client->browser()->GetHost()->GetWindowHandle();
  MoveWindow(browser, 0, PageTop(), static_cast<int>(client.right),
             std::max(1, static_cast<int>(client.bottom) - PageTop()),
             TRUE);
  tab.client->browser()->GetHost()->WasResized();
}

void BrowserWindow::UpdateControls() {
  auto* tab = ActiveTab();
  const bool ready = tab && tab->client && tab->client->browser();
  EnableWindow(back_, ready && tab->can_go_back);
  EnableWindow(forward_, ready && tab->can_go_forward);
  SetWindowTextW(reload_, tab && tab->loading ? L"\u00D7" : L"\u21BB");

  const bool loading = tab && tab->loading;
  if (loading != progress_active_) {
    progress_active_ = loading;
    if (loading) {
      progress_phase_ = 0;
      SetTimer(window_, kProgressTimer, 40, nullptr);
    } else {
      KillTimer(window_, kProgressTimer);
    }
  }

  if (tab && GetFocus() != address_ && !tab->url.starts_with(L"data:")) {
    // Writing the committed address must not look like the user typing.
    suppress_suggestions_ = true;
    SetWindowTextW(address_, tab->url.c_str());
    suppress_suggestions_ = false;
  }
  const std::wstring title = tab && !tab->title.empty() ? tab->title : L"MTK Browser";
  SetWindowTextW(window_, (title + L" — MTK Browser").c_str());
  InvalidateRect(window_, nullptr, FALSE);
}

LRESULT BrowserWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(l_param);
      HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
      MONITORINFO info{sizeof(info)};
      if (GetMonitorInfoW(monitor, &info)) {
        if (!full_screen_) {
          mmi->ptMaxPosition.x = info.rcWork.left - info.rcMonitor.left;
          mmi->ptMaxPosition.y = info.rcWork.top - info.rcMonitor.top;
          mmi->ptMaxSize.x = info.rcWork.right - info.rcWork.left;
          mmi->ptMaxSize.y = info.rcWork.bottom - info.rcWork.top;
          mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x;
          mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y;
        } else {
          mmi->ptMaxPosition.x = 0;
          mmi->ptMaxPosition.y = 0;
          mmi->ptMaxSize.x = info.rcMonitor.right - info.rcMonitor.left;
          mmi->ptMaxSize.y = info.rcMonitor.bottom - info.rcMonitor.top;
          mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x;
          mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y;
        }
      }
      return 0;
    }
    case WM_NCCALCSIZE: {
      if (w_param == TRUE) {
        if (IsZoomed(window_)) {
          auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param);
          HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
          MONITORINFO info{sizeof(info)};
          if (GetMonitorInfoW(monitor, &info)) {
            params->rgrc[0] = full_screen_ ? info.rcMonitor : info.rcWork;
          }
        }
        return 0;
      }
      return 0;
    }
    case WM_NCHITTEST: {
      if (full_screen_) return HTCLIENT;

      const POINT screen_pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      POINT pt = screen_pt;
      ScreenToClient(window_, &pt);

      // Caption buttons area (Minimize, Maximize, Close) has highest priority,
      // ensuring clicks at the window top-right corner hit Close rather than resize.
      if (HitTestCaptionButton(pt) != CaptionButton::kNone) {
        return HTCLIENT;
      }

      RECT win_rect{};
      GetWindowRect(window_, &win_rect);

      // Resizing edges when window is not maximized
      if (!IsZoomed(window_)) {
        const int border = Scaled(6);
        const bool top = screen_pt.y >= win_rect.top && screen_pt.y < win_rect.top + border;
        const bool bottom = screen_pt.y < win_rect.bottom && screen_pt.y >= win_rect.bottom - border;
        const bool left = screen_pt.x >= win_rect.left && screen_pt.x < win_rect.left + border;
        const bool right = screen_pt.x < win_rect.right && screen_pt.x >= win_rect.right - border;

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
      }

      RECT client{};
      GetClientRect(window_, &client);

      // Tab strip area
      if (!full_screen_ && pt.y >= 0 && pt.y < TabBarHeight()) {
        const size_t pinned_count = PinnedCount();
        const bool is_maximized = IsZoomed(window_);
        for (size_t i = 0; i < tabs_.size(); ++i) {
          RECT tab_r = TabRect(i, client.right, tabs_.size(), pinned_count, tab_scroll_, is_maximized);
          if (PtInRect(&tab_r, pt)) {
            return HTCLIENT;
          }
        }
        RECT plus_r = NewTabRect(client.right, tabs_.size(), pinned_count, tab_scroll_, is_maximized);
        InflateRect(&plus_r, Scaled(3), Scaled(8));
        if (PtInRect(&plus_r, pt)) {
          return HTCLIENT;
        }
        // Empty tab strip space acts as caption (window dragging, Aero Snap, double click)
        return HTCAPTION;
      }

      return HTCLIENT;
    }
    case WM_CREATE:
      CreateControls();
      if (!defer_initial_tab_) {
        CreateStartupTabs();
      }
      Layout();
      return 0;
    case WM_SIZE:
      // The Shields panel is positioned against the shield button, so it would
      // float detached once the chrome is laid out again.
      mtk::shields::ClosePanel();
      Layout();
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_MOVE:
      // Same reason as WM_SIZE: the popup is anchored in screen coordinates.
      mtk::shields::ClosePanel();
      break;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window_, &paint);
      RECT client{};
      GetClientRect(window_, &client);
      // Full screen shows only the page, so there is no chrome to draw.
      if (full_screen_) {
        EndPaint(window_, &paint);
        return 0;
      }
      // Draw the chrome off-screen first so gradients never flicker.
      HDC buffer = CreateCompatibleDC(dc);
      HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, ChromeHeight());
      HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap));
      Paint(buffer);
      BitBlt(dc, 0, 0, client.right, ChromeHeight(), buffer, 0, 0, SRCCOPY);
      SelectObject(buffer, old_bitmap);
      DeleteObject(bitmap);
      DeleteDC(buffer);
      EndPaint(window_, &paint);
      return 0;
    }
    case WM_PRINTCLIENT: {
      HDC dc = reinterpret_cast<HDC>(w_param);
      if (!full_screen_) {
        Paint(dc);
      }
      return 0;
    }
    case WM_SYSCHAR:
      // Every browser action is on the Alt layer and this window has no menu
      // bar, so Alt+letter must not fall through to the default menu handling,
      // which answers an unmatched mnemonic with a beep.
      return 0;
    case WM_MOUSEMOVE: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (drag_candidate_ != kNoTab && (w_param & MK_LBUTTON)) {
        UpdateTabDrag(point);
        return 0;
      }
      if (bookmark_drag_index_ != static_cast<size_t>(-1) && (w_param & MK_LBUTTON)) {
        UpdateBookmarkDrag(point);
        return 0;
      }
      TrackHover(point);
      return 0;
    }
    case WM_MOUSELEAVE:
      ClearHover();
      return 0;
    case WM_MOUSEWHEEL: {
      // Wheel over the tab strip scrolls it when the tabs no longer fit.
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      ScreenToClient(window_, &point);
      if (!full_screen_ && point.y >= 0 && point.y < TabBarHeight()) {
        const int notches = GET_WHEEL_DELTA_WPARAM(w_param) / WHEEL_DELTA;
        ScrollTabs(-notches * Scaled(120));
        TrackHover(point);
        return 0;
      }
      break;
    }
    case WM_TIMER:
      if (w_param == kProgressTimer) {
        ++progress_phase_;
        RECT client{};
        GetClientRect(window_, &client);
        RECT track{0, ChromeHeight() - ProgressHeight(), client.right,
                   ChromeHeight()};
        InvalidateRect(window_, &track, FALSE);
      } else if (w_param == kLibrarySaveTimer) {
        KillTimer(window_, kLibrarySaveTimer);
        mtk::library::Store::Get().Flush();
        RefreshHomeSnapshot();
      } else if (w_param == kDownloadSaveTimer) {
        KillTimer(window_, kDownloadSaveTimer);
        mtk::downloads::Manager::Get().Flush();
        RefreshHomeSnapshot();
      } else if (w_param == kUpdateStartupTimer) {
        KillTimer(window_, kUpdateStartupTimer);
        StartUpdateCheck(false);
      } else if (w_param == kUpdatePeriodicTimer) {
        StartUpdateCheck(false);
      } else if (w_param == kShieldAnimTimer) {
        if (shield_hovering_ && shield_strip_.valid() &&
            shield_frame_ + 1 < shield_strip_.frame_count()) {
          ++shield_frame_;
          if (shield_) InvalidateRect(shield_, nullptr, FALSE);
        } else {
          KillTimer(window_, kShieldAnimTimer);
        }
      } else if (w_param == kHomeAnimTimer) {
        if (home_hovering_ && home_strip_.valid() && home_strip_.frame_count() > 1) {
          home_frame_ = (home_frame_ + 1) % home_strip_.frame_count();
          if (home_) InvalidateRect(home_, nullptr, FALSE);
        } else {
          KillTimer(window_, kHomeAnimTimer);
          home_frame_ = 0;
          if (home_) InvalidateRect(home_, nullptr, FALSE);
        }
      } else if (w_param == kReloadAnimTimer) {
        if ((reload_hovering_ || reload_animating_) && reload_strip_.valid() && reload_strip_.frame_count() > 1) {
          reload_frame_ = (reload_frame_ + 1) % reload_strip_.frame_count();
          if (reload_) InvalidateRect(reload_, nullptr, FALSE);
        } else {
          KillTimer(window_, kReloadAnimTimer);
          reload_frame_ = 0;
          if (reload_) InvalidateRect(reload_, nullptr, FALSE);
        }
      } else if (w_param == kDownloadAnimTimer) {
        if (download_hovering_ && download_strip_.valid() && download_strip_.frame_count() > 1) {
          download_frame_ = (download_frame_ + 1) % download_strip_.frame_count();
          if (downloads_) InvalidateRect(downloads_, nullptr, FALSE);
        } else {
          KillTimer(window_, kDownloadAnimTimer);
          download_frame_ = 0;
          if (downloads_) InvalidateRect(downloads_, nullptr, FALSE);
        }
      } else if (w_param == kSessionSaveTimer) {
        KillTimer(window_, kSessionSaveTimer);
        SaveSessionSnapshot();
      }
      return 0;
    case WM_DRAWITEM:
      DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
      return TRUE;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(w_param);
      if (IsLightAppearance()) {
        SetTextColor(dc, RGB(25, 32, 51));
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(edit_brush_light_);
      }
      SetTextColor(dc, kTextColor);
      SetBkColor(dc, kControlColor);
      return reinterpret_cast<LRESULT>(edit_brush_);
    }
    case WM_CTLCOLORSTATIC:
      if (reinterpret_cast<HWND>(l_param) == find_status_) {
        HDC dc = reinterpret_cast<HDC>(w_param);
        if (IsLightAppearance()) {
          SetTextColor(dc, RGB(100, 116, 139));
          SetBkColor(dc, RGB(255, 255, 255));
          return reinterpret_cast<LRESULT>(edit_brush_light_);
        }
        SetTextColor(dc, kMutedColor);
        SetBkColor(dc, kControlColor);
        return reinterpret_cast<LRESULT>(edit_brush_);
      }
      break;
    case WM_LBUTTONDOWN: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      const CaptionButton clicked_caption = HitTestCaptionButton(point);
      if (clicked_caption != CaptionButton::kNone) {
        pressed_caption_button_ = clicked_caption;
        SetCapture(window_);
        RECT controls = WindowControlsRect();
        InvalidateRect(window_, &controls, FALSE);
        return 0;
      }
      if (BookmarksBarHeight() > 0) {
        for (size_t hit_idx = 0; hit_idx < bookmark_bar_hits_.size(); ++hit_idx) {
          const BookmarkHit& hit = bookmark_bar_hits_[hit_idx];
          if (!PtInRect(&hit.bounds, point)) continue;
          if (hit.overflow) {
            POINT screen{hit.bounds.left, hit.bounds.bottom};
            ClientToScreen(window_, &screen);
            ShowBookmarksOverflowMenu(screen);
          } else {
            BeginBookmarkDrag(hit_idx, point);
          }
          return 0;
        }
      }
      if (point.y < TabBarHeight()) {
        RECT client{};
        GetClientRect(window_, &client);
        const size_t pinned = PinnedCount();
        const bool is_maximized = IsZoomed(window_);
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect =
              TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_, is_maximized);
          if (PtInRect(&rect, point)) {
            const RECT close = TabCloseRect(rect, tabs_[index].pinned);
            const RECT mute = TabMuteRect(rect, tabs_[index].pinned);
            if (PtInRect(&close, point)) CloseTab(index);
            else if (tabs_[index].muted && PtInRect(&mute, point)) ToggleMute(index);
            // Selection is deferred to button-up so the press can start a drag.
            else BeginTabDrag(index, point);
            return 0;
          }
        }
        RECT plus = NewTabRect(client.right, tabs_.size(), pinned, tab_scroll_, is_maximized);
        InflateRect(&plus, Scaled(3), Scaled(8));
        if (PtInRect(&plus, point)) CreateTab(kHomeUrl);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      if (pressed_caption_button_ != CaptionButton::kNone) {
        const CaptionButton action = pressed_caption_button_;
        pressed_caption_button_ = CaptionButton::kNone;
        if (GetCapture() == window_) {
          ReleaseCapture();
        }
        const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        const CaptionButton released = HitTestCaptionButton(point);
        RECT controls = WindowControlsRect();
        InvalidateRect(window_, &controls, FALSE);

        if (released == action) {
          if (action == CaptionButton::kMinimize) {
            ShowWindow(window_, SW_MINIMIZE);
          } else if (action == CaptionButton::kMaximize) {
            ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
          } else if (action == CaptionButton::kClose) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
          }
        }
        return 0;
      }
      if (bookmark_drag_index_ != static_cast<size_t>(-1)) {
        const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        EndBookmarkDrag(point);
        return 0;
      }
      if (drag_candidate_ != kNoTab) {
        EndTabDrag();
        return 0;
      }
      break;
    }
    case WM_CAPTURECHANGED:
      if (pressed_caption_button_ != CaptionButton::kNone) {
        pressed_caption_button_ = CaptionButton::kNone;
        RECT controls = WindowControlsRect();
        InvalidateRect(window_, &controls, FALSE);
      }
      if (drag_candidate_ != kNoTab) {
        drag_candidate_ = kNoTab;
        dragging_ = false;
      }
      return 0;
    case WM_MBUTTONUP: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (!full_screen_ && point.y >= 0 && point.y < TabBarHeight()) {
        RECT client{};
        GetClientRect(window_, &client);
        const size_t pinned = PinnedCount();
        const bool is_maximized = IsZoomed(window_);
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect =
              TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_, is_maximized);
          if (PtInRect(&rect, point)) {
            CloseTab(index);
            break;
          }
        }
      }
      return 0;
    }
    case WM_RBUTTONUP: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (!full_screen_ && BookmarksBarHeight() > 0) {
        const int bar_top = TabBarHeight() + ToolbarHeight();
        if (point.y >= bar_top && point.y < bar_top + BookmarksBarHeight()) {
          const BookmarkHit* found = nullptr;
          for (const BookmarkHit& hit : bookmark_bar_hits_) {
            // The overflow button is not a bookmark or a folder, so a right
            // click on it opens the same menu as blank space on the bar.
            if (PtInRect(&hit.bounds, point) && !hit.overflow) {
              found = &hit;
              break;
            }
          }
          POINT screen = point;
          ClientToScreen(window_, &screen);
          ShowBookmarksBarMenu(found, screen);
          return 0;
        }
      }
      if (!full_screen_ && point.y >= 0 && point.y < TabBarHeight()) {
        RECT client{};
        GetClientRect(window_, &client);
        const size_t pinned = PinnedCount();
        const bool is_maximized = IsZoomed(window_);
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect =
              TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_, is_maximized);
          if (PtInRect(&rect, point)) {
            POINT screen = point;
            ClientToScreen(window_, &screen);
            ShowTabMenu(index, screen);
            break;
          }
        }
      }
      return 0;
    }
    case WM_COMMAND: {
      auto* tab = ActiveTab();
      switch (LOWORD(w_param)) {
        case kBackId: if (tab && tab->client->browser()) tab->client->browser()->GoBack(); break;
        case kForwardId: if (tab && tab->client->browser()) tab->client->browser()->GoForward(); break;
        case kReloadId:
          if (tab && tab->client->browser()) {
            if (tab->loading) tab->client->browser()->StopLoad();
            else tab->client->browser()->Reload();
          }
          break;
        case kHomeId: if (tab && tab->client->browser()) tab->client->browser()->GetMainFrame()->LoadURL(kHomeUrl); break;

        case kAddressId:
          if (HIWORD(w_param) == EN_CHANGE) RefreshSuggestions();
          break;
        case kSuggestionsId:
          // Keyboard navigation uses LB_SETCURSEL, which sends no notification,
          // so these arrive only from an actual mouse pick.
          if (HIWORD(w_param) == LBN_SELCHANGE ||
              HIWORD(w_param) == LBN_DBLCLK) {
            AcceptSuggestion();
          }
          break;
        case kFindId:
          if (HIWORD(w_param) == EN_CHANGE) RunFind(true, false);
          break;
        case kFindPreviousId:
          RunFind(false, true);
          break;
        case kFindNextId:
          RunFind(true, true);
          break;
        case kFindCloseId:
          HideFindBar();
          break;
        case kShieldId: {
          mtk::shields::SiteState state;
          if (tab) {
            state.site = mtk::sites::OriginOf(WideToUtf8(tab->url));
            if (!state.site.empty()) {
              state.site_blocked_count =
                  BrowserClient::blocked_count_for_origin(state.site);
              auto rule = mtk::sites::Store::Get().For(state.site);
              state.site_filtering = rule.ad_filtering;
              state.cookies = rule.cookies;
              state.custom_rule_count =
                  BrowserClient::user_rule_blocked_count_for_origin(state.site);
            }
          }
          HWND window = window_;
          mtk::shields::Actions actions;
          actions.on_toggle_shield = [this]() {
            ToggleShieldForCurrentSite();
          };
          actions.on_toggle_cookies = [this]() {
            ToggleCookiesForCurrentSite();
          };
          actions.on_toggle_downloads = [this]() {
            ToggleDownloadProtection();
          };
          actions.on_open_site_settings = [this]() {
            ShowSiteSettingsDialog();
          };
          actions.on_open_user_filters = [this]() {
            ShowUserFiltersDialog();
          };
          actions.on_manage_sites = [window]() {
            if (window) PostMessageW(window, kShieldsManageSitesMessage, 0, 0);
          };
          mtk::shields::ShowPanel(
              instance_, window_, shield_, BrowserClient::blocked_count(),
              mtk::downloads::CurrentMode() == mtk::downloads::Mode::kStandard,
              state, actions);
          break;
        }
        case kDownloadsId:
          // The toolbar button opens the popup; the full window stays on Alt+J
          // and in the menu.
          ShowDownloadsPanel();
          break;
        case kMenuId:
          ShowBrowserMenu();
          break;
        case IDM_ADD_BOOKMARK:
          ToggleBookmark();
          break;
        case IDM_TAB_SEARCH:
          ShowTabSearch();
          break;
        case IDM_BOOKMARKS:
          ShowLibraryDialog();
          break;
        case IDM_VERTICAL_TABS:
          ToggleVerticalTabs();
          break;
        case IDM_VERTICAL_TABS_COLLAPSE:
          ToggleVerticalTabsCollapsed();
          break;
        case IDM_SAVE_PAGE_PDF:
          SaveActivePagePdf();
          break;
        case IDM_TAKE_SCREENSHOT:
          TakeActivePageScreenshot();
          break;
        case IDM_READER_MODE:
          ToggleReaderMode();
          break;
        case IDM_FOCUS_MODE:
          ToggleFocusMode();
          break;
        case IDM_READING_LIST_TOGGLE:
          ToggleReadingList();
          break;
        case IDM_READING_LIST:
          ShowReadingListDialog();
          break;
        case IDM_TRANSLATE_HINDI:
          TranslateActivePage("hi");
          break;
        case IDM_TRANSLATE_ENGLISH:
          TranslateActivePage("en");
          break;
        case IDM_OPEN_AS_WEB_APP:
          OpenActiveSiteAsWebApp();
          break;
        case IDM_MANAGE_WEB_APPS:
          mtk::web_app::ShowAppManager(instance_, window_);
          break;
        case IDM_PASSKEY_CHECK:
          ShowPasskeyCheckPage();
          break;
        case IDM_TASK_MANAGER:
          ShowTaskManager();
          break;
        case IDM_SPLIT_VIEW:
          ToggleSplitView();
          break;
        case IDM_TAB_GROUP: {
          auto* selected = ActiveTab();
          if (selected) {
            std::wstring name = selected->group;
            if (PromptForFolderName(L"Tab group", L"Group name (leave empty to remove):", &name)) {
              selected->group = name;
              ScheduleSessionSave();
              InvalidateRect(window_, nullptr, FALSE);
            }
          }
          break;
        }
        case IDM_TAB_GROUPS:
          ShowTabGroupsMenu();
          break;
        case IDM_WORKSPACES_MENU:
          ShowWorkspacesMenu();
          break;
        case IDM_SET_WORKSPACE: {
          auto* selected = ActiveTab();
          if (selected) {
            std::wstring name = selected->workspace;
            if (PromptForFolderName(L"Workspace", L"Workspace name (leave empty to remove):", &name)) {
              selected->workspace = name;
              ScheduleSessionSave();
              InvalidateRect(window_, nullptr, FALSE);
            }
          }
          break;
        }
        case IDM_DIAGNOSTICS:
          ShowDiagnostics();
          break;
        case IDM_HISTORY:
          OpenHistoryPage();
          break;
        case IDM_DOWNLOADS:
          ShowDownloadsDialog();
          break;
        case IDM_FULL_SCREEN:
          ToggleFullScreen();
          break;
        case IDM_PRINT:
          PrintActiveTab();
          break;
        case IDM_SHORTCUTS:
          ShowShortcutsPage();
          break;
        case IDM_PROFILE_NEW:
          CreateProfile();
          break;
        case IDM_SITE_SETTINGS:
          ShowSiteSettingsDialog();
          break;
        case IDM_SITE_LIST:
          ShowSiteListDialog();
          break;
        case IDM_PASSWORDS:
          ShowPasswordsDialog();
          break;
        case IDM_MAKE_DEFAULT:
          MakeDefaultBrowser();
          break;
        case IDM_USER_FILTERS:
          ShowUserFiltersDialog();
          break;
        case IDM_NEW_WINDOW:
          OpenNewWindow({});
          break;
        case IDM_NEW_PRIVATE_WINDOW:
          OpenNewWindow({}, true);
          break;
        case IDM_BOOKMARKS_BAR:
          ToggleBookmarksBar();
          break;
        case IDM_IMPORT_BOOKMARKS:
          ImportBookmarksFromBrowser();
          break;
        case IDM_REOPEN_TAB:
          ReopenClosedTab();
          break;
        case IDM_CLEAR_DATA:
          ShowClearDataDialog();
          break;
        case IDM_CHECK_UPDATES:
          StartUpdateCheck(true);
          break;
        case IDM_SETTINGS:
          ShowSettingsDialog();
          break;
        case IDM_BOOKMARK_NEW_FOLDER: {
          std::wstring name;
          if (!PromptForFolderName(L"New bookmark folder",
                                   L"Name for the new folder:", &name)) {
            break;
          }
          if (!mtk::library::Store::Get().CreateFolder(name)) {
            MessageBoxW(window_,
                        L"That folder could not be created.\n\nA folder with "
                        L"that name may already exist.",
                        L"Bookmark folders", MB_OK | MB_ICONWARNING);
            break;
          }
          ScheduleLibrarySave();
          InvalidateRect(window_, nullptr, FALSE);
          break;
        }
        case IDM_BOOKMARK_RENAME_FOLDER: {
          if (context_folder_name_.empty()) break;
          std::wstring name;
          if (!PromptForFolderName(L"Rename bookmark folder",
                                   L"New name for this folder:", &name)) {
            break;
          }
          if (!mtk::library::Store::Get().RenameFolder(context_folder_name_,
                                                      name)) {
            MessageBoxW(window_,
                        L"That folder could not be renamed.\n\nAnother folder "
                        L"may already use that name.",
                        L"Bookmark folders", MB_OK | MB_ICONWARNING);
            break;
          }
          ScheduleLibrarySave();
          InvalidateRect(window_, nullptr, FALSE);
          break;
        }
        case IDM_BOOKMARK_DELETE_FOLDER: {
          if (context_folder_name_.empty()) break;
          auto& store = mtk::library::Store::Get();
          size_t inside = 0;
          for (const auto& bookmark : store.bookmarks()) {
            if (_wcsicmp(bookmark.folder.c_str(),
                         context_folder_name_.c_str()) == 0) {
              ++inside;
            }
          }
          // Deleting bookmarks is not reversible, so an occupied folder asks
          // what should happen to its contents instead of assuming.
          bool keep = true;
          if (inside > 0) {
            const std::wstring question =
                L"\"" + context_folder_name_ + L"\" holds " +
                std::to_wstring(inside) +
                (inside == 1 ? L" bookmark." : L" bookmarks.") +
                L"\n\nYes: keep them and move them to the top level."
                L"\nNo: delete them with the folder.";
            const int answer = MessageBoxW(window_, question.c_str(),
                                           L"Delete bookmark folder",
                                           MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL) break;
            keep = answer == IDYES;
          }
          if (store.RemoveFolder(context_folder_name_, keep)) {
            ScheduleLibrarySave();
            InvalidateRect(window_, nullptr, FALSE);
          }
          break;
        }
        case IDM_BOOKMARK_MOVE_EARLIER:
        case IDM_BOOKMARK_MOVE_LATER: {
          if (!context_bookmark_url_.empty()) {
            auto& store = mtk::library::Store::Get();
            const auto& bms = store.bookmarks();
            for (size_t i = 0; i < bms.size(); ++i) {
              if (bms[i].url == context_bookmark_url_) {
                const size_t target = (LOWORD(w_param) == IDM_BOOKMARK_MOVE_EARLIER)
                                          ? (i > 0 ? i - 1 : i)
                                          : (i + 1 < bms.size() ? i + 1 : i);
                if (target != i && store.MoveBookmark(i, target)) {
                  ScheduleLibrarySave();
                  InvalidateRect(window_, nullptr, FALSE);
                }
                break;
              }
            }
          }
          break;
        }
        case IDM_BOOKMARK_REMOVE:
          if (!context_bookmark_url_.empty() &&
              mtk::library::Store::Get().RemoveBookmark(context_bookmark_url_)) {
            ScheduleLibrarySave();
            InvalidateRect(window_, nullptr, FALSE);
          }
          break;
        case IDM_BOOKMARK_MOVE_ROOT:
          if (!context_bookmark_url_.empty() &&
              mtk::library::Store::Get().SetBookmarkFolder(
                  context_bookmark_url_, {})) {
            ScheduleLibrarySave();
            InvalidateRect(window_, nullptr, FALSE);
          }
          break;
        default: {
          // Profile entries occupy a reserved id range; the name comes from the
          // list captured when the menu was built.
          const WORD command = LOWORD(w_param);
          if (command >= IDM_PROFILE_FIRST &&
              command < IDM_PROFILE_FIRST + kMaxProfileMenuEntries) {
            const size_t index = static_cast<size_t>(command - IDM_PROFILE_FIRST);
            if (index < profile_menu_ids_.size()) {
              LaunchProfile(profile_menu_ids_[index]);
            }
            break;
          }
          // A row of the folder popup. The address comes from the list captured
          // when that popup was built, not from the menu text.
          if (command >= IDM_BOOKMARK_OPEN_FIRST &&
              command < IDM_BOOKMARK_OPEN_FIRST + kMaxFolderMenuEntries) {
            const size_t index =
                static_cast<size_t>(command - IDM_BOOKMARK_OPEN_FIRST);
            if (index < folder_menu_urls_.size()) {
              const auto decision =
                  mtk::policy::ResolveInput(folder_menu_urls_[index]);
              if (decision.allow && tab && tab->client->browser()) {
                tab->client->browser()->GetMainFrame()->LoadURL(decision.url);
              }
            }
            break;
          }
          if (command >= IDM_BOOKMARK_MOVE_FIRST &&
              command < IDM_BOOKMARK_MOVE_FIRST + kMaxFolderMenuEntries) {
            const size_t index =
                static_cast<size_t>(command - IDM_BOOKMARK_MOVE_FIRST);
            auto& store = mtk::library::Store::Get();
            if (index < store.folders().size() &&
                !context_bookmark_url_.empty() &&
                store.SetBookmarkFolder(context_bookmark_url_,
                                        store.folders()[index].name)) {
              ScheduleLibrarySave();
              InvalidateRect(window_, nullptr, FALSE);
            }
          }
          break;
        }
      }
      return 0;
    }
    case WM_CLOSE: {
      if (closing_) return 0;
      // The Shields panel hosts its own browser, so it has to unwind with the
      // tabs rather than be torn down implicitly with its owner.
      mtk::shields::ClosePanel();
      mtk::downloads_panel::ClosePanel();
      mtk::tab_preview::Hide();
      SaveWindowPlacement();
      // Freeze session updates before taking the final snapshot. Otherwise a
      // deferred timer can run while CEF tabs are closing and overwrite it.
      closing_ = true;
      KillTimer(window_, kSessionSaveTimer);
      SaveSessionSnapshot();
      mtk::downloads::Manager::Get().Flush();

      std::vector<CefRefPtr<CefBrowser>> closing_browsers;
      closing_browsers.reserve(tabs_.size() + closing_clients_.size());
      for (const auto& tab : tabs_) {
        if (auto browser = tab.client->browser()) {
          closing_browsers.push_back(browser);
        }
      }
      for (const auto& client : closing_clients_) {
        if (auto browser = client->browser()) {
          closing_browsers.push_back(browser);
        }
      }

      for (const auto& browser : closing_browsers) {
        browser->GetHost()->CloseBrowser(true);
      }
      if (tabs_.empty() && closing_clients_.empty() && window_) {
        PostMessageW(window_, kFinalizeCloseMessage, 0, 0);
      }
      return 0;
    }
    case kShieldsManageSitesMessage:
      ShowSiteListDialog();
      return 0;
    case kUpdateResultMessage:
      HandleUpdateResult();
      return 0;
    case kFinalizeCloseMessage:
      if (closing_ && window_) DestroyWindow(window_);
      return 0;
    case WM_DPICHANGED: {
      // Windows supplies the correct new window bounds for the new scale.
      const RECT* suggested = reinterpret_cast<const RECT*>(l_param);
      if (suggested) {
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      ApplyDpi(HIWORD(w_param));
      return 0;
    }
    case WM_DESTROY: {
      KillTimer(window_, kUpdateStartupTimer);
      KillTimer(window_, kUpdatePeriodicTimer);
      KillTimer(window_, kLibrarySaveTimer);
      KillTimer(window_, kDownloadSaveTimer);
      KillTimer(window_, kSessionSaveTimer);
      mtk::library::Store::Get().Flush();
      mtk::downloads::Manager::Get().Flush();
      window_ = nullptr;

      g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), this),
                      g_windows.end());
      // The snapshot belongs to whichever window is still open, so closing the
      // first window does not stop session restore from working.
      if (owns_session_ && !g_windows.empty()) {
        owns_session_ = false;
        g_windows.front()->owns_session_ = true;
      }
      // Only the last window ends the message loop.
      if (g_windows.empty()) CefQuitMessageLoop();
      return 0;
    }
  }
  return DefWindowProcW(window_, message, w_param, l_param);
}

void BrowserWindow::OnBrowserCreated(BrowserClient* client) {
  // CEF calls in outside the window procedure, so the shared metrics have to be
  // pointed at this window before anything is measured or laid out.
  ActivateMetrics();
  const size_t index = FindTab(client);
  const bool detached_closing = std::any_of(
      closing_clients_.begin(), closing_clients_.end(),
      [client](const CefRefPtr<BrowserClient>& pending) {
        return pending.get() == client;
      });
  if ((index == kNoTab && !detached_closing) || !client->browser()) return;
  if (closing_ || detached_closing) {
    // OnAfterCreated is still on CEF's creation stack. Defer CloseBrowser to
    // avoid a re-entrant close inside libcef while restored tabs are pending.
    CefRefPtr<CefBrowser> browser = client->browser();
    CefPostTask(TID_UI, base::BindOnce(
                            [](CefRefPtr<CefBrowser> pending) {
                              if (pending) {
                                pending->GetHost()->CloseBrowser(true);
                              }
                            },
                            browser));
    return;
  }
  if (client->browser()->GetHost()->GetRuntimeStyle() !=
      CEF_RUNTIME_STYLE_ALLOY) {
    MessageBoxW(window_,
                L"CEF created an unexpected Chrome-style window. "
                L"MTK Browser stopped it instead of showing the wrong UI.",
                L"MTK Browser startup error", MB_OK | MB_ICONERROR);
    client->browser()->GetHost()->CloseBrowser(true);
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return;
  }
  ResizeBrowser(tabs_[index]);
  ShowWindow(client->browser()->GetHost()->GetWindowHandle(),
             index == active_tab_ ? SW_SHOW : SW_HIDE);
  UpdateControls();
}

void BrowserWindow::OnBrowserClosed(BrowserClient* client) {
  ActivateMetrics();
  const size_t index = FindTab(client);
  if (index != kNoTab) {
    tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(index));
    if (tabs_.empty()) {
      active_tab_ = 0;
    } else if (active_tab_ >= tabs_.size()) {
      active_tab_ = tabs_.size() - 1;
    } else if (index < active_tab_) {
      --active_tab_;
    }
  }
  closing_clients_.erase(
      std::remove_if(closing_clients_.begin(), closing_clients_.end(),
                     [client](const CefRefPtr<BrowserClient>& pending) {
                       return pending.get() == client;
                     }),
      closing_clients_.end());

  if (tabs_.empty() && closing_clients_.empty()) {
    if (!window_) return;
    if (closing_) {
      PostMessageW(window_, kFinalizeCloseMessage, 0, 0);
    } else {
      PostMessageW(window_, WM_CLOSE, 0, 0);
    }
    return;
  }
  if (closing_ || index == kNoTab) return;

  if (auto* active = ActiveTab(); active && active->client->browser()) {
    ResizeBrowser(*active);
    ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
  }
  UpdateControls();
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::OnBrowserAddressChanged(BrowserClient* client,
                                             const std::wstring& url) {
  ActivateMetrics();
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  tabs_[index].url = url;
  ScheduleSessionSave();
  if (index == active_tab_) UpdateControls();
}

void BrowserWindow::OnBrowserTitleChanged(BrowserClient* client,
                                           const std::wstring& title) {
  ActivateMetrics();
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  tabs_[index].title = title.empty() ? L"New Tab" : title;
  if (!private_session_) {
    const auto decision =
        mtk::policy::EvaluateNavigation(WideToUtf8(tabs_[index].url));
    if (decision.allow && mtk::library::Store::Get().UpdateHistoryTitle(
                              decision.url, tabs_[index].title)) {
      ScheduleLibrarySave();
    }
  }
  if (index == active_tab_) UpdateControls();
  else InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::OnBrowserLoadingChanged(BrowserClient* client, bool loading,
                                             bool can_go_back,
                                             bool can_go_forward) {
  ActivateMetrics();
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  if (loading && index == active_tab_ && find_visible_) HideFindBar();
  tabs_[index].loading = loading;
  tabs_[index].can_go_back = can_go_back;
  tabs_[index].can_go_forward = can_go_forward;
  if (index == active_tab_) {
    SetReloadAnimating(loading);
    UpdateControls();
  }
}

void BrowserWindow::OnBrowserNavigationCompleted(BrowserClient* client,
                                                  const std::string& url) {
  // The remembered zoom is applied for every window, private included: it is a
  // display preference, not browsing data.
  if (const size_t tab_index = FindTab(client); tab_index != kNoTab) {
    ApplySiteZoom(tabs_[tab_index]);
  }
  // Nothing a private window visits is written to history. This is the whole
  // point of the window, so it is checked before anything else.
  if (private_session_) return;
  const size_t index = FindTab(client);
  if (index == kNoTab || url.starts_with("data:") || url == "about:blank") return;
  const auto decision = mtk::policy::EvaluateNavigation(url);
  if (!decision.allow) return;
  if (mtk::library::Store::Get().RecordVisit(decision.url, tabs_[index].title)) {
    ScheduleLibrarySave();
  }
}

void BrowserWindow::OnBrowserFindResult(BrowserClient* client, int identifier,
                                        int count, int active_match_ordinal,
                                        bool final_update) {
  if (FindTab(client) != active_tab_ || !find_visible_ || !find_active_) return;
  if (active_find_identifier_ < 0) active_find_identifier_ = identifier;
  if (identifier != active_find_identifier_) return;
  if (count <= 0) {
    SetWindowTextW(find_status_, final_update ? L"No matches" : L"Searching…");
    return;
  }
  const int position = active_match_ordinal > 0 ? active_match_ordinal : 1;
  const std::wstring status = std::to_wstring(position) + L" of " +
                              std::to_wstring(count);
  SetWindowTextW(find_status_, status.c_str());
}

void BrowserWindow::OnBrowserPopupRequested(BrowserClient*,
                                             const std::string& url) {
  ActivateMetrics();
  CreateTab(url);
}

bool BrowserWindow::OnBrowserShortcut(BrowserClient* client, int virtual_key,
                                      bool control, bool alt, bool shift) {
  if (FindTab(client) != active_tab_) return false;
  return HandleShortcut(virtual_key, control, alt, shift);
}

size_t BrowserWindow::FindTab(BrowserClient* client) const {
  for (size_t index = 0; index < tabs_.size(); ++index) {
    if (tabs_[index].client.get() == client) return index;
  }
  return kNoTab;
}

BrowserWindow::Tab* BrowserWindow::ActiveTab() {
  return active_tab_ < tabs_.size() ? &tabs_[active_tab_] : nullptr;
}

void BrowserWindow::ToggleShieldForCurrentSite() {
  auto* tab = ActiveTab();
  if (!tab) return;
  const std::string origin = mtk::sites::OriginOf(WideToUtf8(tab->url));
  if (origin.empty()) return;

  auto& store = mtk::sites::Store::Get();
  auto rule = store.For(origin);
  rule.ad_filtering = !rule.ad_filtering;
  store.Set(rule);
  store.Flush();

  if (tab->client && tab->client->browser()) {
    tab->client->browser()->ReloadIgnoreCache();
  }
  UpdateShieldsPanel();
}

void BrowserWindow::ToggleCookiesForCurrentSite() {
  auto* tab = ActiveTab();
  if (!tab) return;
  const std::string origin = mtk::sites::OriginOf(WideToUtf8(tab->url));
  if (origin.empty()) return;

  auto& store = mtk::sites::Store::Get();
  auto rule = store.For(origin);
  rule.cookies = !rule.cookies;
  store.Set(rule);
  store.Flush();

  if (tab->client && tab->client->browser()) {
    tab->client->browser()->ReloadIgnoreCache();
  }
  UpdateShieldsPanel();
}

void BrowserWindow::ToggleDownloadProtection() {
  auto& store = mtk::preferences::Store::Get();
  auto values = store.values();
  values.download_mode =
      (values.download_mode == mtk::preferences::DownloadMode::kStandard)
          ? mtk::preferences::DownloadMode::kSafe
          : mtk::preferences::DownloadMode::kStandard;
  store.Save(values);
  UpdateShieldsPanel();
}

void BrowserWindow::UpdateShieldsPanel() {
  auto* tab = ActiveTab();
  mtk::shields::SiteState state;
  if (tab) {
    state.site = mtk::sites::OriginOf(WideToUtf8(tab->url));
    if (!state.site.empty()) {
      state.site_blocked_count =
          BrowserClient::blocked_count_for_origin(state.site);
      auto rule = mtk::sites::Store::Get().For(state.site);
      state.site_filtering = rule.ad_filtering;
      state.cookies = rule.cookies;
      state.custom_rule_count =
          BrowserClient::user_rule_blocked_count_for_origin(state.site);
    }
  }
  mtk::shields::UpdatePanel(
      BrowserClient::blocked_count(),
      mtk::downloads::CurrentMode() == mtk::downloads::Mode::kStandard,
      state);
}

void BrowserWindow::OnBrowserRequestBlocked(BrowserClient* client) {
  const size_t index = FindTab(client);
  if (index == active_tab_) {
    if (mtk::shields::IsPanelOpen()) {
      UpdateShieldsPanel();
    }
    if (shield_) InvalidateRect(shield_, nullptr, FALSE);
  }
}


void BrowserWindow::OnBrowserFaviconUrlsChanged(
    BrowserClient* client, const std::vector<std::string>& icon_urls) {
}

void BrowserWindow::OnBrowserMediaAccessChanged(BrowserClient* client,
                                               bool camera_active,
                                               bool microphone_active) {
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  if (tabs_[index].camera_active == camera_active &&
      tabs_[index].microphone_active == microphone_active) {
    return;
  }
  tabs_[index].camera_active = camera_active;
  tabs_[index].microphone_active = microphone_active;
  RECT client_rect{};
  GetClientRect(window_, &client_rect);
  RECT strip{0, 0, client_rect.right, TabBarHeight()};
  InvalidateRect(window_, &strip, FALSE);
}

void BrowserWindow::OnBrowserPasskeyAction(BrowserClient* client,
                                          mtk::passkeys::Action action) {
}

void BrowserWindow::OnBrowserDownloadsAction(
    BrowserClient* client, const mtk::downloads_page::Action& action) {
}

void BrowserWindow::OnBrowserSettingsAction(
    BrowserClient* client, const mtk::settings_page::Action& action) {
}

void BrowserWindow::OnBrowserFullscreenModeChange(BrowserClient* client,
                                                 bool fullscreen) {
  if (FindTab(client) != active_tab_) return;
  SetFullScreen(fullscreen);
}

void BrowserWindow::OnBrowserGotFocus(BrowserClient* client) {
  const size_t index = FindTab(client);
  if (index != kNoTab && index != active_tab_) {
    SelectTab(index);
  }
}

bool BrowserWindow::IsSplitViewActive() const {
  return split_tab_ < tabs_.size() && split_tab_ != active_tab_;
}

void BrowserWindow::OpenLinkInOtherPane(const std::string& url) {
  if (!IsSplitViewActive()) {
    CreateTab(url);
    return;
  }
  const size_t other_tab = (split_focus_pane_ == 0) ? split_tab_ : active_tab_;
  if (other_tab < tabs_.size() && tabs_[other_tab].client && tabs_[other_tab].client->browser()) {
    tabs_[other_tab].client->browser()->GetMainFrame()->LoadURL(url);
  }
}

void BrowserWindow::SetFullScreen(bool enable) {
  if (full_screen_ != enable) {
    ToggleFullScreen();
  }
}

