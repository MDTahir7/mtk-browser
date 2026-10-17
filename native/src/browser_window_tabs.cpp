#include "browser_window_internal.h"

size_t BrowserWindow::PinnedCount() const {
  size_t count = 0;
  while (count < tabs_.size() && tabs_[count].pinned) ++count;
  return count;
}

int BrowserWindow::MaxTabScroll(int width) const {
  if (tabs_.empty()) return 0;
  const int content =
      TabSlotLeft(tabs_.size(), width, tabs_.size(), PinnedCount(), 0) +
      Scaled(kBaseNewTabWidth);
  return std::max(0, content - (width - TabStripReserve()));
}

void BrowserWindow::ScrollTabs(int delta) {
  RECT client{};
  GetClientRect(window_, &client);
  const int limit = MaxTabScroll(static_cast<int>(client.right));
  const int updated = std::clamp(tab_scroll_ + delta, 0, limit);
  if (updated == tab_scroll_) return;
  tab_scroll_ = updated;
  RECT strip{0, 0, client.right, TabBarHeight()};
  InvalidateRect(window_, &strip, FALSE);
}

void BrowserWindow::EnsureActiveTabVisible() {
  if (tabs_.empty()) return;
  RECT client{};
  GetClientRect(window_, &client);
  const int width = static_cast<int>(client.right);
  const int limit = MaxTabScroll(width);
  if (limit == 0) {
    if (tab_scroll_ != 0) ScrollTabs(-tab_scroll_);
    return;
  }
  const size_t pinned = PinnedCount();
  const int left = TabSlotLeft(active_tab_, width, tabs_.size(), pinned, 0);
  const int right = TabSlotLeft(active_tab_ + 1, width, tabs_.size(), pinned, 0);
  const int visible_right = width - TabStripReserve();
  if (left - tab_scroll_ < Scaled(kBaseTabStripInset)) {
    ScrollTabs(left - tab_scroll_ - Scaled(kBaseTabStripInset));
  } else if (right - tab_scroll_ > visible_right) {
    ScrollTabs(right - tab_scroll_ - visible_right);
  }
}


bool BrowserWindow::ReopenClosedTab() {
  while (!closed_tab_urls_.empty()) {
    const std::string url = closed_tab_urls_.back();
    closed_tab_urls_.pop_back();
    // Re-check the address: the safety policy may have changed since it closed.
    const auto decision = mtk::policy::EvaluateNavigation(url);
    if (decision.allow && CreateTab(decision.url)) {
      UpdateControls();
      return true;
    }
  }
  return false;
}

// MTK Browser keeps every browser action on the Alt layer instead of reusing
// Chrome's and Brave's Ctrl shortcuts. Two consequences are deliberate:
//
//  * Ctrl+C/X/V/A/Z and the other text-editing keys are untouched. Those belong
//    to Windows and to web pages, and remapping them would break typing.
//  * Esc still leaves full screen and closes the find bar, because Esc means
//    "cancel" everywhere and has no browser-specific meaning to avoid.
//
// Alt+Tab, Alt+Space and Alt+F4 belong to Windows, so nothing is bound to them.
// The full list is shown by Alt+/ and is documented in
// docs/KEYBOARD_SHORTCUTS.md.

void BrowserWindow::SelectTab(size_t index) {
  if (index >= tabs_.size() || index == active_tab_) return;
  if (find_visible_) HideFindBar();
  if (auto* old = ActiveTab(); old && old->client->browser()) {
    ShowWindow(old->client->browser()->GetHost()->GetWindowHandle(), SW_HIDE);
  }
  active_tab_ = index;
  tabs_[index].last_active_tick = GetTickCount64();
  if (tabs_[index].discarded) ReviveTab(index);
  if (auto* active = ActiveTab(); active && active->client->browser()) {
    ResizeBrowser(*active);
    ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
    SetFocus(active->client->browser()->GetHost()->GetWindowHandle());
  }
  EnsureActiveTabVisible();
  UpdateControls();
  InvalidateRect(window_, nullptr, FALSE);
  ScheduleSessionSave();
}

