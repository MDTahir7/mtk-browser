#include "browser_window.h"

#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>
#include <thread>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_cookie.h"
#include "include/cef_frame.h"
#include "include/wrapper/cef_closure_task.h"
#include "bookmark_import.h"
#include "download_manager.h"
#include "library_store.h"
#include "profile.h"
#include "site_settings.h"
#include "policy.h"
#include "preferences.h"
#include "resource.h"
#include "session_store.h"
#include "shields_panel.h"
#include "updater.h"

namespace {

constexpr wchar_t kWindowClass[] = L"MTKBrowserWindow";
constexpr char kDefaultHomeUrl[] = "https://www.google.com/?safe=active";
std::string kHomeUrl = kDefaultHomeUrl;
// Design metrics are authored at 96 DPI and scaled through Scaled() so the
// chrome stays crisp and correctly sized on 125%, 150% and 200% displays.
constexpr int kBaseTabBarHeight = 44;
constexpr int kBaseToolbarHeight = 60;
constexpr int kBaseProgressHeight = 3;
constexpr int kBaseBookmarksBarHeight = 32;
int g_dpi = 96;
// Bookmarks bar visibility, mirrored from preferences. The height helpers are
// free functions shared with painting and hit testing, so the flag sits here.
bool g_bookmarks_bar = false;

int Scaled(int value) { return MulDiv(value, g_dpi, 96); }
int TabBarHeight() { return Scaled(kBaseTabBarHeight); }
int ToolbarHeight() { return Scaled(kBaseToolbarHeight); }
int ProgressHeight() { return Scaled(kBaseProgressHeight); }
int BookmarksBarHeight() {
  return g_bookmarks_bar ? Scaled(kBaseBookmarksBarHeight) : 0;
}
int ChromeHeight() {
  return TabBarHeight() + ToolbarHeight() + BookmarksBarHeight();
}
// Open top-level windows, in creation order. Windows own themselves and remove
// their entry here when destroyed; an empty list ends the message loop.
std::vector<BrowserWindow*> g_windows;

constexpr UINT_PTR kProgressTimer = 1;
constexpr UINT_PTR kLibrarySaveTimer = 2;
constexpr UINT_PTR kDownloadSaveTimer = 3;
constexpr UINT_PTR kSessionSaveTimer = 4;
// Hover playback for the rendered shield: one pass at the rate the strip was
// rendered at, then it holds. 33 ms is one frame at 30 fps.
constexpr UINT_PTR kShieldAnimTimer = 7;
constexpr UINT kShieldFrameMs = 33;
constexpr wchar_t kShieldStripFile[] = L"mtk_shield_hover.png";
// First automatic update check runs shortly after startup so it never competes
// with loading the first page, then repeats twice a day.
constexpr UINT_PTR kUpdateStartupTimer = 5;
constexpr UINT_PTR kUpdatePeriodicTimer = 6;
constexpr UINT kUpdateStartupDelayMs = 45 * 1000;
constexpr UINT kUpdatePeriodMs = 12 * 60 * 60 * 1000;
constexpr UINT kFinalizeCloseMessage = WM_APP + 1;
constexpr UINT kUpdateResultMessage = WM_APP + 2;

// Result of the worker thread's check, owned by the UI thread once posted.
mtk::update::Result g_update_result;
constexpr INT_PTR kBackId = 1001;
constexpr INT_PTR kForwardId = 1002;
constexpr INT_PTR kReloadId = 1003;
constexpr INT_PTR kHomeId = 1004;
constexpr INT_PTR kAddressId = 1005;
constexpr INT_PTR kSuggestionsId = 1006;
// Omnibox autocomplete stays short so it never covers the page.
constexpr size_t kMaxSuggestions = 8;
// Bounded closed-tab history for Ctrl+Shift+T.
constexpr size_t kMaxClosedTabs = 10;
constexpr INT_PTR kShieldId = 1007;
constexpr INT_PTR kMenuId = 1008;
constexpr INT_PTR kFindId = 1009;
constexpr INT_PTR kFindPreviousId = 1010;
constexpr INT_PTR kFindNextId = 1011;
constexpr INT_PTR kFindCloseId = 1012;
constexpr INT_PTR kDownloadsId = 1013;

// What an owner-draw toolbar button paints. Kept apart from the window text,
// which is the button's accessible name.
const wchar_t* ButtonGlyph(UINT control_id) {
  switch (control_id) {
    case kBackId: return L"\u2190";
    case kForwardId: return L"\u2192";
    case kReloadId: return L"\u21BB";
    case kHomeId: return L"\u2302";
    case kShieldId: return L"\u25C6";
    case kDownloadsId: return L"\u2193";
    case kMenuId: return L"\u22EE";
    case kFindPreviousId: return L"\u2039";
    case kFindNextId: return L"\u203A";
    case kFindCloseId: return L"\u00D7";
    default: return L"";
  }
}

// Upper bound on profiles listed in the menu. The reserved command id range is
// this large, so the list is truncated rather than colliding with other ids.
constexpr size_t kMaxProfileMenuEntries = 20;

// Tab context menu commands. These only ever reach TrackPopupMenuEx, so they
// stay local to this file instead of taking resource ids.
constexpr int kTabMenuDuplicate = 1401;
constexpr int kTabMenuPin = 1402;
constexpr int kTabMenuMute = 1403;
constexpr int kTabMenuClose = 1404;
constexpr int kTabMenuCloseOthers = 1405;
constexpr int kTabMenuCloseRight = 1406;
constexpr size_t kNoTab = static_cast<size_t>(-1);

constexpr COLORREF kWindowColor = RGB(9, 12, 22);
constexpr COLORREF kTabBarTop = RGB(19, 23, 38);
constexpr COLORREF kTabBarBottom = RGB(14, 18, 30);
constexpr COLORREF kToolbarTop = RGB(27, 32, 50);
constexpr COLORREF kToolbarBottom = RGB(20, 25, 40);
// Private windows use a deeper, plainer chrome so the two kinds of window are
// never confused for one another.
constexpr COLORREF kPrivateBarTop = RGB(32, 24, 52);
constexpr COLORREF kPrivateBarBottom = RGB(22, 17, 38);
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

// Reads a password field without leaving the PIN in a long-lived buffer.
std::wstring TakePinField(HWND dialog, int control) {
  wchar_t buffer[65]{};
  const int length = GetDlgItemTextW(dialog, control, buffer,
                                     static_cast<int>(std::size(buffer)));
  std::wstring pin(buffer, static_cast<size_t>(std::max(0, length)));
  SecureZeroMemory(buffer, sizeof(buffer));
  SetDlgItemTextW(dialog, control, L"");
  return pin;
}

void ScrubPin(std::wstring* pin) {
  if (pin && !pin->empty()) {
    SecureZeroMemory(pin->data(), pin->size() * sizeof(wchar_t));
    pin->clear();
  }
}

struct PinPromptContext {
  const wchar_t* message = nullptr;
  // Wrong attempts are counted per prompt and slowed down, so the short PIN
  // cannot be brute-forced by rapid retries.
  int attempts = 0;
};

INT_PTR CALLBACK PinPromptDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* context = reinterpret_cast<PinPromptContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA,
                        static_cast<LONG_PTR>(l_param));
      SetDlgItemTextW(dialog, IDC_PIN_PROMPT_TEXT,
                      context && context->message
                          ? context->message
                          : L"Enter the parent PIN to continue.");
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* context = reinterpret_cast<PinPromptContext*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        std::wstring pin = TakePinField(dialog, IDC_PIN_PROMPT_VALUE);
        const bool correct = mtk::preferences::VerifyPin(pin);
        ScrubPin(&pin);
        if (correct) {
          EndDialog(dialog, IDOK);
          return TRUE;
        }
        if (context) ++context->attempts;
        const int attempts = context ? context->attempts : 1;
        if (attempts >= 3) Sleep(1500);
        MessageBoxW(dialog, L"That PIN is not correct.", L"Parent PIN",
                    MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_PIN_PROMPT_VALUE));
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

struct SiteSettingsContext {
  mtk::sites::Rule rule;
  // Set when the dialog asked for the site's stored rule to be dropped.
  bool forget = false;
};

// Permission combo boxes all offer the same three choices in the same order.
void FillPermissionCombo(HWND dialog, int control, mtk::sites::Permission value) {
  HWND combo = GetDlgItem(dialog, control);
  if (!combo) return;
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  SendMessageW(combo, CB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(L"Blocked (default)"));
  SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Allowed"));
  SendMessageW(combo, CB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(L"Blocked, and never ask"));
  const int selection = value == mtk::sites::Permission::kAllow
                            ? 1
                            : (value == mtk::sites::Permission::kBlock ? 2 : 0);
  SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
  SetWindowTheme(combo, L"Explorer", nullptr);
}

mtk::sites::Permission PermissionFromCombo(HWND dialog, int control) {
  const LRESULT selection =
      SendMessageW(GetDlgItem(dialog, control), CB_GETCURSEL, 0, 0);
  if (selection == 1) return mtk::sites::Permission::kAllow;
  if (selection == 2) return mtk::sites::Permission::kBlock;
  return mtk::sites::Permission::kAsk;
}

