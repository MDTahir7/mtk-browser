#include "downloads_panel.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
namespace Gdiplus {
using std::min;
using std::max;
}
#include <gdiplus.h>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

#include "download_manager.h"
#include "strings.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace mtk::downloads_panel {
namespace {

constexpr wchar_t kClassName[] = L"MTKDownloadsPanel";
constexpr UINT_PTR kDismissTimer = 1;
constexpr UINT kDismissPollMs = 150;
constexpr size_t kMaxRows = 100;

// Base metrics at 96 DPI.
constexpr int kBaseWidth = 390;
constexpr int kBaseMaxHeight = 580;
constexpr int kBaseHeaderHeight = 52;
constexpr int kBaseFooterHeight = 48;
constexpr int kIconSize = 24;

// Colors matching modern clean flyout (identical to Image 1).
constexpr COLORREF kBg = RGB(255, 255, 255);
constexpr COLORREF kBorder = RGB(226, 232, 240);
constexpr COLORREF kDivider = RGB(228, 228, 229);
constexpr COLORREF kTextPrimary = RGB(17, 24, 39);      // #111827
constexpr COLORREF kTextSecondary = RGB(107, 114, 128); // #6b7280
constexpr COLORREF kHoverBg = RGB(243, 244, 246);       // #f3f4f6
constexpr COLORREF kCloseCircle = RGB(107, 114, 128);   // #6b7280 neutral clean
constexpr COLORREF kCloseHoverBg = RGB(243, 244, 246);  // #f3f4f6
constexpr COLORREF kProgressTrack = RGB(229, 231, 235);
constexpr COLORREF kProgressFill = RGB(79, 70, 229);
constexpr COLORREF kScrollThumb = RGB(203, 213, 225);
constexpr COLORREF kScrollThumbHover = RGB(148, 163, 184);

enum class Action {
  kNone,
  kClose,
  kItem,
  kShowAll,
  kFolder,
  kOpen,
  kRemove,
};

struct Hit {
  RECT bounds{};
  Action action = Action::kNone;
  uint64_t id = 0;
};

struct ItemLayout {
  mtk::downloads::Item item;
  RECT row_rect{};
  int content_y = 0;
  int height = 0;
};

HWND g_panel = nullptr;
HWND g_anchor = nullptr;
HINSTANCE g_instance = nullptr;
Callbacks g_callbacks;
int g_dpi = 96;
bool g_dismiss_armed = false;
std::vector<Hit> g_hits;
uint64_t g_hovered_item_id = 0;
Action g_hovered_action = Action::kNone;
bool g_close_hovered = false;
bool g_show_all_hovered = false;
bool g_is_private = false;

HFONT g_header_font = nullptr;
HFONT g_item_title_font = nullptr;
HFONT g_sub_font = nullptr;
HFONT g_footer_font = nullptr;

std::map<std::wstring, HICON> g_icon_cache;

int g_scroll_y = 0;
int g_max_scroll = 0;
bool g_dragging_scrollbar = false;
int g_drag_start_y = 0;
int g_drag_scroll_start = 0;
RECT g_scrollbar_thumb_rect{};

int Scaled(int value) { return MulDiv(value, g_dpi, 96); }

ULONG_PTR g_gdiplus_token = 0;

void EnsureGdiplus() {
  if (g_gdiplus_token == 0) {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr);
  }
}

inline Gdiplus::Color ToGdiplusColor(COLORREF c, BYTE alpha = 255) {
  return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF colour) {
  const int w = rect.right - rect.left;
  const int h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) return;

  EnsureGdiplus();
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  Gdiplus::SolidBrush brush(ToGdiplusColor(colour));
  if (radius <= 0) {
    g.FillRectangle(&brush, static_cast<float>(rect.left), static_cast<float>(rect.top),
                    static_cast<float>(w), static_cast<float>(h));
    return;
  }

  const float x = static_cast<float>(rect.left);
  const float y = static_cast<float>(rect.top);
  const float fw = static_cast<float>(w);
  const float fh = static_cast<float>(h);
  float d = static_cast<float>(radius * 2);
  if (d > fw) d = fw;
  if (d > fh) d = fh;

  Gdiplus::GraphicsPath path;
  path.AddArc(x, y, d, d, 180.0f, 90.0f);
  path.AddArc(x + fw - d, y, d, d, 270.0f, 90.0f);
  path.AddArc(x + fw - d, y + fh - d, d, d, 0.0f, 90.0f);
  path.AddArc(x, y + fh - d, d, d, 90.0f, 90.0f);
  path.CloseFigure();

  g.FillPath(&brush, &path);
}

void ReleaseFonts() {
  for (HFONT* font : {&g_header_font, &g_item_title_font, &g_sub_font, &g_footer_font}) {
    if (*font) {
      DeleteObject(*font);
      *font = nullptr;
    }
  }
}

void CreateFonts() {
  ReleaseFonts();
  const auto make = [](int height, int weight) {
    return CreateFontW(Scaled(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  };
  g_header_font = make(-16, FW_SEMIBOLD);
  g_item_title_font = make(-13, FW_SEMIBOLD);
  g_sub_font = make(-11, FW_NORMAL);
  g_footer_font = make(-13, FW_SEMIBOLD);
}

void ReleaseIcons() {
  for (auto& [ext, icon] : g_icon_cache) {
    if (icon) DestroyIcon(icon);
  }
  g_icon_cache.clear();
}

std::wstring ExtractExtension(const std::wstring& name) {
  const size_t dot = name.find_last_of(L'.');
  if (dot == std::wstring::npos) return {};
  std::wstring ext = name.substr(dot);
  for (wchar_t& c : ext) c = static_cast<wchar_t>(towlower(c));
  return ext;
}

HICON GetCachedIcon(const std::wstring& file_name, const std::wstring& full_path) {
  const std::wstring ext = ExtractExtension(file_name);
  const auto it = g_icon_cache.find(ext);
  if (it != g_icon_cache.end()) return it->second;

  SHFILEINFOW sfi{};
  DWORD_PTR res = 0;
  // If file exists on disk, try that first for true shell icon.
  if (!full_path.empty()) {
    res = SHGetFileInfoW(full_path.c_str(), 0, &sfi, sizeof(sfi),
                         SHGFI_ICON | SHGFI_SMALLICON);
  }
  if (!res || !sfi.hIcon) {
    if (!ext.empty()) {
      res = SHGetFileInfoW(ext.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                           SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_SMALLICON);
    }
  }

  HICON icon = (res && sfi.hIcon) ? sfi.hIcon : nullptr;
  g_icon_cache[ext] = icon;
  return icon;
}

std::wstring FormatSize(int64_t bytes) {
  if (bytes <= 0) return L"0 B";
  if (bytes < 1024) return std::to_wstring(bytes) + L" B";
  if (bytes < 1024 * 1024) {
    const int64_t kb = (bytes + 1023) / 1024;
    if (kb >= 1000) {
      return std::to_wstring(kb / 1000) + L"," +
             (kb % 1000 < 100 ? (kb % 1000 < 10 ? L"00" : L"0") : L"") +
             std::to_wstring(kb % 1000) + L" KB";
    }
    return std::to_wstring(kb) + L" KB";
  }
  if (bytes < 1024LL * 1024 * 1024) {
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f MB", mb);
    return buf;
  }
  const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
  wchar_t buf[32];
  swprintf_s(buf, L"%.1f GB", gb);
  return buf;
}

std::wstring FormatTimeAgo(const mtk::downloads::Item& item) {
  FILETIME ft{};
  bool got_time = false;
  if (!item.full_path.empty()) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(item.full_path.c_str(), GetFileExInfoStandard, &data)) {
      ft = data.ftLastWriteTime;
      got_time = true;
    }
  }
  if (!got_time) {
    return item.state == mtk::downloads::State::kComplete ? L"Recently" : L"Today";
  }
  FILETIME now_ft{};
  GetSystemTimeAsFileTime(&now_ft);
  ULARGE_INTEGER u_now, u_file;
  u_now.LowPart = now_ft.dwLowDateTime;
  u_now.HighPart = now_ft.dwHighDateTime;
  u_file.LowPart = ft.dwLowDateTime;
  u_file.HighPart = ft.dwHighDateTime;
  if (u_now.QuadPart <= u_file.QuadPart) return L"Just now";
  const int64_t diff_sec = static_cast<int64_t>((u_now.QuadPart - u_file.QuadPart) / 10000000ULL);
  if (diff_sec < 60) return L"Just now";
  const int64_t diff_min = diff_sec / 60;
  if (diff_min < 60) {
    return std::to_wstring(diff_min) + (diff_min == 1 ? L" minute ago" : L" minutes ago");
  }
  const int64_t diff_hr = diff_min / 60;
  if (diff_hr < 24) {
    return std::to_wstring(diff_hr) + (diff_hr == 1 ? L" hour ago" : L" hours ago");
  }
  const int64_t diff_days = diff_hr / 24;
  if (diff_days == 1) return L"Yesterday";
  if (diff_days < 30) {
    return std::to_wstring(diff_days) + L" days ago";
  }
  return std::to_wstring(diff_days / 30) + L" months ago";
}