void BrowserWindow::RememberClosedTab(const Tab& tab) {
  // Only real, still-permitted web addresses are worth reopening. Blocked and
  // internal pages are dropped so Ctrl+Shift+T can never restore them.
  const std::string url = WideToUtf8(tab.url);
  const auto decision = mtk::policy::EvaluateNavigation(url);
  if (!decision.allow || (!decision.url.starts_with("https://") &&
                          !decision.url.starts_with("http://"))) {
    return;
  }
  closed_tab_urls_.push_back(decision.url);
  if (closed_tab_urls_.size() > kMaxClosedTabs) {
    closed_tab_urls_.erase(closed_tab_urls_.begin());
  }
}

void BrowserWindow::CloseTab(size_t index) {
  if (index >= tabs_.size()) return;
  if (index == active_tab_ && find_visible_) HideFindBar();
  if (tabs_.size() == 1) {
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return;
  }
  RememberClosedTab(tabs_[index]);
  auto closing = tabs_[index].client;
  // Remove the tab from the strip now, but retain its client until
  // OnBeforeClose so parent shutdown cannot outrun CEF's lifecycle.
  closing_clients_.push_back(closing);
  tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(index));
  if (active_tab_ >= tabs_.size()) active_tab_ = tabs_.size() - 1;
  else if (index < active_tab_) --active_tab_;
  if (closing->browser()) closing->browser()->GetHost()->CloseBrowser(true);
  if (auto* active = ActiveTab(); active && active->client->browser()) {
    ResizeBrowser(*active);
    ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
  }
  EnsureActiveTabVisible();
  UpdateControls();
  InvalidateRect(window_, nullptr, FALSE);
  ScheduleSessionSave();
}

void BrowserWindow::MoveTab(size_t from, size_t to) {
  if (from >= tabs_.size() || to >= tabs_.size() || from == to) return;
  // Pinned tabs are the leading run of the strip, so a tab may only move
  // inside its own group. Dragging across the boundary clamps to it.
  const size_t pinned = PinnedCount();
  if (tabs_[from].pinned) {
    if (pinned == 0) return;
    to = std::min(to, pinned - 1);
  } else {
    to = std::max(to, pinned);
  }
  if (from == to) return;

  Tab moved = std::move(tabs_[from]);
  tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(from));
  tabs_.insert(tabs_.begin() + static_cast<ptrdiff_t>(to), std::move(moved));

  // The active tab is tracked by index, so it follows the same shift.
  if (active_tab_ == from) {
    active_tab_ = to;
  } else if (from < active_tab_ && to >= active_tab_) {
    --active_tab_;
  } else if (from > active_tab_ && to <= active_tab_) {
    ++active_tab_;
  }
  InvalidateRect(window_, nullptr, FALSE);
  ScheduleSessionSave();
}

void BrowserWindow::TogglePin(size_t index) {
  if (index >= tabs_.size()) return;
  const size_t pinned = PinnedCount();
  const bool was_pinned = tabs_[index].pinned;
  tabs_[index].pinned = !was_pinned;
  // Pinning moves the tab to the end of the pinned run; unpinning moves it to
  // the start of the rest, which is what every mainstream browser does.
  const size_t destination = was_pinned ? pinned - 1 : pinned;
  if (index != destination) {
    Tab moved = std::move(tabs_[index]);
    tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(index));
    tabs_.insert(tabs_.begin() + static_cast<ptrdiff_t>(destination),
                 std::move(moved));
    if (active_tab_ == index) {
      active_tab_ = destination;
    } else if (index < active_tab_ && destination >= active_tab_) {
      --active_tab_;
    } else if (index > active_tab_ && destination <= active_tab_) {
      ++active_tab_;
    }
  }
  Layout();
  EnsureActiveTabVisible();
  InvalidateRect(window_, nullptr, FALSE);
  ScheduleSessionSave();
}

