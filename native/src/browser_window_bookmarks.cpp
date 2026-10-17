#include "browser_window_internal.h"

void BrowserWindow::ToggleBookmark() {
  auto* tab = ActiveTab();
  if (!tab) return;
  const auto decision = mtk::policy::EvaluateNavigation(WideToUtf8(tab->url));
  if (!decision.allow || tab->url.starts_with(L"data:")) {
    MessageBoxW(window_, L"This page cannot be saved as a bookmark.",
                L"MTK Browser", MB_OK | MB_ICONWARNING);
    return;
  }
  auto& store = mtk::library::Store::Get();
  const bool bookmarked = store.IsBookmarked(decision.url);
  if (bookmarked) {
    if (!store.RemoveBookmark(decision.url) || !store.Flush()) {
      MessageBoxW(window_, L"MTK Browser could not remove the bookmark.",
                  L"MTK Browser", MB_OK | MB_ICONERROR);
      return;
    }
    ScheduleLibrarySave();
    InvalidateRect(window_, nullptr, FALSE);
    MessageBoxW(window_, L"Bookmark removed.", L"MTK Browser",
                MB_OK | MB_ICONINFORMATION);
    return;
  }

  std::wstring target_folder;
  const auto& folders = store.folders();
  if (!folders.empty()) {
    HMENU popup = CreatePopupMenu();
    if (popup) {
      AppendMenuW(popup, MF_STRING, 1, L"Save on Bookmarks Bar (Top level)");
      AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
      for (size_t i = 0; i < folders.size() && i < kMaxFolderMenuEntries; ++i) {
        std::wstring label = L"Folder: " + folders[i].name;
        AppendMenuW(popup, MF_STRING, static_cast<UINT_PTR>(2 + i), label.c_str());
      }
      POINT pt{};
      GetCursorPos(&pt);
      const int choice = static_cast<int>(TrackPopupMenuEx(
          popup, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, window_, nullptr));
      DestroyMenu(popup);
      if (choice <= 0) return;
      if (choice >= 2 && static_cast<size_t>(choice - 2) < folders.size()) {
        target_folder = folders[static_cast<size_t>(choice - 2)].name;
      }
    }
  }

  if (!store.SaveBookmarkAndFlush(decision.url, tab->title, target_folder)) {
    MessageBoxW(window_, L"MTK Browser could not save the bookmark.",
                L"MTK Browser", MB_OK | MB_ICONERROR);
    return;
  }
  ScheduleLibrarySave();
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::LayoutSuggestions() {
  if (!suggestions_ || !IsWindowVisible(suggestions_)) return;
  const int count = static_cast<int>(suggestion_urls_.size());
  if (count == 0) return;
  RECT box{};
  GetWindowRect(address_, &box);
  MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&box), 2);
  const int row = static_cast<int>(
      SendMessageW(suggestions_, LB_GETITEMHEIGHT, 0, 0));
  const int height = std::max(row, row * count) + Scaled(4);
  const int left = static_cast<int>(box.left) - Scaled(30);
  const int width =
      std::max(Scaled(220), static_cast<int>(box.right) - left + Scaled(24));
  MoveWindow(suggestions_, left, ChromeHeight(), width, height, TRUE);
}

void BrowserWindow::HideSuggestions() {
  if (!suggestions_) return;
  suggestion_urls_.clear();
  SendMessageW(suggestions_, LB_RESETCONTENT, 0, 0);
  ShowWindow(suggestions_, SW_HIDE);
}