INT_PTR CALLBACK SiteSettingsDialogProc(HWND dialog, UINT message,
                                       WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* context = reinterpret_cast<SiteSettingsContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      if (!context) return TRUE;
      SetDlgItemTextW(dialog, IDC_SITE_ORIGIN,
                      Utf8ToWide(context->rule.origin).c_str());
      FillPermissionCombo(dialog, IDC_SITE_CAMERA, context->rule.camera);
      FillPermissionCombo(dialog, IDC_SITE_MICROPHONE, context->rule.microphone);
      FillPermissionCombo(dialog, IDC_SITE_LOCATION, context->rule.location);
      FillPermissionCombo(dialog, IDC_SITE_NOTIFICATIONS,
                          context->rule.notifications);
      CheckDlgButton(dialog, IDC_SITE_AD_FILTERING,
                     context->rule.ad_filtering ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_SITE_COOKIES,
                     context->rule.cookies ? BST_CHECKED : BST_UNCHECKED);
      SetDlgItemTextW(
          dialog, IDC_SITE_ZOOM_TEXT,
          (std::to_wstring(context->rule.zoom_percent) + L"%").c_str());
      return TRUE;
    }
    case WM_COMMAND: {
      auto* context = reinterpret_cast<SiteSettingsContext*>(
          GetWindowLongPtrW(dialog, GWLP_USERDATA));
      switch (LOWORD(w_param)) {
        case IDC_SITE_ZOOM_RESET:
          if (context) {
            context->rule.zoom_percent = mtk::sites::kDefaultZoomPercent;
            SetDlgItemTextW(
                dialog, IDC_SITE_ZOOM_TEXT,
                (std::to_wstring(context->rule.zoom_percent) + L"%").c_str());
          }
          return TRUE;
        case IDC_SITE_FORGET:
          if (context) context->forget = true;
          EndDialog(dialog, IDOK);
          return TRUE;
        case IDOK:
          if (context) {
            context->rule.camera = PermissionFromCombo(dialog, IDC_SITE_CAMERA);
            context->rule.microphone =
                PermissionFromCombo(dialog, IDC_SITE_MICROPHONE);
            context->rule.location =
                PermissionFromCombo(dialog, IDC_SITE_LOCATION);
            context->rule.notifications =
                PermissionFromCombo(dialog, IDC_SITE_NOTIFICATIONS);
            context->rule.ad_filtering =
                IsDlgButtonChecked(dialog, IDC_SITE_AD_FILTERING) == BST_CHECKED;
            context->rule.cookies =
                IsDlgButtonChecked(dialog, IDC_SITE_COOKIES) == BST_CHECKED;
          }
          EndDialog(dialog, IDOK);
          return TRUE;
        case IDCANCEL:
          EndDialog(dialog, IDCANCEL);
          return TRUE;
        default:
          break;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

struct TextPromptContext {
  const wchar_t* caption = nullptr;
  const wchar_t* message = nullptr;
  std::wstring value;
};

INT_PTR CALLBACK TextPromptDialogProc(HWND dialog, UINT message,
                                      WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* context = reinterpret_cast<TextPromptContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      if (context) {
        if (context->caption) SetWindowTextW(dialog, context->caption);
        if (context->message) {
          SetDlgItemTextW(dialog, IDC_TEXT_PROMPT_LABEL, context->message);
        }
      }
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* context = reinterpret_cast<TextPromptContext*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        if (context) {
          HWND field = GetDlgItem(dialog, IDC_TEXT_PROMPT_VALUE);
          const int length = GetWindowTextLengthW(field);
          // Bounded because the value is used to build a path.
          std::wstring value(static_cast<size_t>(std::min(length, 64)) + 1,
                             L'\0');
          GetWindowTextW(field, value.data(), std::min(length, 64) + 1);
          value.resize(static_cast<size_t>(std::min(length, 64)));
          context->value = std::move(value);
        }
        EndDialog(dialog, IDOK);
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

// One-line text prompt. Returns false when the dialog was cancelled or the
// field was left empty.
bool PromptForText(HINSTANCE instance, HWND owner, const wchar_t* caption,
                   const wchar_t* message, std::wstring* value) {
  TextPromptContext context{caption, message, {}};
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_TEXT_PROMPT), owner,
                      TextPromptDialogProc,
                      reinterpret_cast<LPARAM>(&context)) != IDOK) {
    return false;
  }
  if (context.value.empty()) return false;
  *value = std::move(context.value);
  return true;
}

// Asks for the parent PIN when one is set. Returns true when access is granted,
// which is also the case when no PIN has been configured yet.
bool AuthorizeParentAction(HINSTANCE instance, HWND owner,
                           const wchar_t* message) {
  if (!mtk::preferences::PinIsSet()) return true;
  PinPromptContext context{message, 0};
  return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_PIN_PROMPT), owner,
                         PinPromptDialogProc,
                         reinterpret_cast<LPARAM>(&context)) == IDOK;
}

