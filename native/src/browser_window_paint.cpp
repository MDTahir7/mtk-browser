#include "browser_window_internal.h"

void BrowserWindow::PaintTabStrip(HDC dc, const RECT& client) {
  SetBkMode(dc, TRANSPARENT);
  const bool is_light = IsLightAppearance();
  const bool is_maximized = IsZoomed(window_);
  const size_t pinned_count = PinnedCount();
  for (size_t index = 0; index < tabs_.size(); ++index) {
    const RECT rect =
        TabRect(index, client.right, tabs_.size(), pinned_count, tab_scroll_, is_maximized);
    const bool active = index == active_tab_;
    const bool hovered = index == hovered_tab_;
    const bool pinned = tabs_[index].pinned;

    const int radius = Scaled(12);
    if (active) {
      if (is_light) {
        RECT body = rect;
        FillRounded(dc, body, radius, RGB(255, 255, 255));
        RECT foot{rect.left, rect.bottom - radius, rect.right, rect.bottom};
        HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &foot, brush);
        DeleteObject(brush);
      } else {
        RECT body = rect;
        FillRounded(dc, body, radius, kActiveTabBottom);
        RECT sheen{rect.left, rect.top, rect.right,
                   rect.top + (rect.bottom - rect.top) / 2};
        FillRounded(dc, sheen, radius, kActiveTabTop);
        RECT foot{rect.left, rect.bottom - radius, rect.right, rect.bottom};
        HBRUSH brush = CreateSolidBrush(kActiveTabBottom);
        FillRect(dc, &foot, brush);
        DeleteObject(brush);
        RECT accent{rect.left + radius, rect.top, rect.right - radius,
                    rect.top + Scaled(2)};
        FillRounded(dc, accent, Scaled(2), kAccentColor);
      }
    } else if (hovered) {
      FillRounded(dc, rect, radius, is_light ? RGB(236, 240, 248) : kHoverTabColor);
    }

    // A pinned tab has no room for a title, so its dot is centred and acts as
    // the whole tab, the way a favicon-only pinned tab reads elsewhere.
    const int dot_x = pinned ? (rect.left + rect.right) / 2 - Scaled(6)
                             : rect.left + Scaled(18);
    const int dot_y = (rect.top + rect.bottom) / 2;
    const COLORREF dot_color = tabs_[index].loading
        ? kAccentSoftColor
        : (active ? (is_light ? RGB(62, 117, 237) : kAccentColor)
                  : (is_light ? RGB(148, 163, 184) : kDisabledColor));
    FillCircle(dc, dot_x, dot_y, pinned ? Scaled(5) : Scaled(4), dot_color);

    if (!pinned) {
      SelectObject(dc, tab_font_);
      const COLORREF text_color = active
          ? (is_light ? RGB(25, 32, 51) : kTextColor)
          : (is_light ? RGB(100, 116, 139) : kMutedColor);
      SetTextColor(dc, text_color);
      RECT text = rect;
      text.left = dot_x + Scaled(12);
      text.right = rect.right - (tabs_[index].muted ? Scaled(62) : Scaled(38));
      DrawTextW(dc, tabs_[index].title.c_str(), -1, &text,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (tabs_[index].muted) {
      RECT mute = TabMuteRect(rect, pinned);
      SelectObject(dc, glyph_font_);
      SetTextColor(dc, active ? (is_light ? RGB(25, 32, 51) : kTextColor)
                              : (is_light ? RGB(100, 116, 139) : kMutedColor));
      DrawTextW(dc, L"\U0001F507", -1, &mute,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }

    if (!pinned) {
      const RECT close = TabCloseRect(rect, pinned);
      if (hovered && hovered_tab_close_) {
        FillRounded(dc, close, (close.bottom - close.top) / 2,
                    is_light ? RGB(226, 232, 240) : kControlHoverColor);
      }
      SelectObject(dc, glyph_font_);
      SetTextColor(dc, hovered || active ? (is_light ? RGB(71, 85, 105) : kTextColor)
                                         : (is_light ? RGB(148, 163, 184) : kDisabledColor));
      DrawTextW(dc, L"\u00D7", -1, const_cast<RECT*>(&close),
                DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
  }

  RECT plus =
      NewTabRect(client.right, tabs_.size(), pinned_count, tab_scroll_, is_maximized);
  if (hovered_new_tab_) {
    FillRounded(dc, plus, Scaled(10), is_light ? RGB(218, 224, 235) : kControlHoverColor);
  }
  SelectObject(dc, glyph_font_);
  SetTextColor(dc, hovered_new_tab_ ? (is_light ? RGB(15, 23, 42) : kTextColor)
                                    : (is_light ? RGB(71, 85, 105) : kMutedColor));
  DrawTextW(dc, L"+", -1, &plus, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void BrowserWindow::PaintOmniboxFrame(HDC dc) {
  HWND field = find_visible_ ? find_ : address_;
  RECT box{};
  GetWindowRect(field, &box);
  MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&box), 2);
  InflateRect(&box, Scaled(30), Scaled(9));

  const bool is_light = IsLightAppearance();
  const bool focused = GetFocus() == field;
  const int radius = (box.bottom - box.top) / 2;
  const COLORREF box_bg = is_light ? RGB(241, 245, 249) : kControlColor;
  const COLORREF box_border = focused
      ? (is_light ? RGB(168, 85, 247) : kAccentColor)
      : (is_light ? RGB(226, 232, 240) : kBorderColor);
  FillRounded(dc, box, radius, box_bg);
  StrokeRounded(dc, box, radius, box_border, focused ? Scaled(2) : 1);

  auto* tab = ActiveTab();
  const bool secure = tab && tab->url.starts_with(L"https://");
  SelectObject(dc, glyph_font_);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, find_visible_
      ? (is_light ? RGB(100, 116, 139) : kMutedColor)
      : (secure ? (is_light ? RGB(22, 163, 74) : kShieldColor)
                : (is_light ? RGB(100, 116, 139) : kMutedColor)));
  RECT leading{box.left + Scaled(6), box.top, box.left + Scaled(30), box.bottom};
  DrawTextW(dc,
            find_visible_ ? L"\U0001F50D"
                          : (secure ? L"\U0001F512" : L"\u26A0"),
            -1, &leading, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void BrowserWindow::PaintProgress(HDC dc, const RECT& client) {
  if (!progress_active_) return;
  const int track_y = ChromeHeight() - ProgressHeight();
  RECT track{0, track_y, client.right, ChromeHeight()};
  const bool is_light = IsLightAppearance();
  HBRUSH brush = CreateSolidBrush(is_light ? RGB(226, 232, 240) : kControlColor);
  FillRect(dc, &track, brush);
  DeleteObject(brush);

  const int span = std::max(120, static_cast<int>(client.right) / 4);
  const int travel = static_cast<int>(client.right) + span;
  const int offset = (progress_phase_ * Scaled(24)) % std::max(1, travel);
  RECT bar{offset - span, track_y, offset, ChromeHeight()};
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
  RECT tab_bar{0, 0, client.right, TabBarHeight()};
  RECT toolbar{0, TabBarHeight(), client.right,
               TabBarHeight() + ToolbarHeight()};
  const bool is_light = IsLightAppearance();
  if (private_session_) {
    FillGradient(dc, tab_bar, kPrivateBarTop, kPrivateBarBottom);
    FillGradient(dc, toolbar, kPrivateBarBottom, kPrivateBarBottom);
  } else if (is_light) {
    const COLORREF kLightTabBar = RGB(226, 232, 240);
    const COLORREF kLightToolbar = RGB(255, 255, 255);
    FillGradient(dc, tab_bar, kLightTabBar, kLightTabBar);
    FillGradient(dc, toolbar, kLightToolbar, kLightToolbar);
    RECT divider{0, toolbar.bottom - 1, client.right, toolbar.bottom};
    HBRUSH div_br = CreateSolidBrush(RGB(226, 232, 240));
    FillRect(dc, &divider, div_br);
    DeleteObject(div_br);
  } else {
    FillGradient(dc, tab_bar, kTabBarTop, kTabBarBottom);
    FillGradient(dc, toolbar, kToolbarTop, kToolbarBottom);
  }

  PaintTabStrip(dc, client);
  PaintBookmarksBar(dc, client);
  PaintOmniboxFrame(dc);
  PaintProgress(dc, client);
  PaintCaptionButtons(dc, client);
}

void BrowserWindow::DrawButton(const DRAWITEMSTRUCT& item) {
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
  RECT bounds = item.rcItem;
  const bool is_light = IsLightAppearance();

  if (is_light) {
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(item.hDC, &bounds, bg);
    DeleteObject(bg);
  } else {
    FillGradient(item.hDC, bounds, kToolbarTop, kToolbarBottom);
  }
  SetBkMode(item.hDC, TRANSPARENT);

  if (!disabled && (hot || pressed)) {
    const int center_x = (bounds.left + bounds.right) / 2;
    const int center_y = (bounds.top + bounds.bottom) / 2;
    const int radius = std::min(bounds.right - bounds.left,
                                bounds.bottom - bounds.top) / 2 - Scaled(2);
    COLORREF circle_color;
    if (is_light) {
      circle_color = pressed ? RGB(203, 213, 225) : RGB(226, 232, 240);
    } else {
      circle_color = pressed ? kBorderColor : kControlHoverColor;
    }
    FillCircle(item.hDC, center_x, center_y, radius, circle_color);
  }

  bool painted_icon = false;
  if (item.CtlID == kShieldId && shield_strip_.valid()) {
    RECT icon = bounds;
    const int inset = Scaled(6);
    InflateRect(&icon, -inset, -inset);
    painted_icon = shield_strip_.Draw(item.hDC, icon,
                                      shield_hovering_ ? shield_frame_ : 0);
  } else if (item.CtlID == kHomeId && home_strip_.valid()) {
    RECT icon = bounds;
    const int inset = Scaled(4);
    InflateRect(&icon, -inset, -inset);
    painted_icon = home_strip_.Draw(item.hDC, icon,
                                    home_hovering_ ? home_frame_ : 0);
  } else if (item.CtlID == kReloadId && reload_strip_.valid()) {
    RECT icon = bounds;
    const int inset = Scaled(5);
    InflateRect(&icon, -inset, -inset);
    painted_icon = reload_strip_.Draw(item.hDC, icon,
                                      reload_hovering_ ? reload_frame_ : 0);
  } else if (item.CtlID == kDownloadsId && download_strip_.valid()) {
    RECT icon = bounds;
    const int inset = Scaled(5);
    InflateRect(&icon, -inset, -inset);
    painted_icon = download_strip_.Draw(item.hDC, icon,
                                        download_hovering_ ? download_frame_ : 0);
  }

  if (!painted_icon) {
    const wchar_t* text = ButtonGlyph(item.CtlID);
    SelectObject(item.hDC, glyph_font_);
    COLORREF glyph_color;
    if (disabled) {
      glyph_color = is_light ? RGB(148, 163, 184) : kDisabledColor;
    } else if (item.CtlID == kShieldId) {
      glyph_color = is_light ? RGB(124, 58, 237) : kShieldColor;
    } else {
      glyph_color = is_light ? RGB(51, 65, 85) : kTextColor;
    }
    SetTextColor(item.hDC, glyph_color);
    DrawTextW(item.hDC, text, -1, &bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  unsigned long badge_value = 0;
  if (item.CtlID == kShieldId) {
    badge_value = BrowserClient::blocked_count();
  } else if (item.CtlID == kDownloadsId) {
    badge_value = static_cast<unsigned long>(
        mtk::downloads::Manager::Get().ActiveCount());
  }
  if (badge_value > 0) {
    const std::wstring label =
        badge_value > 999 ? L"999+" : std::to_wstring(badge_value);
    SelectObject(item.hDC, badge_font_);
    RECT badge{bounds.right - Scaled(26), bounds.top + Scaled(1),
               bounds.right - Scaled(2), bounds.top + Scaled(15)};
    FillRounded(item.hDC, badge, Scaled(7),
                item.CtlID == kDownloadsId ? kAccentSoftColor : kAccentColor);
    SetTextColor(item.hDC, RGB(255, 255, 255));
    DrawTextW(item.hDC, label.c_str(), -1, &badge,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void BrowserWindow::TrackHover(POINT point) {
  if (!mouse_tracking_) {
    TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
    TrackMouseEvent(&track);
    mouse_tracking_ = true;
  }

  const CaptionButton previous_caption = hovered_caption_button_;
  hovered_caption_button_ = HitTestCaptionButton(point);
  if (previous_caption != hovered_caption_button_) {
    RECT controls = WindowControlsRect();
    InvalidateRect(window_, &controls, FALSE);
  }

  RECT client{};
  GetClientRect(window_, &client);
  const size_t previous_tab = hovered_tab_;
  const bool previous_close = hovered_tab_close_;
  const bool previous_new_tab = hovered_new_tab_;

  hovered_tab_ = static_cast<size_t>(-1);
  hovered_tab_close_ = false;
  hovered_new_tab_ = false;

  if (point.y < TabBarHeight()) {
    const size_t pinned = PinnedCount();
    const bool is_maximized = IsZoomed(window_);
    for (size_t index = 0; index < tabs_.size(); ++index) {
      const RECT rect =
          TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_, is_maximized);
      if (PtInRect(&rect, point)) {
        hovered_tab_ = index;
        const RECT close = TabCloseRect(rect, tabs_[index].pinned);
        hovered_tab_close_ = PtInRect(&close, point) != FALSE;
        break;
      }
    }
    if (hovered_tab_ == static_cast<size_t>(-1)) {
      RECT plus =
          NewTabRect(client.right, tabs_.size(), pinned, tab_scroll_, is_maximized);
      hovered_new_tab_ = PtInRect(&plus, point) != FALSE;
    }
  }

  const size_t previous_bookmark = hovered_bookmark_;
  hovered_bookmark_ = static_cast<size_t>(-1);
  if (BookmarksBarHeight() > 0) {
    // The hovered button is identified by its position on the bar, because a
    // folder and a bookmark can both carry the same list index.
    for (size_t position = 0; position < bookmark_bar_hits_.size(); ++position) {
      if (PtInRect(&bookmark_bar_hits_[position].bounds, point)) {
        hovered_bookmark_ = position;
        break;
      }
    }
  }
  if (previous_bookmark != hovered_bookmark_) {
    const int top = TabBarHeight() + ToolbarHeight();
    RECT bar{0, top, client.right, top + BookmarksBarHeight()};
    InvalidateRect(window_, &bar, FALSE);
  }

  if (previous_tab != hovered_tab_ || previous_close != hovered_tab_close_ ||
      previous_new_tab != hovered_new_tab_) {
    RECT strip{0, 0, client.right, TabBarHeight()};
    InvalidateRect(window_, &strip, FALSE);
    POINT screen_pt = point;
    ClientToScreen(window_, &screen_pt);
    UpdateTabTooltip(screen_pt);
  }
}

void BrowserWindow::ClearHover() {
  mouse_tracking_ = false;
  mtk::tab_preview::Hide();
  if (hovered_caption_button_ != CaptionButton::kNone) {
    hovered_caption_button_ = CaptionButton::kNone;
    RECT controls = WindowControlsRect();
    InvalidateRect(window_, &controls, FALSE);
  }
  if (hovered_tab_ == static_cast<size_t>(-1) && !hovered_new_tab_) return;
  hovered_tab_ = static_cast<size_t>(-1);
  hovered_tab_close_ = false;
  hovered_new_tab_ = false;
  RECT client{};
  GetClientRect(window_, &client);
  RECT strip{0, 0, client.right, TabBarHeight()};
  InvalidateRect(window_, &strip, FALSE);
}

void BrowserWindow::TakeActivePageScreenshot() {
  CaptureActivePageScreenshot();
}

void BrowserWindow::CaptureActivePageScreenshot() {
  const auto* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return;
  const auto browser = tab->client->browser();
  HWND browser_hwnd = browser->GetHost()->GetWindowHandle();
  if (!browser_hwnd || !IsWindow(browser_hwnd)) return;

  RECT rect{};
  GetClientRect(browser_hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) return;

  POINT pt = {0, 0};
  ClientToScreen(browser_hwnd, &pt);

  HDC screen_dc = GetDC(nullptr);
  if (!screen_dc) return;
  HDC mem_dc = CreateCompatibleDC(screen_dc);
  if (!mem_dc) {
    ReleaseDC(nullptr, screen_dc);
    return;
  }
  HBITMAP hbm = CreateCompatibleBitmap(screen_dc, width, height);
  if (!hbm) {
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    return;
  }
  HGDIOBJ old_bm = SelectObject(mem_dc, hbm);
  BitBlt(mem_dc, 0, 0, width, height, screen_dc, pt.x, pt.y, SRCCOPY);
  SelectObject(mem_dc, old_bm);
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);

  mtk::downloads::CopyBitmapToClipboard(window_, hbm);

  const std::wstring suggested_name = mtk::downloads::SuggestScreenshotFileName(tab->title);
  std::wstring target_path;
  std::wstring final_file_name = suggested_name;

  const auto& prefs = mtk::preferences::Store::Get().values();
  if (prefs.download_ask_location) {
    if (!mtk::downloads::PromptDownloadLocation(window_, suggested_name, &target_path, &final_file_name)) {
      DeleteObject(hbm);
      return;
    }
  } else {
    target_path = mtk::downloads::UniqueTargetPath(suggested_name);
    if (target_path.empty()) {
      DeleteObject(hbm);
      return;
    }
    const size_t slash = target_path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
      final_file_name = target_path.substr(slash + 1);
    }
  }

  const bool saved = mtk::downloads::SaveBitmapToPng(hbm, target_path);
  DeleteObject(hbm);

  if (saved) {
    mtk::downloads::Item item;
    item.id = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) |
              static_cast<uint64_t>(GetTickCount64() & 0xFFFFFFFF);
    item.file_name = final_file_name;
    item.full_path = target_path;
    item.url = (browser->GetMainFrame() && browser->GetMainFrame()->IsValid())
                   ? browser->GetMainFrame()->GetURL().ToString()
                   : "";
    item.state = mtk::downloads::State::kComplete;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(target_path.c_str(), GetFileExInfoStandard, &fad)) {
      item.received_bytes = (static_cast<int64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
      item.total_bytes = item.received_bytes;
    }
    item.percent = 100;
    item.is_private = private_session_;
    mtk::downloads::Manager::Get().Upsert(item);

    const std::wstring message =
        std::wstring(mtk::strings::Get(mtk::strings::Id::kScreenshotSaved)) + target_path;
    MessageBoxW(window_, message.c_str(), L"MTK Browser", MB_OK | MB_ICONINFORMATION);
  } else {
    MessageBoxW(window_, mtk::strings::Get(mtk::strings::Id::kScreenshotFailed),
                L"MTK Browser", MB_OK | MB_ICONERROR);
  }
}

bool BrowserWindow::RetryDownload(uint64_t id) {
  const auto& items = mtk::downloads::Manager::Get().items();
  const auto found = std::find_if(items.begin(), items.end(), [id](const auto& item) {
    return item.id == id;
  });
  if (found == items.end() || !mtk::downloads::CanRetry(*found)) return false;

  const auto decision = mtk::policy::EvaluateNavigation(found->url);
  if (!decision.allow || (!decision.url.starts_with("https://") &&
                          !decision.url.starts_with("http://"))) {
    return false;
  }
  auto* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return false;
  tab->client->browser()->GetHost()->StartDownload(decision.url);
  return true;
}

bool BrowserWindow::ResumeInterruptedDownload(uint64_t id) {
  auto* item = mtk::downloads::Manager::Get().FindItem(id);
  if (!item || !mtk::downloads::CanResume(*item)) return false;

  const auto decision = mtk::policy::EvaluateNavigation(item->url);
  if (!decision.allow || (!decision.url.starts_with("https://") &&
                          !decision.url.starts_with("http://"))) {
    return false;
  }

  // When a partial file or byte offset exists on disk, resume directly from the
  // interrupted offset via WinHTTP Range + If-Range before falling back to a
  // fresh CEF download.
  if (mtk::downloads::GetExistingPartialBytes(item->full_path) > 0 ||
      !item->full_path.empty()) {
    auto* active = ActiveTab();
    BrowserClient* client_ptr = active ? active->client.get() : nullptr;
    if (mtk::downloads::ResumeDownloadWithRange(
            id, [this, client_ptr]() { OnBrowserDownloadsChanged(client_ptr); })) {
      return true;
    }
  }

  auto* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return false;
  item->state = mtk::downloads::State::kInProgress;
  item->detail = L"resuming from " + std::to_wstring(item->received_bytes) + L" bytes";
  mtk::downloads::Manager::Get().Flush();
  OnBrowserDownloadsChanged(tab->client.get());
  tab->client->browser()->GetHost()->StartDownload(decision.url);
  return true;
}

int BrowserWindow::CaptionButtonWidth() const {
  return Scaled(46);
}

int BrowserWindow::CaptionButtonHeight() const {
  return TabBarHeight();
}

RECT BrowserWindow::WindowControlsRect() const {
  if (full_screen_) return {0, 0, 0, 0};
  RECT client{};
  GetClientRect(window_, &client);
  const int width = CaptionButtonWidth() * 3;
  const int height = CaptionButtonHeight();
  return {client.right - width, 0, client.right, height};
}

RECT BrowserWindow::MinimizeButtonRect() const {
  if (full_screen_) return {0, 0, 0, 0};
  RECT client{};
  GetClientRect(window_, &client);
  const int btn_w = CaptionButtonWidth();
  const int height = CaptionButtonHeight();
  return {client.right - btn_w * 3, 0, client.right - btn_w * 2, height};
}

RECT BrowserWindow::MaximizeButtonRect() const {
  if (full_screen_) return {0, 0, 0, 0};
  RECT client{};
  GetClientRect(window_, &client);
  const int btn_w = CaptionButtonWidth();
  const int height = CaptionButtonHeight();
  return {client.right - btn_w * 2, 0, client.right - btn_w, height};
}

RECT BrowserWindow::CloseButtonRect() const {
  if (full_screen_) return {0, 0, 0, 0};
  RECT client{};
  GetClientRect(window_, &client);
  const int btn_w = CaptionButtonWidth();
  const int height = CaptionButtonHeight();
  return {client.right - btn_w, 0, client.right, height};
}

BrowserWindow::CaptionButton BrowserWindow::HitTestCaptionButton(POINT pt) const {
  if (full_screen_) return CaptionButton::kNone;
  const RECT min_r = MinimizeButtonRect();
  if (PtInRect(&min_r, pt)) return CaptionButton::kMinimize;
  const RECT max_r = MaximizeButtonRect();
  if (PtInRect(&max_r, pt)) return CaptionButton::kMaximize;
  const RECT close_r = CloseButtonRect();
  if (PtInRect(&close_r, pt)) return CaptionButton::kClose;
  return CaptionButton::kNone;
}

void BrowserWindow::PaintCaptionButtons(HDC dc, const RECT& client) {
  if (full_screen_) return;

  const RECT min_r = MinimizeButtonRect();
  const RECT max_r = MaximizeButtonRect();
  const RECT close_r = CloseButtonRect();
  const bool is_light = IsLightAppearance();

  const COLORREF hover_bg = is_light ? RGB(220, 226, 238) : RGB(35, 40, 52);
  const COLORREF press_bg = is_light ? RGB(205, 214, 228) : RGB(48, 55, 72);

  // Draw Minimize Button background
  if (hovered_caption_button_ == CaptionButton::kMinimize) {
    COLORREF bg = (pressed_caption_button_ == CaptionButton::kMinimize) ? press_bg : hover_bg;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dc, &min_r, br);
    DeleteObject(br);
  }

  // Draw Maximize Button background
  if (hovered_caption_button_ == CaptionButton::kMaximize) {
    COLORREF bg = (pressed_caption_button_ == CaptionButton::kMaximize) ? press_bg : hover_bg;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dc, &max_r, br);
    DeleteObject(br);
  }

  // Draw Close Button background (Distinctive red on hover/press)
  if (hovered_caption_button_ == CaptionButton::kClose) {
    COLORREF bg = (pressed_caption_button_ == CaptionButton::kClose)
        ? RGB(190, 10, 25)
        : RGB(232, 17, 35);
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dc, &close_r, br);
    DeleteObject(br);
  }

  const int pen_width = (g_dpi >= 144) ? 2 : 1;
  const COLORREF default_color = is_light ? RGB(60, 64, 75) : kTextColor;
  const COLORREF hover_color = (hovered_caption_button_ == CaptionButton::kClose)
                                   ? RGB(255, 255, 255)
                                   : (is_light ? RGB(15, 23, 42) : RGB(255, 255, 255));

  // 1. Minimize Glyph: Sleek horizontal bar
  {
    const COLORREF c = (hovered_caption_button_ == CaptionButton::kMinimize) ? hover_color : default_color;
    HPEN pen = CreatePen(PS_SOLID, pen_width, c);
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    const int cx = (min_r.left + min_r.right) / 2;
    const int cy = (min_r.top + min_r.bottom) / 2;
    const int half = Scaled(5);
    MoveToEx(dc, cx - half, cy, nullptr);
    LineTo(dc, cx + half + 1, cy);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
  }

  // 2. Maximize / Restore Glyph
  {
    const COLORREF c = (hovered_caption_button_ == CaptionButton::kMaximize) ? hover_color : default_color;
    HPEN pen = CreatePen(PS_SOLID, pen_width, c);
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));

    const int cx = (max_r.left + max_r.right) / 2;
    const int cy = (max_r.top + max_r.bottom) / 2;
    const int half = Scaled(5);

    if (IsZoomed(window_)) {
      // Restore: Two clean overlapping squares
      const int offset = Scaled(2);
      Rectangle(dc, cx - half + offset, cy - half - offset, cx + half + offset + 1, cy + half - offset + 1);
      COLORREF front_bg = (hovered_caption_button_ == CaptionButton::kMaximize)
          ? ((pressed_caption_button_ == CaptionButton::kMaximize) ? press_bg : hover_bg)
          : (is_light ? RGB(226, 232, 240) : kTabBarTop);
      HBRUSH front_br = CreateSolidBrush(front_bg);
      SelectObject(dc, front_br);
      Rectangle(dc, cx - half, cy - half + offset, cx + half - offset + 1, cy + half + 1);
      SelectObject(dc, old_brush);
      DeleteObject(front_br);
    } else {
      // Maximize: Crisp single square
      Rectangle(dc, cx - half, cy - half, cx + half + 1, cy + half + 1);
    }

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
  }

  // 3. Close Glyph: Modern crisp 'X'
  {
    const COLORREF c = (hovered_caption_button_ == CaptionButton::kClose) ? hover_color : default_color;
    HPEN pen = CreatePen(PS_SOLID, pen_width, c);
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));

    const int cx = (close_r.left + close_r.right) / 2;
    const int cy = (close_r.top + close_r.bottom) / 2;
    const int half = Scaled(4);

    MoveToEx(dc, cx - half, cy - half, nullptr);
    LineTo(dc, cx + half + 1, cy + half + 1);
    MoveToEx(dc, cx + half, cy - half, nullptr);
    LineTo(dc, cx - half - 1, cy + half + 1);

    SelectObject(dc, old_pen);
    DeleteObject(pen);
  }
}