void BrowserWindow::ToggleMute(size_t index) {
  if (index >= tabs_.size()) return;
  auto browser = tabs_[index].client ? tabs_[index].client->browser() : nullptr;
  if (!browser) return;
  const bool muted = !tabs_[index].muted;
  browser->GetHost()->SetAudioMuted(muted);
  tabs_[index].muted = muted;
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::DuplicateTab(size_t index) {
  if (index >= tabs_.size()) return;
  // The duplicate goes through the same policy check as any other navigation,
  // so a tab showing a blocked page cannot be cloned into an allowed one.
  const std::string url = WideToUtf8(tabs_[index].url);
  const auto decision = mtk::policy::EvaluateNavigation(url);
  if (!decision.allow || (!decision.url.starts_with("https://") &&
                          !decision.url.starts_with("http://"))) {
    CreateTab(kHomeUrl);
    return;
  }
  CreateTab(decision.url);
}

void BrowserWindow::CloseOtherTabs(size_t index) {
  if (index >= tabs_.size()) return;
  // Walk from the end so each removal cannot shift an index still to come.
  for (size_t position = tabs_.size(); position-- > 0;) {
    if (position == index || tabs_[position].pinned) continue;
    CloseTab(position);
    if (index > position) --index;
  }
}

void BrowserWindow::CloseTabsToTheRight(size_t index) {
  if (index >= tabs_.size()) return;
  for (size_t position = tabs_.size(); position-- > index + 1;) {
    if (tabs_[position].pinned) continue;
    CloseTab(position);
  }
}

void BrowserWindow::BeginTabDrag(size_t index, POINT point) {
  if (index >= tabs_.size()) return;
  drag_candidate_ = index;
  drag_origin_ = point;
  dragging_ = false;
  SetCapture(window_);
}

void BrowserWindow::UpdateTabDrag(POINT point) {
  if (drag_candidate_ >= tabs_.size()) return;
  if (!dragging_) {
    if (std::abs(point.x - drag_origin_.x) < GetSystemMetrics(SM_CXDRAG) &&
        std::abs(point.y - drag_origin_.y) < GetSystemMetrics(SM_CYDRAG)) {
      return;
    }
    dragging_ = true;
  }
  RECT client{};
  GetClientRect(window_, &client);
  const size_t pinned = PinnedCount();
  for (size_t index = 0; index < tabs_.size(); ++index) {
    const RECT rect =
        TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_);
    // Only swap once the pointer is past a slot's midpoint, so a tab does not
    // oscillate between two positions.
    const int middle = (rect.left + rect.right) / 2;
    if (index < drag_candidate_ && point.x < middle) {
      MoveTab(drag_candidate_, index);
      drag_candidate_ = index;
      return;
    }
    if (index > drag_candidate_ && point.x > middle) {
      MoveTab(drag_candidate_, index);
      drag_candidate_ = index;
      return;
    }
  }
}

void BrowserWindow::EndTabDrag() {
  const bool was_dragging = dragging_;
  const size_t candidate = drag_candidate_;
  drag_candidate_ = static_cast<size_t>(-1);
  dragging_ = false;
  if (GetCapture() == window_) ReleaseCapture();
  if (was_dragging && candidate < tabs_.size() && tabs_.size() > 1) {
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
      POINT client_pt = cursor;
      ScreenToClient(window_, &client_pt);
      if (client_pt.y > TabBarHeight() + Scaled(48) || client_pt.y < -Scaled(36)) {
        DetachTabToNewWindow(candidate);
        return;
      }
    }
  }
  if (!was_dragging && candidate < tabs_.size()) SelectTab(candidate);
}

void BrowserWindow::ShowTabMenu(size_t index, POINT screen) {
  if (index >= tabs_.size()) return;
  HMENU popup = CreatePopupMenu();
  if (!popup) return;
  const bool pinned = tabs_[index].pinned;
  const bool muted = tabs_[index].muted;
  using Text = mtk::strings::Id;
  const auto label = [](Text id, const wchar_t* accelerator = nullptr) {
    std::wstring text = mtk::strings::Get(id);
    if (accelerator) text += accelerator;
    return text;
  };
  AppendMenuW(popup, MF_STRING, kTabMenuDuplicate,
              label(Text::kDuplicateTab, L"\tAlt+U").c_str());
  AppendMenuW(popup, MF_STRING, kTabMenuPin,
              label(pinned ? Text::kUnpinTab : Text::kPinTab,
                    L"\tAlt+Shift+P")
                  .c_str());
  AppendMenuW(popup, MF_STRING, kTabMenuMute,
              label(muted ? Text::kUnmuteSite : Text::kMuteSite, L"\tAlt+M")
                  .c_str());
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(popup, MF_STRING, kTabMenuClose,
              label(Text::kCloseTab, L"\tAlt+X").c_str());
  AppendMenuW(popup, MF_STRING | (tabs_.size() > 1 ? 0 : MF_GRAYED),
              kTabMenuCloseOthers, label(Text::kCloseOtherTabs).c_str());
  AppendMenuW(popup,
              MF_STRING | (index + 1 < tabs_.size() ? 0 : MF_GRAYED),
              kTabMenuCloseRight, label(Text::kCloseTabsToRight).c_str());
  const int choice = static_cast<int>(TrackPopupMenuEx(
      popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
      screen.x, screen.y, window_, nullptr));
  DestroyMenu(popup);
  switch (choice) {
    case kTabMenuDuplicate: DuplicateTab(index); break;
    case kTabMenuPin: TogglePin(index); break;
    case kTabMenuMute: ToggleMute(index); break;
    case kTabMenuClose: CloseTab(index); break;
    case kTabMenuCloseOthers: CloseOtherTabs(index); break;
    case kTabMenuCloseRight: CloseTabsToTheRight(index); break;
    default: break;
  }
}