INT_PTR CALLBACK PinSetupDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                    LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      const bool existing = mtk::preferences::PinIsSet();
      EnableWindow(GetDlgItem(dialog, IDC_PIN_SETUP_CURRENT), existing);
      EnableWindow(GetDlgItem(dialog, IDC_PIN_SETUP_CURRENT_LABEL), existing);
      EnableWindow(GetDlgItem(dialog, IDC_PIN_SETUP_REMOVE), existing);
      if (!existing) {
        SetDlgItemTextW(dialog, IDC_PIN_SETUP_CURRENT_LABEL,
                        L"Current PIN (none set yet)");
      }
      return TRUE;
    }
    case WM_COMMAND: {
      const bool existing = mtk::preferences::PinIsSet();
      // Changing or removing a PIN always requires the current one.
      const auto current_ok = [&](HWND owner) {
        if (!existing) return true;
        std::wstring current = TakePinField(dialog, IDC_PIN_SETUP_CURRENT);
        const bool ok = mtk::preferences::VerifyPin(current);
        ScrubPin(&current);
        if (!ok) {
          MessageBoxW(owner, L"The current PIN is not correct.", L"Parent PIN",
                      MB_OK | MB_ICONWARNING);
        }
        return ok;
      };

      if (LOWORD(w_param) == IDC_PIN_SETUP_REMOVE) {
        if (!existing || !current_ok(dialog)) return TRUE;
        if (MessageBoxW(dialog,
                        L"Remove the parent PIN?\n\nSafety settings will be "
                        L"changeable by anyone using this computer.",
                        L"Remove parent PIN?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        mtk::preferences::Values values =
            mtk::preferences::Store::Get().values();
        values.pin_salt.clear();
        values.pin_hash.clear();
        if (!mtk::preferences::Store::Get().Save(values)) {
          MessageBoxW(dialog, L"MTK Browser could not remove the PIN.",
                      L"Parent PIN", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }

      if (LOWORD(w_param) == IDOK) {
        if (!current_ok(dialog)) return TRUE;
        std::wstring fresh = TakePinField(dialog, IDC_PIN_SETUP_NEW);
        std::wstring confirm = TakePinField(dialog, IDC_PIN_SETUP_CONFIRM);
        const bool too_short = fresh.size() < mtk::preferences::kMinPinLength;
        const bool mismatch = fresh != confirm;
        ScrubPin(&confirm);
        if (too_short) {
          ScrubPin(&fresh);
          MessageBoxW(dialog, L"Use a PIN of at least 4 characters.",
                      L"Parent PIN", MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        if (mismatch) {
          ScrubPin(&fresh);
          MessageBoxW(dialog, L"The two new PINs do not match.", L"Parent PIN",
                      MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        mtk::preferences::Values values =
            mtk::preferences::Store::Get().values();
        const bool derived = mtk::preferences::DerivePin(
            fresh, &values.pin_salt, &values.pin_hash);
        ScrubPin(&fresh);
        if (!derived || !mtk::preferences::Store::Get().Save(values)) {
          MessageBoxW(dialog, L"MTK Browser could not save the PIN.",
                      L"Parent PIN", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

struct ClearDataChoice {
  bool history = true;
  bool downloads = true;
  bool cookies = false;
  bool cache = true;
  bool session = false;
  bool bookmarks = false;
  // Per-site permissions, cookie choices, Shields state and zoom. Off by
  // default: these are decisions someone made, not traces left behind.
  bool site_settings = false;
};

INT_PTR CALLBACK ClearDataDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      const auto* choice = reinterpret_cast<const ClearDataChoice*>(l_param);
      if (!choice) return TRUE;
      // Bookmarks stay unchecked by default: they are saved deliberately and
      // are not browsing traces.
      CheckDlgButton(dialog, IDC_CLEAR_HISTORY,
                     choice->history ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_DOWNLOADS,
                     choice->downloads ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_COOKIES,
                     choice->cookies ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_CACHE,
                     choice->cache ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_SESSION,
                     choice->session ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_BOOKMARKS,
                     choice->bookmarks ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_SITE_SETTINGS,
                     choice->site_settings ? BST_CHECKED : BST_UNCHECKED);
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* choice = reinterpret_cast<ClearDataChoice*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        if (choice) {
          const auto checked = [dialog](int control) {
            return IsDlgButtonChecked(dialog, control) == BST_CHECKED;
          };
          choice->history = checked(IDC_CLEAR_HISTORY);
          choice->downloads = checked(IDC_CLEAR_DOWNLOADS);
          choice->cookies = checked(IDC_CLEAR_COOKIES);
          choice->cache = checked(IDC_CLEAR_CACHE);
          choice->session = checked(IDC_CLEAR_SESSION);
          choice->bookmarks = checked(IDC_CLEAR_BOOKMARKS);
          choice->site_settings = checked(IDC_CLEAR_SITE_SETTINGS);
          const bool anything = choice->history || choice->downloads ||
                                choice->cookies || choice->cache ||
                                choice->session || choice->bookmarks ||
                                choice->site_settings;
          if (!anything) {
            MessageBoxW(dialog, L"Select at least one item to clear.",
                        L"Clear browsing data", MB_OK | MB_ICONINFORMATION);
            return TRUE;
          }
          if (choice->bookmarks &&
              MessageBoxW(dialog,
                          L"Delete all bookmarks as well?\n\nThis cannot be "
                          L"undone.",
                          L"Clear browsing data",
                          MB_YESNO | MB_ICONWARNING) != IDYES) {
            return TRUE;
          }
        }
        EndDialog(dialog, IDOK);
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

void RefreshPinState(HWND dialog) {
  SetDlgItemTextW(dialog, IDC_SETTINGS_PIN_STATE,
                  mtk::preferences::PinIsSet()
                      ? L"A parent PIN is required to open Settings."
                      : L"No parent PIN. Anyone can change these settings.");
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

      HWND startup = GetDlgItem(dialog, IDC_SETTINGS_STARTUP);
      SendMessageW(startup, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"Open the home page"));
      SendMessageW(startup, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"Continue where you left off"));
      SendMessageW(startup, CB_SETCURSEL,
                   values.startup_mode ==
                           mtk::preferences::StartupMode::kContinueSession ? 1 : 0,
                   0);
      SetWindowTheme(startup, L"Explorer", nullptr);

      HWND downloads = GetDlgItem(dialog, IDC_SETTINGS_DOWNLOADS);
      SendMessageW(downloads, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"Safe downloads (recommended)"));
      SendMessageW(downloads, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(
                       L"Standard downloads (like other browsers)"));
      SendMessageW(downloads, CB_SETCURSEL,
                   values.download_mode ==
                           mtk::preferences::DownloadMode::kStandard ? 1 : 0,
                   0);
      SetWindowTheme(downloads, L"Explorer", nullptr);
      CheckDlgButton(dialog, IDC_SETTINGS_AUTO_UPDATE,
                     values.auto_update ? BST_CHECKED : BST_UNCHECKED);
      RefreshPinState(dialog);
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDC_SETTINGS_PIN) {
        DialogBoxParamW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(
                            dialog, GWLP_HINSTANCE)),
                        MAKEINTRESOURCEW(IDD_PIN_SETUP), dialog,
                        PinSetupDialogProc, 0);
        RefreshPinState(dialog);
        return TRUE;
      }
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

        const bool standard_downloads =
            SendMessageW(GetDlgItem(dialog, IDC_SETTINGS_DOWNLOADS),
                         CB_GETCURSEL, 0, 0) == 1;
        const bool was_standard =
            values.download_mode == mtk::preferences::DownloadMode::kStandard;
        // Leaving safe downloads is an explicit, acknowledged choice.
        if (standard_downloads && !was_standard &&
            MessageBoxW(dialog,
                        L"Standard downloads save any file type, including "
                        L"programs, scripts and installers.\n\nOnly files you "
                        L"trust should be opened. Continue?",
                        L"Turn on standard downloads?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        values.download_mode = standard_downloads
                                   ? mtk::preferences::DownloadMode::kStandard
                                   : mtk::preferences::DownloadMode::kSafe;
        values.startup_mode =
            SendMessageW(GetDlgItem(dialog, IDC_SETTINGS_STARTUP),
                         CB_GETCURSEL, 0, 0) == 1
                ? mtk::preferences::StartupMode::kContinueSession
                : mtk::preferences::StartupMode::kHomePage;
        const bool auto_update =
            IsDlgButtonChecked(dialog, IDC_SETTINGS_AUTO_UPDATE) == BST_CHECKED;
        // Turning update checks off means security fixes stop arriving, so it
        // has to be acknowledged rather than toggled silently.
        if (!auto_update && values.auto_update &&
            MessageBoxW(dialog,
                        L"With update checks off, MTK Browser will not receive "
                        L"security or safety fixes automatically.\n\nTurn them "
                        L"off anyway?",
                        L"Turn off update checks?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          CheckDlgButton(dialog, IDC_SETTINGS_AUTO_UPDATE, BST_CHECKED);
          return TRUE;
        }
        values.auto_update = auto_update;
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

// Local calendar day number for a Unix timestamp, used only to decide which
// group a history row belongs to.
long long LocalDayNumber(long long unix_seconds) {
  if (unix_seconds <= 0) return -1;
  ULARGE_INTEGER ticks{};
  ticks.QuadPart = static_cast<unsigned long long>(unix_seconds) * 10000000ULL +
                   116444736000000000ULL;
  FILETIME utc{ticks.LowPart, ticks.HighPart};
  FILETIME local{};
  if (!FileTimeToLocalFileTime(&utc, &local)) return -1;
  SYSTEMTIME parts{};
  if (!FileTimeToSystemTime(&local, &parts)) return -1;
  // A plain year/month/day key is enough: groups only need to compare equal for
  // the same calendar day and to order downwards.
  return static_cast<long long>(parts.wYear) * 10000 +
         static_cast<long long>(parts.wMonth) * 100 + parts.wDay;
}

std::wstring HistoryGroupLabel(long long day) {
  if (day < 0) return L"Earlier";
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const long long today = static_cast<long long>(now.wYear) * 10000 +
                          static_cast<long long>(now.wMonth) * 100 + now.wDay;
  if (day == today) return L"Today";

  // Yesterday is computed from a real timestamp rather than by subtracting one
  // from the day key, so month and year boundaries are correct.
  FILETIME now_file{};
  SystemTimeToFileTime(&now, &now_file);
  ULARGE_INTEGER ticks{now_file.dwLowDateTime, now_file.dwHighDateTime};
  ticks.QuadPart -= 24ULL * 60ULL * 60ULL * 10000000ULL;
  FILETIME previous{ticks.LowPart, ticks.HighPart};
  SYSTEMTIME yesterday{};
  if (FileTimeToSystemTime(&previous, &yesterday)) {
    const long long key = static_cast<long long>(yesterday.wYear) * 10000 +
                          static_cast<long long>(yesterday.wMonth) * 100 +
                          yesterday.wDay;
    if (day == key) return L"Yesterday";
  }

  SYSTEMTIME date{};
  date.wYear = static_cast<WORD>(day / 10000);
  date.wMonth = static_cast<WORD>((day / 100) % 100);
  date.wDay = static_cast<WORD>(day % 100);
  wchar_t formatted[128]{};
  if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &date, nullptr,
                      formatted, 128, nullptr) > 0) {
    return formatted;
  }
  return L"Earlier";
}

bool MatchesLibraryQuery(const mtk::library::Entry& entry,
                         const std::wstring& lowered_query) {
  if (lowered_query.empty()) return true;
  std::wstring haystack = entry.title + L" " + Utf8ToWide(entry.url);
  std::transform(haystack.begin(), haystack.end(), haystack.begin(), towlower);
  return haystack.find(lowered_query) != std::wstring::npos;
}

// Item data is entry index + 1, so a group header can use 0 and never be
// mistaken for a row that can be opened or removed.
constexpr LRESULT kLibraryHeaderData = 0;

void PopulateLibraryList(HWND dialog, bool bookmarks) {
  HWND list = GetDlgItem(dialog, IDC_LIBRARY_LIST);
  HWND search = GetDlgItem(dialog, IDC_LIBRARY_SEARCH);
  std::wstring query;
  if (search) {
    const int length = GetWindowTextLengthW(search);
    query.resize(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(search, query.data(), length + 1);
    query.resize(static_cast<size_t>(length));
    std::transform(query.begin(), query.end(), query.begin(), towlower);
  }

  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  const auto& entries = LibraryEntries(bookmarks);
  long long current_day = std::numeric_limits<long long>::min();
  size_t shown = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (!MatchesLibraryQuery(entries[index], query)) continue;
    // History is stored newest first, so the groups come out in order without
    // sorting anything.
    if (!bookmarks) {
      const long long day = LocalDayNumber(entries[index].visited);
      if (day != current_day) {
        current_day = day;
        const std::wstring header = L"— " + HistoryGroupLabel(day) + L" —";
        const LRESULT header_row = SendMessageW(
            list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(header.c_str()));
        if (header_row != LB_ERR && header_row != LB_ERRSPACE) {
          SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(header_row),
                       kLibraryHeaderData);
        }
      }
    }
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(LibraryDisplayText(entries[index]).c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                   static_cast<LPARAM>(index + 1));
      if (shown == 0) SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(row), 0);
      ++shown;
    }
  }
  if (shown == 0 && !query.empty()) {
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"Nothing matches that search."));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                   kLibraryHeaderData);
    }
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

// Entry index behind the selected row, or -1 when a header row is selected.
ptrdiff_t SelectedLibraryIndex(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_LIBRARY_LIST);
  const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR) return -1;
  const LRESULT data =
      SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
  if (data == LB_ERR || data == kLibraryHeaderData) return -1;
  return static_cast<ptrdiff_t>(data) - 1;
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
      if (command == IDC_LIBRARY_SEARCH && notification == EN_CHANGE) {
        PopulateLibraryList(dialog, bookmarks);
        return TRUE;
      }
      const ptrdiff_t selected = SelectedLibraryIndex(dialog);
      if (command == IDC_LIBRARY_OPEN) {
        if (selected < 0) return TRUE;
        EndDialog(dialog, selected + 1);
        return TRUE;
      }
      if (command == IDC_LIBRARY_REMOVE) {
        if (selected < 0) return TRUE;
        const size_t index = static_cast<size_t>(selected);
        const bool removed =
            bookmarks ? mtk::library::Store::Get().RemoveBookmarkAt(index)
                      : mtk::library::Store::Get().RemoveHistoryAt(index);
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

// Set while the modeless-feeling modal Downloads dialog is on screen so live
// progress updates from the CEF download handler can refresh the list.
HWND g_downloads_dialog = nullptr;

void RefreshDownloadControls(HWND dialog);

// The header line and the "Open file" button follow the mode chosen in
// Settings, so the dialog always states what the browser will actually do.
void RefreshDownloadModeUi(HWND dialog) {
  const bool standard =
      mtk::downloads::CurrentMode() == mtk::downloads::Mode::kStandard;
  SetDlgItemTextW(
      dialog, IDC_DOWNLOADS_MODE_TEXT,
      standard
          ? L"Standard downloads: any file type is saved. Opening a file is "
            L"your choice, and risky types are confirmed first."
          : L"Safe downloads: only known file types are saved, and MTK Browser "
            L"never opens a downloaded file.");
  EnableWindow(GetDlgItem(dialog, IDC_DOWNLOADS_OPEN), standard ? TRUE : FALSE);
}

void PopulateDownloadList(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_DOWNLOADS_LIST);
  const LRESULT previous = SendMessageW(list, LB_GETCURSEL, 0, 0);
  const LRESULT top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);

  const auto& items = mtk::downloads::Manager::Get().items();
  // Newest first: an active download should never be buried under old rows.
  for (size_t offset = items.size(); offset > 0; --offset) {
    const size_t index = offset - 1;
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(
            mtk::downloads::DescribeItem(items[index]).c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                   static_cast<LPARAM>(items[index].id));
    }
  }
  if (items.empty()) {
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"No download history yet."));
    SendMessageW(list, LB_SETITEMDATA, 0, static_cast<LPARAM>(-1));
  } else {
    const LRESULT count = SendMessageW(list, LB_GETCOUNT, 0, 0);
    SendMessageW(list, LB_SETTOPINDEX, static_cast<WPARAM>(std::max<LRESULT>(top, 0)), 0);
    SendMessageW(list, LB_SETCURSEL,
                 static_cast<WPARAM>(previous >= 0 && previous < count ? previous : 0),
                 0);
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  RefreshDownloadControls(dialog);
}

