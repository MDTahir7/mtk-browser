#include "screenshot_panel.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "strings.h"

namespace mtk::screenshot_panel {
namespace {

constexpr wchar_t kClassName[] = L"MTKScreenshotPanel";
constexpr UINT_PTR kDismissTimer = 1;
constexpr UINT kDismissPollMs = 120;

// Base metrics at 96 DPI
constexpr int kBaseWidth = 336;
constexpr int kBaseHeaderHeight = 38;
constexpr int kBaseCardHeight = 66;
constexpr int kBaseGap = 8;
constexpr int kBasePadding = 10;
constexpr int kBaseFooterHeight = 32;
constexpr int kBaseRadius = 14;

// Pure Matte Black & Red Luxury Palette
constexpr COLORREF kBg = RGB(12, 12, 14);                // Pitch matte black
constexpr COLORREF kBorder = RGB(38, 38, 44);            // Dark titanium border
constexpr COLORREF kGlassHighlight = RGB(80, 24, 28);    // Subtle ruby specular rim

// Header
constexpr COLORREF kHeaderBg = RGB(18, 18, 22);          // Elevated matte black
constexpr COLORREF kHeaderBorder = RGB(34, 34, 40);      // Divider line
constexpr COLORREF kHeaderText = RGB(245, 245, 250);     // Crisp white
constexpr COLORREF kHeaderEscBg = RGB(26, 26, 30);       // Keycap fill
constexpr COLORREF kHeaderEscBorder = RGB(46, 46, 54);   // Keycap border
constexpr COLORREF kHeaderEscText = RGB(170, 170, 180);  // Keycap label

// Card Base & Hover
constexpr COLORREF kCardBg = RGB(20, 20, 24);            // Sleek dark surface
constexpr COLORREF kCardBorder = RGB(36, 36, 42);        // Subtle card border
constexpr COLORREF kCardHoverBg = RGB(32, 16, 20);       // Deep velvet crimson fill
constexpr COLORREF kCardHoverBorder = RGB(239, 68, 68);  // Vivid Scarlet Red (#ef4444)
constexpr COLORREF kCardPressedBg = RGB(24, 12, 16);     // Pressed depression

// Icon Badges
constexpr COLORREF kBadgeBg = RGB(28, 26, 30);
constexpr COLORREF kBadgeBorder = RGB(48, 42, 46);
constexpr COLORREF kBadgeHoverBg = RGB(54, 18, 24);      // Rich ruby red
constexpr COLORREF kBadgeHoverBorder = RGB(185, 28, 28); // Ruby red border
constexpr COLORREF kIconColor = RGB(235, 235, 242);      // Crisp silver
constexpr COLORREF kIconHoverColor = RGB(255, 255, 255); // Pure white on hover

// Typography
constexpr COLORREF kTitleColor = RGB(245, 245, 250);
constexpr COLORREF kTitleHoverColor = RGB(255, 255, 255);
constexpr COLORREF kSubtitleColor = RGB(150, 150, 160);
constexpr COLORREF kSubtitleHoverColor = RGB(215, 195, 200);

// Keycap Shortcut Pills on right
constexpr COLORREF kPillBg = RGB(28, 28, 34);
constexpr COLORREF kPillBorder = RGB(46, 46, 54);
constexpr COLORREF kPillText = RGB(165, 165, 175);
constexpr COLORREF kPillHoverBg = RGB(50, 16, 22);
constexpr COLORREF kPillHoverBorder = RGB(185, 28, 28);
constexpr COLORREF kPillHoverText = RGB(252, 165, 165); // Crimson text

// Footer
constexpr COLORREF kFooterBg = RGB(14, 14, 16);
constexpr COLORREF kFooterBorder = RGB(30, 30, 36);
constexpr COLORREF kFooterText = RGB(145, 145, 155);
constexpr COLORREF kFooterIcon = RGB(239, 68, 68);       // Scarlet red checkmark

struct OptionVisual {
  const wchar_t* title_en;
  const wchar_t* title_hi;
  const wchar_t* subtitle_en;
  const wchar_t* subtitle_hi;
  const wchar_t* key_en;
  const wchar_t* key_hi;
  Mode mode;
};

const OptionVisual kOptions[3] = {
    {L"Visible area", L"दिखाई देने वाला क्षेत्र",
     L"Capture current active viewport", L"वर्तमान सक्रिय व्यूपोर्ट कैप्चर करें",
     L"Alt+1", L"Alt+1",
     Mode::kVisibleArea},

    {L"Snip region", L"क्षेत्र स्निप",
     L"Drag crosshairs to crop any selection", L"क्षेत्र चुनने के लिए ड्रैग करें",
     L"Alt+2", L"Alt+2",
     Mode::kSnipRegion},

    {L"Entire webpage", L"पूरा वेबपेज",
     L"Scroll and capture entire page", L"पूरे पेज को स्क्रॉल कर कैप्चर करें",
     L"Alt+3", L"Alt+3",
     Mode::kFullPage},
};

HWND g_panel = nullptr;
HWND g_anchor = nullptr;
HWND g_owner = nullptr;
HINSTANCE g_instance = nullptr;
Callbacks g_callbacks;
int g_dpi = 96;
int g_hovered = -1;
int g_pressed = -1;
HFONT g_header_font = nullptr;
HFONT g_title_font = nullptr;
HFONT g_subtitle_font = nullptr;
HFONT g_tag_font = nullptr;
HFONT g_footer_font = nullptr;
bool g_dismiss_armed = false;

int Scaled(int val) {
  return MulDiv(val, g_dpi, 96);
}

void ReleaseFonts() {
  auto free_font = [](HFONT& f) {
    if (f) {
      DeleteObject(f);
      f = nullptr;
    }
  };
  free_font(g_header_font);
  free_font(g_title_font);
  free_font(g_subtitle_font);
  free_font(g_tag_font);
  free_font(g_footer_font);
}

void CreateFonts() {
  ReleaseFonts();
  const auto make = [](int height, int weight) {
    return CreateFontW(Scaled(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  };
  g_header_font = make(-12, FW_SEMIBOLD);
  g_title_font = make(-13, FW_SEMIBOLD);
  g_subtitle_font = make(-10, FW_NORMAL);
  g_tag_font = make(-10, FW_NORMAL);
  g_footer_font = make(-10, FW_NORMAL);
}

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF bg, COLORREF border, int border_width = 1) {
  HBRUSH brush = CreateSolidBrush(bg);
  HPEN pen = border_width > 0 ? CreatePen(PS_SOLID, border_width, border)
                              : static_cast<HPEN>(GetStockObject(NULL_PEN));
  HGDIOBJ old_brush = SelectObject(dc, brush);
  HGDIOBJ old_pen = SelectObject(dc, pen);

  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);

  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(brush);
  if (border_width > 0) DeleteObject(pen);
}

void DrawHeaderCamera(HDC dc, int cx, int cy, int size) {
  HPEN pen = CreatePen(PS_SOLID, Scaled(1), RGB(239, 68, 68));
  HBRUSH brush = CreateSolidBrush(RGB(40, 16, 20));
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, brush);