bool BrowserWindow::IsLightAppearance() const {
  return mtk::preferences::ShouldUseLightAppearance(
      mtk::preferences::Store::Get().values().theme_mode);
}


void BrowserWindow::SaveActivePagePdf() {
  const auto* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return;
  const auto client = tab->client;
  const auto browser = client->browser();
  const auto frame = browser->GetMainFrame();
  if (!frame || !frame->IsValid()) return;
  const std::string original_url = frame->GetURL();

  const std::wstring suggested = mtk::downloads::SuggestPdfFileName(tab->title);
  wchar_t path[MAX_PATH]{};
  wcsncpy_s(path, suggested.c_str(), _TRUNCATE);

  const std::wstring initial_dir = mtk::downloads::DownloadDirectory();
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.lpstrFilter = L"PDF (*.pdf)\0*.pdf\0\0";
  dialog.lpstrDefExt = L"pdf";
  if (!initial_dir.empty()) {
    dialog.lpstrInitialDir = initial_dir.c_str();
  }
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetSaveFileNameW(&dialog)) return;
  if (!frame->IsValid() || frame->GetURL().ToString() != original_url ||
      FindTab(client.get()) == kNoTab) return;

  CefPdfPrintSettings settings;
  settings.print_background = 1;
  browser->GetHost()->PrintToPDF(path, settings, nullptr);
}