const mtk::downloads::Item* SelectedDownload(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_DOWNLOADS_LIST);
  const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR) return nullptr;
  const LRESULT data =
      SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
  if (data == LB_ERR || data < 0) return nullptr;
  for (const auto& item : mtk::downloads::Manager::Get().items()) {
    if (item.id == static_cast<uint64_t>(data)) return &item;
  }
  return nullptr;
}

void RefreshDownloadControls(HWND dialog) {
  const auto* item = SelectedDownload(dialog);
  const bool live_callback = item && (item->id >> 32) == 0 &&
      BrowserClient::CanControlDownload(static_cast<uint32_t>(item->id));
  const bool paused = live_callback &&
      item->state == mtk::downloads::State::kPaused;
  const bool controllable = live_callback &&
      (item->state == mtk::downloads::State::kStarting ||
       item->state == mtk::downloads::State::kInProgress || paused);
  SetDlgItemTextW(dialog, IDC_DOWNLOADS_PAUSE, paused ? L"Resume" : L"Pause");
  EnableWindow(GetDlgItem(dialog, IDC_DOWNLOADS_PAUSE), controllable ? TRUE : FALSE);
  EnableWindow(GetDlgItem(dialog, IDC_DOWNLOADS_CANCEL), controllable ? TRUE : FALSE);
}

