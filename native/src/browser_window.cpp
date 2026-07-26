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
#include "policy.h"

namespace {

constexpr wchar_t kWindowClass[] = L"MTKBrowserWindow";
constexpr char kHomeUrl[] = "https://www.google.com/?safe=active";
constexpr int kTabBarHeight = 46;
constexpr int kToolbarHeight = 62;
constexpr int kChromeHeight = kTabBarHeight + kToolbarHeight;
constexpr int kProgressHeight = 3;
constexpr UINT_PTR kProgressTimer = 1;
constexpr UINT kFinalizeCloseMessage = WM_APP + 1;
constexpr INT_PTR kBackId = 1001;
constexpr INT_PTR kForwardId = 1002;
constexpr INT_PTR kReloadId = 1003;
constexpr INT_PTR kHomeId = 1004;
constexpr INT_PTR kAddressId = 1005;
constexpr INT_PTR kGoId = 1006;
constexpr INT_PTR kShieldId = 1007;
constexpr INT_PTR kMenuId = 1008;
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

BrowserWindow::BrowserWindow(HINSTANCE instance) : instance_(instance) {}

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

bool BrowserWindow::HandleShortcut(int virtual_key, bool control, bool alt,
                                   bool shift) {
  auto* tab = ActiveTab();
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
  go_ = CreateWindowW(L"BUTTON", L"Go", button_style, 0, 0, 0, 0, window_,
                      reinterpret_cast<HMENU>(kGoId), instance_, nullptr);
  for (HWND control : {back_, forward_, reload_, home_, shield_, menu_, address_, go_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
  }
  SendMessageW(address_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELPARAM(12, 12));
  SetWindowTheme(address_, L"DarkMode_CFD", nullptr);
  SetWindowSubclass(address_, AddressProc, 1,
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

void BrowserWindow::SelectTab(size_t index) {
  if (index >= tabs_.size() || index == active_tab_) return;
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

  const int right_space = 132;
  const int omnibox_width = std::max(120, width - x - right_space);
  MoveWindow(address_, x + 22, y + 8, omnibox_width - 34, 21, TRUE);
  MoveWindow(go_, width - 118, y, 56, button, TRUE);
  MoveWindow(menu_, width - 50, y, button, button, TRUE);
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
        case kMenuId: MessageBoxW(window_, L"Settings, history and subscription controls are coming in the next milestone.", L"MTK Browser", MB_OK); break;
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
  if (index == active_tab_) UpdateControls();
  else InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::OnBrowserLoadingChanged(BrowserClient* client, bool loading,
                                             bool can_go_back,
                                             bool can_go_forward) {
  const size_t index = FindTab(client);
  if (index == kNoTab) return;
  tabs_[index].loading = loading;
  tabs_[index].can_go_back = can_go_back;
  tabs_[index].can_go_forward = can_go_forward;
  if (index == active_tab_) UpdateControls();
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