bool BrowserWindow::ReopenClosedTabAt(size_t recent_index) {
  if (closed_tab_urls_.empty() || recent_index >= closed_tab_urls_.size()) {
    return false;
  }
  const size_t pos = closed_tab_urls_.size() - 1 - recent_index;
  const std::string url = closed_tab_urls_[pos];
  closed_tab_urls_.erase(closed_tab_urls_.begin() + static_cast<ptrdiff_t>(pos));
  const auto decision = mtk::policy::EvaluateNavigation(url);
  if (!decision.allow) return false;
  return CreateTab(decision.url);
}

bool BrowserWindow::DetachTabToNewWindow(size_t index) {
  if (index >= tabs_.size() || tabs_.size() <= 1) return false;
  const std::string url = WideToUtf8(tabs_[index].url);
  const auto decision = mtk::policy::EvaluateNavigation(url);
  const std::string target_url =
      (decision.allow && (decision.url.starts_with("https://") ||
                          decision.url.starts_with("http://") ||
                          mtk::home::IsHomeUrl(decision.url)))
          ? decision.url
          : kHomeUrl;
  CloseTab(index);
  OpenNewWindow(target_url, private_session_);
  return true;
}

bool BrowserWindow::DiscardTab(size_t index) {
  if (index >= tabs_.size()) return false;
  Tab& tab = tabs_[index];
  const bool is_active = (index == active_tab_);
  const bool is_split = (index == split_tab_);
  if (!mtk::memory::IsEligibleForDiscard(
          tab.ToMemoryState(is_active, is_split), GetTickCount64(), 0)) {
    return false;
  }
  tab.discarded = true;
  if (tab.client && tab.client->browser()) {
    tab.client->browser()->GetHost()->WasHidden(true);
    ShowWindow(tab.client->browser()->GetHost()->GetWindowHandle(), SW_HIDE);
  }
  InvalidateRect(window_, nullptr, FALSE);
  return true;
}

bool BrowserWindow::ReviveTab(size_t index) {
  if (index >= tabs_.size()) return false;
  Tab& tab = tabs_[index];
  if (!tab.discarded) return false;
  tab.discarded = false;
  tab.last_active_tick = GetTickCount64();
  if (tab.client && tab.client->browser()) {
    if (index == active_tab_ || index == split_tab_) {
      tab.client->browser()->GetHost()->WasHidden(false);
      ShowWindow(tab.client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
    }
  }
  InvalidateRect(window_, nullptr, FALSE);
  return true;
}

void BrowserWindow::CheckMemorySaver() {
  if (!mtk::preferences::Store::Get().values().memory_saver) return;
  const uint64_t now = GetTickCount64();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool is_active = (i == active_tab_);
    const bool is_split = (i == split_tab_);
    if (mtk::memory::IsEligibleForDiscard(
            tabs_[i].ToMemoryState(is_active, is_split), now)) {
      DiscardTab(i);
    }
  }
}