// Reveals the file in Explorer instead of launching it. MTK Browser never
// starts downloaded content itself, so a saved file cannot execute by mistake.
void RevealInExplorer(HWND owner, const std::wstring& path) {
  if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(owner, L"This file is no longer on disk.", L"MTK Browser",
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  const std::wstring arguments = L"/select,\"" + path + L"\"";
  ShellExecuteW(owner, L"open", L"explorer.exe", arguments.c_str(), nullptr,
                SW_SHOWNORMAL);
}

// Standard mode only. Launching is left to the shell exactly like a mainstream
// browser, but a file type that can execute code is confirmed first and MTK
// Browser still never starts a download on its own.
void OpenDownloadedFile(HWND owner, const mtk::downloads::Item& item) {
  if (item.full_path.empty() ||
      GetFileAttributesW(item.full_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(owner, L"This file is no longer on disk.", L"MTK Browser",
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (item.risky || mtk::downloads::IsRiskyFileName(item.file_name)) {
    const std::wstring warning =
        L"This file can run programs on your computer.\n\n" + item.file_name +
        L"\n\nOpen it anyway?";
    if (MessageBoxW(owner, warning.c_str(), L"MTK Browser",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
      return;
    }
  }
  ShellExecuteW(owner, L"open", item.full_path.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

INT_PTR CALLBACK DownloadsDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM) {
  switch (message) {
    case WM_INITDIALOG:
      g_downloads_dialog = dialog;
      RefreshDownloadModeUi(dialog);
      PopulateDownloadList(dialog);
      return TRUE;
    case WM_COMMAND: {
      const WORD command = LOWORD(w_param);
      if (command == IDC_DOWNLOADS_LIST && HIWORD(w_param) == LBN_SELCHANGE) {
        RefreshDownloadControls(dialog);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_FOLDER) {
        const std::wstring directory = mtk::downloads::DownloadDirectory();
        if (directory.empty()) {
          MessageBoxW(dialog, L"MTK Browser could not open the downloads folder.",
                      L"MTK Browser", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        ShellExecuteW(dialog, L"open", directory.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_OPEN) {
        if (mtk::downloads::CurrentMode() != mtk::downloads::Mode::kStandard) {
          MessageBoxW(dialog,
                      L"Safe downloads never open a saved file. Switch "
                      L"Downloads to standard mode in Settings to open files "
                      L"from here.",
                      L"MTK Browser", MB_OK | MB_ICONINFORMATION);
          return TRUE;
        }
        const auto* item = SelectedDownload(dialog);
        if (!item) return TRUE;
        if (item->state != mtk::downloads::State::kComplete) {
          MessageBoxW(dialog, L"This download has not finished yet.",
                      L"MTK Browser", MB_OK | MB_ICONINFORMATION);
          return TRUE;
        }
        OpenDownloadedFile(dialog, *item);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_SHOW ||
          (command == IDC_DOWNLOADS_LIST && HIWORD(w_param) == LBN_DBLCLK)) {
        const auto* item = SelectedDownload(dialog);
        if (!item) return TRUE;
        if (item->state != mtk::downloads::State::kComplete) {
          MessageBoxW(dialog, L"This download has not finished yet.",
                      L"MTK Browser", MB_OK | MB_ICONINFORMATION);
          return TRUE;
        }
        RevealInExplorer(dialog, item->full_path);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_PAUSE) {
        const auto* item = SelectedDownload(dialog);
        if (!item || (item->id >> 32) != 0) return TRUE;
        const uint32_t live_id = static_cast<uint32_t>(item->id);
        if (item->state == mtk::downloads::State::kPaused) {
          BrowserClient::ResumeDownload(live_id);
        } else if (item->state == mtk::downloads::State::kStarting ||
                   item->state == mtk::downloads::State::kInProgress) {
          BrowserClient::PauseDownload(live_id);
        }
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_CANCEL) {
        const auto* item = SelectedDownload(dialog);
        if (!item) return TRUE;
        if (item->state != mtk::downloads::State::kStarting &&
            item->state != mtk::downloads::State::kInProgress &&
            item->state != mtk::downloads::State::kPaused) {
          mtk::downloads::Manager::Get().Remove(item->id);
          mtk::downloads::Manager::Get().Flush();
          PopulateDownloadList(dialog);
          return TRUE;
        }
        if ((item->id >> 32) == 0) {
          BrowserClient::CancelDownload(static_cast<uint32_t>(item->id));
        }
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_CLEAR) {
        mtk::downloads::Manager::Get().ClearFinished();
        mtk::downloads::Manager::Get().Flush();
        PopulateDownloadList(dialog);
        return TRUE;
      }
      if (command == IDCANCEL) {
        EndDialog(dialog, 0);
        return TRUE;
      }
      break;
    }
    case WM_DESTROY:
      if (g_downloads_dialog == dialog) g_downloads_dialog = nullptr;
      break;
    case WM_CLOSE:
      EndDialog(dialog, 0);
      return TRUE;
  }
  return FALSE;
}

constexpr int kBaseTabGap = 6;
constexpr int kBaseTabStripInset = 10;
constexpr int kBaseNewTabWidth = 38;

// Pinned tabs keep a fixed narrow slot, wide enough for the state dot only.
constexpr int kBasePinnedTabWidth = 54;

// Width reserved at the right of the strip for the new-tab button.
int TabStripReserve() { return Scaled(96); }

int PinnedSlotWidth() { return Scaled(kBasePinnedTabWidth); }

// Slot width for the tabs that are not pinned. They stop shrinking at a
// readable minimum, after which the strip scrolls instead.
int TabSlotWidth(int width, size_t count, size_t pinned) {
  const size_t unpinned = count > pinned ? count - pinned : 0;
  const int available =
      std::max(Scaled(200),
               width - TabStripReserve() - Scaled(kBaseTabStripInset) -
                   static_cast<int>(pinned) * PinnedSlotWidth());
  return std::clamp(available / static_cast<int>(std::max<size_t>(unpinned, 1)),
                    Scaled(150), Scaled(256));
}

// Left edge of slot `index`. `index == count` is the position just past the
// last tab, which is where the new-tab button sits.
int TabSlotLeft(size_t index, int width, size_t count, size_t pinned,
                int scroll) {
  const int slot = TabSlotWidth(width, count, pinned);
  const size_t leading = std::min(index, pinned);
  const size_t trailing = index > pinned ? index - pinned : 0;
  return Scaled(kBaseTabStripInset) +
         static_cast<int>(leading) * PinnedSlotWidth() +
         static_cast<int>(trailing) * slot - scroll;
}

RECT TabRect(size_t index, int width, size_t count, size_t pinned,
             int scroll = 0) {
  const int left = TabSlotLeft(index, width, count, pinned, scroll);
  const int right =
      TabSlotLeft(index + 1, width, count, pinned, scroll) - Scaled(kBaseTabGap);
  return {left, Scaled(6), right, TabBarHeight()};
}

RECT NewTabRect(int width, size_t count, size_t pinned, int scroll = 0) {
  const int left = TabSlotLeft(count, width, count, pinned, scroll);
  return {left + Scaled(2), Scaled(8), left + Scaled(kBaseNewTabWidth),
          TabBarHeight() - Scaled(4)};
}

// Pinned tabs deliberately have no close button, so an accidental click cannot
// discard a tab the user chose to keep. They close from the tab menu instead.
RECT TabCloseRect(const RECT& tab, bool pinned) {
  if (pinned) return RECT{0, 0, 0, 0};
  const int center_y = (tab.top + tab.bottom) / 2;
  const int half = Scaled(12);
  return {tab.right - Scaled(34), center_y - half, tab.right - Scaled(10),
          center_y + half};
}

// The audio indicator is only shown while a tab is muted, and clicking it
// unmutes. CEF gives no reliable "is playing audio" signal in this runtime, so
// an always-visible speaker would be guesswork.
RECT TabMuteRect(const RECT& tab, bool pinned) {
  const int center_y = (tab.top + tab.bottom) / 2;
  const int half = Scaled(11);
  const int right = pinned ? tab.right - Scaled(6) : tab.right - Scaled(38);
  return {right - Scaled(22), center_y - half, right, center_y + half};
}

}  // namespace

BrowserWindow::BrowserWindow(HINSTANCE instance) : instance_(instance) {
  RefreshHomeUrl();
  g_bookmarks_bar =
      mtk::preferences::Store::Get().values().show_bookmarks_bar;
}

void BrowserWindow::StartUpdateCheck(bool user_initiated) {
  if (update_check_running_) return;
  if (!user_initiated &&
      !mtk::preferences::Store::Get().values().auto_update) {
    return;
  }
  update_check_running_ = true;
  update_check_user_initiated_ = user_initiated;
  HWND window = window_;
  // The check does network I/O, so it must not run on the UI thread. The worker
  // only fills a result and posts a message; it never touches browser state.
  std::thread([window]() {
    g_update_result = mtk::update::Check();
    PostMessageW(window, kUpdateResultMessage, 0, 0);
  }).detach();
}

void BrowserWindow::HandleUpdateResult() {
  update_check_running_ = false;
  const mtk::update::Result result = g_update_result;
  const bool user_initiated = update_check_user_initiated_;
  update_check_user_initiated_ = false;

  switch (result.status) {
    case mtk::update::Status::kUpdateReady: {
      // The installer has already been signature- and checksum-verified, but it
      // is still the user who decides when to install.
      const std::wstring message =
          result.detail +
          L"\n\nInstalling closes MTK Browser and runs the verified installer."
          L"\n\nInstall now?";
      if (MessageBoxW(window_, message.c_str(), L"MTK Browser update",
                      MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        if (mtk::update::LaunchInstaller(result.installer_path)) {
          PostMessageW(window_, WM_CLOSE, 0, 0);
        } else {
          MessageBoxW(window_, L"The update could not be started.",
                      L"MTK Browser update", MB_OK | MB_ICONERROR);
        }
      }
      break;
    }
    case mtk::update::Status::kRejected:
      // A failed signature is a security event, so it is always surfaced.
      MessageBoxW(window_, result.detail.c_str(), L"MTK Browser update refused",
                  MB_OK | MB_ICONWARNING);
      break;
    case mtk::update::Status::kUpToDate:
    case mtk::update::Status::kDisabled:
    case mtk::update::Status::kCheckFailed:
      // Background noise stays silent; an explicit check always answers.
      if (user_initiated) {
        MessageBoxW(window_, result.detail.c_str(), L"MTK Browser update",
                    MB_OK | MB_ICONINFORMATION);
      }
      break;
  }
}

void BrowserWindow::ShowClearDataDialog() {
  // Erasing history is also how a supervised user would hide activity, so this
  // sits behind the parent PIN when one has been set.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Clearing browsing data is protected. Enter the parent PIN.")) {
    return;
  }
  ClearDataChoice choice;
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_CLEAR_DATA), window_,
                      ClearDataDialogProc,
                      reinterpret_cast<LPARAM>(&choice)) != IDOK) {
    return;
  }

  std::wstring report;
  auto& library = mtk::library::Store::Get();
  bool library_changed = false;
  if (choice.history && library.ClearHistory()) {
    library_changed = true;
    report += L"\n• Browsing history removed";
  }
  if (choice.bookmarks && library.ClearBookmarks()) {
    library_changed = true;
    report += L"\n• Bookmarks removed";
  }
  if (library_changed && !library.Flush()) {
    MessageBoxW(window_,
                L"MTK Browser could not save the cleared history to disk.",
                L"Clear browsing data", MB_OK | MB_ICONERROR);
  }

  if (choice.downloads) {
    // Only finished rows are removed; a running transfer keeps its entry so it
    // can still be paused or canceled.
    const size_t removed = mtk::downloads::Manager::Get().ClearFinished();
    mtk::downloads::Manager::Get().Flush();
    report += L"\n• Download history cleared (" + std::to_wstring(removed) +
              L" finished entries; files on disk kept)";
    UpdateControls();
  }

  if (choice.cookies) {
    if (auto manager = CefCookieManager::GetGlobalManager(nullptr);
        manager && manager->DeleteCookies(CefString(), CefString(), nullptr)) {
      report += L"\n• Cookies and site data cleared";
    } else {
      report += L"\n• Cookies could not be cleared";
    }
  }

  if (choice.cache) {
    // CEF exposes no direct cache-clearing API, so the DevTools command is used
    // through the active browser. It applies to the whole request context.
    if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
      tab->client->browser()->GetHost()->ExecuteDevToolsMethod(
          0, "Network.clearBrowserCache", nullptr);
      report += L"\n• Cached files and images cleared";
    } else {
      report += L"\n• Cache could not be cleared: no page is open";
    }
  }

  if (choice.session) {
    report += mtk::session::Clear() ? L"\n• Saved tabs from the last session removed"
                                    : L"\n• No saved session to remove";
  }

  if (choice.site_settings) {
    auto& sites = mtk::sites::Store::Get();
    report += sites.Clear() && sites.Flush()
                  ? L"\n• Per-site settings reset to the defaults"
                  : L"\n• Per-site settings could not be reset";
  }

  MessageBoxW(window_, (L"Clear browsing data finished." + report).c_str(),
              L"Clear browsing data", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::ShowSettingsDialog() {
  // Settings hold the safety-relevant choices (home page, download mode), so
  // they sit behind the parent PIN once one has been set.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Settings are protected. Enter the parent PIN to open them.")) {
    return;
  }
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SETTINGS), window_,
                      SettingsDialogProc, 0) == IDOK &&
      mtk::preferences::Store::Get().values().startup_mode ==
          mtk::preferences::StartupMode::kHomePage) {
    mtk::session::Clear();
  }
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
              bookmarked ? L"Remove bookmark\tAlt+B" : L"Add bookmark\tAlt+B");
  AppendMenuW(popup, MF_STRING, IDM_BOOKMARKS, L"Bookmarks\tAlt+O");
  AppendMenuW(popup, MF_STRING, IDM_BOOKMARKS_BAR,
              mtk::preferences::Store::Get().values().show_bookmarks_bar
                  ? L"Hide bookmarks bar\tAlt+Shift+B"
                  : L"Show bookmarks bar\tAlt+Shift+B");
  AppendMenuW(popup, MF_STRING, IDM_IMPORT_BOOKMARKS,
              L"Import bookmarks from another browser...");
  AppendMenuW(popup, MF_STRING, IDM_HISTORY, L"History\tAlt+H");
  const size_t active_downloads = mtk::downloads::Manager::Get().ActiveCount();
  const std::wstring downloads_label =
      active_downloads == 0
          ? std::wstring(L"Downloads\tAlt+J")
          : L"Downloads (" + std::to_wstring(active_downloads) + L" active)\tAlt+J";
  AppendMenuW(popup, MF_STRING, IDM_DOWNLOADS, downloads_label.c_str());
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(popup, MF_STRING, IDM_NEW_WINDOW, L"New window\tAlt+W");
  AppendMenuW(popup, MF_STRING, IDM_NEW_PRIVATE_WINDOW,
              L"New private window\tAlt+Shift+W");
  AppendMenuW(popup,
              MF_STRING | (closed_tab_urls_.empty() ? MF_GRAYED : MF_ENABLED),
              IDM_REOPEN_TAB, L"Reopen closed tab\tAlt+Shift+T");
  AppendMenuW(popup, MF_STRING, IDM_FULL_SCREEN,
              full_screen_ ? L"Exit full screen\tAlt+Enter"
                           : L"Full screen\tAlt+Enter");
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_PRINT,
              L"Print...\tAlt+P");
  AppendMenuW(popup, MF_STRING, IDM_SHORTCUTS, L"Keyboard shortcuts\tAlt+/");
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED),
              IDM_SITE_SETTINGS, L"Settings for this site...\tAlt+S");
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);

  // Profiles. Each one is a separate process because the data stores and the
  // Chromium cache are per-process, so switching opens a window rather than
  // swapping data underneath this one.
  if (HMENU profiles = CreatePopupMenu()) {
    const auto available = mtk::profile::List();
    profile_menu_ids_.clear();
    for (size_t index = 0; index < available.size() &&
                           index < kMaxProfileMenuEntries;
         ++index) {
      const bool active = available[index] == mtk::profile::ActiveId();
      AppendMenuW(profiles,
                  MF_STRING | (active ? MF_CHECKED | MF_GRAYED : MF_ENABLED),
                  static_cast<UINT_PTR>(IDM_PROFILE_FIRST + index),
                  available[index].c_str());
      profile_menu_ids_.push_back(available[index]);
    }
    AppendMenuW(profiles, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(profiles, MF_STRING, IDM_PROFILE_NEW, L"New profile...");
    const std::wstring label =
        L"Profile: " + mtk::profile::ActiveId();
    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles),
                label.c_str());
  }
  AppendMenuW(popup, MF_STRING, IDM_CLEAR_DATA, L"Clear browsing data...");
  AppendMenuW(popup,
              MF_STRING | (update_check_running_ ? MF_GRAYED : MF_ENABLED),
              IDM_CHECK_UPDATES,
              update_check_running_ ? L"Checking for updates..."
                                    : L"Check for updates...");
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