std::vector<mtk::downloads::Item> VisibleItems() {
  const auto& all = mtk::downloads::Manager::Get().items();
  std::vector<mtk::downloads::Item> rows;
  for (size_t index = all.size(); index-- > 0 && rows.size() < kMaxRows;) {
    if (!g_is_private && all[index].is_private) continue;
    rows.push_back(all[index]);
  }
  return rows;
}


std::vector<ItemLayout> MeasureRows(HDC /*dc*/, const std::vector<mtk::downloads::Item>& items,
                                    int /*text_width*/) {
  std::vector<ItemLayout> layouts;
  int current_y = 0;
  for (const auto& item : items) {
    ItemLayout layout;
    layout.item = item;
    layout.content_y = current_y;

    const bool running = item.state == mtk::downloads::State::kInProgress ||
                         item.state == mtk::downloads::State::kPaused;
    const int row_h = running ? Scaled(64) : Scaled(56);

    layout.height = row_h;
    current_y += row_h;
    layouts.push_back(layout);
  }
  return layouts;
}

int CalculatePanelHeight(const std::vector<ItemLayout>& layouts) {
  int content_h = 0;
  for (const auto& l : layouts) content_h += l.height;
  if (content_h == 0) content_h = Scaled(80); // empty message height
  const int total = Scaled(kBaseHeaderHeight) + content_h + Scaled(kBaseFooterHeight);
  return std::clamp<int>(total, Scaled(180), Scaled(kBaseMaxHeight));
}

bool PanelHoldsForeground() {
  HWND foreground = GetForegroundWindow();
  if (!foreground || !g_panel) return false;
  return foreground == g_panel || GetParent(foreground) == g_panel ||
         foreground == GetWindow(g_panel, GW_OWNER);
}

bool CopyFileToClipboard(HWND owner, const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;

  const size_t characters = path.size() + 2;
  const size_t bytes = sizeof(DROPFILES) + characters * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GHND, bytes);
  if (!memory) return false;
  auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
  if (!drop) {
    GlobalFree(memory);
    return false;
  }
  drop->pFiles = sizeof(DROPFILES);
  drop->fWide = TRUE;
  auto* text = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(drop) +
                                          sizeof(DROPFILES));
  memcpy(text, path.c_str(), path.size() * sizeof(wchar_t));
  text[path.size()] = L'\0';
  text[path.size() + 1] = L'\0';
  GlobalUnlock(memory);

  if (!OpenClipboard(owner)) {
    GlobalFree(memory);
    return false;
  }
  EmptyClipboard();
  const bool ok = SetClipboardData(CF_HDROP, memory) != nullptr;
  CloseClipboard();
  if (!ok) GlobalFree(memory);
  return ok;
}

void ShowInFolder(const std::wstring& path) {
  if (path.empty()) return;
  bool selected = false;
  if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str())) {
    if (SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0))) {
      selected = true;
    }
    ILFree(item);
  }
  if (!selected) {
    const std::wstring param = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, nullptr, L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
  }
}

