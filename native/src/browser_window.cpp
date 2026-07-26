#include "browser_window.h"

#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "library_store.h"
#include "policy.h"
#include "preferences.h"
#include "resource.h"

namespace {

constexpr wchar_t kWindowClass[] = L"MTKBrowserWindow";
constexpr char kDefaultHomeUrl[] = "https://www.google.com/?safe=active";
std::string kHomeUrl = kDefaultHomeUrl;
constexpr int kTabBarHeight = 46;
constexpr int kToolbarHeight = 62;
constexpr int kChromeHeight = kTabBarHeight + kToolbarHeight;
constexpr int kProgressHeight = 3;
constexpr UINT_PTR kProgressTimer = 1;
constexpr UINT_PTR kLibrarySaveTimer = 2;
constexpr UINT kFinalizeCloseMessage = WM_APP + 1;
constexpr INT_PTR kBackId = 1001;
constexpr INT_PTR kForwardId = 1002;
constexpr INT_PTR kReloadId = 1003;
constexpr INT_PTR kHomeId = 1004;
constexpr INT_PTR kAddressId = 1005;
constexpr INT_PTR kGoId = 1006;
constexpr INT_PTR kShieldId = 1007;
constexpr INT_PTR kMenuId = 1008;
constexpr INT_PTR kFindId = 1009;
constexpr INT_PTR kFindPreviousId = 1010;
constexpr INT_PTR kFindNextId = 1011;
constexpr INT_PTR kFindCloseId = 1012;
constexpr size_t kNoTab = static_cast<size_t>(-1);

constexpr COLORREF kWindowColor = RGB(9, 12, 22);
constexpr COLORREF kTabBarTop = RGB(19, 23, 38);
constexpr COLORREF kTabBarBottom = RGB(14, 18, 30);
constexpr COLORREF kToolbarTop = RGB(27, 32, 50);
constexpr COLORREF kToolbarBottom = RGB(20, 25, 40);
constexpr COLORREF kActiveTabTop = RGB(41, 48, 74);
constexpr COLORREF kActiveTabBottom = RGB(28, 34, 54);
constexpr COLORREF kHoverTabColor = RGB(28, 34, 53);
constexpr COLORREF kControlColor = RGB(31, 37, 58);
constexpr COLORREF kControlHoverColor = RGB(45, 53, 82);
constexpr COLORREF kBorderColor = RGB(52, 62, 95);
constexpr COLORREF kAccentColor = RGB(124, 92, 255);
constexpr COLORREF kAccentSoftColor = RGB(96, 165, 250);
constexpr COLORREF kShieldColor = RGB(74, 222, 128);
constexpr COLORREF kTextColor = RGB(240, 244, 255);
constexpr COLORREF kMutedColor = RGB(150, 162, 190);
constexpr COLORREF kDisabledColor = RGB(84, 94, 120);

// Simple vertical gradient painted as scanlines; the chrome areas are small so
// this stays inexpensive while giving the shell a premium finish.
void FillGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
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

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
  HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1,
                                   rect.bottom + 1, radius, radius);
  HBRUSH brush = CreateSolidBrush(color);
  FillRgn(dc, region, brush);
  DeleteObject(brush);
  DeleteObject(region);
}

