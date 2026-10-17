#include "tab_preview.h"

#include <dwmapi.h>

#include <algorithm>
#include <array>

#include "policy.h"

namespace mtk::tab_preview {
namespace {

constexpr wchar_t kPreviewClassName[] = L"MTKTabPreviewPopup";
constexpr int kBaseCardWidth = 280;

HWND g_preview_window = nullptr;
PreviewCardData g_data;
int g_dpi = 96;
bool g_is_light = false;
HBITMAP g_thumbnail = nullptr;
int g_thumb_w = 0;
int g_thumb_h = 0;

int Scaled(int value) { return MulDiv(value, g_dpi, 96); }

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

void ReleaseThumbnail() {
  if (g_thumbnail) {
    DeleteObject(g_thumbnail);
    g_thumbnail = nullptr;
  }
  g_thumb_w = 0;
  g_thumb_h = 0;
}

// Determines if a captured bitmap has real visual web content rather than
// being a uniform GDI blank/white/black capture from a GPU DirectX surface.
bool IsBitmapVisuallyMeaningful(HBITMAP bmp, int width, int height) {
  if (!bmp || width <= 0 || height <= 0) return false;
  HDC screen_dc = GetDC(nullptr);
  if (!screen_dc) return false;
  HDC mem_dc = CreateCompatibleDC(screen_dc);
  if (!mem_dc) {
    ReleaseDC(nullptr, screen_dc);
    return false;
  }
  HGDIOBJ old = SelectObject(mem_dc, bmp);

  COLORREF first_color = GetPixel(mem_dc, width / 2, height / 2);
  int different_samples = 0;
  for (int step = 1; step <= 4; ++step) {
    int sx = (width * step) / 5;
    for (int ystep = 1; ystep <= 4; ++ystep) {
      int sy = (height * ystep) / 5;
      COLORREF c = GetPixel(mem_dc, sx, sy);
      if (c != first_color) {
        different_samples++;
      }
    }
  }

  SelectObject(mem_dc, old);
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);