void OpenDownloadedFile(HWND owner, const mtk::downloads::Item& item) {
  if (item.full_path.empty()) return;
  if (item.risky) {
    const std::wstring question =
        L"\"" + item.file_name +
        L"\" is an executable program or script. Running it can harm your computer. Do you want to open it?";
    if (MessageBoxW(owner, question.c_str(), L"MTK Browser \u2014 Security Warning",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
      return;
    }
  }
  ShellExecuteW(nullptr, L"open", item.full_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

const mtk::downloads::Item* FindItem(uint64_t id) {
  const auto& all = mtk::downloads::Manager::Get().items();
  const auto match = std::find_if(
      all.begin(), all.end(),
      [id](const mtk::downloads::Item& item) { return item.id == id; });
  return match == all.end() ? nullptr : &*match;
}

void DrawImageIcon(HDC dc, int x, int y, int w, int h) {
  const int fold = Scaled(5);
  POINT pts[5] = {
      {x, y},
      {x + w - fold, y},
      {x + w, y + fold},
      {x + w, y + h},
      {x, y + h}};
  HBRUSH sheet_brush = CreateSolidBrush(RGB(255, 255, 255));
  HPEN sheet_pen = CreatePen(PS_SOLID, Scaled(1), RGB(196, 199, 197));
  HGDIOBJ old_b = SelectObject(dc, sheet_brush);
  HGDIOBJ old_p = SelectObject(dc, sheet_pen);
  Polygon(dc, pts, 5);

  POINT flap_pts[3] = {
      {x + w - fold, y},
      {x + w - fold, y + fold},
      {x + w, y + fold}};
  HBRUSH flap_brush = CreateSolidBrush(RGB(235, 238, 240));
  SelectObject(dc, flap_brush);
  Polygon(dc, flap_pts, 3);
  DeleteObject(flap_brush);

  const int cx1 = x + Scaled(4);
  const int cy1 = y + Scaled(8);
  const int cx2 = cx1 + Scaled(11);
  const int cy2 = cy1 + Scaled(12);

  RECT card_rect{cx1, cy1, cx2, cy2};
  FillRounded(dc, card_rect, Scaled(2), RGB(2, 132, 199));

  HBRUSH sun_brush = CreateSolidBrush(RGB(255, 255, 255));
  SelectObject(dc, sun_brush);
  SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, cx1 + Scaled(7), cy1 + Scaled(2), cx1 + Scaled(10), cy1 + Scaled(5));
  DeleteObject(sun_brush);

  POINT m1_pts[3] = {
      {cx1 + Scaled(1), cy2 - Scaled(1)},
      {cx1 + Scaled(5), cy1 + Scaled(5)},
      {cx1 + Scaled(10), cy2 - Scaled(1)}};
  HBRUSH m1_brush = CreateSolidBrush(RGB(56, 189, 248));
  SelectObject(dc, m1_brush);
  Polygon(dc, m1_pts, 3);
  DeleteObject(m1_brush);

  POINT m2_pts[3] = {
      {cx1 + Scaled(4), cy2 - Scaled(1)},
      {cx1 + Scaled(8), cy1 + Scaled(7)},
      {cx1 + Scaled(11), cy2 - Scaled(1)}};
  HBRUSH m2_brush = CreateSolidBrush(RGB(186, 230, 253));
  SelectObject(dc, m2_brush);
  Polygon(dc, m2_pts, 3);
  DeleteObject(m2_brush);

  SelectObject(dc, old_p);
  SelectObject(dc, old_b);
  DeleteObject(sheet_pen);
  DeleteObject(sheet_brush);
}

void DrawVideoConeIcon(HDC dc, int x, int y, int w, int h) {
  HBRUSH base_brush = CreateSolidBrush(RGB(234, 88, 12));
  HGDIOBJ old_b = SelectObject(dc, base_brush);
  HGDIOBJ old_p = SelectObject(dc, GetStockObject(NULL_PEN));

  RoundRect(dc, x + Scaled(2), y + h - Scaled(5), x + w - Scaled(2), y + h, Scaled(4), Scaled(4));

  HBRUSH cone_brush = CreateSolidBrush(RGB(249, 115, 22));
  SelectObject(dc, cone_brush);

  POINT cone_pts[4] = {
      {x + Scaled(4), y + h - Scaled(4)},
      {x + Scaled(10), y + Scaled(3)},
      {x + w - Scaled(10), y + Scaled(3)},
      {x + w - Scaled(4), y + h - Scaled(4)}};
  Polygon(dc, cone_pts, 4);

  SelectObject(dc, base_brush);
  RoundRect(dc, x + Scaled(9), y, x + w - Scaled(9), y + Scaled(5), Scaled(4), Scaled(4));

  HBRUSH white_brush = CreateSolidBrush(RGB(255, 255, 255));
  SelectObject(dc, white_brush);

  POINT b1_pts[4] = {
      {x + Scaled(5), y + h - Scaled(8)},
      {x + Scaled(7), y + h - Scaled(13)},
      {x + w - Scaled(7), y + h - Scaled(13)},
      {x + w - Scaled(5), y + h - Scaled(8)}};
  Polygon(dc, b1_pts, 4);

  POINT b2_pts[4] = {
      {x + Scaled(8), y + h - Scaled(16)},
      {x + Scaled(9), y + h - Scaled(20)},
      {x + w - Scaled(9), y + h - Scaled(20)},
      {x + w - Scaled(8), y + h - Scaled(16)}};
  Polygon(dc, b2_pts, 4);

  SelectObject(dc, old_p);
  SelectObject(dc, old_b);
  DeleteObject(white_brush);
  DeleteObject(cone_brush);
  DeleteObject(base_brush);
}

void DrawZipFolderIcon(HDC dc, int x, int y, int w, int h) {
  HBRUSH tab_brush = CreateSolidBrush(RGB(217, 119, 6));
  HGDIOBJ old_b = SelectObject(dc, tab_brush);
  HGDIOBJ old_p = SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, x + Scaled(1), y, x + Scaled(11), y + Scaled(7), Scaled(3), Scaled(3));

  HBRUSH body_brush = CreateSolidBrush(RGB(245, 158, 11));
  SelectObject(dc, body_brush);
  RoundRect(dc, x, y + Scaled(4), x + w, y + h, Scaled(5), Scaled(5));

  HBRUSH flap_brush = CreateSolidBrush(RGB(251, 191, 36));
  SelectObject(dc, flap_brush);
  RoundRect(dc, x, y + Scaled(7), x + w, y + h, Scaled(5), Scaled(5));

  HBRUSH zip_brush = CreateSolidBrush(RGB(148, 163, 184));
  SelectObject(dc, zip_brush);
  Rectangle(dc, x + Scaled(1), y + Scaled(11), x + w - Scaled(1), y + Scaled(16));

  HPEN pen_light = CreatePen(PS_SOLID, Scaled(1), RGB(255, 255, 255));
  HPEN pen_dark = CreatePen(PS_SOLID, Scaled(1), RGB(71, 85, 105));
  for (int tx = x + Scaled(6); tx < x + w - Scaled(2); tx += Scaled(3)) {
    SelectObject(dc, pen_light);
    MoveToEx(dc, tx, y + Scaled(11), nullptr);
    LineTo(dc, tx, y + Scaled(13));
    SelectObject(dc, pen_dark);
    MoveToEx(dc, tx + Scaled(1), y + Scaled(13), nullptr);
    LineTo(dc, tx + Scaled(1), y + Scaled(16));
  }

  HBRUSH slider_brush = CreateSolidBrush(RGB(203, 213, 225));
  SelectObject(dc, slider_brush);
  SelectObject(dc, pen_dark);
  RoundRect(dc, x + Scaled(2), y + Scaled(10), x + Scaled(6), y + Scaled(17), Scaled(2), Scaled(2));
  RoundRect(dc, x + Scaled(3), y + Scaled(15), x + Scaled(6), y + Scaled(20), Scaled(2), Scaled(2));

  SelectObject(dc, old_p);
  SelectObject(dc, old_b);
  DeleteObject(pen_light);
  DeleteObject(pen_dark);
  DeleteObject(slider_brush);
  DeleteObject(zip_brush);
  DeleteObject(flap_brush);
  DeleteObject(body_brush);
  DeleteObject(tab_brush);
}

void DrawDocumentIcon(HDC dc, int x, int y, int w, int h, const std::wstring& ext) {
  const int fold = Scaled(6);
  POINT pts[5] = {
      {x, y},
      {x + w - fold, y},
      {x + w, y + fold},
      {x + w, y + h},
      {x, y + h}};
  HBRUSH sheet_brush = CreateSolidBrush(RGB(255, 255, 255));
  HPEN sheet_pen = CreatePen(PS_SOLID, Scaled(1), RGB(203, 213, 225));
  HGDIOBJ old_b = SelectObject(dc, sheet_brush);
  HGDIOBJ old_p = SelectObject(dc, sheet_pen);
  Polygon(dc, pts, 5);

  POINT flap_pts[3] = {
      {x + w - fold, y},
      {x + w - fold, y + fold},
      {x + w, y + fold}};
  HBRUSH flap_brush = CreateSolidBrush(RGB(241, 245, 249));
  SelectObject(dc, flap_brush);
  Polygon(dc, flap_pts, 3);
  DeleteObject(flap_brush);

  COLORREF badge_color = RGB(148, 163, 184);
  std::wstring badge_text = L"DOC";
  if (ext == L".pdf") {
    badge_color = RGB(239, 68, 68);
    badge_text = L"PDF";
  } else if (ext == L".doc" || ext == L".docx") {
    badge_color = RGB(37, 99, 235);
    badge_text = L"DOC";
  } else if (ext == L".xls" || ext == L".xlsx" || ext == L".csv") {
    badge_color = RGB(16, 185, 129);
    badge_text = L"XLS";
  } else if (ext == L".ppt" || ext == L".pptx") {
    badge_color = RGB(249, 115, 22);
    badge_text = L"PPT";
  } else if (ext == L".txt") {
    badge_color = RGB(107, 114, 128);
    badge_text = L"TXT";
  }

  RECT badge_rect{x + Scaled(2), y + h - Scaled(11), x + w - Scaled(2), y + h - Scaled(2)};
  FillRounded(dc, badge_rect, Scaled(2), badge_color);

  SetTextColor(dc, RGB(255, 255, 255));
  SelectObject(dc, g_sub_font);
  DrawTextW(dc, badge_text.c_str(), -1, &badge_rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

  SelectObject(dc, old_p);
  SelectObject(dc, old_b);
  DeleteObject(sheet_pen);
  DeleteObject(sheet_brush);
}

void DrawAudioIcon(HDC dc, int x, int y, int w, int h) {
  const int fold = Scaled(5);
  POINT pts[5] = {
      {x, y},
      {x + w - fold, y},
      {x + w, y + fold},
      {x + w, y + h},
      {x, y + h}};
  HBRUSH sheet_brush = CreateSolidBrush(RGB(255, 255, 255));
  HPEN sheet_pen = CreatePen(PS_SOLID, Scaled(1), RGB(196, 199, 197));
  HGDIOBJ old_b = SelectObject(dc, sheet_brush);
  HGDIOBJ old_p = SelectObject(dc, sheet_pen);
  Polygon(dc, pts, 5);

  POINT flap_pts[3] = {
      {x + w - fold, y},
      {x + w - fold, y + fold},
      {x + w, y + fold}};
  HBRUSH flap_brush = CreateSolidBrush(RGB(235, 238, 240));
  SelectObject(dc, flap_brush);
  Polygon(dc, flap_pts, 3);
  DeleteObject(flap_brush);

  RECT badge_rect{x + Scaled(2), y + h - Scaled(11), x + w - Scaled(2), y + h - Scaled(2)};
  FillRounded(dc, badge_rect, Scaled(2), RGB(168, 85, 247));

  SetTextColor(dc, RGB(255, 255, 255));
  SelectObject(dc, g_sub_font);
  DrawTextW(dc, L"MP3", -1, &badge_rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

  SelectObject(dc, old_p);
  SelectObject(dc, old_b);
  DeleteObject(sheet_pen);
  DeleteObject(sheet_brush);
}

void DrawItemIcon(HDC dc, const std::wstring& name, const std::wstring& full_path, int x, int y) {
  const std::wstring ext = ExtractExtension(name);
  if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".webp" ||
      ext == L".gif" || ext == L".bmp" || ext == L".svg" || ext == L".ico" ||
      ext == L".avif") {
    DrawImageIcon(dc, x, y, Scaled(19), Scaled(25));
    return;
  }
  if (ext == L".mkv" || ext == L".mp4" || ext == L".avi" || ext == L".mov" ||
      ext == L".webm" || ext == L".flv" || ext == L".wmv") {
    DrawVideoConeIcon(dc, x, y, Scaled(22), Scaled(25));
    return;
  }
  if (ext == L".zip" || ext == L".rar" || ext == L".7z" || ext == L".tar" ||
      ext == L".gz" || ext == L".bz2") {
    DrawZipFolderIcon(dc, x, y + Scaled(2), Scaled(24), Scaled(21));
    return;
  }
  if (ext == L".mp3" || ext == L".wav" || ext == L".ogg" || ext == L".flac" ||
      ext == L".m4a") {
    DrawAudioIcon(dc, x, y, Scaled(19), Scaled(25));
    return;
  }
  if (ext == L".pdf" || ext == L".doc" || ext == L".docx" || ext == L".txt" ||
      ext == L".xls" || ext == L".xlsx" || ext == L".ppt" || ext == L".pptx" ||
      ext == L".csv") {
    DrawDocumentIcon(dc, x, y, Scaled(19), Scaled(25), ext);
    return;
  }
  HICON shell_icon = GetCachedIcon(name, full_path);
  if (shell_icon) {
    DrawIconEx(dc, x, y, shell_icon, Scaled(24), Scaled(24), 0, nullptr, DI_NORMAL);
  } else {
    DrawDocumentIcon(dc, x, y, Scaled(19), Scaled(25), ext);
  }
}

void DrawFolderIcon(HDC dc, int cx, int cy, COLORREF color) {
  EnsureGdiplus();
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  const float stroke = static_cast<float>(std::max(1, Scaled(16))) / 10.0f;
  Gdiplus::Pen pen(ToGdiplusColor(color), stroke);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);

  const float w = static_cast<float>(Scaled(18));
  const float h = static_cast<float>(Scaled(14));
  const float x = static_cast<float>(cx) - w * 0.5f;
  const float y = static_cast<float>(cy) - h * 0.5f;
  const float tab_w = static_cast<float>(Scaled(6));
  const float tab_h = static_cast<float>(Scaled(3));
  const float r = static_cast<float>(Scaled(2));

  Gdiplus::GraphicsPath path;
  // Tab top-left corner
  path.AddArc(x, y, r * 2.0f, r * 2.0f, 180.0f, 90.0f);
  // Tab top edge
  path.AddLine(x + r, y, x + tab_w, y);
  // Slanted tab shoulder
  path.AddLine(x + tab_w, y, x + tab_w + static_cast<float>(Scaled(2)), y + tab_h);
  // Right top edge
  path.AddLine(x + tab_w + static_cast<float>(Scaled(2)), y + tab_h, x + w - r, y + tab_h);
  // Body top-right corner
  path.AddArc(x + w - r * 2.0f, y + tab_h, r * 2.0f, r * 2.0f, 270.0f, 90.0f);
  // Right edge
  path.AddLine(x + w, y + tab_h + r, x + w, y + h - r);
  // Bottom-right corner
  path.AddArc(x + w - r * 2.0f, y + h - r * 2.0f, r * 2.0f, r * 2.0f, 0.0f, 90.0f);
  // Bottom edge
  path.AddLine(x + w - r, y + h, x + r, y + h);
  // Bottom-left corner
  path.AddArc(x, y + h - r * 2.0f, r * 2.0f, r * 2.0f, 90.0f, 90.0f);
  // Left edge
  path.AddLine(x, y + h - r, x, y + r);
  path.CloseFigure();

  g.DrawPath(&pen, &path);
}

void DrawOpenIcon(HDC dc, int cx, int cy, COLORREF color) {
  EnsureGdiplus();
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  const float stroke = static_cast<float>(std::max(1, Scaled(16))) / 10.0f;
  Gdiplus::Pen pen(ToGdiplusColor(color), stroke);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);

  const float bw = static_cast<float>(Scaled(12));
  const float bh = static_cast<float>(Scaled(12));
  const float bx = static_cast<float>(cx) - static_cast<float>(Scaled(7));
  const float by = static_cast<float>(cy) - static_cast<float>(Scaled(4));
  const float r = static_cast<float>(Scaled(2));

  // 1. Box with open top-right
  Gdiplus::GraphicsPath box_path;
  box_path.AddLine(bx + bw * 0.5f, by, bx + r, by);
  box_path.AddArc(bx, by, r * 2.0f, r * 2.0f, 270.0f, -90.0f);
  box_path.AddLine(bx, by + r, bx, by + bh - r);
  box_path.AddArc(bx, by + bh - r * 2.0f, r * 2.0f, r * 2.0f, 180.0f, -90.0f);
  box_path.AddLine(bx + r, by + bh, bx + bw - r, by + bh);
  box_path.AddArc(bx + bw - r * 2.0f, by + bh - r * 2.0f, r * 2.0f, r * 2.0f, 90.0f, -90.0f);
  box_path.AddLine(bx + bw, by + bh - r, bx + bw, by + bh * 0.5f);

  g.DrawPath(&pen, &box_path);

  // 2. Diagonal arrow pointing top-right
  const float arrow_x = static_cast<float>(cx) + static_cast<float>(Scaled(6));
  const float arrow_y = static_cast<float>(cy) - static_cast<float>(Scaled(6));
  const float start_x = static_cast<float>(cx) - static_cast<float>(Scaled(2));
  const float start_y = static_cast<float>(cy) + static_cast<float>(Scaled(2));

  // Shaft
  g.DrawLine(&pen, start_x, start_y, arrow_x, arrow_y);

  // Arrowhead arms
  const float arm = static_cast<float>(Scaled(5));
  Gdiplus::GraphicsPath arrow_head;
  arrow_head.AddLine(arrow_x - arm, arrow_y, arrow_x, arrow_y);
  arrow_head.AddLine(arrow_x, arrow_y, arrow_x, arrow_y + arm);
  g.DrawPath(&pen, &arrow_head);
}