void BrowserWindow::RefreshSuggestions() {
  if (!suggestions_ || suppress_suggestions_) return;
  const int length = GetWindowTextLengthW(address_);
  std::wstring input(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(address_, input.data(), length + 1);
  input.resize(static_cast<size_t>(length));

  std::wstring needle = input;
  std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
  if (needle.size() < 2) {
    HideSuggestions();
    return;
  }

  suggestion_urls_.clear();
  SendMessageW(suggestions_, LB_RESETCONTENT, 0, 0);
  // Bookmarks are offered before history because they were saved on purpose.
  const auto consider = [&](const std::vector<mtk::library::Entry>& entries,
                            const wchar_t* tag) {
    for (const auto& entry : entries) {
      if (suggestion_urls_.size() >= kMaxSuggestions) return;
      std::wstring url = Utf8ToWide(entry.url);
      std::wstring haystack = url + L" " + entry.title;
      std::transform(haystack.begin(), haystack.end(), haystack.begin(), towlower);
      if (haystack.find(needle) == std::wstring::npos) continue;
      if (std::find(suggestion_urls_.begin(), suggestion_urls_.end(),
                    entry.url) != suggestion_urls_.end()) {
        continue;
      }
      // Never suggest something the safety policy would refuse anyway.
      const auto decision = mtk::policy::EvaluateNavigation(entry.url);
      if (!decision.allow) continue;
      std::wstring label = tag;
      label += entry.title.empty() ? url : entry.title + L"  —  " + url;
      std::replace_if(label.begin(), label.end(), [](wchar_t character) {
        return character == L'\r' || character == L'\n' || character == L'\t';
      }, L' ');
      suggestion_urls_.push_back(decision.url);
      SendMessageW(suggestions_, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(label.c_str()));
    }
  };
  consider(mtk::library::Store::Get().bookmarks(), L"★  ");
  consider(mtk::library::Store::Get().history(), L"🕘  ");

  if (suggestion_urls_.empty()) {
    HideSuggestions();
    return;
  }
  SendMessageW(suggestions_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
  ShowWindow(suggestions_, SW_SHOWNOACTIVATE);
  SetWindowPos(suggestions_, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  LayoutSuggestions();
}

void BrowserWindow::MoveSuggestion(int delta) {
  if (!suggestions_ || !IsWindowVisible(suggestions_) ||
      suggestion_urls_.empty()) {
    return;
  }
  const int count = static_cast<int>(suggestion_urls_.size());
  const int current =
      static_cast<int>(SendMessageW(suggestions_, LB_GETCURSEL, 0, 0));
  int next = current < 0 ? (delta > 0 ? 0 : count - 1) : current + delta;
  next = std::clamp(next, 0, count - 1);
  SendMessageW(suggestions_, LB_SETCURSEL, static_cast<WPARAM>(next), 0);
}

bool BrowserWindow::AcceptSuggestion() {
  if (!suggestions_ || !IsWindowVisible(suggestions_)) return false;
  const int selected =
      static_cast<int>(SendMessageW(suggestions_, LB_GETCURSEL, 0, 0));
  if (selected < 0 || selected >= static_cast<int>(suggestion_urls_.size())) {
    return false;
  }
  const std::string url = suggestion_urls_[static_cast<size_t>(selected)];
  HideSuggestions();
  // The stored address is re-checked here: the list may have been built before
  // the policy or filter list changed.
  const auto decision = mtk::policy::EvaluateNavigation(url);
  if (!decision.allow) {
    MessageBoxW(window_, decision.reason.c_str(),
                L"MTK Browser blocked this link", MB_OK | MB_ICONWARNING);
    return true;
  }
  suppress_suggestions_ = true;
  SetWindowTextW(address_, Utf8ToWide(decision.url).c_str());
  suppress_suggestions_ = false;
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(decision.url);
  }
  return true;
}

void BrowserWindow::NavigateFromOmnibox() {
  HideSuggestions();
  const int length = GetWindowTextLengthW(address_);
  std::wstring input(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(address_, input.data(), length + 1);
  input.resize(static_cast<size_t>(length));
  const auto decision = mtk::policy::ResolveInput(WideToUtf8(input));
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    if (decision.allow) {
      tab->client->browser()->GetMainFrame()->LoadURL(decision.url);
    } else {
      MessageBoxW(window_, decision.reason.c_str(), L"MTK Browser blocked this request",
                  MB_OK | MB_ICONWARNING);
    }
  }
}

void BrowserWindow::PaintBookmarksBar(HDC dc, const RECT& client) {
  const int height = BookmarksBarHeight();
  if (height == 0) return;
  const int top = TabBarHeight() + ToolbarHeight();
  RECT bar{0, top, client.right, top + height};
  const bool is_light = IsLightAppearance();
  if (is_light) {
    HBRUSH bar_br = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &bar, bar_br);
    DeleteObject(bar_br);
  } else {
    FillGradient(dc, bar, kToolbarBottom, kToolbarBottom);
  }

  bookmark_bar_hits_.clear();
  bookmark_overflow_entries_.clear();
  const auto& store = mtk::library::Store::Get();
  const auto& bookmarks = store.bookmarks();
  const auto& folders = store.folders();
  SetBkMode(dc, TRANSPARENT);
  SelectObject(dc, tab_font_);

  if (bookmarks.empty() && folders.empty()) {
    RECT label{Scaled(14), top, client.right, top + height};
    SetTextColor(dc, kDisabledColor);
    DrawTextW(dc, L"No bookmarks yet. Ctrl+D saves the page you are on.", -1,
              &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    return;
  }

  int x = Scaled(10);
  const int overflow_width = Scaled(32);
  // Once a button would not leave room for the overflow indicator either, every
  // remaining folder and bookmark goes into the overflow list instead of just
  // being dropped, so nothing on the bar becomes unreachable.
  bool overflowing = false;

  // One button per label, only as wide as the text needs and capped so a single
  // long name cannot take the whole bar.
  const auto draw_button = [&](const std::wstring& label, size_t index,
                               bool folder) {
    RECT measure{0, 0, Scaled(160), height};
    DrawTextW(dc, label.c_str(), -1, &measure,
              DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    const int width =
        std::clamp<int>(measure.right + Scaled(20), Scaled(48), Scaled(180));
    if (overflowing || x + width > client.right - Scaled(10) - overflow_width) {
      overflowing = true;
      bookmark_overflow_entries_.emplace_back(index, folder);
      return;
    }

    // Hover is tracked by position on the bar, which is what this button is
    // about to become.
    const bool hovered = hovered_bookmark_ == bookmark_bar_hits_.size();
    RECT button{x, top + Scaled(4), x + width, top + height - Scaled(4)};
    if (hovered) FillRounded(dc, button, Scaled(8), kControlHoverColor);
    if (bookmark_dragging_ && bookmark_drop_target_ == bookmark_bar_hits_.size()) {
      StrokeRounded(dc, button, Scaled(8), kAccentColor, 2);
    }
    RECT text = button;
    text.left += Scaled(10);
    text.right -= Scaled(10);
    SetTextColor(dc, hovered ? kTextColor : kMutedColor);
    DrawTextW(dc, label.c_str(), -1, &text,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    bookmark_bar_hits_.push_back(BookmarkHit{button, index, folder, false});
    x += width + Scaled(6);
  };

  // Folders lead the bar, so the grouping stays visible as loose bookmarks are
  // added. The hover index is shared, so folders occupy the low range.
  for (size_t index = 0; index < folders.size(); ++index) {
    draw_button(folders[index].name + L"  ▾", index, true);
  }
  for (size_t index = 0; index < bookmarks.size(); ++index) {
    // A bookmark inside a folder is reached through that folder's popup.
    if (!bookmarks[index].folder.empty()) continue;
    draw_button(bookmarks[index].title, index, false);
  }

  if (!bookmark_overflow_entries_.empty()) {
    const bool hovered = hovered_bookmark_ == bookmark_bar_hits_.size();
    RECT button{x, top + Scaled(4), x + overflow_width, top + height - Scaled(4)};
    if (hovered) FillRounded(dc, button, Scaled(8), kControlHoverColor);
    if (bookmark_dragging_ && bookmark_drop_target_ == bookmark_bar_hits_.size()) {
      StrokeRounded(dc, button, Scaled(8), kAccentColor, 2);
    }
    SetTextColor(dc, hovered ? kTextColor : kMutedColor);
    DrawTextW(dc, L"\u00BB", -1, &button,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    bookmark_bar_hits_.push_back(
        BookmarkHit{button, static_cast<size_t>(-1), false, true});
  }
}

void BrowserWindow::ShowBookmarkFolderMenu(size_t folder_index, POINT screen) {
  const auto& store = mtk::library::Store::Get();
  if (folder_index >= store.folders().size()) return;
  const std::wstring name = store.folders()[folder_index].name;

  folder_menu_urls_.clear();
  HMENU popup = CreatePopupMenu();
  if (!popup) return;
  for (const auto& bookmark : store.bookmarks()) {
    if (folder_menu_urls_.size() >= kMaxFolderMenuEntries) break;
    if (_wcsicmp(bookmark.folder.c_str(), name.c_str()) != 0) continue;
    std::wstring label = bookmark.title.empty()
                             ? Utf8ToWide(bookmark.url)
                             : bookmark.title;
    // Ampersands in a title would otherwise be read as menu accelerators.
    for (size_t at = label.find(L'&'); at != std::wstring::npos;
         at = label.find(L'&', at + 2)) {
      label.insert(at, 1, L'&');
    }
    AppendMenuW(popup, MF_STRING,
                static_cast<UINT_PTR>(IDM_BOOKMARK_OPEN_FIRST +
                                      folder_menu_urls_.size()),
                label.c_str());
    folder_menu_urls_.push_back(bookmark.url);
  }
  if (folder_menu_urls_.empty()) {
    AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, L"(empty)");
  }
  TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0,
                 window_, nullptr);
  DestroyMenu(popup);
}

void BrowserWindow::ShowBookmarksOverflowMenu(POINT screen) {
  const auto& store = mtk::library::Store::Get();
  folder_menu_urls_.clear();
  HMENU popup = CreatePopupMenu();
  if (!popup) return;

  const auto escape_ampersands = [](std::wstring label) {
    for (size_t at = label.find(L'&'); at != std::wstring::npos;
         at = label.find(L'&', at + 2)) {
      label.insert(at, 1, L'&');
    }
    return label;
  };

  // Command ids double up as an index into bookmark_overflow_entries_ itself,
  // since that vector already carries exactly what each row needs: which kind
  // of entry it is and its index into the matching store list. A folder row
  // opens its own submenu of that folder's bookmarks, in one Chinese-menu-box
  // nested popup rather than a second click through the bar.
  for (size_t position = 0; position < bookmark_overflow_entries_.size() &&
                            position < kMaxFolderMenuEntries;
       ++position) {
    const auto [index, is_folder] = bookmark_overflow_entries_[position];
    if (is_folder) {
      if (index >= store.folders().size()) continue;
      const std::wstring& name = store.folders()[index].name;
      HMENU submenu = CreatePopupMenu();
      if (!submenu) continue;
      bool any = false;
      for (const auto& bookmark : store.bookmarks()) {
        if (folder_menu_urls_.size() >= kMaxFolderMenuEntries) break;
        if (_wcsicmp(bookmark.folder.c_str(), name.c_str()) != 0) continue;
        const std::wstring label = escape_ampersands(
            bookmark.title.empty() ? Utf8ToWide(bookmark.url)
                                   : bookmark.title);
        AppendMenuW(submenu, MF_STRING,
                    static_cast<UINT_PTR>(IDM_BOOKMARK_OPEN_FIRST +
                                          folder_menu_urls_.size()),
                    label.c_str());
        folder_menu_urls_.push_back(bookmark.url);
        any = true;
      }
      if (!any) AppendMenuW(submenu, MF_STRING | MF_GRAYED, 0, L"(empty)");
      AppendMenuW(popup, MF_STRING | MF_POPUP,
                 reinterpret_cast<UINT_PTR>(submenu),
                 escape_ampersands(name + L"  ▾").c_str());
    } else {
      if (index >= store.bookmarks().size()) continue;
      const auto& bookmark = store.bookmarks()[index];
      const std::wstring label = escape_ampersands(
          bookmark.title.empty() ? Utf8ToWide(bookmark.url) : bookmark.title);
      if (folder_menu_urls_.size() >= kMaxFolderMenuEntries) continue;
      AppendMenuW(popup, MF_STRING,
                  static_cast<UINT_PTR>(IDM_BOOKMARK_OPEN_FIRST +
                                        folder_menu_urls_.size()),
                  label.c_str());
      folder_menu_urls_.push_back(bookmark.url);
    }
  }
  TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0,
                window_, nullptr);
  DestroyMenu(popup);
}


void BrowserWindow::ShowBookmarksBarMenu(const BookmarkHit* hit, POINT screen) {
  auto& store = mtk::library::Store::Get();
  context_bookmark_url_.clear();
  context_folder_name_.clear();
  folder_menu_urls_.clear();

  HMENU popup = CreatePopupMenu();
  if (!popup) return;

  if (hit && hit->folder && hit->index < store.folders().size()) {
    context_folder_name_ = store.folders()[hit->index].name;
    AppendMenuW(popup, MF_STRING, IDM_BOOKMARK_RENAME_FOLDER,
                L"Rename folder...");
    AppendMenuW(popup, MF_STRING, IDM_BOOKMARK_DELETE_FOLDER,
                L"Delete folder...");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  } else if (hit && !hit->folder && hit->index < store.bookmarks().size()) {
    const auto& bookmark = store.bookmarks()[hit->index];
    context_bookmark_url_ = bookmark.url;
    HMENU move = CreatePopupMenu();
    if (move) {
      AppendMenuW(move, MF_STRING |
                      (bookmark.folder.empty() ? MF_GRAYED : MF_ENABLED),
                  IDM_BOOKMARK_MOVE_ROOT, L"Top level");
      const auto& folders = store.folders();
      for (size_t index = 0;
           index < folders.size() && index < kMaxFolderMenuEntries; ++index) {
        const bool current =
            _wcsicmp(folders[index].name.c_str(), bookmark.folder.c_str()) == 0;
        AppendMenuW(move, MF_STRING | (current ? MF_GRAYED : MF_ENABLED),
                    static_cast<UINT_PTR>(IDM_BOOKMARK_MOVE_FIRST + index),
                    folders[index].name.c_str());
      }
      AppendMenuW(popup, MF_POPUP | (store.folders().empty() ? MF_GRAYED : 0),
                  reinterpret_cast<UINT_PTR>(move), L"Move to");
    }
    AppendMenuW(popup, MF_STRING | (hit->index > 0 ? MF_ENABLED : MF_GRAYED), IDM_BOOKMARK_MOVE_EARLIER, L"Move earlier");
    AppendMenuW(popup, MF_STRING | (hit->index + 1 < store.bookmarks().size() ? MF_ENABLED : MF_GRAYED), IDM_BOOKMARK_MOVE_LATER, L"Move later");
    AppendMenuW(popup, MF_STRING, IDM_BOOKMARK_REMOVE, L"Remove bookmark");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  }

  const bool room = store.folders().size() < mtk::library::kMaxFolders;
  AppendMenuW(popup, MF_STRING | (room ? MF_ENABLED : MF_GRAYED),
              IDM_BOOKMARK_NEW_FOLDER, L"New folder...");
  TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0,
                 window_, nullptr);
  DestroyMenu(popup);
}

void BrowserWindow::ToggleBookmarksBar() {
  mtk::preferences::Values values = mtk::preferences::Store::Get().values();
  values.show_bookmarks_bar = !values.show_bookmarks_bar;
  if (!mtk::preferences::Store::Get().Save(values)) {
    MessageBoxW(window_, L"MTK Browser could not save that preference.",
                L"MTK Browser", MB_OK | MB_ICONERROR);
    return;
  }
  g_bookmarks_bar = values.show_bookmarks_bar;
  bookmark_bar_hits_.clear();
  hovered_bookmark_ = static_cast<size_t>(-1);
  // The page area starts below the chrome, so every tab has to be resized.
  Layout();
  for (Tab& tab : tabs_) ResizeBrowser(tab);
  InvalidateRect(window_, nullptr, TRUE);
}


void BrowserWindow::ImportBookmarksFromBrowser() {
  const auto sources = mtk::bookmark_import::AvailableSources();
  if (sources.empty()) {
    MessageBoxW(window_,
                L"No Chrome, Edge or Brave bookmarks file was found for this "
                L"Windows user account.",
                L"Import bookmarks", MB_OK | MB_ICONINFORMATION);
    return;
  }

  // With more than one browser installed the user picks; with one, importing
  // still asks first, because it writes to their bookmarks.
  size_t chosen = 0;
  if (sources.size() > 1) {
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    for (size_t index = 0; index < sources.size(); ++index) {
      AppendMenuW(popup, MF_STRING, static_cast<UINT_PTR>(index + 1),
                  sources[index].browser.c_str());
    }
    POINT point{};
    GetCursorPos(&point);
    const int choice = static_cast<int>(TrackPopupMenuEx(
        popup, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, window_,
        nullptr));
    DestroyMenu(popup);
    if (choice <= 0) return;
    chosen = static_cast<size_t>(choice - 1);
  } else {
    const std::wstring question =
        L"Import bookmarks from " + sources[0].browser + L"?";
    if (MessageBoxW(window_, question.c_str(), L"Import bookmarks",
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
      return;
    }
  }

  size_t refused = 0;
  const auto entries = mtk::bookmark_import::Read(sources[chosen].path, &refused);
  if (entries.empty() && refused == 0) {
    MessageBoxW(window_,
                L"That bookmarks file could not be read, or it contained no "
                L"bookmarks.",
                L"Import bookmarks", MB_OK | MB_ICONWARNING);
    return;
  }
  auto& store = mtk::library::Store::Get();
  const size_t added = store.ImportBookmarks(entries);
  if (added > 0 && !store.Flush()) {
    MessageBoxW(window_, L"MTK Browser could not save the imported bookmarks.",
                L"Import bookmarks", MB_OK | MB_ICONERROR);
    return;
  }
  std::wstring report = L"Imported " + std::to_wstring(added) +
                        L" bookmarks from " + sources[chosen].browser + L".";
  if (const size_t duplicates = entries.size() - added; duplicates > 0) {
    report += L"\n" + std::to_wstring(duplicates) +
              L" were already saved here and were skipped.";
  }
  if (refused > 0) {
    report += L"\n" + std::to_wstring(refused) +
              L" were not imported because MTK Browser will not open them.";
  }
  MessageBoxW(window_, report.c_str(), L"Import bookmarks",
              MB_OK | MB_ICONINFORMATION);
  InvalidateRect(window_, nullptr, FALSE);
}


void BrowserWindow::BeginBookmarkDrag(size_t hit_index, POINT point) {
  if (hit_index >= bookmark_bar_hits_.size() ||
      bookmark_bar_hits_[hit_index].overflow) {
    return;
  }
  bookmark_drag_index_ = hit_index;
  bookmark_drop_target_ = static_cast<size_t>(-1);
  bookmark_drag_origin_ = point;
  bookmark_dragging_ = false;
  SetCapture(window_);
}

void BrowserWindow::UpdateBookmarkDrag(POINT point) {
  if (bookmark_drag_index_ >= bookmark_bar_hits_.size()) return;
  if (!bookmark_dragging_) {
    if (std::abs(point.x - bookmark_drag_origin_.x) <
            GetSystemMetrics(SM_CXDRAG) &&
        std::abs(point.y - bookmark_drag_origin_.y) <
            GetSystemMetrics(SM_CYDRAG)) {
      return;
    }
    bookmark_dragging_ = true;
  }

  size_t new_target = static_cast<size_t>(-1);
  for (size_t i = 0; i < bookmark_bar_hits_.size(); ++i) {
    if (bookmark_bar_hits_[i].overflow) continue;
    if (PtInRect(&bookmark_bar_hits_[i].bounds, point)) {
      new_target = i;
      break;
    }
  }
  if (new_target != bookmark_drop_target_) {
    bookmark_drop_target_ = new_target;
    const int top = TabBarHeight() + ToolbarHeight();
    RECT client{};
    GetClientRect(window_, &client);
    RECT bar{0, top, client.right, top + BookmarksBarHeight()};
    InvalidateRect(window_, &bar, FALSE);
  }
}

void BrowserWindow::EndBookmarkDrag(POINT point) {
  const bool was_dragging = bookmark_dragging_;
  const size_t source_hit_idx = bookmark_drag_index_;
  const size_t target_hit_idx = bookmark_drop_target_;
  bookmark_drag_index_ = static_cast<size_t>(-1);
  bookmark_drop_target_ = static_cast<size_t>(-1);
  bookmark_dragging_ = false;
  if (GetCapture() == window_) ReleaseCapture();

  if (source_hit_idx >= bookmark_bar_hits_.size()) return;
  const BookmarkHit source = bookmark_bar_hits_[source_hit_idx];

  if (!was_dragging) {
    POINT screen{source.bounds.left, source.bounds.bottom};
    ClientToScreen(window_, &screen);
    if (source.overflow) {
      ShowBookmarksOverflowMenu(screen);
    } else if (source.folder) {
      ShowBookmarkFolderMenu(source.index, screen);
    } else {
      OpenLibraryEntry(source.index);
    }
    return;
  }

  auto& store = mtk::library::Store::Get();
  if (target_hit_idx < bookmark_bar_hits_.size() &&
      target_hit_idx != source_hit_idx) {
    const BookmarkHit target = bookmark_bar_hits_[target_hit_idx];
    if (source.folder && target.folder) {
      if (store.MoveFolder(source.index, target.index)) {
        ScheduleLibrarySave();
      }
    } else if (!source.folder && target.folder) {
      if (source.index < store.bookmarks().size() &&
          target.index < store.folders().size()) {
        if (store.SetBookmarkFolder(store.bookmarks()[source.index].url,
                                    store.folders()[target.index].name) &&
            store.Flush()) {
          ScheduleLibrarySave();
        }
      }
    } else if (!source.folder && !target.folder) {
      if (store.MoveBookmark(source.index, target.index)) {
        ScheduleLibrarySave();
      }
    }
  } else if (!source.folder && source.index < store.bookmarks().size()) {
    const int bar_top = TabBarHeight() + ToolbarHeight();
    if (point.y < bar_top - Scaled(16) ||
        point.y > bar_top + BookmarksBarHeight() + Scaled(16)) {
      const auto& bm = store.bookmarks()[source.index];
      mtk::dragdrop::StartBookmarkDrag(bm.url, bm.title);
    }
  }
  InvalidateRect(window_, nullptr, FALSE);
}