void BrowserWindow::ShowDownloadsDialog() {
  DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_DOWNLOADS), window_,
                  DownloadsDialogProc, 0);
}

void BrowserWindow::OnBrowserDownloadsChanged(BrowserClient*) {
  // The download handler runs on this same UI thread, so the open dialog can be
  // refreshed directly.
  if (g_downloads_dialog) PopulateDownloadList(g_downloads_dialog);
  if (downloads_) InvalidateRect(downloads_, nullptr, FALSE);
  if (window_) SetTimer(window_, kDownloadSaveTimer, 1500, nullptr);
}

void BrowserWindow::ScheduleLibrarySave() {
  SetTimer(window_, kLibrarySaveTimer, 1500, nullptr);
}

void BrowserWindow::ScheduleSessionSave() {
  if (closing_ || !window_ || !owns_session_) return;
  if (mtk::preferences::Store::Get().values().startup_mode ==
      mtk::preferences::StartupMode::kContinueSession) {
    SetTimer(window_, kSessionSaveTimer, 1200, nullptr);
  }
}

void BrowserWindow::SaveSessionSnapshot() {
  // A window that does not own the session must not overwrite the snapshot,
  // and must not clear it either.
  if (!owns_session_) return;
  if (mtk::preferences::Store::Get().values().startup_mode !=
      mtk::preferences::StartupMode::kContinueSession) {
    mtk::session::Clear();
    return;
  }
  std::vector<std::string> urls;
  urls.reserve(tabs_.size());
  for (const auto& tab : tabs_) urls.push_back(WideToUtf8(tab.url));
  mtk::session::Save(urls, active_tab_);
}