void DrawTrashIcon(HDC dc, int cx, int cy, COLORREF color) {
  EnsureGdiplus();
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  const float stroke = static_cast<float>(std::max(1, Scaled(16))) / 10.0f;
  Gdiplus::Pen pen(ToGdiplusColor(color), stroke);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);

  const float fcx = static_cast<float>(cx);
  const float fcy = static_cast<float>(cy);

  // 1. Top handle
  const float handle_hw = static_cast<float>(Scaled(2.5f));
  const float handle_top = fcy - static_cast<float>(Scaled(7.0f));
  const float lid_y = fcy - static_cast<float>(Scaled(5.0f));
  const float hr = static_cast<float>(Scaled(1.0f));

  Gdiplus::GraphicsPath handle_path;
  handle_path.AddLine(fcx - handle_hw, lid_y, fcx - handle_hw, handle_top + hr);
  handle_path.AddArc(fcx - handle_hw, handle_top, hr * 2.0f, hr * 2.0f, 180.0f, 90.0f);
  handle_path.AddLine(fcx - handle_hw + hr, handle_top, fcx + handle_hw - hr, handle_top);
  handle_path.AddArc(fcx + handle_hw - hr * 2.0f, handle_top, hr * 2.0f, hr * 2.0f, 270.0f, 90.0f);
  handle_path.AddLine(fcx + handle_hw, handle_top + hr, fcx + handle_hw, lid_y);
  g.DrawPath(&pen, &handle_path);

  // 2. Lid horizontal bar
  const float lid_hw = static_cast<float>(Scaled(7.0f));
  g.DrawLine(&pen, fcx - lid_hw, lid_y, fcx + lid_hw, lid_y);

  // 3. Bucket body with rounded bottom corners
  const float bucket_top_hw = static_cast<float>(Scaled(5.2f));
  const float bucket_bot_hw = static_cast<float>(Scaled(4.2f));
  const float bucket_top_y = lid_y + static_cast<float>(Scaled(2.0f));
  const float bucket_bot_y = fcy + static_cast<float>(Scaled(6.5f));
  const float br = static_cast<float>(Scaled(1.5f));

  Gdiplus::GraphicsPath bucket_path;
  bucket_path.AddLine(fcx - bucket_top_hw, bucket_top_y,
                      fcx - bucket_bot_hw, bucket_bot_y - br);
  bucket_path.AddArc(fcx - bucket_bot_hw, bucket_bot_y - br * 2.0f,
                     br * 2.0f, br * 2.0f, 180.0f, -90.0f);
  bucket_path.AddLine(fcx - bucket_bot_hw + br, bucket_bot_y,
                      fcx + bucket_bot_hw - br, bucket_bot_y);
  bucket_path.AddArc(fcx + bucket_bot_hw - br * 2.0f, bucket_bot_y - br * 2.0f,
                     br * 2.0f, br * 2.0f, 90.0f, -90.0f);
  bucket_path.AddLine(fcx + bucket_bot_hw, bucket_bot_y - br,
                      fcx + bucket_top_hw, bucket_top_y);

  g.DrawPath(&pen, &bucket_path);

  // 4. Two vertical ribs inside bucket
  const float rib_hw = static_cast<float>(Scaled(1.8f));
  const float rib_top = bucket_top_y + static_cast<float>(Scaled(2.0f));
  const float rib_bot = bucket_bot_y - static_cast<float>(Scaled(2.5f));

  g.DrawLine(&pen, fcx - rib_hw, rib_top, fcx - rib_hw, rib_bot);
  g.DrawLine(&pen, fcx + rib_hw, rib_top, fcx + rib_hw, rib_bot);
}