void BrowserWindow::UpdateTabTooltip(POINT screen_point) {
  if (!window_ || full_screen_ || app_mode_) {
    mtk::tab_preview::Hide();
    return;
  }
  POINT client_pt = screen_point;
  ScreenToClient(window_, &client_pt);
  if (hovered_tab_ >= tabs_.size() || hovered_tab_close_ ||
      client_pt.y < 0 || client_pt.y >= TabBarHeight()) {
    mtk::tab_preview::Hide();
    return;
  }

  RECT client{};
  GetClientRect(window_, &client);
  const RECT tab_rc = TabRect(hovered_tab_, client.right, tabs_.size(),
                              PinnedCount(), tab_scroll_, IsZoomed(window_));
  POINT tl{tab_rc.left, tab_rc.top};
  POINT br{tab_rc.right, tab_rc.bottom};
  ClientToScreen(window_, &tl);
  ClientToScreen(window_, &br);
  const RECT screen_tab_rc{tl.x, tl.y, br.x, br.y};

  const Tab& tab = tabs_[hovered_tab_];
  mtk::tab_preview::PreviewCardData card;
  card.title = tab.title;
  card.url = WideToUtf8(tab.url);
  card.group = tab.group;
  card.workspace = tab.workspace;
  card.is_https = card.url.starts_with("https://");
  card.is_internal = card.url.starts_with("mtk://");
  card.pinned = tab.pinned;
  card.muted = tab.muted;
  card.sleeping = tab.discarded;
  card.camera_active = tab.camera_active;
  card.microphone_active = tab.microphone_active;
  if (tab.client && tab.client->browser() && !tab.discarded) {
    card.browser_view = tab.client->browser()->GetHost()->GetWindowHandle();
  }
  mtk::tab_preview::Show(instance_, window_, screen_tab_rc, card, dpi_,
                         IsLightAppearance());
}

void BrowserWindow::SetActiveWorkspace(const std::wstring& name) {
  active_workspace_ = name;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (name.empty() || _wcsicmp(tabs_[i].workspace.c_str(), name.c_str()) == 0) {
      SelectTab(i);
      break;
    }
  }
  InvalidateRect(window_, nullptr, FALSE);
}

RECT BrowserWindow::SplitDividerRect() const {
  if (!IsSplitViewActive() || !window_) return {0, 0, 0, 0};
  RECT client{};
  GetClientRect(window_, &client);
  const int top = PageTop();
  const int total_w = std::max<int>(1, client.right - client.left);
  const int split_x = static_cast<int>(total_w * std::clamp(split_ratio_, 0.2f, 0.8f));
  const int half_div = Scaled(3);
  return {split_x - half_div, top, split_x + half_div, client.bottom};
}

bool BrowserWindow::IsOverSplitDivider(POINT pt) const {
  if (!IsSplitViewActive()) return false;
  const RECT div = SplitDividerRect();
  return PtInRect(&div, pt) != FALSE;
}

void BrowserWindow::PaintSplitDivider(HDC dc) {
  if (!IsSplitViewActive()) return;
  const RECT div = SplitDividerRect();
  const COLORREF color = split_hovered_ || split_dragging_ ? kAccentColor : kBorderColor;
  HBRUSH br = CreateSolidBrush(color);
  FillRect(dc, &div, br);
  DeleteObject(br);
}

void BrowserWindow::SetTabMemoryState(Tab& tab, bool hidden) {
  if (!tab.client || !tab.client->browser()) return;
  const bool saver_enabled = mtk::preferences::Store::Get().values().memory_saver;
  tab.client->browser()->GetHost()->WasHidden(hidden && saver_enabled && !tab.pinned);
}