void BrowserWindow::CreateStartupTabs() {
  // A window opened by the user starts on the addresses it was given. Only the
  // window that starts the process follows the startup preference, so a second
  // window never restores the previous session on top of the current one.
  if (!startup_urls_.empty()) {
    for (const auto& url : startup_urls_) CreateTab(url);
    if (!tabs_.empty()) return;
  }
  if (mtk::preferences::Store::Get().values().startup_mode ==
          mtk::preferences::StartupMode::kContinueSession &&
      owns_session_) {
    const auto snapshot = mtk::session::Load();
    for (const auto& url : snapshot.urls) CreateTab(url);
    if (!tabs_.empty()) {
      active_tab_ = std::min(snapshot.active_index, tabs_.size() - 1);
      return;
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
}

BrowserWindow* BrowserWindow::Open(HINSTANCE instance, int show_command,
                                   const std::vector<std::string>& startup_urls,
                                   bool private_session) {
  auto* window = new BrowserWindow(instance);
  window->startup_urls_ = startup_urls;
  window->private_session_ = private_session;
  if (private_session) {
    // An empty cache_path is what makes the context in-memory: cookies, cache
    // and storage live only as long as this context does.
    CefRequestContextSettings settings;
    window->request_context_ = CefRequestContext::CreateContext(settings, nullptr);
    if (!window->request_context_) {
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
  window_ = CreateWindowExW(0, kWindowClass, title.c_str(),
                            WS_OVERLAPPEDWINDOW,
                            offset == 0 ? CW_USEDEFAULT : 60 + offset,
                            offset == 0 ? CW_USEDEFAULT : 40 + offset,
                            1360, 860, nullptr, nullptr, instance_, this);
  if (!window_) return false;

  const BOOL dark = TRUE;
  DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
  ShowWindow(window_, show_command);
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
      // TME_LEAVE has to be re-armed on every entry, otherwise the leave
      // notification arrives only once and the animation would keep running.
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
      ShowLibraryDialog(true);
      return true;
    case 'H':
      ShowLibraryDialog(false);
      return true;
    case 'J':
      ShowDownloadsDialog();
      return true;
    case 'S':
      ShowSiteSettingsDialog();
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
  const auto make_font = [](int height, int weight, const wchar_t* face) {
    return CreateFontW(Scaled(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
  };
  for (HFONT* font : {&ui_font_, &tab_font_, &glyph_font_, &badge_font_}) {
    if (*font) {
      DeleteObject(*font);
      *font = nullptr;
    }
  }
  ui_font_ = make_font(-16, FW_MEDIUM, L"Segoe UI Variable");
  tab_font_ = make_font(-15, FW_SEMIBOLD, L"Segoe UI Variable");
  glyph_font_ = make_font(-18, FW_NORMAL, L"Segoe UI Symbol");
  badge_font_ = make_font(-11, FW_BOLD, L"Segoe UI");
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
  const DWORD button_style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
  // Button text is the accessible name, not the drawn glyph: an owner-draw
  // button is announced by its window text, and "←" or "⋮" tells a screen
  // reader user nothing. ButtonGlyph() supplies what is painted instead.
  back_ = CreateWindowW(L"BUTTON", L"Back", button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kBackId), instance_, nullptr);
  forward_ = CreateWindowW(L"BUTTON", L"Forward", button_style, 0, 0, 0, 0, window_,
                           reinterpret_cast<HMENU>(kForwardId), instance_, nullptr);
  reload_ = CreateWindowW(L"BUTTON", L"Reload", button_style, 0, 0, 0, 0, window_,
                          reinterpret_cast<HMENU>(kReloadId), instance_, nullptr);
  home_ = CreateWindowW(L"BUTTON", L"Home page", button_style, 0, 0, 0, 0, window_,
                        reinterpret_cast<HMENU>(kHomeId), instance_, nullptr);
  shield_ = CreateWindowW(L"BUTTON", L"MTK Shields", button_style, 0, 0, 0, 0,
                          window_, reinterpret_cast<HMENU>(kShieldId), instance_,
                          nullptr);
  downloads_ = CreateWindowW(L"BUTTON", L"Downloads", button_style, 0, 0, 0, 0,
                             window_, reinterpret_cast<HMENU>(kDownloadsId),
                             instance_, nullptr);
  menu_ = CreateWindowW(L"BUTTON", L"Browser menu", button_style, 0, 0, 0, 0,
                        window_, reinterpret_cast<HMENU>(kMenuId), instance_,
                        nullptr);
  address_ = CreateWindowW(L"EDIT", L"Search or enter address",
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
  find_previous_ = CreateWindowW(L"BUTTON", L"Find previous",
                                 button_style & ~WS_VISIBLE, 0, 0, 0, 0, window_,
                                 reinterpret_cast<HMENU>(kFindPreviousId),
                                 instance_, nullptr);
  find_next_ = CreateWindowW(L"BUTTON", L"Find next",
                             button_style & ~WS_VISIBLE, 0, 0, 0, 0, window_,
                             reinterpret_cast<HMENU>(kFindNextId), instance_,
                             nullptr);
  find_close_ = CreateWindowW(L"BUTTON", L"Close find bar",
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
  SetWindowTheme(address_, L"DarkMode_CFD", nullptr);
  SetWindowTheme(find_, L"DarkMode_CFD", nullptr);
  SetWindowSubclass(address_, AddressProc, 1,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(find_, FindProc, 2,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(shield_, ShieldProc, 3,
                    reinterpret_cast<DWORD_PTR>(this));
  // A missing or malformed strip is not an error: the shield keeps its glyph.
  shield_strip_.Load(kShieldStripFile);
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
      {"Alt+S", "Settings for this site"},
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

void BrowserWindow::ShowSiteSettingsDialog() {
  auto* tab = ActiveTab();
  const std::string origin =
      tab ? mtk::sites::OriginOf(WideToUtf8(tab->url)) : std::string();
  if (origin.empty()) {
    MessageBoxW(window_,
                L"This page has no site settings. Open an ordinary web page "
                L"first.",
                L"Site settings", MB_OK | MB_ICONINFORMATION);
    return;
  }
  // Allowing a camera or a microphone is a safety decision, so the dialog sits
  // behind the parent PIN like the rest of Settings.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Site settings are protected. Enter the parent PIN to change them.")) {
    return;
  }

  auto& store = mtk::sites::Store::Get();
  SiteSettingsContext context{store.For(origin), false};
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SITE_SETTINGS), window_,
                      SiteSettingsDialogProc,
                      reinterpret_cast<LPARAM>(&context)) != IDOK) {
    return;
  }

  const bool changed = context.forget ? store.Forget(origin)
                                      : store.Set(context.rule);
  if (!changed || !store.Flush()) {
    MessageBoxW(window_, L"MTK Browser could not save the site settings.",
                L"Site settings", MB_OK | MB_ICONERROR);
    return;
  }
  // Cookie and filtering changes take effect on the next load, and the zoom is
  // applied straight away so the result is visible.
  if (tab) ApplySiteZoom(*tab);
  if (tab && tab->client->browser()) tab->client->browser()->ReloadIgnoreCache();
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
  // A press that never left the dead zone is an ordinary click, so it selects.
  if (!was_dragging && candidate < tabs_.size()) SelectTab(candidate);
}

void BrowserWindow::ShowTabMenu(size_t index, POINT screen) {
  if (index >= tabs_.size()) return;
  HMENU popup = CreatePopupMenu();
  if (!popup) return;
  const bool pinned = tabs_[index].pinned;
  const bool muted = tabs_[index].muted;
  AppendMenuW(popup, MF_STRING, kTabMenuDuplicate, L"Duplicate tab\tAlt+U");
  AppendMenuW(popup, MF_STRING, kTabMenuPin,
              pinned ? L"Unpin tab\tAlt+Shift+P" : L"Pin tab\tAlt+Shift+P");
  AppendMenuW(popup, MF_STRING, kTabMenuMute,
              muted ? L"Unmute site\tAlt+M" : L"Mute site\tAlt+M");
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(popup, MF_STRING, kTabMenuClose, L"Close tab\tAlt+X");
  AppendMenuW(popup, MF_STRING | (tabs_.size() > 1 ? 0 : MF_GRAYED),
              kTabMenuCloseOthers, L"Close other tabs");
  AppendMenuW(popup,
              MF_STRING | (index + 1 < tabs_.size() ? 0 : MF_GRAYED),
              kTabMenuCloseRight, L"Close tabs to the right");
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

void BrowserWindow::PaintTabStrip(HDC dc, const RECT& client) {
  SetBkMode(dc, TRANSPARENT);
  const size_t pinned_count = PinnedCount();
  for (size_t index = 0; index < tabs_.size(); ++index) {
    const RECT rect =
        TabRect(index, client.right, tabs_.size(), pinned_count, tab_scroll_);
    const bool active = index == active_tab_;
    const bool hovered = index == hovered_tab_;
    const bool pinned = tabs_[index].pinned;

    const int radius = Scaled(14);
    if (active) {
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
    } else if (hovered) {
      FillRounded(dc, rect, radius, kHoverTabColor);
    }

    // A pinned tab has no room for a title, so its dot is centred and acts as
    // the whole tab, the way a favicon-only pinned tab reads elsewhere.
    const int dot_x = pinned ? (rect.left + rect.right) / 2 - Scaled(6)
                             : rect.left + Scaled(18);
    const int dot_y = (rect.top + rect.bottom) / 2;
    FillCircle(dc, dot_x, dot_y, pinned ? Scaled(5) : Scaled(4),
               tabs_[index].loading ? kAccentSoftColor
                                    : (active ? kAccentColor : kDisabledColor));

    if (!pinned) {
      SelectObject(dc, tab_font_);
      SetTextColor(dc, active ? kTextColor : kMutedColor);
      RECT text = rect;
      text.left = dot_x + Scaled(12);
      text.right = rect.right - (tabs_[index].muted ? Scaled(62) : Scaled(38));
      DrawTextW(dc, tabs_[index].title.c_str(), -1, &text,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (tabs_[index].muted) {
      RECT mute = TabMuteRect(rect, pinned);
      SelectObject(dc, glyph_font_);
      SetTextColor(dc, active ? kTextColor : kMutedColor);
      DrawTextW(dc, L"\U0001F507", -1, &mute,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }

    if (!pinned) {
      const RECT close = TabCloseRect(rect, pinned);
      if (hovered && hovered_tab_close_) {
        FillRounded(dc, close, (close.bottom - close.top) / 2,
                    kControlHoverColor);
      }
      SelectObject(dc, glyph_font_);
      SetTextColor(dc, hovered || active ? kTextColor : kDisabledColor);
      DrawTextW(dc, L"\u00D7", -1, const_cast<RECT*>(&close),
                DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
  }

  RECT plus =
      NewTabRect(client.right, tabs_.size(), pinned_count, tab_scroll_);
  if (hovered_new_tab_) FillRounded(dc, plus, Scaled(10), kControlHoverColor);
  SelectObject(dc, glyph_font_);
  SetTextColor(dc, hovered_new_tab_ ? kTextColor : kMutedColor);
  DrawTextW(dc, L"+", -1, &plus, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void BrowserWindow::PaintOmniboxFrame(HDC dc) {
  HWND field = find_visible_ ? find_ : address_;
  RECT box{};
  GetWindowRect(field, &box);
  MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&box), 2);
  InflateRect(&box, Scaled(30), Scaled(9));

  const bool focused = GetFocus() == field;
  const int radius = (box.bottom - box.top) / 2;
  FillRounded(dc, box, radius, kControlColor);
  StrokeRounded(dc, box, radius, focused ? kAccentColor : kBorderColor,
                focused ? Scaled(2) : 1);

  auto* tab = ActiveTab();
  const bool secure = tab && tab->url.starts_with(L"https://");
  SelectObject(dc, glyph_font_);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, find_visible_ ? kMutedColor
                                 : (secure ? kShieldColor : kMutedColor));
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
  HBRUSH brush = CreateSolidBrush(kControlColor);
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
  // A private window is drawn darker, so it is obvious at a glance which window
  // is which. Relying on the title alone would be too easy to miss.
  if (private_session_) {
    FillGradient(dc, tab_bar, kPrivateBarTop, kPrivateBarBottom);
    FillGradient(dc, toolbar, kPrivateBarBottom, kPrivateBarBottom);
  } else {
    FillGradient(dc, tab_bar, kTabBarTop, kTabBarBottom);
    FillGradient(dc, toolbar, kToolbarTop, kToolbarBottom);
  }

  PaintTabStrip(dc, client);
  PaintBookmarksBar(dc, client);
  PaintOmniboxFrame(dc);
  PaintProgress(dc, client);
}

void BrowserWindow::PaintBookmarksBar(HDC dc, const RECT& client) {
  const int height = BookmarksBarHeight();
  if (height == 0) return;
  const int top = TabBarHeight() + ToolbarHeight();
  RECT bar{0, top, client.right, top + height};
  FillGradient(dc, bar, kToolbarBottom, kToolbarBottom);

  bookmark_bar_hits_.clear();
  const auto& bookmarks = mtk::library::Store::Get().bookmarks();
  SetBkMode(dc, TRANSPARENT);
  SelectObject(dc, tab_font_);

  if (bookmarks.empty()) {
    RECT label{Scaled(14), top, client.right, top + height};
    SetTextColor(dc, kDisabledColor);
    DrawTextW(dc, L"No bookmarks yet. Ctrl+D saves the page you are on.", -1,
              &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    return;
  }

  int x = Scaled(10);
  for (size_t index = 0; index < bookmarks.size(); ++index) {
    const std::wstring& title = bookmarks[index].title;
    // Each button is only as wide as its label needs, capped so one long title
    // cannot take the whole bar.
    RECT measure{0, 0, Scaled(160), height};
    DrawTextW(dc, title.c_str(), -1, &measure,
              DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    const int width =
        std::clamp<int>(measure.right + Scaled(20), Scaled(48), Scaled(180));
    if (x + width > client.right - Scaled(10)) break;

    RECT button{x, top + Scaled(4), x + width, top + height - Scaled(4)};
    const bool hovered = index == hovered_bookmark_;
    if (hovered) FillRounded(dc, button, Scaled(8), kControlHoverColor);
    RECT text = button;
    text.left += Scaled(10);
    text.right -= Scaled(10);
    SetTextColor(dc, hovered ? kTextColor : kMutedColor);
    DrawTextW(dc, title.c_str(), -1, &text,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    bookmark_bar_hits_.push_back(BookmarkHit{button, index});
    x += width + Scaled(6);
  }
}

void BrowserWindow::DrawButton(const DRAWITEMSTRUCT& item) {
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
  RECT bounds = item.rcItem;

  FillGradient(item.hDC, bounds, kToolbarTop, kToolbarBottom);
  SetBkMode(item.hDC, TRANSPARENT);

  if (!disabled && (hot || pressed)) {
    const int center_x = (bounds.left + bounds.right) / 2;
    const int center_y = (bounds.top + bounds.bottom) / 2;
    const int radius = std::min(bounds.right - bounds.left,
                                bounds.bottom - bounds.top) / 2 - Scaled(2);
    FillCircle(item.hDC, center_x, center_y, radius,
               pressed ? kBorderColor : kControlHoverColor);
  }

  bool painted_icon = false;
  if (item.CtlID == kShieldId && shield_strip_.valid()) {
    // Frame 0 is the resting pose: a bare shield. Hover plays the sequence once
    // and holds the last frame, where the swords are crossed and the tick is
    // shown.
    RECT icon = bounds;
    const int inset = Scaled(6);
    InflateRect(&icon, -inset, -inset);
    painted_icon = shield_strip_.Draw(item.hDC, icon,
                                      shield_hovering_ ? shield_frame_ : 0);
  }

  if (!painted_icon) {
    const wchar_t* text = ButtonGlyph(item.CtlID);
    SelectObject(item.hDC, glyph_font_);
    SetTextColor(item.hDC, disabled ? kDisabledColor
                                    : (item.CtlID == kShieldId ? kShieldColor
                                                               : kTextColor));
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
    for (size_t index = 0; index < tabs_.size(); ++index) {
      const RECT rect =
          TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_);
      if (PtInRect(&rect, point)) {
        hovered_tab_ = index;
        const RECT close = TabCloseRect(rect, tabs_[index].pinned);
        hovered_tab_close_ = PtInRect(&close, point) != FALSE;
        break;
      }
    }
    if (hovered_tab_ == static_cast<size_t>(-1)) {
      RECT plus =
          NewTabRect(client.right, tabs_.size(), pinned, tab_scroll_);
      hovered_new_tab_ = PtInRect(&plus, point) != FALSE;
    }
  }

  const size_t previous_bookmark = hovered_bookmark_;
  hovered_bookmark_ = static_cast<size_t>(-1);
  if (BookmarksBarHeight() > 0) {
    for (const BookmarkHit& hit : bookmark_bar_hits_) {
      if (PtInRect(&hit.bounds, point)) {
        hovered_bookmark_ = hit.index;
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
  }
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

void BrowserWindow::LaunchProfile(const std::wstring& profile_id) {
  if (!mtk::profile::IsValidId(profile_id)) return;
  wchar_t executable[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    MessageBoxW(window_, L"MTK Browser could not find its own program file.",
                L"Profiles", MB_OK | MB_ICONERROR);
    return;
  }
  // The id is validated above, so it cannot contain a quote or a space and the
  // command line cannot be broken out of.
  std::wstring command_line = L"\"" + std::wstring(executable) + L"\"";
  if (profile_id != mtk::profile::kDefaultId) {
    command_line += L" --profile=" + profile_id;
  }
  STARTUPINFOW startup{sizeof(STARTUPINFOW)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable, command_line.data(), nullptr, nullptr, FALSE,
                      0, nullptr, nullptr, &startup, &process)) {
    MessageBoxW(window_, L"That profile could not be opened.", L"Profiles",
                MB_OK | MB_ICONERROR);
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

void BrowserWindow::CreateProfile() {
  // Profiles hold separate browsing data, so creating one is a parent-level
  // action and sits behind the PIN like the other safety settings.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Profiles are protected. Enter the parent PIN to add one.")) {
    return;
  }
  std::wstring name;
  if (!PromptForText(instance_, window_, L"New profile",
                     L"Profile name (letters, digits, '-' and '_')", &name)) {
    return;
  }
  if (!mtk::profile::IsValidId(name) || name == mtk::profile::kDefaultId) {
    MessageBoxW(window_,
                L"That name cannot be used. Use up to 32 letters, digits, '-' "
                L"or '_', and not \"Default\".",
                L"Profiles", MB_OK | MB_ICONWARNING);
    return;
  }
  if (!mtk::profile::Create(name)) {
    MessageBoxW(window_, L"That profile could not be created.", L"Profiles",
                MB_OK | MB_ICONERROR);
    return;
  }
  LaunchProfile(name);
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

void BrowserWindow::ClearHover() {
  mouse_tracking_ = false;
  if (hovered_tab_ == static_cast<size_t>(-1) && !hovered_new_tab_) return;
  hovered_tab_ = static_cast<size_t>(-1);
  hovered_tab_close_ = false;
  hovered_new_tab_ = false;
  RECT client{};
  GetClientRect(window_, &client);
  RECT strip{0, 0, client.right, TabBarHeight()};
  InvalidateRect(window_, &strip, FALSE);
}

LRESULT BrowserWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_CREATE:
      CreateControls();
      CreateStartupTabs();
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
      } else if (w_param == kDownloadSaveTimer) {
        KillTimer(window_, kDownloadSaveTimer);
        mtk::downloads::Manager::Get().Flush();
      } else if (w_param == kUpdateStartupTimer) {
        KillTimer(window_, kUpdateStartupTimer);
        StartUpdateCheck(false);
      } else if (w_param == kUpdatePeriodicTimer) {
        StartUpdateCheck(false);
      } else if (w_param == kShieldAnimTimer) {
        // The shield sequence plays once and holds its final pose: the swords
        // are drawn, they meet, and the tick stays. Looping it would make the
        // swords retract into the shield again, which reads as a glitch.
        if (shield_hovering_ && shield_strip_.valid() &&
            shield_frame_ + 1 < shield_strip_.frame_count()) {
          ++shield_frame_;
          if (shield_) InvalidateRect(shield_, nullptr, FALSE);
        } else {
          KillTimer(window_, kShieldAnimTimer);
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
      if (BookmarksBarHeight() > 0) {
        for (const BookmarkHit& hit : bookmark_bar_hits_) {
          if (PtInRect(&hit.bounds, point)) {
            OpenLibraryEntry(true, hit.index);
            return 0;
          }
        }
      }
      if (point.y < TabBarHeight()) {
        RECT client{};
        GetClientRect(window_, &client);
        const size_t pinned = PinnedCount();
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect =
              TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_);
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
        RECT plus = NewTabRect(client.right, tabs_.size(), pinned, tab_scroll_);
        InflateRect(&plus, Scaled(3), Scaled(8));
        if (PtInRect(&plus, point)) CreateTab(kHomeUrl);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      if (drag_candidate_ != kNoTab) {
        EndTabDrag();
        return 0;
      }
      break;
    case WM_CAPTURECHANGED:
      // Losing capture for any other reason must not leave a drag armed.
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
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect =
              TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_);
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
      if (!full_screen_ && point.y >= 0 && point.y < TabBarHeight()) {
        RECT client{};
        GetClientRect(window_, &client);
        const size_t pinned = PinnedCount();
        for (size_t index = 0; index < tabs_.size(); ++index) {
          const RECT rect =
              TabRect(index, client.right, tabs_.size(), pinned, tab_scroll_);
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
        case kShieldId:
          mtk::shields::ShowPanel(
              instance_, window_, shield_, BrowserClient::blocked_count(),
              mtk::downloads::CurrentMode() ==
                  mtk::downloads::Mode::kStandard);
          break;
        case kDownloadsId:
          ShowDownloadsDialog();
          break;
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
  if (index == active_tab_) UpdateControls();
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