void BrowserWindow::ToggleReaderMode() {
  auto* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return;
  auto frame = tab->client->browser()->GetMainFrame();
  if (!frame) return;

  tab->reader_mode = !tab->reader_mode;
  if (tab->reader_mode) {
    constexpr char kScript[] = R"JS((() => {
      const source = document.querySelector('article, main, [role="main"]') || document.body;
      if (!source) return;
      const title = document.title || 'Reader View';
      const bodyHtml = source.innerHTML;
      document.body.innerHTML = `
        <div style="max-width:740px;margin:40px auto;padding:0 24px;font-family:Georgia,Cambria,serif;font-size:18px;line-height:1.8;color:#1a202c;background:#fafaf9;">
          <h1 style="font-size:32px;line-height:1.2;margin-bottom:24px;color:#0f172a;">${title}</h1>
          <div>${bodyHtml}</div>
        </div>
      `;
      document.body.style.backgroundColor = '#fafaf9';
    })())JS";
    frame->ExecuteJavaScript(kScript, frame->GetURL(), 0);
  } else {
    tab->client->browser()->Reload();
  }
}

void BrowserWindow::ApplyFocusMode(Tab& tab) {
  if (!tab.client || !tab.client->browser()) return;
  auto frame = tab.client->browser()->GetMainFrame();
  if (!frame) return;
  const std::string url = frame->GetURL().ToString();
  const std::string host = mtk::policy::HostOf(url);
  if (host.find("youtube.com") == std::string::npos) return;

  if (tab.focus_mode) {
    frame->ExecuteJavaScript(mtk::focus::kEnableScript, url, 0);
  } else {
    frame->ExecuteJavaScript(mtk::focus::kDisableScript, url, 0);
  }
}