void BrowserWindow::SyncTabVisibility() {
  if (split_tab_ >= tabs_.size() || split_tab_ == active_tab_) {
    split_tab_ = kNoTab;
    split_focus_pane_ = 0;
  }
  for (size_t index = 0; index < tabs_.size(); ++index) {
    Tab& tab = tabs_[index];
    if (!tab.client || !tab.client->browser()) continue;
    const bool visible = index == active_tab_ || index == split_tab_;
    SetTabMemoryState(tab, !visible);
    if (visible) {
      ResizeBrowser(tab);
      ShowWindow(tab.client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
    } else {
      ShowWindow(tab.client->browser()->GetHost()->GetWindowHandle(), SW_HIDE);
    }
  }
}

RECT BrowserWindow::TabStripRect() const {
  if (app_mode_ || !window_) return {0, 0, 0, 0};
  RECT client{};
  GetClientRect(window_, &client);
  return {0, 0, client.right - CaptionButtonWidth() * 3, TabBarHeight()};
}

bool BrowserWindow::InTabStrip(POINT point) const {
  if (app_mode_ || full_screen_ || !window_) return false;
  RECT client{};
  GetClientRect(window_, &client);
  return point.y >= 0 && point.y < TabBarHeight() && point.x < client.right - CaptionButtonWidth() * 3;
}


void BrowserWindow::ShowTabSearch() {
  std::vector<mtk::tabs::SearchEntry> entries;
  size_t active_choice = 0;
  struct Target {
    HWND window;
    CefRefPtr<BrowserClient> client;
  };
  std::vector<Target> targets;
  for (BrowserWindow* candidate : g_windows) {
    if (!candidate || candidate->private_session_ != private_session_) continue;
    for (size_t tab_index = 0; tab_index < candidate->tabs_.size(); ++tab_index) {
      const auto& tab = candidate->tabs_[tab_index];
      std::wstring title = tab.group.empty() ? tab.title
                                             : L"[" + tab.group + L"] " + tab.title;
      if (candidate != this && !private_session_) title = L"[other window] " + title;
      entries.push_back({std::move(title), tab.url});
      targets.push_back({candidate->window_, tab.client});
      if (candidate == this && tab_index == active_tab_) {
        active_choice = targets.size() - 1;
      }
    }
  }
  const ptrdiff_t choice =
      mtk::tabs::ShowSearch(instance_, window_, entries, active_choice);
  if (choice >= 0 && static_cast<size_t>(choice) < targets.size()) {
    const auto& target = targets[static_cast<size_t>(choice)];
    for (BrowserWindow* candidate : g_windows) {
      if (candidate->window_ == target.window) {
        SetForegroundWindow(candidate->window_);
        const size_t tab_index = candidate->FindTab(target.client.get());
        if (tab_index != kNoTab) candidate->SelectTab(tab_index);
        break;
      }
    }
  }
}

void BrowserWindow::ToggleVerticalTabs() {
  auto& prefs = mtk::preferences::Store::Get();
  auto values = prefs.values();
  values.vertical_tabs = !values.vertical_tabs;
  prefs.Save(values);
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::ToggleVerticalTabsCollapsed() {
  auto& prefs = mtk::preferences::Store::Get();
  auto values = prefs.values();
  values.vertical_tabs_collapsed = !values.vertical_tabs_collapsed;
  prefs.Save(values);
  InvalidateRect(window_, nullptr, FALSE);
}


void BrowserWindow::ToggleSplitView() {
  if (tabs_.size() < 2) return;
  if (IsSplitViewActive()) {
    split_tab_ = kNoTab;
  } else {
    split_tab_ = (active_tab_ + 1) % tabs_.size();
    split_ratio_ = 0.5f;
  }
  SyncTabVisibility();
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::ShowTabGroupsMenu() {
  std::set<std::wstring> groups;
  for (const auto& tab : tabs_) {
    if (!tab.group.empty()) groups.insert(tab.group);
  }
  if (groups.empty()) {
    MessageBoxW(window_, L"No tab groups have been created yet.\n\nUse 'Set tab group...' to name a group for the active tab.",
                L"Tab groups", MB_OK | MB_ICONINFORMATION);
    return;
  }
  std::wstring list = L"Active tab groups:\n";
  for (const auto& g : groups) list += L"\u2022 " + g + L"\n";
  MessageBoxW(window_, list.c_str(), L"Tab groups", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::ShowWorkspacesMenu() {
  std::set<std::wstring> workspaces;
  for (const auto& tab : tabs_) {
    if (!tab.workspace.empty()) workspaces.insert(tab.workspace);
  }
  if (workspaces.empty()) {
    MessageBoxW(window_, L"No workspaces have been configured yet.\n\nUse 'Set workspace...' to assign the active tab to a workspace.",
                L"Workspaces", MB_OK | MB_ICONINFORMATION);
    return;
  }
  std::wstring list = L"Configured workspaces:\n";
  for (const auto& w : workspaces) list += L"\u2022 " + w + L"\n";
  MessageBoxW(window_, list.c_str(), L"Workspaces", MB_OK | MB_ICONINFORMATION);
}