void StrokeRounded(HDC dc, const RECT& rect, int radius, COLORREF color,
                   int thickness) {
  HPEN pen = CreatePen(PS_SOLID, thickness, color);
  HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
  HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

void FillCircle(HDC dc, int center_x, int center_y, int radius, COLORREF color) {
  HRGN region = CreateEllipticRgn(center_x - radius, center_y - radius,
                                 center_x + radius, center_y + radius);
  HBRUSH brush = CreateSolidBrush(color);
  FillRgn(dc, region, brush);
  DeleteObject(brush);
  DeleteObject(region);
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring Utf8ToWide(const std::string& value) {
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

void RefreshHomeUrl() {
  const auto decision = mtk::policy::ResolveInput(
      WideToUtf8(mtk::preferences::Store::Get().values().home_page));
  kHomeUrl = decision.allow ? decision.url : kDefaultHomeUrl;
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                    LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      const auto& values = mtk::preferences::Store::Get().values();
      SetDlgItemTextW(dialog, IDC_SETTINGS_HOME_PAGE, values.home_page.c_str());
      HWND language = GetDlgItem(dialog, IDC_SETTINGS_LANGUAGE);
      SendMessageW(language, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"English (United States) and Hindi"));
      SendMessageW(language, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"Hindi and English (United States)"));
      const int choice = values.accept_language == "hi,en-US,en" ? 1 : 0;
      SendMessageW(language, CB_SETCURSEL, choice, 0);
      SetWindowTheme(language, L"Explorer", nullptr);
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        wchar_t home_page[2049]{};
        const int length = GetDlgItemTextW(
            dialog, IDC_SETTINGS_HOME_PAGE, home_page,
            static_cast<int>(sizeof(home_page) / sizeof(home_page[0])));
        if (length == 0 ||
            length >= static_cast<int>(sizeof(home_page) / sizeof(home_page[0]))) {
          MessageBoxW(dialog, L"Enter a home page or search address.",
                      L"MTK Browser Settings", MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        const auto decision = mtk::policy::ResolveInput(WideToUtf8(home_page));
        if (!decision.allow) {
          MessageBoxW(dialog, decision.reason.c_str(), L"Home page blocked",
                      MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        mtk::preferences::Values values = mtk::preferences::Store::Get().values();
        values.home_page = Utf8ToWide(decision.url);
        const int selected = static_cast<int>(SendMessageW(
            GetDlgItem(dialog, IDC_SETTINGS_LANGUAGE), CB_GETCURSEL, 0, 0));
        values.accept_language = selected == 1 ? "hi,en-US,en" : "en-US,en,hi";
        if (!mtk::preferences::Store::Get().Save(values)) {
          MessageBoxW(dialog, L"MTK Browser could not save these settings.",
                      L"MTK Browser Settings", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        RefreshHomeUrl();
        HWND owner = GetParent(dialog);
        EndDialog(dialog, IDOK);
        MessageBoxW(owner,
                    L"Saved. Home and new tabs use the new page. Website language "
                    L"preference applies after you restart MTK Browser.",
                    L"MTK Browser Settings", MB_OK | MB_ICONINFORMATION);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

const std::vector<mtk::library::Entry>& LibraryEntries(bool bookmarks) {
  return bookmarks ? mtk::library::Store::Get().bookmarks()
                   : mtk::library::Store::Get().history();
}

std::wstring LibraryDisplayText(const mtk::library::Entry& entry) {
  std::wstring text = entry.title.empty() ? Utf8ToWide(entry.url) : entry.title;
  std::replace_if(text.begin(), text.end(), [](wchar_t character) {
    return character == L'\r' || character == L'\n' || character == L'\t';
  }, L' ');
  if (text.size() > 180) text.resize(180);
  const std::wstring url = Utf8ToWide(entry.url);
  if (url != text) text += L"  —  " + url;
  return text;
}

void PopulateLibraryList(HWND dialog, bool bookmarks) {
  HWND list = GetDlgItem(dialog, IDC_LIBRARY_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  const auto& entries = LibraryEntries(bookmarks);
  for (size_t index = 0; index < entries.size(); ++index) {
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(LibraryDisplayText(entries[index]).c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row), index);
    }
  }
  if (!entries.empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
}

INT_PTR CALLBACK LibraryDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                   LPARAM l_param) {
  const bool bookmarks = GetWindowLongPtrW(dialog, GWLP_USERDATA) != 0;
  switch (message) {
    case WM_INITDIALOG: {
      const bool initial_bookmarks = l_param != 0;
      SetWindowLongPtrW(dialog, GWLP_USERDATA, l_param);
      SetWindowTextW(dialog, initial_bookmarks ? L"Bookmarks — MTK Browser"
                                                : L"History — MTK Browser");
      if (initial_bookmarks) {
        SetWindowTextW(GetDlgItem(dialog, IDC_LIBRARY_CLEAR),
                       L"Remove all");
      }
      PopulateLibraryList(dialog, initial_bookmarks);
      return TRUE;
    }
    case WM_COMMAND: {
      const WORD command = LOWORD(w_param);
      const WORD notification = HIWORD(w_param);
      if (command == IDC_LIBRARY_LIST && notification == LBN_DBLCLK) {
        SendMessageW(dialog, WM_COMMAND, IDC_LIBRARY_OPEN, 0);
        return TRUE;
      }
      const LRESULT row = SendMessageW(GetDlgItem(dialog, IDC_LIBRARY_LIST),
                                       LB_GETCURSEL, 0, 0);
      if (command == IDC_LIBRARY_OPEN) {
        if (row == LB_ERR) return TRUE;
        const LRESULT index = SendMessageW(GetDlgItem(dialog, IDC_LIBRARY_LIST),
            LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
        if (index != LB_ERR) EndDialog(dialog, index + 1);
        return TRUE;
      }
      if (command == IDC_LIBRARY_REMOVE) {
        if (row == LB_ERR) return TRUE;
        const LRESULT index = SendMessageW(GetDlgItem(dialog, IDC_LIBRARY_LIST),
            LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
        const bool removed = index != LB_ERR &&
            (bookmarks ? mtk::library::Store::Get().RemoveBookmarkAt(index)
                       : mtk::library::Store::Get().RemoveHistoryAt(index));
        if (removed && !mtk::library::Store::Get().Flush()) {
          MessageBoxW(dialog, L"MTK Browser could not save the library change.",
                      L"MTK Browser", MB_OK | MB_ICONERROR);
        }
        PopulateLibraryList(dialog, bookmarks);
        return TRUE;
      }
      if (command == IDC_LIBRARY_CLEAR) {
        const wchar_t* prompt = bookmarks ? L"Remove all saved bookmarks?"
                                          : L"Clear all browsing history?";
        if (MessageBoxW(dialog, prompt, L"MTK Browser", MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        const bool cleared = bookmarks ? mtk::library::Store::Get().ClearBookmarks()
                                       : mtk::library::Store::Get().ClearHistory();
        if (cleared && !mtk::library::Store::Get().Flush()) {
          MessageBoxW(dialog, L"MTK Browser could not save the library change.",
                      L"MTK Browser", MB_OK | MB_ICONERROR);
        }
        PopulateLibraryList(dialog, bookmarks);
        return TRUE;
      }
      if (command == IDCANCEL) {
        EndDialog(dialog, 0);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, 0);
      return TRUE;
  }
  return FALSE;
}

constexpr int kTabGap = 4;
constexpr int kTabStripInset = 10;

RECT TabRect(size_t index, int width, size_t count) {
  const int available = std::max(200, width - 96 - kTabStripInset);
  const int slot = std::clamp(available / static_cast<int>(std::max<size_t>(count, 1)),
                              132, 248);
  const int left = kTabStripInset + static_cast<int>(index) * slot;
  return {left, 7, left + slot - kTabGap, kTabBarHeight};
}

RECT TabCloseRect(const RECT& tab) {
  const int center_y = (tab.top + tab.bottom) / 2;
  return {tab.right - 30, center_y - 11, tab.right - 8, center_y + 11};
}

}  // namespace

BrowserWindow::BrowserWindow(HINSTANCE instance) : instance_(instance) {
  RefreshHomeUrl();
}

void BrowserWindow::ShowSettingsDialog() {
  DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SETTINGS), window_,
                  SettingsDialogProc, 0);
}

void BrowserWindow::ShowBrowserMenu() {
  HMENU popup = CreatePopupMenu();
  if (!popup) return;
  auto* tab = ActiveTab();
  const auto decision = tab ? mtk::policy::EvaluateNavigation(WideToUtf8(tab->url))
                             : mtk::policy::NavigationDecision{false, {}, L""};
  const bool can_bookmark = decision.allow && !tab->url.starts_with(L"data:");
  const bool bookmarked = can_bookmark &&
      mtk::library::Store::Get().IsBookmarked(decision.url);
  AppendMenuW(popup, MF_STRING | (can_bookmark ? MF_ENABLED : MF_GRAYED),
              IDM_ADD_BOOKMARK,
              bookmarked ? L"Remove bookmark" : L"Add bookmark");
  AppendMenuW(popup, MF_STRING, IDM_BOOKMARKS, L"Bookmarks");
  AppendMenuW(popup, MF_STRING, IDM_HISTORY, L"History");
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(popup, MF_STRING, IDM_SETTINGS, L"Settings");
  POINT point{};
  GetCursorPos(&point);
  const UINT command = TrackPopupMenu(
      popup, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
      point.x, point.y, 0, window_, nullptr);
  DestroyMenu(popup);
  if (command) PostMessageW(window_, WM_COMMAND, command, 0);
}

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
  const bool changed = bookmarked ? store.RemoveBookmark(decision.url)
      : store.AddBookmark(decision.url, tab->title);
  if (!changed || !store.Flush()) {
    MessageBoxW(window_, L"MTK Browser could not save the bookmark change.",
                L"MTK Browser", MB_OK | MB_ICONERROR);
    return;
  }
  MessageBoxW(window_, bookmarked ? L"Bookmark removed." : L"Bookmark saved.",
              L"MTK Browser", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::ShowLibraryDialog(bool bookmarks) {
  const INT_PTR selection = DialogBoxParamW(
      instance_, MAKEINTRESOURCEW(IDD_LIBRARY), window_, LibraryDialogProc,
      bookmarks ? 1 : 0);
  if (selection > 0) OpenLibraryEntry(bookmarks, static_cast<size_t>(selection - 1));
}

void BrowserWindow::OpenLibraryEntry(bool bookmarks, size_t index) {
  const auto& entries = bookmarks ? mtk::library::Store::Get().bookmarks()
                                  : mtk::library::Store::Get().history();
  if (index >= entries.size()) return;
  const auto decision = mtk::policy::EvaluateNavigation(entries[index].url);
  if (!decision.allow) {
    MessageBoxW(window_, decision.reason.c_str(), L"MTK Browser blocked this link",
                MB_OK | MB_ICONWARNING);
    return;
  }
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(decision.url);
  }
}

void BrowserWindow::ScheduleLibrarySave() {
  SetTimer(window_, kLibrarySaveTimer, 1500, nullptr);
}

BrowserWindow::~BrowserWindow() {
  for (HFONT font : {ui_font_, tab_font_, glyph_font_, badge_font_}) {
    if (font) DeleteObject(font);
  }
  if (edit_brush_) DeleteObject(edit_brush_);
}

bool BrowserWindow::Create(int show_command) {
  WNDCLASSW window_class{};
  window_class.hInstance = instance_;
  window_class.lpfnWndProc = WindowProc;
  window_class.lpszClassName = kWindowClass;
  window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
  window_class.hbrBackground = CreateSolidBrush(kWindowColor);
  RegisterClassW(&window_class);

  window_ = CreateWindowExW(0, kWindowClass, L"MTK Browser",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            1360, 860, nullptr, nullptr, instance_, this);
  if (!window_) return false;

  const BOOL dark = TRUE;
  DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
  ShowWindow(window_, show_command);
  UpdateWindow(window_);
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
  return self ? self->HandleMessage(message, w_param, l_param)
              : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK BrowserWindow::AddressProc(HWND window, UINT message,
                                             WPARAM w_param, LPARAM l_param,
                                             UINT_PTR, DWORD_PTR reference_data) {
  auto* self = reinterpret_cast<BrowserWindow*>(reference_data);
  if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && self) {
    if (w_param == VK_RETURN) {
      self->NavigateFromOmnibox();
      return 0;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (self->HandleShortcut(static_cast<int>(w_param), control, alt, shift)) {
      return 0;
    }
  }
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, AddressProc, 1);
  return DefSubclassProc(window, message, w_param, l_param);
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
  if (message == WM_NCDESTROY) RemoveWindowSubclass(window, FindProc, 2);
  return DefSubclassProc(window, message, w_param, l_param);
}

bool BrowserWindow::HandleShortcut(int virtual_key, bool control, bool alt,
                                   bool shift) {
  auto* tab = ActiveTab();
  if (!control && !alt && virtual_key == VK_ESCAPE && find_visible_) {
    HideFindBar();
    return true;
  }
  if (control && !alt && virtual_key == 'F') {
    ShowFindBar();
    return true;
  }
  if ((!control && !alt && virtual_key == VK_F3) ||
      (control && !alt && virtual_key == 'G')) {
    if (find_visible_) RunFind(!shift, true);
    else ShowFindBar();
    return true;
  }
  if (control && !alt &&
      (virtual_key == VK_OEM_PLUS || virtual_key == VK_ADD)) {
    ZoomActiveTab(CEF_ZOOM_COMMAND_IN);
    return true;
  }
  if (control && !alt &&
      (virtual_key == VK_OEM_MINUS || virtual_key == VK_SUBTRACT)) {
    ZoomActiveTab(CEF_ZOOM_COMMAND_OUT);
    return true;
  }
  if (control && !alt && virtual_key == '0') {
    ZoomActiveTab(CEF_ZOOM_COMMAND_RESET);
    return true;
  }
  if (control && !alt && virtual_key == 'L') {
    SetFocus(address_);
    SendMessageW(address_, EM_SETSEL, 0, -1);
    return true;
  }
  if (control && !alt && virtual_key == 'T') {
    CreateTab(kHomeUrl);
    return true;
  }
  if (control && !alt && virtual_key == 'W') {
    CloseTab(active_tab_);
    return true;
  }
  if (control && !alt && virtual_key == VK_TAB && !tabs_.empty()) {
    const size_t offset = shift ? tabs_.size() - 1 : 1;
    SelectTab((active_tab_ + offset) % tabs_.size());
    return true;
  }
  if (alt && !control && virtual_key == VK_LEFT) {
    if (tab && tab->client->browser() && tab->can_go_back) {
      tab->client->browser()->GoBack();
    }
    return true;
  }
  if (alt && !control && virtual_key == VK_RIGHT) {
    if (tab && tab->client->browser() && tab->can_go_forward) {
      tab->client->browser()->GoForward();
    }
    return true;
  }
  if ((!control && !alt && virtual_key == VK_F5) ||
      (control && !alt && virtual_key == 'R')) {
    if (tab && tab->client->browser()) {
      if (tab->loading) tab->client->browser()->StopLoad();
      else tab->client->browser()->Reload();
    }
    return true;
  }
  return false;
}

void BrowserWindow::CreateControls() {
  const auto make_font = [](int height, int weight, const wchar_t* face) {
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
  };
  ui_font_ = make_font(-16, FW_MEDIUM, L"Segoe UI Variable");
  tab_font_ = make_font(-14, FW_SEMIBOLD, L"Segoe UI Variable");
  glyph_font_ = make_font(-17, FW_NORMAL, L"Segoe UI Symbol");
  badge_font_ = make_font(-11, FW_BOLD, L"Segoe UI");
  edit_brush_ = CreateSolidBrush(kControlColor);
  const DWORD button_style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
  back_ = CreateWindowW(L"BUTTON", L"←", button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kBackId), instance_, nullptr);
  forward_ = CreateWindowW(L"BUTTON", L"→", button_style, 0, 0, 0, 0, window_,
                           reinterpret_cast<HMENU>(kForwardId), instance_, nullptr);
  reload_ = CreateWindowW(L"BUTTON", L"↻", button_style, 0, 0, 0, 0, window_,
                          reinterpret_cast<HMENU>(kReloadId), instance_, nullptr);
  home_ = CreateWindowW(L"BUTTON", L"⌂", button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kHomeId), instance_, nullptr);
  shield_ = CreateWindowW(L"BUTTON", L"◆", button_style, 0, 0, 0, 0, window_,
                          reinterpret_cast<HMENU>(kShieldId), instance_, nullptr);
  menu_ = CreateWindowW(L"BUTTON", L"⋮", button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kMenuId), instance_, nullptr);
  address_ = CreateWindowW(L"EDIT", L"Search or enter address",
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           0, 0, 0, 0, window_,
                           reinterpret_cast<HMENU>(kAddressId), instance_, nullptr);
  find_ = CreateWindowW(L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                        0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kFindId), instance_, nullptr);
  find_status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_RIGHT,
                               0, 0, 0, 0, window_, nullptr, instance_, nullptr);
  find_previous_ = CreateWindowW(L"BUTTON", L"‹", button_style & ~WS_VISIBLE,
                                 0, 0, 0, 0, window_,
                                 reinterpret_cast<HMENU>(kFindPreviousId), instance_, nullptr);
  find_next_ = CreateWindowW(L"BUTTON", L"›", button_style & ~WS_VISIBLE,
                             0, 0, 0, 0, window_,
                             reinterpret_cast<HMENU>(kFindNextId), instance_, nullptr);
  find_close_ = CreateWindowW(L"BUTTON", L"×", button_style & ~WS_VISIBLE,
                              0, 0, 0, 0, window_,
                              reinterpret_cast<HMENU>(kFindCloseId), instance_, nullptr);
  go_ = CreateWindowW(L"BUTTON", L"Go", button_style, 0, 0, 0, 0, window_,
                      reinterpret_cast<HMENU>(kGoId), instance_, nullptr);
  for (HWND control : {back_, forward_, reload_, home_, shield_, menu_, address_,
                       find_, find_status_, find_previous_, find_next_, find_close_, go_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
  }
  SendMessageW(address_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELPARAM(12, 12));
  SetWindowTheme(address_, L"DarkMode_CFD", nullptr);
  SetWindowTheme(find_, L"DarkMode_CFD", nullptr);
  SetWindowSubclass(address_, AddressProc, 1,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(find_, FindProc, 2,
                    reinterpret_cast<DWORD_PTR>(this));
}

void BrowserWindow::CreateTab(const std::string& initial_url) {
  if (!tabs_.empty()) {
    if (auto* active = ActiveTab(); active && active->client->browser()) {
      ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_HIDE);
    }
  }
  Tab tab;
  tab.client = new BrowserClient(this);
  tabs_.push_back(tab);
  active_tab_ = tabs_.size() - 1;

  RECT client{};
  GetClientRect(window_, &client);
  CefWindowInfo info;
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  info.SetAsChild(
      window_, CefRect(0, kChromeHeight, static_cast<int>(client.right),
                       std::max(1, static_cast<int>(client.bottom) -
                                       kChromeHeight)));
  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(info, tabs_.back().client, initial_url, settings,
                                nullptr, nullptr);
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::ShowFindBar() {
  if (!find_visible_) {
    find_visible_ = true;
    find_active_ = false;
    active_find_identifier_ = -1;
    SetWindowTextW(find_status_, L"Type to find");
    ShowWindow(address_, SW_HIDE);
    ShowWindow(go_, SW_HIDE);
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
  ShowWindow(go_, SW_SHOW);
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

void BrowserWindow::ZoomActiveTab(cef_zoom_command_t command) {
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    auto host = tab->client->browser()->GetHost();
    if (host->CanZoom(command)) host->Zoom(command);
  }
}

void BrowserWindow::SelectTab(size_t index) {
  if (index >= tabs_.size() || index == active_tab_) return;
  if (find_visible_) HideFindBar();
  if (auto* old = ActiveTab(); old && old->client->browser()) {
    ShowWindow(old->client->browser()->GetHost()->GetWindowHandle(), SW_HIDE);
  }
  active_tab_ = index;
  if (auto* active = ActiveTab(); active && active->client->browser()) {
    ResizeBrowser(*active);
    ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
    SetFocus(active->client->browser()->GetHost()->GetWindowHandle());
  }
  UpdateControls();
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::CloseTab(size_t index) {
  if (index >= tabs_.size()) return;
  if (index == active_tab_ && find_visible_) HideFindBar();
  if (tabs_.size() == 1) {
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return;
  }
  auto closing = tabs_[index].client;
  tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(index));
  if (active_tab_ >= tabs_.size()) active_tab_ = tabs_.size() - 1;
  else if (index < active_tab_) --active_tab_;
  if (closing->browser()) closing->browser()->GetHost()->CloseBrowser(true);
  if (auto* active = ActiveTab(); active && active->client->browser()) {
    ResizeBrowser(*active);
    ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
  }
  UpdateControls();
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::NavigateFromOmnibox() {
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

void BrowserWindow::Layout() {
  RECT client{};
  GetClientRect(window_, &client);
  const int width = client.right;
  const int button = 36;
  const int y = kTabBarHeight + (kToolbarHeight - kProgressHeight - button) / 2;
  int x = 12;
  for (HWND control : {back_, forward_, reload_, home_}) {
    MoveWindow(control, x, y, button, button, TRUE);
    x += button + 4;
  }
  MoveWindow(shield_, x + 6, y, button + 6, button, TRUE);
  x += button + 20;

  MoveWindow(menu_, width - 50, y, button, button, TRUE);
  if (find_visible_) {
    const int status_x = width - 248;
    MoveWindow(find_, x + 22, y + 8,
               std::max(120, status_x - x - 30), 21, TRUE);
    MoveWindow(find_status_, status_x, y + 8, 82, 21, TRUE);
    MoveWindow(find_previous_, width - 158, y, 30, button, TRUE);
    MoveWindow(find_next_, width - 124, y, 30, button, TRUE);
    MoveWindow(find_close_, width - 90, y, 30, button, TRUE);
  } else {
    const int right_space = 132;
    const int omnibox_width = std::max(120, width - x - right_space);
    MoveWindow(address_, x + 22, y + 8, omnibox_width - 34, 21, TRUE);
    MoveWindow(go_, width - 118, y, 56, button, TRUE);
  }
  for (auto& tab : tabs_) ResizeBrowser(tab);
}

void BrowserWindow::ResizeBrowser(Tab& tab) {
  if (!tab.client || !tab.client->browser()) return;
  RECT client{};
  GetClientRect(window_, &client);
  HWND browser = tab.client->browser()->GetHost()->GetWindowHandle();
  MoveWindow(browser, 0, kChromeHeight, static_cast<int>(client.right),
             std::max(1, static_cast<int>(client.bottom) - kChromeHeight),
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
    SetWindowTextW(address_, tab->url.c_str());
  }
  const std::wstring title = tab && !tab->title.empty() ? tab->title : L"MTK Browser";
  SetWindowTextW(window_, (title + L" — MTK Browser").c_str());
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::PaintTabStrip(HDC dc, const RECT& client) {
  SetBkMode(dc, TRANSPARENT);
  for (size_t index = 0; index < tabs_.size(); ++index) {
    const RECT rect = TabRect(index, client.right, tabs_.size());
    const bool active = index == active_tab_;
    const bool hovered = index == hovered_tab_;

    if (active) {
      RECT body = rect;
      FillRounded(dc, body, 12, kActiveTabBottom);
      RECT sheen{rect.left, rect.top, rect.right, rect.top + (rect.bottom - rect.top) / 2};
      FillRounded(dc, sheen, 12, kActiveTabTop);
      RECT foot{rect.left, rect.bottom - 12, rect.right, rect.bottom};
      HBRUSH brush = CreateSolidBrush(kActiveTabBottom);
      FillRect(dc, &foot, brush);
      DeleteObject(brush);
      RECT accent{rect.left + 12, rect.top, rect.right - 12, rect.top + 2};
      FillRounded(dc, accent, 2, kAccentColor);
    } else if (hovered) {
      FillRounded(dc, rect, 12, kHoverTabColor);
    }

    const int dot_x = rect.left + 18;
    const int dot_y = (rect.top + rect.bottom) / 2;
    FillCircle(dc, dot_x, dot_y, 4,
               tabs_[index].loading ? kAccentSoftColor
                                    : (active ? kAccentColor : kDisabledColor));

    SelectObject(dc, tab_font_);
    SetTextColor(dc, active ? kTextColor : kMutedColor);
    RECT text = rect;
    text.left = dot_x + 12;
    text.right = rect.right - 34;
    DrawTextW(dc, tabs_[index].title.c_str(), -1, &text,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    const RECT close = TabCloseRect(rect);
    if (hovered && hovered_tab_close_) {
      FillRounded(dc, close, 11, kControlHoverColor);
    }
    SelectObject(dc, glyph_font_);
    SetTextColor(dc, hovered || active ? kTextColor : kDisabledColor);
    DrawTextW(dc, L"\u00D7", -1, const_cast<RECT*>(&close),
              DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  }

  const RECT last = TabRect(tabs_.size(), client.right, tabs_.size());
  RECT plus{last.left + 2, 11, last.left + 36, kTabBarHeight - 4};
  if (hovered_new_tab_) FillRounded(dc, plus, 10, kControlHoverColor);
  SelectObject(dc, glyph_font_);
  SetTextColor(dc, hovered_new_tab_ ? kTextColor : kMutedColor);
  DrawTextW(dc, L"+", -1, &plus, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void BrowserWindow::PaintOmniboxFrame(HDC dc) {
  RECT box{};
  GetWindowRect(address_, &box);
  MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&box), 2);
  InflateRect(&box, 12, 9);

  const bool focused = GetFocus() == address_;
  FillRounded(dc, box, 19, kControlColor);
  StrokeRounded(dc, box, 19, focused ? kAccentColor : kBorderColor, focused ? 2 : 1);

  auto* tab = ActiveTab();
  const bool secure = tab && tab->url.starts_with(L"https://");
  SelectObject(dc, glyph_font_);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, secure ? kShieldColor : kMutedColor);
  RECT lock{box.left + 12, box.top, box.left + 34, box.bottom};
  DrawTextW(dc, secure ? L"\U0001F512" : L"\u26A0", -1, &lock,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void BrowserWindow::PaintProgress(HDC dc, const RECT& client) {
  if (!progress_active_) return;
  const int track_y = kChromeHeight - kProgressHeight;
  RECT track{0, track_y, client.right, kChromeHeight};
  HBRUSH brush = CreateSolidBrush(kControlColor);
  FillRect(dc, &track, brush);
  DeleteObject(brush);

  const int span = std::max(120, static_cast<int>(client.right) / 4);
  const int travel = static_cast<int>(client.right) + span;
  const int offset = (progress_phase_ * 24) % std::max(1, travel);
  RECT bar{offset - span, track_y, offset, kChromeHeight};
  bar.left = std::max<LONG>(0, bar.left);
  bar.right = std::min<LONG>(client.right, bar.right);
  if (bar.right > bar.left) {
    HBRUSH accent = CreateSolidBrush(kAccentColor);
    FillRect(dc, &bar, accent);
    DeleteObject(accent);
  }
}

void BrowserWindow::Paint(HDC dc) {
  RECT client{};
  GetClientRect(window_, &client);
  RECT tab_bar{0, 0, client.right, kTabBarHeight};
  RECT toolbar{0, kTabBarHeight, client.right, kChromeHeight};
  FillGradient(dc, tab_bar, kTabBarTop, kTabBarBottom);
  FillGradient(dc, toolbar, kToolbarTop, kToolbarBottom);

  PaintTabStrip(dc, client);
  PaintOmniboxFrame(dc);
  PaintProgress(dc, client);
}

void BrowserWindow::DrawButton(const DRAWITEMSTRUCT& item) {
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
  RECT bounds = item.rcItem;

  FillGradient(item.hDC, bounds, kToolbarTop, kToolbarBottom);
  SetBkMode(item.hDC, TRANSPARENT);

  if (item.CtlID == kGoId) {
    RECT pill = bounds;
    InflateRect(&pill, -2, -6);
    FillRounded(item.hDC, pill, (pill.bottom - pill.top) / 2,
                pressed ? kAccentSoftColor : kAccentColor);
    SelectObject(item.hDC, ui_font_);
    SetTextColor(item.hDC, RGB(255, 255, 255));
    DrawTextW(item.hDC, L"Go", -1, &pill,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  if (!disabled && (hot || pressed)) {
    const int center_x = (bounds.left + bounds.right) / 2;
    const int center_y = (bounds.top + bounds.bottom) / 2;
    const int radius = std::min(bounds.right - bounds.left,
                                bounds.bottom - bounds.top) / 2 - 2;
    FillCircle(item.hDC, center_x, center_y, radius,
               pressed ? kBorderColor : kControlHoverColor);
  }

  wchar_t text[16]{};
  GetWindowTextW(item.hwndItem, text, 16);
  SelectObject(item.hDC, glyph_font_);
  SetTextColor(item.hDC, disabled ? kDisabledColor
                                  : (item.CtlID == kShieldId ? kShieldColor
                                                             : kTextColor));
  DrawTextW(item.hDC, text, -1, &bounds,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  if (item.CtlID == kShieldId) {
    const unsigned long blocked = BrowserClient::blocked_count();
    if (blocked > 0) {
      const std::wstring label =
          blocked > 999 ? L"999+" : std::to_wstring(blocked);
      SelectObject(item.hDC, badge_font_);
      RECT badge{bounds.right - 26, bounds.top + 2, bounds.right - 2,
                 bounds.top + 16};
      FillRounded(item.hDC, badge, 7, kAccentColor);
      SetTextColor(item.hDC, RGB(255, 255, 255));
      DrawTextW(item.hDC, label.c_str(), -1, &badge,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
}

void BrowserWindow::TrackHover(POINT point) {
  if (!mouse_tracking_) {
    TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
    TrackMouseEvent(&track);
    mouse_tracking_ = true;
  }

  RECT client{};
  GetClientRect(window_, &client);
  const size_t previous_tab = hovered_tab_;
  const bool previous_close = hovered_tab_close_;
  const bool previous_new_tab = hovered_new_tab_;

  hovered_tab_ = static_cast<size_t>(-1);
  hovered_tab_close_ = false;
  hovered_new_tab_ = false;

  if (point.y < kTabBarHeight) {
    for (size_t index = 0; index < tabs_.size(); ++index) {
      const RECT rect = TabRect(index, client.right, tabs_.size());
      if (PtInRect(&rect, point)) {
        hovered_tab_ = index;
        const RECT close = TabCloseRect(rect);
        hovered_tab_close_ = PtInRect(&close, point) != FALSE;
        break;
      }
    }
    if (hovered_tab_ == static_cast<size_t>(-1)) {
      const RECT last = TabRect(tabs_.size(), client.right, tabs_.size());
      RECT plus{last.left + 2, 11, last.left + 36, kTabBarHeight - 4};
      hovered_new_tab_ = PtInRect(&plus, point) != FALSE;
    }
  }

  if (previous_tab != hovered_tab_ || previous_close != hovered_tab_close_ ||
      previous_new_tab != hovered_new_tab_) {
    RECT strip{0, 0, client.right, kTabBarHeight};
    InvalidateRect(window_, &strip, FALSE);
  }
}

void BrowserWindow::ClearHover() {
  mouse_tracking_ = false;
  if (hovered_tab_ == static_cast<size_t>(-1) && !hovered_new_tab_) return;
  hovered_tab_ = static_cast<size_t>(-1);
  hovered_tab_close_ = false;
  hovered_new_tab_ = false;
  RECT client{};
  GetClientRect(window_, &client);
  RECT strip{0, 0, client.right, kTabBarHeight};
  InvalidateRect(window_, &strip, FALSE);
}

LRESULT BrowserWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_CREATE:
      CreateControls();
      CreateTab(kHomeUrl);
      Layout();
      return 0;
    case WM_SIZE:
      Layout();
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window_, &paint);
      RECT client{};
      GetClientRect(window_, &client);
      // Draw the chrome off-screen first so gradients never flicker.
      HDC buffer = CreateCompatibleDC(dc);
      HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, kChromeHeight);
      HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap));
      Paint(buffer);
      BitBlt(dc, 0, 0, client.right, kChromeHeight, buffer, 0, 0, SRCCOPY);
      SelectObject(buffer, old_bitmap);
      DeleteObject(bitmap);
      DeleteDC(buffer);
      EndPaint(window_, &paint);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      TrackHover(point);
      return 0;
    }
    case WM_MOUSELEAVE:
      ClearHover();
      return 0;
    case WM_TIMER:
      if (w_param == kProgressTimer) {
        ++progress_phase_;
        RECT client{};
        GetClientRect(window_, &client);
        RECT track{0, kChromeHeight - kProgressHeight, client.right,
                   kChromeHeight};
        InvalidateRect(window_, &track, FALSE);
      } else if (w_param == kLibrarySaveTimer) {
        KillTimer(window_, kLibrarySaveTimer);
        mtk::library::Store::Get().Flush();
      }
      return 0;
    case WM_DRAWITEM:
      DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
      return TRUE;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(w_param);
      SetTextColor(dc, kTextColor);
      SetBkColor(dc, kControlColor);
      return reinterpret_cast<LRESULT>(edit_brush_);
    }
    case WM_CTLCOLORSTATIC:
      if (reinterpret_cast<HWND>(l_param) == find_status_) {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetTextColor(dc, kMutedColor);
        SetBkColor(dc, kControlColor);
        return reinterpret_cast<LRESULT>(edit_brush_);
      }
      break;
    case WM_LBUTTONDOWN: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (point.y < kTabBarHeight) {
        RECT client{};
        GetClientRect(window_, &client);
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect = TabRect(index, client.right, tabs_.size());
          if (PtInRect(&rect, point)) {
            const RECT close = TabCloseRect(rect);
            if (PtInRect(&close, point)) CloseTab(index);
            else SelectTab(index);
            return 0;
          }
        }
        const RECT next = TabRect(tabs_.size(), client.right, tabs_.size());
        RECT plus{next.left, 0, next.left + 44, kTabBarHeight};
        if (PtInRect(&plus, point)) CreateTab(kHomeUrl);
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
        case kGoId: NavigateFromOmnibox(); break;
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
          const std::wstring summary =
              L"MTK Shields are active.\n\nBlocked on this session: " +
              std::to_wstring(BrowserClient::blocked_count()) +
              L"\n\n"
              L"\u2022 Ad, tracker and telemetry requests blocked\n"
              L"\u2022 Adult and piracy domains blocked\n"
              L"\u2022 Adult search terms blocked\n"
              L"\u2022 SafeSearch and HTTPS upgrade enforced\n"
              L"\u2022 Downloads, popups and risky permissions denied";
          MessageBoxW(window_, summary.c_str(), L"MTK Shields",
                      MB_OK | MB_ICONINFORMATION);
          break;
        }
        case kMenuId:
          ShowBrowserMenu();
          break;
        case IDM_ADD_BOOKMARK:
          ToggleBookmark();
          break;
        case IDM_BOOKMARKS:
          ShowLibraryDialog(true);
          break;
        case IDM_HISTORY:
          ShowLibraryDialog(false);
          break;
        case IDM_SETTINGS:
          ShowSettingsDialog();
          break;
      }
      return 0;
    }
    case WM_CLOSE: {
      if (closing_) return 0;
      closing_ = true;

      std::vector<CefRefPtr<CefBrowser>> closing_browsers;
      closing_browsers.reserve(tabs_.size());
      for (const auto& tab : tabs_) {
        if (auto browser = tab.client->browser()) {
          closing_browsers.push_back(browser);
        }
      }

      for (const auto& browser : closing_browsers) {
        browser->GetHost()->CloseBrowser(true);
      }
      if (tabs_.empty() && window_) {
        PostMessageW(window_, kFinalizeCloseMessage, 0, 0);
      }
      return 0;
    }
    case kFinalizeCloseMessage:
      if (closing_ && window_) DestroyWindow(window_);
      return 0;
    case WM_DESTROY:
      KillTimer(window_, kLibrarySaveTimer);
      mtk::library::Store::Get().Flush();
      window_ = nullptr;
      CefQuitMessageLoop();
      return 0;
  }
  return DefWindowProcW(window_, message, w_param, l_param);
}

void BrowserWindow::OnBrowserCreated(BrowserClient* client) {
  const size_t index = FindTab(client);
  if (index == kNoTab || !client->browser()) return;
  if (closing_) {
    client->browser()->GetHost()->CloseBrowser(true);
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

void BrowserWindow::OnBrowserCloseReady(BrowserClient* client) {
  const size_t index = FindTab(client);
  if (!closing_) {
    // Individual tab close or window.close(): tear down only the CEF child.
    if (client && client->browser()) {
      HWND child = client->browser()->GetHost()->GetWindowHandle();
      if (child) DestroyWindow(child);
    }
    return;
  }

  if (index != kNoTab) tabs_[index].close_ready = true;
  const bool all_ready = std::all_of(
      tabs_.begin(), tabs_.end(), [](const Tab& tab) {
        return !tab.client->browser() || tab.close_ready;
      });
  if (all_ready && window_) {
    PostMessageW(window_, kFinalizeCloseMessage, 0, 0);
  }
}

void BrowserWindow::OnBrowserClosed(BrowserClient* client) {
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

  if (tabs_.empty()) {
    if (!window_) return;
    if (closing_) {
      PostMessageW(window_, kFinalizeCloseMessage, 0, 0);
    } else {
      PostMessageW(window_, WM_CLOSE, 0, 0);
    }
    return;
  }
  if (closing_) return;

  if (auto* active = ActiveTab(); active && active->client->browser()) {
    ResizeBrowser(*active);
    ShowWindow(active->client->browser()->GetHost()->GetWindowHandle(), SW_SHOW);
  }
  UpdateControls();
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::OnBrowserAddressChanged(BrowserClient* client,
                                             const std::wstring& url) {
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  tabs_[index].url = url;
  if (index == active_tab_) UpdateControls();
}

void BrowserWindow::OnBrowserTitleChanged(BrowserClient* client,
                                           const std::wstring& title) {
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  tabs_[index].title = title.empty() ? L"New Tab" : title;
  const auto decision = mtk::policy::EvaluateNavigation(WideToUtf8(tabs_[index].url));
  if (decision.allow && mtk::library::Store::Get().UpdateHistoryTitle(
                            decision.url, tabs_[index].title)) {
    ScheduleLibrarySave();
  }
  if (index == active_tab_) UpdateControls();
  else InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::OnBrowserLoadingChanged(BrowserClient* client, bool loading,
                                             bool can_go_back,
                                             bool can_go_forward) {
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  if (loading && index == active_tab_ && find_visible_) HideFindBar();
  tabs_[index].loading = loading;
  tabs_[index].can_go_back = can_go_back;
  tabs_[index].can_go_forward = can_go_forward;
  if (index == active_tab_) UpdateControls();
}

void BrowserWindow::OnBrowserNavigationCompleted(BrowserClient* client,
                                                  const std::string& url) {
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