  const int w = Scaled(16);
  const int h = Scaled(12);
  const int l = cx - w / 2;
  const int t = cy - h / 2 + Scaled(1);
  const int r = l + w;
  const int b = t + h;

  // Body
  RoundRect(dc, l, t, r, b, Scaled(3), Scaled(3));

  // Top flash notch
  RECT bump = {cx - Scaled(3), t - Scaled(2), cx + Scaled(3), t + Scaled(1)};
  HBRUSH red_brush = CreateSolidBrush(RGB(239, 68, 68));
  FillRect(dc, &bump, red_brush);
  DeleteObject(red_brush);

  // Center lens
  HPEN lens_pen = CreatePen(PS_SOLID, Scaled(1), RGB(255, 255, 255));
  SelectObject(dc, lens_pen);
  SelectObject(dc, GetStockObject(NULL_BRUSH));
  const int lr = Scaled(3);
  Ellipse(dc, cx - lr, cy - lr + Scaled(1), cx + lr + 1, cy + lr + Scaled(1) + 1);

  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(lens_pen);
  DeleteObject(brush);
  DeleteObject(pen);
}

void DrawMonitorIcon(HDC dc, int cx, int cy, int size, COLORREF color) {
  HPEN pen = CreatePen(PS_SOLID, Scaled(2), color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

  // Display screen bezel
  const int sw = Scaled(22);
  const int sh = Scaled(14);
  const int sl = cx - sw / 2;
  const int st = cy - sh / 2 - Scaled(3);
  const int sr = sl + sw;
  const int sb = st + sh;
  RoundRect(dc, sl, st, sr, sb, Scaled(3), Scaled(3));

  // Stand neck
  MoveToEx(dc, cx, sb, nullptr);
  LineTo(dc, cx, sb + Scaled(4));

  // Stand base
  MoveToEx(dc, cx - Scaled(6), sb + Scaled(4), nullptr);
  LineTo(dc, cx + Scaled(6), sb + Scaled(4));

  // Inner subtle screen horizon line
  HPEN thin_pen = CreatePen(PS_SOLID, Scaled(1), RGB(160, 160, 170));
  SelectObject(dc, thin_pen);
  MoveToEx(dc, sl + Scaled(3), cy - Scaled(2), nullptr);
  LineTo(dc, sr - Scaled(3), cy - Scaled(2));

  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(thin_pen);
  DeleteObject(pen);
}

void DrawSnipIcon(HDC dc, int cx, int cy, int size, COLORREF color) {
  HPEN pen = CreatePen(PS_SOLID, Scaled(2), color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

  const int s = Scaled(9);
  const int arm = Scaled(5);

  // 4 Corner Brackets
  // Top-left
  MoveToEx(dc, cx - s, cy - s + arm, nullptr);
  LineTo(dc, cx - s, cy - s);
  LineTo(dc, cx - s + arm, cy - s);

  // Top-right
  MoveToEx(dc, cx + s - arm, cy - s, nullptr);
  LineTo(dc, cx + s, cy - s);
  LineTo(dc, cx + s, cy - s + arm);

  // Bottom-left
  MoveToEx(dc, cx - s, cy + s - arm, nullptr);
  LineTo(dc, cx - s, cy + s);
  LineTo(dc, cx - s + arm, cy + s);

  // Bottom-right
  MoveToEx(dc, cx + s - arm, cy + s, nullptr);
  LineTo(dc, cx + s, cy + s);
  LineTo(dc, cx + s, cy + s - arm);

  // High-precision crosshairs
  MoveToEx(dc, cx - Scaled(4), cy, nullptr);
  LineTo(dc, cx + Scaled(5), cy);
  MoveToEx(dc, cx, cy - Scaled(4), nullptr);
  LineTo(dc, cx, cy + Scaled(5));

  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

void DrawFullPageIcon(HDC dc, int cx, int cy, int size, COLORREF color) {
  HPEN pen = CreatePen(PS_SOLID, Scaled(2), color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

  const int pw = Scaled(16);
  const int ph = Scaled(20);
  const int pl = cx - pw / 2;
  const int pt = cy - ph / 2;
  const int pr = pl + pw;
  const int pb = pt + ph;
  const int fold = Scaled(5);

  // Document outline with folded ear
  POINT pts[5] = {
      {pl, pt},
      {pr - fold, pt},
      {pr, pt + fold},
      {pr, pb},
      {pl, pb},
  };
  Polygon(dc, pts, 5);

  // Fold line
  MoveToEx(dc, pr - fold, pt, nullptr);
  LineTo(dc, pr - fold, pt + fold);
  LineTo(dc, pr, pt + fold);

  // Top content lines
  HPEN thin_pen = CreatePen(PS_SOLID, Scaled(1), color);
  SelectObject(dc, thin_pen);
  MoveToEx(dc, pl + Scaled(3), pt + Scaled(4), nullptr);
  LineTo(dc, pr - fold - Scaled(1), pt + Scaled(4));
  MoveToEx(dc, pl + Scaled(3), pt + Scaled(7), nullptr);
  LineTo(dc, pr - Scaled(3), pt + Scaled(7));

  // Downward auto-scroll arrow in lower body
  SelectObject(dc, pen);
  const int arrow_top = pt + Scaled(10);
  const int arrow_tip = pb - Scaled(3);
  MoveToEx(dc, cx, arrow_top, nullptr);
  LineTo(dc, cx, arrow_tip);
  // Arrowhead fins
  MoveToEx(dc, cx - Scaled(3), arrow_tip - Scaled(3), nullptr);
  LineTo(dc, cx, arrow_tip);
  LineTo(dc, cx + Scaled(3), arrow_tip - Scaled(3));

  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(thin_pen);
  DeleteObject(pen);
}

void DrawCheckmarkIcon(HDC dc, int cx, int cy, COLORREF color) {
  HPEN pen = CreatePen(PS_SOLID, Scaled(2), color);
  HGDIOBJ old_pen = SelectObject(dc, pen);

  MoveToEx(dc, cx - Scaled(4), cy, nullptr);
  LineTo(dc, cx - Scaled(1), cy + Scaled(3));
  LineTo(dc, cx + Scaled(5), cy - Scaled(4));

  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

RECT GetCardRect(int index) {
  const int pad = Scaled(kBasePadding);
  const int gap = Scaled(kBaseGap);
  const int header_h = Scaled(kBaseHeaderHeight);
  const int card_h = Scaled(kBaseCardHeight);
  const int card_w = Scaled(kBaseWidth) - pad * 2;
  const int top = header_h + pad + index * (card_h + gap);
  return RECT{pad, top, pad + card_w, top + card_h};
}

int HitTestCard(POINT pt) {
  for (int i = 0; i < 3; ++i) {
    RECT r = GetCardRect(i);
    if (PtInRect(&r, pt)) return i;
  }
  return -1;
}

void ArmDismissTimer() {
  if (!g_panel || g_dismiss_armed) return;
  SetTimer(g_panel, kDismissTimer, kDismissPollMs, nullptr);
  g_dismiss_armed = true;
}

LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT client;
      GetClientRect(hwnd, &client);

      const bool is_hindi = strings::IsHindi();

      // Memory DC for flicker-free double buffering
      HDC mem_dc = CreateCompatibleDC(dc);
      HBITMAP mem_bm = CreateCompatibleBitmap(dc, client.right, client.bottom);
      HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);

      // Create rounded region to cleanly clip background, header, and footer
      HRGN win_rgn = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1,
                                         Scaled(kBaseRadius), Scaled(kBaseRadius));
      SelectClipRgn(mem_dc, win_rgn);

      // 1. Overall Pitch Matte Black Background
      HBRUSH bg_brush = CreateSolidBrush(kBg);
      FillRect(mem_dc, &client, bg_brush);
      DeleteObject(bg_brush);

      // 2. Header Area
      const int header_h = Scaled(kBaseHeaderHeight);
      RECT header_rc = {0, 0, client.right, header_h};
      HBRUSH header_brush = CreateSolidBrush(kHeaderBg);
      FillRect(mem_dc, &header_rc, header_brush);
      DeleteObject(header_brush);

      // Header bottom border line
      HPEN hline_pen = CreatePen(PS_SOLID, 1, kHeaderBorder);
      HGDIOBJ old_pen = SelectObject(mem_dc, hline_pen);
      MoveToEx(mem_dc, 0, header_h - 1, nullptr);
      LineTo(mem_dc, client.right, header_h - 1);
      SelectObject(mem_dc, old_pen);
      DeleteObject(hline_pen);

      // Header camera icon
      DrawHeaderCamera(mem_dc, Scaled(20), header_h / 2, Scaled(16));

      // Header Title
      SelectObject(mem_dc, g_header_font);
      SetBkMode(mem_dc, TRANSPARENT);
      SetTextColor(mem_dc, kHeaderText);
      const wchar_t* header_text = is_hindi ? L"स्क्रीनशॉट" : L"Screenshot";
      RECT htext_rc = {Scaled(34), 0, client.right - Scaled(50), header_h};
      DrawTextW(mem_dc, header_text, -1, &htext_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Header [ Esc ] keycap pill
      const int esc_w = Scaled(40);
      const int esc_h = Scaled(20);
      const int esc_r = client.right - Scaled(12);
      const int esc_l = esc_r - esc_w;
      const int esc_t = (header_h - esc_h) / 2;
      RECT esc_rc = {esc_l, esc_t, esc_r, esc_t + esc_h};
      FillRounded(mem_dc, esc_rc, Scaled(4), kHeaderEscBg, kHeaderEscBorder, 1);
      SelectObject(mem_dc, g_tag_font);
      SetTextColor(mem_dc, kHeaderEscText);
      DrawTextW(mem_dc, L"Esc", -1, &esc_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // 3. Option Cards (3 Cards)
      for (int i = 0; i < 3; ++i) {
        RECT card = GetCardRect(i);
        const bool hovered = (g_hovered == i);
        const bool pressed = (g_pressed == i);
        const auto& opt = kOptions[i];

        COLORREF card_bg = pressed ? kCardPressedBg : (hovered ? kCardHoverBg : kCardBg);
        COLORREF card_border = hovered ? kCardHoverBorder : kCardBorder;
        int border_thickness = hovered ? Scaled(2) : 1;

        FillRounded(mem_dc, card, Scaled(10), card_bg, card_border, border_thickness);

        // Icon Badge on Left
        const int badge_size = Scaled(44);
        const int badge_inset = Scaled(10);
        const int badge_l = card.left + badge_inset;
        const int badge_t = card.top + (card.bottom - card.top - badge_size) / 2;
        RECT badge_rc = {badge_l, badge_t, badge_l + badge_size, badge_t + badge_size};

        COLORREF b_bg = hovered ? kBadgeHoverBg : kBadgeBg;
        COLORREF b_bdr = hovered ? kBadgeHoverBorder : kBadgeBorder;
        FillRounded(mem_dc, badge_rc, Scaled(9), b_bg, b_bdr, 1);

        const int ic_cx = (badge_rc.left + badge_rc.right) / 2;
        const int ic_cy = (badge_rc.top + badge_rc.bottom) / 2;
        COLORREF ic_col = hovered ? kIconHoverColor : kIconColor;

        if (i == 0) {
          DrawMonitorIcon(mem_dc, ic_cx, ic_cy, badge_size, ic_col);
        } else if (i == 1) {
          DrawSnipIcon(mem_dc, ic_cx, ic_cy, badge_size, ic_col);
        } else if (i == 2) {
          DrawFullPageIcon(mem_dc, ic_cx, ic_cy, badge_size, ic_col);
        }

        // Texts on Right
        const int text_l = badge_rc.right + Scaled(12);
        const int tag_w = Scaled(46);
        const int tag_h = Scaled(20);
        const int tag_r = card.right - Scaled(10);
        const int tag_l = tag_r - tag_w;
        const int tag_t = card.top + (card.bottom - card.top - tag_h) / 2;
        const int text_r = tag_l - Scaled(6);

        const int title_y = card.top + Scaled(12);
        const int sub_y = title_y + Scaled(22);

        // Title
        SelectObject(mem_dc, g_title_font);
        SetTextColor(mem_dc, hovered ? kTitleHoverColor : kTitleColor);
        RECT title_rc = {text_l, title_y, text_r, title_y + Scaled(20)};
        const wchar_t* title_str = is_hindi ? opt.title_hi : opt.title_en;
        DrawTextW(mem_dc, title_str, -1, &title_rc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        // Subtitle
        SelectObject(mem_dc, g_subtitle_font);
        SetTextColor(mem_dc, hovered ? kSubtitleHoverColor : kSubtitleColor);
        RECT sub_rc = {text_l, sub_y, text_r, sub_y + Scaled(18)};
        const wchar_t* sub_str = is_hindi ? opt.subtitle_hi : opt.subtitle_en;
        DrawTextW(mem_dc, sub_str, -1, &sub_rc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        // Shortcut Keycap Pill (e.g. [ Alt+1 ], [ Alt+2 ], [ Alt+3 ])
        RECT tag_rc = {tag_l, tag_t, tag_r, tag_t + tag_h};
        COLORREF chip_bg = hovered ? kPillHoverBg : kPillBg;
        COLORREF chip_border = hovered ? kPillHoverBorder : kPillBorder;
        COLORREF chip_text = hovered ? kPillHoverText : kPillText;

        FillRounded(mem_dc, tag_rc, Scaled(4), chip_bg, chip_border, 1);
        SelectObject(mem_dc, g_tag_font);
        SetTextColor(mem_dc, chip_text);
        const wchar_t* tag_str = is_hindi ? opt.key_hi : opt.key_en;
        DrawTextW(mem_dc, tag_str, -1, &tag_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }

      // 4. Footer Status Area
      const int footer_h = Scaled(kBaseFooterHeight);
      const int footer_t = client.bottom - footer_h;
      RECT footer_rc = {0, footer_t, client.right, client.bottom};
      HBRUSH footer_brush = CreateSolidBrush(kFooterBg);
      FillRect(mem_dc, &footer_rc, footer_brush);
      DeleteObject(footer_brush);

      // Footer top border line
      HPEN fline_pen = CreatePen(PS_SOLID, 1, kFooterBorder);
      HGDIOBJ old_fpen = SelectObject(mem_dc, fline_pen);
      MoveToEx(mem_dc, 0, footer_t, nullptr);
      LineTo(mem_dc, client.right, footer_t);
      SelectObject(mem_dc, old_fpen);
      DeleteObject(fline_pen);

      // Clean crimson red checkmark icon
      DrawCheckmarkIcon(mem_dc, Scaled(20), footer_t + footer_h / 2, kFooterIcon);

      // Footer Text
      SelectObject(mem_dc, g_footer_font);
      SetTextColor(mem_dc, kFooterText);
      const wchar_t* footer_text = is_hindi
          ? L"क्लिपबोर्ड पर कॉपी और डाउनलोड्स में सहेजा गया"
          : L"Copied to clipboard and saved to Downloads";
      RECT ftext_rc = {Scaled(32), footer_t, client.right - Scaled(10), client.bottom};
      DrawTextW(mem_dc, footer_text, -1, &ftext_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Remove clipping region
      SelectClipRgn(mem_dc, nullptr);
      DeleteObject(win_rgn);

      // 5. Specular ruby highlight line along top edge
      HPEN spec_pen = CreatePen(PS_SOLID, 1, kGlassHighlight);
      HGDIOBJ old_spec = SelectObject(mem_dc, spec_pen);
      MoveToEx(mem_dc, Scaled(kBaseRadius), 0, nullptr);
      LineTo(mem_dc, client.right - Scaled(kBaseRadius), 0);
      SelectObject(mem_dc, old_spec);
      DeleteObject(spec_pen);

      // 6. Outer Border
      HPEN border_pen = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ old_border_pen = SelectObject(mem_dc, border_pen);
      SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
      RoundRect(mem_dc, client.left, client.top, client.right, client.bottom,
                Scaled(kBaseRadius), Scaled(kBaseRadius));
      SelectObject(mem_dc, old_border_pen);
      DeleteObject(border_pen);

      // Transfer to screen
      BitBlt(dc, 0, 0, client.right, client.bottom, mem_dc, 0, 0, SRCCOPY);
      SelectObject(mem_dc, old_bm);
      DeleteObject(mem_bm);
      DeleteDC(mem_dc);

      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_MOUSEMOVE: {
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const int hit = HitTestCard(pt);
      if (hit != g_hovered) {
        g_hovered = hit;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
      return 0;
    }

    case WM_MOUSELEAVE: {
      if (g_hovered != -1 || g_pressed != -1) {
        g_hovered = -1;
        g_pressed = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_SETCURSOR: {
      POINT pt;
      GetCursorPos(&pt);
      ScreenToClient(hwnd, &pt);
      if (HitTestCard(pt) >= 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_LBUTTONDOWN: {
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const int hit = HitTestCard(pt);
      if (hit >= 0) {
        g_pressed = hit;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_LBUTTONUP: {
      const int prev_pressed = g_pressed;
      g_pressed = -1;
      if (GetCapture() == hwnd) {
        ReleaseCapture();
      }
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const int hit = HitTestCard(pt);
      if (hit >= 0 && hit < 3 && hit == prev_pressed) {
        const Mode mode = kOptions[hit].mode;
        auto cb = g_callbacks.on_select;
        ClosePanel();
        if (cb) {
          cb(mode);
        }
      } else {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_CAPTURECHANGED: {
      if (g_pressed != -1) {
        g_pressed = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_TIMER: {
      if (wparam == kDismissTimer) {
        HWND fg = GetForegroundWindow();
        if (fg != g_panel && fg != g_owner) {
          ClosePanel();
          return 0;
        }
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
          POINT cursor;
          GetCursorPos(&cursor);
          RECT prc{}, arc{};
          GetWindowRect(g_panel, &prc);
          if (g_anchor) GetWindowRect(g_anchor, &arc);
          if (!PtInRect(&prc, cursor) && !PtInRect(&arc, cursor)) {
            ClosePanel();
            return 0;
          }
        }
      }
      return 0;
    }

    case WM_KEYDOWN: {
      if (wparam == VK_ESCAPE) {
        ClosePanel();
        return 0;
      }
      if (wparam == '1' || wparam == VK_NUMPAD1) {
        auto cb = g_callbacks.on_select;
        ClosePanel();
        if (cb) cb(Mode::kVisibleArea);
        return 0;
      }
      if (wparam == '2' || wparam == VK_NUMPAD2) {
        auto cb = g_callbacks.on_select;
        ClosePanel();
        if (cb) cb(Mode::kSnipRegion);
        return 0;
      }
      if (wparam == '3' || wparam == VK_NUMPAD3) {
        auto cb = g_callbacks.on_select;
        ClosePanel();
        if (cb) cb(Mode::kFullPage);
        return 0;
      }
      break;
    }

    case WM_KILLFOCUS: {
      ClosePanel();
      return 0;
    }

    case WM_DESTROY: {
      if (g_dismiss_armed) {
        KillTimer(hwnd, kDismissTimer);
        g_dismiss_armed = false;
      }
      ReleaseFonts();
      g_panel = nullptr;
      g_pressed = -1;
      g_hovered = -1;
      return 0;
    }

    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void RegisterPanelClass(HINSTANCE instance) {
  static bool registered = false;
  if (registered) return;

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = PanelWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  registered = true;
}

}  // namespace

void TogglePanel(HINSTANCE instance, HWND owner, HWND anchor, Callbacks callbacks) {
  if (g_panel && IsWindow(g_panel)) {
    ClosePanel();
    return;
  }

  g_instance = instance;
  g_owner = owner;
  g_anchor = anchor;
  g_callbacks = std::move(callbacks);
  g_hovered = -1;
  g_pressed = -1;

  RegisterPanelClass(instance);

  g_dpi = 96;
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    if (auto fn = reinterpret_cast<GetDpiFn>(GetProcAddress(user32, "GetDpiForWindow"))) {
      g_dpi = static_cast<int>(fn(owner));
    }
  }
  if (g_dpi <= 0) g_dpi = 96;

  CreateFonts();

  const int total_h = Scaled(kBaseHeaderHeight + kBasePadding +
                             (3 * kBaseCardHeight) + (2 * kBaseGap) +
                             kBasePadding + kBaseFooterHeight);
  const int total_w = Scaled(kBaseWidth);

  RECT anchor_rc{};
  if (anchor && IsWindow(anchor)) {
    GetWindowRect(anchor, &anchor_rc);
  } else {
    GetWindowRect(owner, &anchor_rc);
  }

  int x = (anchor_rc.left + anchor_rc.right) / 2 - total_w / 2;
  int y = anchor_rc.bottom + Scaled(4);

  // Monitor work area bounds clamping
  HMONITOR mon = MonitorFromRect(&anchor_rc, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{sizeof(mi)};
  if (GetMonitorInfoW(mon, &mi)) {
    if (x + total_w > mi.rcWork.right - Scaled(8)) {
      x = mi.rcWork.right - total_w - Scaled(8);
    }
    if (x < mi.rcWork.left + Scaled(8)) {
      x = mi.rcWork.left + Scaled(8);
    }
    if (y + total_h > mi.rcWork.bottom) {
      y = anchor_rc.top - total_h - Scaled(4);
    }
  }

  g_panel = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
      kClassName, L"Screenshot",
      WS_POPUP,
      x, y, total_w, total_h,
      owner, nullptr, instance, nullptr);

  if (!g_panel) return;

  // Windows 11 rounded corners
  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(g_panel, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

  ShowWindow(g_panel, SW_SHOWNOACTIVATE);
  UpdateWindow(g_panel);
  ArmDismissTimer();
}

void ClosePanel() {
  if (g_panel && IsWindow(g_panel)) {
    DestroyWindow(g_panel);
  }
  g_panel = nullptr;
  g_dismiss_armed = false;
  g_hovered = -1;
  g_pressed = -1;
}

bool IsOpen() {
  return g_panel != nullptr && IsWindow(g_panel);
}

}  // namespace mtk::screenshot_panel