void PaintPanel(HDC dc, const RECT& client) {
  // 1. Background and Border
  HBRUSH bg_brush = CreateSolidBrush(kBg);
  FillRect(dc, &client, bg_brush);
  DeleteObject(bg_brush);

  HBRUSH border_brush = CreateSolidBrush(kBorder);
  FrameRect(dc, &client, border_brush);
  DeleteObject(border_brush);

  SetBkMode(dc, TRANSPARENT);
  g_hits.clear();

  // 2. Header
  RECT header_text{client.left + Scaled(24), client.top + Scaled(18),
                   client.right - Scaled(60), client.top + Scaled(42)};
  SelectObject(dc, g_header_font);
  SetTextColor(dc, kTextPrimary);
  const wchar_t* title_str = mtk::strings::Get(mtk::strings::Id::kRecentDownloadHistory);
  if (!title_str || title_str[0] == L'\0') title_str = L"Recent download history";
  DrawTextW(dc, title_str, -1, &header_text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

  // Close button (subtle circle with clean X)
  const int cb_cx = client.right - Scaled(28);
  const int cb_cy = client.top + Scaled(28);
  const int cb_r = Scaled(12);
  RECT close_hit{cb_cx - cb_r - Scaled(4), cb_cy - cb_r - Scaled(4),
                 cb_cx + cb_r + Scaled(4), cb_cy + cb_r + Scaled(4)};

  {
    EnsureGdiplus();
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const float fcx = static_cast<float>(cb_cx);
    const float fcy = static_cast<float>(cb_cy);
    const float fr = static_cast<float>(cb_r);

    if (g_close_hovered) {
      Gdiplus::SolidBrush cb_hover(ToGdiplusColor(kCloseHoverBg));
      g.FillEllipse(&cb_hover, fcx - fr, fcy - fr, fr * 2.0f, fr * 2.0f);
    }

    const COLORREF circle_color = g_close_hovered ? RGB(17, 24, 39) : kCloseCircle;
    Gdiplus::Pen circle_pen(ToGdiplusColor(circle_color), static_cast<float>(std::max(1, Scaled(1))));
    g.DrawEllipse(&circle_pen, fcx - fr, fcy - fr, fr * 2.0f, fr * 2.0f);

    // Draw 'X' inside with rounded ends and crisp anti-aliasing
    const float xr = static_cast<float>(Scaled(35)) / 10.0f;
    const float x_stroke = static_cast<float>(std::max(1, Scaled(14))) / 10.0f;
    Gdiplus::Pen x_pen(ToGdiplusColor(circle_color), x_stroke);
    x_pen.SetStartCap(Gdiplus::LineCapRound);
    x_pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&x_pen, fcx - xr, fcy - xr, fcx + xr, fcy + xr);
    g.DrawLine(&x_pen, fcx + xr, fcy - xr, fcx - xr, fcy + xr);
  }

  g_hits.push_back(Hit{close_hit, Action::kClose, 0});

  // 3. List Content Area
  const int list_top = client.top + Scaled(kBaseHeaderHeight);
  const int list_bottom = client.bottom - Scaled(kBaseFooterHeight);
  const int list_height = list_bottom - list_top;

  const auto raw_items = VisibleItems();
  const int text_width = client.right - Scaled(110);
  const auto layouts = MeasureRows(dc, raw_items, text_width);

  int total_content_h = 0;
  for (const auto& l : layouts) total_content_h += l.height;

  g_max_scroll = std::max(0, total_content_h - list_height);
  g_scroll_y = std::clamp(g_scroll_y, 0, g_max_scroll);

  // Clipping region for scrollable items
  HRGN clip_rgn = CreateRectRgn(client.left, list_top, client.right - Scaled(8), list_bottom);
  SelectClipRgn(dc, clip_rgn);

  if (raw_items.empty()) {
    RECT empty_rect{client.left + Scaled(24), list_top + Scaled(30),
                    client.right - Scaled(24), list_top + Scaled(70)};
    SelectObject(dc, g_item_title_font);
    SetTextColor(dc, kTextSecondary);
    DrawTextW(dc, L"No recent downloads", -1, &empty_rect,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  }

  for (size_t i = 0; i < layouts.size(); ++i) {
    const auto& l = layouts[i];
    const int item_top = list_top + l.content_y - g_scroll_y;
    const int item_bottom = item_top + l.height;

    // Check visibility
    if (item_bottom < list_top || item_top > list_bottom) continue;

    RECT row_rect{client.left + Scaled(12), item_top,
                  client.right - Scaled(12), item_bottom};

    const bool row_hovered = (g_hovered_item_id == l.item.id);

    // Vertical center for all icons and buttons
    const bool running = (l.item.state == mtk::downloads::State::kInProgress ||
                          l.item.state == mtk::downloads::State::kPaused);
    const int cy_mid = item_top + (running ? Scaled(24) : Scaled(28));

    // Action button bounds (always computed so text bounding box is strictly fixed)
    const int btn_w = Scaled(28);
    const int btn_h = Scaled(28);
    const int btn_gap = Scaled(4);
    const int btn_y = cy_mid - btn_h / 2;

    const int trash_r = row_rect.right - Scaled(6);
    const int trash_l = trash_r - btn_w;
    RECT trash_rect{trash_l, btn_y, trash_r, btn_y + btn_h};

    const int open_r = trash_l - btn_gap;
    const int open_l = open_r - btn_w;
    RECT open_rect{open_l, btn_y, open_r, btn_y + btn_h};

    const int folder_r = open_l - btn_gap;
    const int folder_l = folder_r - btn_w;
    RECT folder_rect{folder_l, btn_y, folder_r, btn_y + btn_h};

    // Text column boundaries: strictly stops Scaled(12) before the folder button!
    const int tx = client.left + Scaled(56);
    const int text_right = folder_l - Scaled(12);

    if (row_hovered) {
      FillRounded(dc, row_rect, Scaled(8), kHoverBg);

      // Register buttons before row_rect so they are hit-tested first!
      g_hits.push_back(Hit{folder_rect, Action::kFolder, l.item.id});
      g_hits.push_back(Hit{open_rect, Action::kOpen, l.item.id});
      g_hits.push_back(Hit{trash_rect, Action::kRemove, l.item.id});

      const bool folder_hover = (g_hovered_action == Action::kFolder);
      const bool open_hover = (g_hovered_action == Action::kOpen);
      const bool trash_hover = (g_hovered_action == Action::kRemove);

      if (folder_hover) FillRounded(dc, folder_rect, Scaled(6), RGB(229, 231, 235));
      if (open_hover) FillRounded(dc, open_rect, Scaled(6), RGB(229, 231, 235));
      if (trash_hover) FillRounded(dc, trash_rect, Scaled(6), RGB(254, 226, 226));

      DrawFolderIcon(dc, (folder_l + folder_r) / 2, cy_mid,
                     folder_hover ? RGB(17, 24, 39) : RGB(75, 85, 99));
      DrawOpenIcon(dc, (open_l + open_r) / 2, cy_mid,
                   open_hover ? RGB(17, 24, 39) : RGB(75, 85, 99));
      DrawTrashIcon(dc, (trash_l + trash_r) / 2, cy_mid,
                    trash_hover ? RGB(220, 38, 38) : RGB(75, 85, 99));
    }

    // Register row hit after buttons
    g_hits.push_back(Hit{row_rect, Action::kItem, l.item.id});

    // Left file icon: centered vertically at cy_mid
    const int ix = client.left + Scaled(24);
    const int iy = cy_mid - Scaled(13);
    DrawItemIcon(dc, l.item.file_name, l.item.full_path, ix, iy);

    // Title text (single line, elided cleanly with ellipsis)
    RECT title_rect{tx, cy_mid - Scaled(18), text_right, cy_mid};
    SelectObject(dc, g_item_title_font);
    SetTextColor(dc, kTextPrimary);
    DrawTextW(dc, l.item.file_name.c_str(), -1, &title_rect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Subtitle text
    std::wstring sub_str;
    if (running) {
      sub_str = FormatSize(l.item.received_bytes) + L" / " +
                FormatSize(l.item.total_bytes) +
                (l.item.state == mtk::downloads::State::kPaused ? L" \u2022 Paused"
                                                               : L" \u2022 Downloading");
    } else if (l.item.state == mtk::downloads::State::kFailed) {
      sub_str = FormatSize(l.item.total_bytes) + L" \u2022 Failed";
    } else if (l.item.state == mtk::downloads::State::kCanceled) {
      sub_str = FormatSize(l.item.total_bytes) + L" \u2022 Canceled";
    } else {
      const int64_t sz = l.item.received_bytes > 0 ? l.item.received_bytes : l.item.total_bytes;
      sub_str = FormatSize(sz) + L" \u2022 " + FormatTimeAgo(l.item);
    }

    RECT sub_rect{tx, cy_mid + Scaled(1), text_right, cy_mid + Scaled(17)};
    SelectObject(dc, g_sub_font);
    SetTextColor(dc, kTextSecondary);
    DrawTextW(dc, sub_str.c_str(), -1, &sub_rect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Progress bar if active
    if (running && l.item.percent >= 0) {
      RECT track{tx, item_top + Scaled(50), text_right, item_top + Scaled(54)};
      FillRounded(dc, track, Scaled(2), kProgressTrack);
      RECT fill = track;
      fill.right = track.left + MulDiv(track.right - track.left,
                                       std::clamp(l.item.percent, 0, 100), 100);
      if (fill.right > fill.left) FillRounded(dc, fill, Scaled(2), kProgressFill);
    }
  }

  SelectClipRgn(dc, nullptr);
  DeleteObject(clip_rgn);

  // 4. Scrollbar
  if (g_max_scroll > 0) {
    const int track_h = list_height;
    const int thumb_h = std::max(Scaled(28), MulDiv(track_h, track_h, total_content_h));
    const int thumb_y = list_top + MulDiv(track_h - thumb_h, g_scroll_y, g_max_scroll);
    const int sx = client.right - Scaled(5);
    g_scrollbar_thumb_rect = RECT{sx - Scaled(2), thumb_y, client.right - Scaled(1), thumb_y + thumb_h};

    const bool sb_hovered = g_dragging_scrollbar ||
                            PtInRect(&g_scrollbar_thumb_rect, POINT{g_scrollbar_thumb_rect.left, thumb_y});
    FillRounded(dc, g_scrollbar_thumb_rect, Scaled(2), sb_hovered ? kScrollThumbHover : kScrollThumb);
  } else {
    g_scrollbar_thumb_rect = RECT{};
  }

  // 5. Divider Line
  RECT div_rect{client.left, client.bottom - Scaled(kBaseFooterHeight),
                client.right, client.bottom - Scaled(kBaseFooterHeight) + 1};
  HBRUSH div_brush = CreateSolidBrush(kDivider);
  FillRect(dc, &div_rect, div_brush);
  DeleteObject(div_brush);

  // 6. Footer
  RECT footer_rect{client.left, client.bottom - Scaled(kBaseFooterHeight) + 1,
                   client.right, client.bottom};
  if (g_show_all_hovered) {
    HBRUSH fh_brush = CreateSolidBrush(kHoverBg);
    FillRect(dc, &footer_rect, fh_brush);
    DeleteObject(fh_brush);
  }

  RECT footer_text{client.left + Scaled(24), footer_rect.top,
                   client.right - Scaled(60), footer_rect.bottom};
  SelectObject(dc, g_footer_font);
  SetTextColor(dc, kTextPrimary);
  const wchar_t* full_str = mtk::strings::Get(mtk::strings::Id::kFullDownloadHistory);
  if (!full_str || full_str[0] == L'\0') full_str = L"Full download history";
  DrawTextW(dc, full_str, -1, &footer_text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

  // External link icon
  const int fx = client.right - Scaled(30);
  const int fy = footer_rect.top + (footer_rect.bottom - footer_rect.top) / 2;
  DrawOpenIcon(dc, fx, fy, kTextSecondary);

  g_hits.push_back(Hit{footer_rect, Action::kShowAll, 0});
}

void ResizeToContent() {
  if (!g_panel) return;
  HDC dc = GetDC(g_panel);
  const auto items = VisibleItems();
  const int text_width = Scaled(kBaseWidth) - Scaled(100);
  const auto layouts = MeasureRows(dc, items, text_width);
  ReleaseDC(g_panel, dc);

  const int width = Scaled(kBaseWidth);
  const int height = CalculatePanelHeight(layouts);
  RECT bounds{};
  GetWindowRect(g_panel, &bounds);
  SetWindowPos(g_panel, HWND_TOPMOST, bounds.left, bounds.top, width, height,
               SWP_NOACTIVATE);
  InvalidateRect(g_panel, nullptr, TRUE);
}

void ShowItemContextMenu(HWND window, const mtk::downloads::Item& item, POINT pt) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  AppendMenuW(menu, MF_STRING, 1, L"Open");
  AppendMenuW(menu, MF_STRING, 2, L"Show in folder");
  AppendMenuW(menu, MF_STRING, 3, L"Copy to clipboard");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 4, L"Remove from history");

  SetMenuDefaultItem(menu, 1, FALSE);
  const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      pt.x, pt.y, window, nullptr);
  DestroyMenu(menu);

  switch (chosen) {
    case 1:
      OpenDownloadedFile(window, item);
      break;
    case 2:
      ShowInFolder(item.full_path);
      break;
    case 3:
      CopyFileToClipboard(window, item.full_path);
      break;
    case 4:
      if (mtk::downloads::Manager::Get().Remove(item.id)) {
        mtk::downloads::Manager::Get().Flush();
        ResizeToContent();
      }
      break;
  }
}

void RunAction(const Hit& hit) {
  switch (hit.action) {
    case Action::kClose:
      ClosePanel();
      break;
    case Action::kItem: {
      if (const auto* item = FindItem(hit.id)) {
        if (item->state == mtk::downloads::State::kComplete) {
          OpenDownloadedFile(g_panel, *item);
        } else if (item->state == mtk::downloads::State::kInProgress) {
          if (g_callbacks.pause && g_callbacks.controllable && g_callbacks.controllable(item->id)) {
            g_callbacks.pause(item->id);
          }
        } else if (item->state == mtk::downloads::State::kPaused) {
          if (g_callbacks.resume && g_callbacks.controllable && g_callbacks.controllable(item->id)) {
            g_callbacks.resume(item->id);
          }
        } else if (mtk::downloads::CanRetry(*item) && g_callbacks.retry) {
          g_callbacks.retry(item->id);
        }
      }
      break;
    }
    case Action::kFolder: {
      if (const auto* item = FindItem(hit.id)) {
        ShowInFolder(item->full_path);
      }
      break;
    }
    case Action::kOpen: {
      if (const auto* item = FindItem(hit.id)) {
        OpenDownloadedFile(g_panel, *item);
      }
      break;
    }
    case Action::kRemove: {
      if (mtk::downloads::Manager::Get().Remove(hit.id)) {
        mtk::downloads::Manager::Get().Flush();
        ResizeToContent();
      }
      break;
    }
    case Action::kShowAll: {
      ClosePanel();
      if (g_callbacks.show_all) {
        g_callbacks.show_all();
      } else {
        HWND main_win = g_anchor ? GetAncestor(g_anchor, GA_ROOT)
                                 : (g_panel ? GetWindow(g_panel, GW_OWNER) : nullptr);
        if (!main_win && g_panel) main_win = GetAncestor(g_panel, GA_ROOT);
        HWND address_edit = main_win ? GetDlgItem(main_win, 1005) : nullptr;
        if (address_edit) {
          SetWindowTextW(address_edit, L"mtk://downloads/");
          SendMessageW(address_edit, WM_KEYDOWN, VK_RETURN, 0);
        }
      }
      break;
    }
    default:
      break;
  }
  if (g_panel) InvalidateRect(g_panel, nullptr, FALSE);
}

LRESULT CALLBACK PanelProc(HWND window, UINT message, WPARAM w_param,
                           LPARAM l_param) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);

      // Double-buffering for flicker-free rendering
      HDC mem_dc = CreateCompatibleDC(dc);
      HBITMAP mem_bm = CreateCompatibleBitmap(dc, client.right, client.bottom);
      HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);

      PaintPanel(mem_dc, client);

      BitBlt(dc, 0, 0, client.right, client.bottom, mem_dc, 0, 0, SRCCOPY);
      SelectObject(mem_dc, old_bm);
      DeleteObject(mem_bm);
      DeleteDC(mem_dc);

      EndPaint(window, &paint);
      return 0;
    }
    case WM_MOUSEWHEEL: {
      const short delta = GET_WHEEL_DELTA_WPARAM(w_param);
      g_scroll_y -= (delta / WHEEL_DELTA) * Scaled(42);
      g_scroll_y = std::clamp(g_scroll_y, 0, g_max_scroll);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};

      if (g_dragging_scrollbar && g_max_scroll > 0) {
        RECT client{};
        GetClientRect(window, &client);
        const int list_height = client.bottom - Scaled(kBaseFooterHeight) - Scaled(kBaseHeaderHeight);
        const int delta_y = point.y - g_drag_start_y;
        g_scroll_y = g_drag_scroll_start + MulDiv(delta_y, g_max_scroll, list_height);
        g_scroll_y = std::clamp(g_scroll_y, 0, g_max_scroll);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }

      uint64_t hovered_id = 0;
      Action hovered_action = Action::kNone;
      bool close_hovered = false;
      bool show_all_hovered = false;

      for (const Hit& hit : g_hits) {
        if (PtInRect(&hit.bounds, point)) {
          if (hit.action == Action::kClose) {
            close_hovered = true;
            hovered_action = Action::kClose;
            break;
          } else if (hit.action == Action::kShowAll) {
            show_all_hovered = true;
            hovered_action = Action::kShowAll;
            break;
          } else if (hit.action == Action::kFolder ||
                     hit.action == Action::kOpen ||
                     hit.action == Action::kRemove) {
            hovered_id = hit.id;
            hovered_action = hit.action;
            break;
          } else if (hit.action == Action::kItem) {
            hovered_id = hit.id;
            hovered_action = Action::kItem;
          }
        }
      }

      if (hovered_id != g_hovered_item_id ||
          hovered_action != g_hovered_action ||
          close_hovered != g_close_hovered ||
          show_all_hovered != g_show_all_hovered) {
        g_hovered_item_id = hovered_id;
        g_hovered_action = hovered_action;
        g_close_hovered = close_hovered;
        g_show_all_hovered = show_all_hovered;
        InvalidateRect(window, nullptr, FALSE);
      }
      TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      return 0;
    }
    case WM_MOUSELEAVE: {
      if (g_hovered_item_id != 0 || g_hovered_action != Action::kNone ||
          g_close_hovered || g_show_all_hovered) {
        g_hovered_item_id = 0;
        g_hovered_action = Action::kNone;
        g_close_hovered = false;
        g_show_all_hovered = false;
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (g_max_scroll > 0 && PtInRect(&g_scrollbar_thumb_rect, point)) {
        g_dragging_scrollbar = true;
        g_drag_start_y = point.y;
        g_drag_scroll_start = g_scroll_y;
        SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      break;
    }
    case WM_LBUTTONUP: {
      if (g_dragging_scrollbar) {
        g_dragging_scrollbar = false;
        ReleaseCapture();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      const Hit* target_hit = nullptr;
      for (const Hit& hit : g_hits) {
        if (PtInRect(&hit.bounds, point)) {
          if (hit.action == Action::kFolder ||
              hit.action == Action::kOpen ||
              hit.action == Action::kRemove ||
              hit.action == Action::kClose ||
              hit.action == Action::kShowAll) {
            target_hit = &hit;
            break;
          }
          if (hit.action == Action::kItem && !target_hit) {
            target_hit = &hit;
          }
        }
      }
      if (target_hit) {
        RunAction(*target_hit);
      }
      return 0;
    }
    case WM_RBUTTONUP: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      for (const Hit& hit : g_hits) {
        if (hit.action == Action::kItem && PtInRect(&hit.bounds, point)) {
          if (const auto* item = FindItem(hit.id)) {
            POINT screen_pt = point;
            ClientToScreen(window, &screen_pt);
            ShowItemContextMenu(window, *item, screen_pt);
          }
          break;
        }
      }
      return 0;
    }
    case WM_SETCURSOR: {
      if (LOWORD(l_param) == HTCLIENT) {
        if (g_hovered_item_id != 0 || g_close_hovered || g_show_all_hovered ||
            g_hovered_action != Action::kNone) {
          SetCursor(LoadCursor(nullptr, IDC_HAND));
          return TRUE;
        }
      }
      break;
    }
    case WM_KEYDOWN:
      if (w_param == VK_ESCAPE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_ACTIVATE:
      if (g_dismiss_armed && LOWORD(w_param) == WA_INACTIVE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_TIMER:
      if (w_param == kDismissTimer && g_dismiss_armed && !PanelHoldsForeground()) {
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      KillTimer(window, kDismissTimer);
      g_panel = nullptr;
      g_anchor = nullptr;
      g_dismiss_armed = false;
      g_hits.clear();
      g_hovered_item_id = 0;
      g_hovered_action = Action::kNone;
      g_close_hovered = false;
      g_show_all_hovered = false;
      g_scroll_y = 0;
      g_dragging_scrollbar = false;
      ReleaseFonts();
      ReleaseIcons();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

bool EnsureClass(HINSTANCE instance) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW description{sizeof(WNDCLASSEXW)};
  description.lpfnWndProc = PanelProc;
  description.hInstance = instance;
  description.hCursor = LoadCursor(nullptr, IDC_ARROW);
  description.lpszClassName = kClassName;
  registered = RegisterClassExW(&description) != 0 ||
               GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return registered;
}

}  // namespace

bool IsOpen() { return g_panel != nullptr; }

void ClosePanel() {
  g_is_private = false;
  if (g_panel) PostMessageW(g_panel, WM_CLOSE, 0, 0);
}

void Refresh() {
  if (!g_panel) return;
  ResizeToContent();
}

void TogglePanel(HINSTANCE instance, HWND owner, HWND anchor,
                 Callbacks callbacks, bool is_private) {
  if (g_panel) {
    const bool same_anchor = g_anchor == anchor;
    ClosePanel();
    if (same_anchor) return;
  }
  if (!EnsureClass(instance)) return;

  g_is_private = is_private;
  g_instance = instance;
  g_callbacks = std::move(callbacks);
  g_dpi = static_cast<int>(GetDpiForWindow(owner));
  if (g_dpi <= 0) g_dpi = 96;
  CreateFonts();

  HDC dc = GetDC(owner);
  const auto items = VisibleItems();
  const int text_width = Scaled(kBaseWidth) - Scaled(100);
  const auto layouts = MeasureRows(dc, items, text_width);
  ReleaseDC(owner, dc);

  RECT button{};
  GetWindowRect(anchor, &button);
  const int width = Scaled(kBaseWidth);
  const int height = CalculatePanelHeight(layouts);

  int x = button.right - width;
  int y = button.bottom + Scaled(4);
  HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(MONITORINFO)};
  if (GetMonitorInfoW(monitor, &info)) {
    x = std::clamp<int>(x, info.rcWork.left + Scaled(4),
                        std::max<int>(info.rcWork.left + Scaled(4),
                                      info.rcWork.right - width - Scaled(4)));
    if (y + height > info.rcWork.bottom) {
      y = std::max<int>(info.rcWork.top + Scaled(4), button.top - height - Scaled(4));
    }
  }

  g_panel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"",
                            WS_POPUP | WS_BORDER, x, y, width, height, owner,
                            nullptr, instance, nullptr);
  if (!g_panel) {
    ReleaseFonts();
    return;
  }
  g_anchor = anchor;

  // DWM round corners
  const int corner_pref = DWMWCP_ROUND;
  DwmSetWindowAttribute(g_panel, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_pref,
                        sizeof(corner_pref));

  ShowWindow(g_panel, SW_SHOWNOACTIVATE);
  SetForegroundWindow(g_panel);
  g_dismiss_armed = true;
  SetTimer(g_panel, kDismissTimer, kDismissPollMs, nullptr);
  InvalidateRect(g_panel, nullptr, TRUE);
}

}  // namespace mtk::downloads_panel