void BrowserWindow::ToggleFocusMode() {
  auto* tab = ActiveTab();
  if (!tab) return;
  tab->focus_mode = !tab->focus_mode;
  ApplyFocusMode(*tab);
}

void BrowserWindow::ToggleReadingList() {
  auto* tab = ActiveTab();
  if (!tab) return;
  const std::string url = WideToUtf8(tab->url);
  if (!mtk::policy::EvaluateNavigation(url).allow) return;
  auto& store = mtk::library::Store::Get();
  const bool already_saved = store.IsInReadingList(url);
  const bool changed = already_saved
      ? store.RemoveReadingListAndFlush(url)
      : store.SaveReadingListAndFlush(url, tab->title);
  if (!changed) {
    MessageBoxW(window_, mtk::strings::Get(Text::kReadingListUpdateFailed),
                mtk::strings::Get(Text::kReadingList), MB_OK | MB_ICONERROR);
  }
}

void BrowserWindow::ShowReadingListDialog() {
  const auto& entries = mtk::library::Store::Get().reading_list();
  if (entries.empty()) {
    MessageBoxW(window_, mtk::strings::Get(Text::kReadingListEmpty),
                mtk::strings::Get(Text::kReadingList), MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (auto* tab = ActiveTab(); tab && tab->client && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(entries[0].url);
  }
}

void BrowserWindow::TranslateActivePage(const std::string& target_language) {
  auto* tab = ActiveTab();
  if (!tab) return;
  const std::string source_url = WideToUtf8(tab->url);
  const auto decision = mtk::policy::EvaluateNavigation(source_url);
  if (!decision.allow) return;
  const std::string translated =
      mtk::translation::GoogleTranslateUrl(decision.url, target_language);
  if (translated.empty()) return;
  if (MessageBoxW(window_, mtk::strings::Get(Text::kTranslationPrivacyNotice),
                  mtk::strings::Get(Text::kTranslationPrivacyTitle),
                  MB_YESNO | MB_ICONINFORMATION) != IDYES) {
    return;
  }
  CreateTab(translated);
}

void BrowserWindow::OpenActiveSiteAsWebApp() {
  auto* tab = ActiveTab();
  if (!tab) return;
  const auto decision = mtk::policy::EvaluateNavigation(WideToUtf8(tab->url));
  const std::string app_id = decision.allow ? mtk::web_app::IdForUrl(decision.url) : "";
  if (app_id.empty()) return;

  mtk::web_app::InstallOptions options;
  if (!mtk::web_app::PromptInstallApp(instance_, window_, decision.url, tab->title, &options)) {
    return;
  }

  wchar_t executable[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, executable, MAX_PATH)) {
    mtk::web_app::CreateAppShortcuts(executable, decision.url, options.display_name,
                                    options.desktop_shortcut, options.start_menu_shortcut);
  }
}

void BrowserWindow::ShowPasskeyCheckPage() {
  auto* tab = ActiveTab();
  if (tab && tab->client && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(mtk::passkeys::kUrlWide);
    return;
  }
  CreateTab(mtk::passkeys::kUrl);
}

void BrowserWindow::ShowTaskManager() {
  mtk::task_manager::Show(instance_, window_);
}


void BrowserWindow::ShowDiagnostics() {
  const auto& prefs = mtk::preferences::Store::Get().values();
  const std::wstring message = L"MTK Browser — local diagnostics\n\nBuild SDK: " +
      Utf8ToWide(CEF_VERSION) + L"\nOpen windows: " + std::to_wstring(g_windows.size()) +
      L"\nTabs in this window: " + std::to_wstring(tabs_.size()) +
      L"\nDisplay scaling: " + std::to_wstring(g_dpi) + L" DPI" +
      L"\nPrivate window: " + (private_session_ ? L"Yes" : L"No") +
      L"\nMemory saver: " + (prefs.memory_saver ? L"On" : L"Off") +
      L"\n\nThis information stays on this computer. It contains no browsing addresses or saved credentials.";
  MessageBoxW(window_, message.c_str(), L"MTK Browser", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::OpenDownloadsPage() {
  auto* tab = ActiveTab();
  if (tab && tab->client && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(mtk::downloads_page::kUrlWide);
    return;
  }
  CreateTab(mtk::downloads_page::kUrl);
}

void BrowserWindow::OpenSettingsPage() {
  auto* tab = ActiveTab();
  if (tab && tab->client && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(mtk::settings_page::kUrlWide);
    return;
  }
  CreateTab(mtk::settings_page::kUrl);
}

void BrowserWindow::DeleteProfile() {
  MessageBoxW(window_, L"Only inactive profiles can be removed.", L"Delete profile", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::RenameProfile() {
  MessageBoxW(window_, L"The default profile cannot be renamed.", L"Rename profile", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::ShowDeveloperTools() {
  auto* tab = ActiveTab();
  if (!tab || !tab->client || !tab->client->browser()) return;
  CefWindowInfo windowInfo;
  CefBrowserSettings settings;
  CefPoint inspectAt;
#if defined(OS_WIN)
  windowInfo.SetAsPopup(window_, "DevTools");
#endif
  tab->client->browser()->GetHost()->ShowDevTools(windowInfo, tab->client, settings, inspectAt);
}