  // GDI dummy surfaces have 0 variance (100% white/black).
  // Real rendered web content has varied pixel colors.
  return different_samples >= 3;
}

void CaptureViewThumbnail(HWND view_hwnd, int target_w, int target_h) {
  ReleaseThumbnail();
  if (!view_hwnd || !IsWindowVisible(view_hwnd) || target_w <= 0 || target_h <= 0) {
    return;
  }
  RECT rc{};
  if (!GetClientRect(view_hwnd, &rc) || rc.right <= 16 || rc.bottom <= 16) {
    return;
  }
  HDC view_dc = GetDC(view_hwnd);
  if (!view_dc) return;
  HDC mem_dc = CreateCompatibleDC(view_dc);
  HBITMAP bmp = CreateCompatibleBitmap(view_dc, target_w, target_h);
  if (mem_dc && bmp) {
    HGDIOBJ old = SelectObject(mem_dc, bmp);
    SetStretchBltMode(mem_dc, HALFTONE);

    // Try PrintWindow with PW_RENDERFULLCONTENT first
    BOOL printed = PrintWindow(view_hwnd, mem_dc, 2 /* PW_RENDERFULLCONTENT */);
    if (!printed) {
      StretchBlt(mem_dc, 0, 0, target_w, target_h, view_dc, 0, 0, rc.right,
                 std::min<int>(rc.bottom, (rc.right * target_h) / std::max(1, target_w)),
                 SRCCOPY);
    }
    SelectObject(mem_dc, old);

    // Validate that the capture contains actual content
    if (IsBitmapVisuallyMeaningful(bmp, target_w, target_h)) {
      g_thumbnail = bmp;
      g_thumb_w = target_w;
      g_thumb_h = target_h;
    } else {
      DeleteObject(bmp);
    }
  } else if (bmp) {
    DeleteObject(bmp);
  }
  if (mem_dc) DeleteDC(mem_dc);
  ReleaseDC(view_hwnd, view_dc);
}

int CalculateCardHeight(const PreviewCardData& data, bool has_thumb) {
  int h = Scaled(12); // top padding below accent
  h += Scaled(20);    // title row
  h += Scaled(18);    // subtitle row
  const bool has_badges = !data.group.empty() || !data.workspace.empty() ||
                          data.sleeping || data.pinned || data.muted ||
                          data.camera_active || data.microphone_active;
  if (has_badges) {
    h += Scaled(18);  // badges row
  }
  if (has_thumb && g_thumb_h > 0) {
    h += g_thumb_h + Scaled(8);
  }
  h += Scaled(12);    // bottom padding
  return h;
}

void PaintCard(HWND window, HDC dc) {
  RECT client{};
  GetClientRect(window, &client);

  const COLORREF bg_color = g_is_light ? RGB(255, 255, 255) : RGB(15, 23, 42);
  const COLORREF border_color = g_is_light ? RGB(226, 232, 240) : RGB(51, 65, 85);
  const COLORREF title_color = g_is_light ? RGB(15, 23, 42) : RGB(248, 250, 252);
  const COLORREF sub_color = g_is_light ? RGB(100, 116, 139) : RGB(148, 163, 184);

  HBRUSH bg_brush = CreateSolidBrush(bg_color);
  FillRect(dc, &client, bg_brush);
  DeleteObject(bg_brush);

  // Top accent stripe (uses group color when grouped, or MTK electric violet).
  const COLORREF accent =
      g_data.group.empty() ? RGB(124, 92, 255) : GroupAccentColor(g_data.group);
  RECT stripe{0, 0, client.right, Scaled(3)};
  HBRUSH stripe_brush = CreateSolidBrush(accent);
  FillRect(dc, &stripe, stripe_brush);
  DeleteObject(stripe_brush);

  SetBkMode(dc, TRANSPARENT);
  HFONT title_font = CreateFontW(
      -Scaled(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  HFONT sub_font = CreateFontW(
      -Scaled(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  int y = Scaled(11);
  const int pad_x = Scaled(14);

  HGDIOBJ old_font = SelectObject(dc, title_font);
  SetTextColor(dc, title_color);
  RECT title_rc{pad_x, y, client.right - pad_x, y + Scaled(20)};
  const std::wstring display_title =
      g_data.title.empty() ? L"New Tab" : g_data.title;
  DrawTextW(dc, display_title.c_str(), -1, &title_rc,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  y += Scaled(21);

  SelectObject(dc, sub_font);
  SetTextColor(dc, sub_color);
  const std::wstring subtitle = FormatPreviewSubtitle(g_data);
  RECT sub_rc{pad_x, y, client.right - pad_x, y + Scaled(17)};
  DrawTextW(dc, subtitle.c_str(), -1, &sub_rc,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  y += Scaled(19);

  // Status badges row (Group, Workspace, Sleeping/Memory Saver, Media).
  std::wstring badges;
  if (!g_data.group.empty()) badges += L"[" + g_data.group + L"]  ";
  if (!g_data.workspace.empty()) badges += L"{" + g_data.workspace + L"}  ";
  if (g_data.sleeping) badges += L"\u25CB Sleeping (Memory Saved)  ";
  if (g_data.pinned) badges += L"Pinned  ";
  if (g_data.muted) badges += L"Muted  ";
  if (g_data.camera_active || g_data.microphone_active) {
    badges += L"\u25CF Media Active";
  }
  if (!badges.empty()) {
    SetTextColor(dc, accent);
    RECT badge_rc{pad_x, y, client.right - pad_x, y + Scaled(17)};
    DrawTextW(dc, badges.c_str(), -1, &badge_rc,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += Scaled(19);
  }

  // Draw thumbnail ONLY if a genuine visual thumbnail was successfully captured
  if (g_thumbnail && g_thumb_w > 0 && g_thumb_h > 0) {
    HDC thumb_dc = CreateCompatibleDC(dc);
    HGDIOBJ old_bmp = SelectObject(thumb_dc, g_thumbnail);
    BitBlt(dc, pad_x, y + Scaled(4), g_thumb_w, g_thumb_h, thumb_dc, 0, 0,
           SRCCOPY);
    SelectObject(thumb_dc, old_bmp);
    DeleteDC(thumb_dc);

    // Frame the thumbnail neatly
    HPEN thumb_pen = CreatePen(PS_SOLID, 1, border_color);
    HGDIOBJ old_p = SelectObject(dc, thumb_pen);
    HGDIOBJ old_b = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, pad_x - 1, y + Scaled(3), pad_x + g_thumb_w + 1,
              y + Scaled(4) + g_thumb_h + 1);
    SelectObject(dc, old_b);
    SelectObject(dc, old_p);
    DeleteObject(thumb_pen);
  }

  SelectObject(dc, old_font);
  if (title_font) DeleteObject(title_font);
  if (sub_font) DeleteObject(sub_font);

  HPEN border_pen = CreatePen(PS_SOLID, 1, border_color);
  HGDIOBJ old_pen = SelectObject(dc, border_pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  Rectangle(dc, 0, 0, client.right, client.bottom);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(border_pen);
}

LRESULT CALLBACK PreviewWndProc(HWND window, UINT message, WPARAM w_param,
                                LPARAM l_param) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(window, &ps);
      PaintCard(window, dc);
      EndPaint(window, &ps);
      return 0;
    }
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    default:
      break;
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

bool EnsurePreviewClass(HINSTANCE instance) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = PreviewWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kPreviewClassName;
  registered = RegisterClassExW(&wc) != 0 ||
               GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return registered;
}

}  // namespace

COLORREF GroupAccentColor(std::wstring_view group_name) {
  if (group_name.empty()) return RGB(124, 92, 255);
  static constexpr std::array<COLORREF, 8> kPalette = {
      RGB(124, 92, 255),   // Electric Violet
      RGB(56, 189, 248),   // Sky Cyan
      RGB(52, 211, 153),   // Emerald
      RGB(251, 191, 36),   // Amber
      RGB(248, 113, 113),  // Coral Red
      RGB(232, 121, 249),  // Fuchsia
      RGB(96, 165, 250),   // Royal Blue
      RGB(163, 230, 53),   // Lime
  };
  uint32_t hash = 2166136261u;
  for (const wchar_t ch : group_name) {
    hash ^= static_cast<uint32_t>(towlower(ch));
    hash *= 16777619u;
  }
  return kPalette[hash % kPalette.size()];
}

std::wstring FormatPreviewSubtitle(const PreviewCardData& data) {
  std::wstring result;
  if (data.url.starts_with("mtk://") || data.is_internal) {
    result = L"MTK Internal  \u2022  " + Utf8ToWide(data.url);
  } else {
    const std::string host = mtk::policy::HostOf(data.url);
    const std::wstring wide_host =
        host.empty() ? Utf8ToWide(data.url) : Utf8ToWide(host);
    if (data.is_https || data.url.starts_with("https://")) {
      result = L"HTTPS  \u2022  " + wide_host;
    } else {
      result = wide_host;
    }
  }
  if (data.sleeping) {
    result += L"  (Sleeping)";
  }
  return result;
}

void Show(HINSTANCE instance, HWND owner, const RECT& tab_screen_rect,
          const PreviewCardData& data, int dpi, bool is_light) {
  if (!owner || !EnsurePreviewClass(instance)) return;
  g_dpi = dpi > 0 ? dpi : 96;
  g_is_light = is_light;
  g_data = data;

  const int width = Scaled(kBaseCardWidth);
  const int thumb_w = width - Scaled(28);
  const int thumb_h = Scaled(80);
  CaptureViewThumbnail(data.browser_view, thumb_w, thumb_h);

  const int height = CalculateCardHeight(data, g_thumbnail != nullptr);

  int x = tab_screen_rect.left;
  int y = tab_screen_rect.bottom + Scaled(6);
  HMONITOR monitor = MonitorFromRect(&tab_screen_rect, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(MONITORINFO)};
  if (GetMonitorInfoW(monitor, &info)) {
    x = std::clamp<int>(x, info.rcWork.left + Scaled(6),
                        std::max<int>(info.rcWork.left + Scaled(6),
                                      info.rcWork.right - width - Scaled(6)));
    if (y + height > info.rcWork.bottom) {
      y = tab_screen_rect.top - height - Scaled(6);
    }
  }

  if (!g_preview_window) {
    g_preview_window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kPreviewClassName,
        L"", WS_POPUP, x, y, width, height, owner, nullptr, instance, nullptr);
    if (!g_preview_window) return;
    const int corner_pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_preview_window, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner_pref, sizeof(corner_pref));
  }

  SetWindowPos(g_preview_window, HWND_TOPMOST, x, y, width, height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(g_preview_window, nullptr, FALSE);
}

void Hide() {
  ReleaseThumbnail();
  if (g_preview_window) {
    ShowWindow(g_preview_window, SW_HIDE);
  }
}

bool IsVisible() {
  return g_preview_window != nullptr && IsWindowVisible(g_preview_window) != FALSE;
}

}  // namespace mtk::tab_preview
