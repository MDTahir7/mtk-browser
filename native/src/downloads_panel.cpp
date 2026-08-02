#include "downloads_panel.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "download_manager.h"
#include "strings.h"

namespace mtk::downloads_panel {
namespace {

constexpr wchar_t kClassName[] = L"MTKDownloadsPanel";
constexpr UINT_PTR kDismissTimer = 1;
constexpr UINT kDismissPollMs = 150;
// Enough rows to cover "what did I just download" without turning the popup
// into a window of its own. The full list stays one click away.
constexpr size_t kMaxRows = 5;

// Base metrics at 96 DPI.
constexpr int kBaseWidth = 380;
constexpr int kBaseRowHeight = 62;
constexpr int kBaseHeaderHeight = 38;
constexpr int kBaseFooterHeight = 38;

constexpr COLORREF kBackground = RGB(17, 21, 34);
constexpr COLORREF kBorder = RGB(44, 52, 78);
constexpr COLORREF kText = RGB(240, 244, 255);
constexpr COLORREF kMuted = RGB(150, 162, 190);
constexpr COLORREF kAccent = RGB(124, 92, 255);
constexpr COLORREF kAccentSoft = RGB(94, 214, 255);
constexpr COLORREF kHover = RGB(30, 36, 56);
constexpr COLORREF kTrack = RGB(32, 38, 58);

enum class Action {
  kPause,
  kResume,
  kCancel,
  kOpen,
  kFolder,
  kCopy,
  kRemove,
  kShowAll,
  kClearFinished,
};

struct Hit {
  RECT bounds{};
  Action action = Action::kShowAll;
  uint64_t id = 0;
};

HWND g_panel = nullptr;
HWND g_anchor = nullptr;
HINSTANCE g_instance = nullptr;
Callbacks g_callbacks;
int g_dpi = 96;
bool g_dismiss_armed = false;
std::vector<Hit> g_hits;
size_t g_hovered = static_cast<size_t>(-1);
HFONT g_title_font = nullptr;
HFONT g_body_font = nullptr;

int Scaled(int value) { return MulDiv(value, g_dpi, 96); }

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF colour) {
  HBRUSH brush = CreateSolidBrush(colour);
  HGDIOBJ old_brush = SelectObject(dc, brush);
  HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(brush);
}

void ReleaseFonts() {
  for (HFONT* font : {&g_title_font, &g_body_font}) {
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
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable");
  };
  g_title_font = make(-14, FW_SEMIBOLD);
  g_body_font = make(-12, FW_NORMAL);
}

// Newest first, capped. The manager keeps history in insertion order, so the
// list is walked backwards.
std::vector<mtk::downloads::Item> VisibleItems() {
  const auto& all = mtk::downloads::Manager::Get().items();
  std::vector<mtk::downloads::Item> rows;
  for (size_t index = all.size(); index-- > 0 && rows.size() < kMaxRows;) {
    rows.push_back(all[index]);
  }
  return rows;
}

int PanelHeight(size_t row_count) {
  const int rows = static_cast<int>(std::max<size_t>(row_count, 1));
  return Scaled(kBaseHeaderHeight) + rows * Scaled(kBaseRowHeight) +
         Scaled(kBaseFooterHeight);
}

bool PanelHoldsForeground() {
  HWND foreground = GetForegroundWindow();
  if (!foreground || !g_panel) return false;
  return foreground == g_panel || GetParent(foreground) == g_panel ||
         foreground == GetWindow(g_panel, GW_OWNER);
}

// Puts the file on the clipboard as a shell file drop, which is what Explorer
// pastes. This stands in for dragging the file out of the panel: a drag loop
// needs an OLE-initialised thread, which the browser UI thread does not
// guarantee, and a failed drag would be worse than no drag.
bool CopyFileToClipboard(HWND owner, const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;

  // DROPFILES is followed by a double-null terminated list of paths.
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
  if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str())) {
    SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
  }
}

// Only ever called for a file the download policy already accepted, and a risky
// file type asks first.
void OpenDownloadedFile(HWND owner, const mtk::downloads::Item& item) {
  if (item.full_path.empty()) return;
  if (mtk::downloads::CurrentMode() != mtk::downloads::Mode::kStandard) {
    MessageBoxW(owner,
                L"Safe downloads never open files. Switch to standard downloads "
                L"in Settings if you want MTK Browser to open them.",
                L"Downloads", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (item.risky) {
    const std::wstring question =
        L"\"" + item.file_name +
        L"\" is a type of file that can run code on this computer.\n\nOpen it "
        L"anyway?";
    if (MessageBoxW(owner, question.c_str(), L"Downloads",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
      return;
    }
  }
  SHELLEXECUTEINFOW info{sizeof(SHELLEXECUTEINFOW)};
  info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.hwnd = owner;
  info.lpVerb = L"open";
  info.lpFile = item.full_path.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    MessageBoxW(owner, L"Windows could not open that file.", L"Downloads",
                MB_OK | MB_ICONWARNING);
  }
}

void PaintRowActions(HDC dc, const mtk::downloads::Item& item, const RECT& row) {
  const bool active = item.state == mtk::downloads::State::kInProgress ||
                      item.state == mtk::downloads::State::kPaused ||
                      item.state == mtk::downloads::State::kStarting;
  struct Entry {
    const wchar_t* label;
    Action action;
  };
  using Text = mtk::strings::Id;
  std::vector<Entry> entries;
  if (active) {
    const bool controllable =
        g_callbacks.controllable && g_callbacks.controllable(item.id);
    if (controllable) {
      entries.push_back(
          item.state == mtk::downloads::State::kPaused
              ? Entry{mtk::strings::Get(Text::kResume), Action::kResume}
              : Entry{mtk::strings::Get(Text::kPause), Action::kPause});
      entries.push_back(Entry{mtk::strings::Get(Text::kCancel), Action::kCancel});
    }
  } else if (item.state == mtk::downloads::State::kComplete) {
    entries.push_back(Entry{mtk::strings::Get(Text::kOpen), Action::kOpen});
    entries.push_back(Entry{mtk::strings::Get(Text::kFolder), Action::kFolder});
    entries.push_back(Entry{mtk::strings::Get(Text::kCopy), Action::kCopy});
  } else {
    entries.push_back(Entry{mtk::strings::Get(Text::kRemove), Action::kRemove});
  }

  SelectObject(dc, g_body_font);
  int x = row.left + Scaled(12);
  const int y = row.bottom - Scaled(20);
  for (const Entry& entry : entries) {
    RECT measure{0, 0, Scaled(120), Scaled(16)};
    DrawTextW(dc, entry.label, -1, &measure, DT_SINGLELINE | DT_CALCRECT);
    RECT hit{x, y - Scaled(3), x + measure.right + Scaled(12), y + Scaled(15)};
    const bool hovered =
        g_hovered < g_hits.size() && g_hits[g_hovered].action == entry.action &&
        g_hits[g_hovered].id == item.id;
    if (hovered) FillRounded(dc, hit, Scaled(7), kHover);
    SetTextColor(dc, hovered ? kText : kAccentSoft);
    RECT text = hit;
    text.left += Scaled(6);
    DrawTextW(dc, entry.label, -1, &text, DT_SINGLELINE | DT_VCENTER);
    g_hits.push_back(Hit{hit, entry.action, item.id});
    x = hit.right + Scaled(4);
  }
}

void Paint(HDC dc, const RECT& client) {
  HBRUSH background = CreateSolidBrush(kBackground);
  FillRect(dc, &client, background);
  DeleteObject(background);
  HBRUSH border = CreateSolidBrush(kBorder);
  FrameRect(dc, &client, border);
  DeleteObject(border);
  SetBkMode(dc, TRANSPARENT);

  g_hits.clear();

  RECT header{client.left + Scaled(14), client.top + Scaled(10),
              client.right - Scaled(14),
              client.top + Scaled(kBaseHeaderHeight)};
  SelectObject(dc, g_title_font);
  SetTextColor(dc, kText);
  DrawTextW(dc, mtk::strings::Get(mtk::strings::Id::kDownloads), -1, &header,
            DT_SINGLELINE | DT_VCENTER);

  const auto rows = VisibleItems();
  int y = client.top + Scaled(kBaseHeaderHeight);
  if (rows.empty()) {
    RECT empty{client.left + Scaled(14), y, client.right - Scaled(14),
               y + Scaled(kBaseRowHeight)};
    SelectObject(dc, g_body_font);
    SetTextColor(dc, kMuted);
    DrawTextW(dc, mtk::strings::Get(mtk::strings::Id::kNothingDownloaded), -1,
              &empty, DT_SINGLELINE | DT_VCENTER);
    y = empty.bottom;
  }

  for (const auto& item : rows) {
    RECT row{client.left + Scaled(6), y, client.right - Scaled(6),
             y + Scaled(kBaseRowHeight)};
    SelectObject(dc, g_title_font);
    SetTextColor(dc, kText);
    RECT name{row.left + Scaled(12), row.top + Scaled(8), row.right - Scaled(12),
              row.top + Scaled(26)};
    DrawTextW(dc, item.file_name.c_str(), -1, &name,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Progress replaces the status line while bytes are still moving, because
    // it says the same thing more directly.
    const bool running = item.state == mtk::downloads::State::kInProgress ||
                         item.state == mtk::downloads::State::kPaused;
    if (running && item.percent >= 0) {
      RECT track{name.left, name.bottom + Scaled(3), name.right,
                 name.bottom + Scaled(7)};
      FillRounded(dc, track, Scaled(2), kTrack);
      RECT fill = track;
      fill.right = track.left + MulDiv(track.right - track.left,
                                       std::clamp(item.percent, 0, 100), 100);
      if (fill.right > fill.left) FillRounded(dc, fill, Scaled(2), kAccent);
    }
    SelectObject(dc, g_body_font);
    SetTextColor(dc, kMuted);
    RECT status{name.left, name.bottom + Scaled(8), name.right,
                name.bottom + Scaled(24)};
    DrawTextW(dc, mtk::downloads::DescribeItem(item).c_str(), -1, &status,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    PaintRowActions(dc, item, row);
    y = row.bottom;
  }

  RECT footer{client.left + Scaled(6), y, client.right - Scaled(6),
              client.bottom};
  SelectObject(dc, g_body_font);
  struct FooterEntry {
    const wchar_t* label;
    Action action;
  };
  const FooterEntry footer_entries[] = {
      {mtk::strings::Get(mtk::strings::Id::kAllDownloads), Action::kShowAll},
      {mtk::strings::Get(mtk::strings::Id::kClearFinished),
       Action::kClearFinished},
  };
  int footer_x = footer.left + Scaled(12);
  for (const FooterEntry& entry : footer_entries) {
    RECT measure{0, 0, Scaled(160), Scaled(16)};
    DrawTextW(dc, entry.label, -1, &measure, DT_SINGLELINE | DT_CALCRECT);
    RECT hit{footer_x, footer.top + Scaled(8),
             footer_x + measure.right + Scaled(12), footer.top + Scaled(28)};
    const bool hovered = g_hovered < g_hits.size() &&
                         g_hits[g_hovered].action == entry.action &&
                         g_hits[g_hovered].id == 0;
    if (hovered) FillRounded(dc, hit, Scaled(7), kHover);
    SetTextColor(dc, hovered ? kText : kAccentSoft);
    RECT text = hit;
    text.left += Scaled(6);
    DrawTextW(dc, entry.label, -1, &text, DT_SINGLELINE | DT_VCENTER);
    g_hits.push_back(Hit{hit, entry.action, 0});
    footer_x = hit.right + Scaled(6);
  }
}

const mtk::downloads::Item* FindItem(uint64_t id) {
  const auto& all = mtk::downloads::Manager::Get().items();
  const auto match = std::find_if(
      all.begin(), all.end(),
      [id](const mtk::downloads::Item& item) { return item.id == id; });
  return match == all.end() ? nullptr : &*match;
}

void ResizeToContent() {
  if (!g_panel) return;
  const int width = Scaled(kBaseWidth);
  const int height = PanelHeight(VisibleItems().size());
  RECT bounds{};
  GetWindowRect(g_panel, &bounds);
  SetWindowPos(g_panel, HWND_TOPMOST, bounds.left, bounds.top, width, height,
               SWP_NOACTIVATE);
  InvalidateRect(g_panel, nullptr, TRUE);
}

void RunAction(const Hit& hit) {
  switch (hit.action) {
    case Action::kPause:
      if (g_callbacks.pause) g_callbacks.pause(hit.id);
      break;
    case Action::kResume:
      if (g_callbacks.resume) g_callbacks.resume(hit.id);
      break;
    case Action::kCancel:
      if (g_callbacks.cancel) g_callbacks.cancel(hit.id);
      break;
    case Action::kOpen:
      if (const auto* item = FindItem(hit.id)) OpenDownloadedFile(g_panel, *item);
      break;
    case Action::kFolder:
      if (const auto* item = FindItem(hit.id)) ShowInFolder(item->full_path);
      break;
    case Action::kCopy:
      if (const auto* item = FindItem(hit.id)) {
        if (!CopyFileToClipboard(g_panel, item->full_path)) {
          MessageBoxW(g_panel, L"That file could not be copied.", L"Downloads",
                      MB_OK | MB_ICONWARNING);
        }
      }
      break;
    case Action::kRemove:
      if (mtk::downloads::Manager::Get().Remove(hit.id)) {
        mtk::downloads::Manager::Get().Flush();
        ResizeToContent();
      }
      break;
    case Action::kClearFinished:
      if (mtk::downloads::Manager::Get().ClearFinished() > 0) {
        mtk::downloads::Manager::Get().Flush();
        ResizeToContent();
      }
      break;
    case Action::kShowAll: {
      auto show_all = g_callbacks.show_all;
      ClosePanel();
      // The full window is modal, so the panel is closed first and the callback
      // runs afterwards.
      if (show_all) show_all();
      return;
    }
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
      Paint(dc, client);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      size_t hovered = static_cast<size_t>(-1);
      for (size_t index = 0; index < g_hits.size(); ++index) {
        if (PtInRect(&g_hits[index].bounds, point)) {
          hovered = index;
          break;
        }
      }
      if (hovered != g_hovered) {
        g_hovered = hovered;
        InvalidateRect(window, nullptr, FALSE);
      }
      TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
      TrackMouseEvent(&track);
      return 0;
    }
    case WM_MOUSELEAVE:
      if (g_hovered != static_cast<size_t>(-1)) {
        g_hovered = static_cast<size_t>(-1);
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      // The hit list is rebuilt on every paint, so a copy is taken before an
      // action can repaint and invalidate it.
      for (const Hit hit : g_hits) {
        if (PtInRect(&hit.bounds, point)) {
          RunAction(hit);
          break;
        }
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (w_param == VK_ESCAPE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
      break;
    case WM_ACTIVATE:
      // Clicking anywhere else dismisses the popup, which is what a toolbar
      // popup is expected to do.
      if (g_dismiss_armed && LOWORD(w_param) == WA_INACTIVE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_TIMER:
      // A safety net for the cases where no deactivation message arrives, such
      // as focus moving to another process.
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
      g_hovered = static_cast<size_t>(-1);
      ReleaseFonts();
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
  if (g_panel) PostMessageW(g_panel, WM_CLOSE, 0, 0);
}

void Refresh() {
  if (!g_panel) return;
  ResizeToContent();
}

void TogglePanel(HINSTANCE instance, HWND owner, HWND anchor,
                 Callbacks callbacks) {
  if (g_panel) {
    // Pressing the same button again closes the panel rather than reopening it.
    const bool same_anchor = g_anchor == anchor;
    ClosePanel();
    if (same_anchor) return;
  }
  if (!EnsureClass(instance)) return;

  g_instance = instance;
  g_callbacks = std::move(callbacks);
  g_dpi = static_cast<int>(GetDpiForWindow(owner));
  if (g_dpi <= 0) g_dpi = 96;
  CreateFonts();

  RECT button{};
  GetWindowRect(anchor, &button);
  const int width = Scaled(kBaseWidth);
  const int height = PanelHeight(VisibleItems().size());
  // Right-aligned under the button, then pulled back onto the monitor if that
  // would put it off screen.
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
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(g_panel, 20, &dark, sizeof(dark));
  ShowWindow(g_panel, SW_SHOWNOACTIVATE);
  SetForegroundWindow(g_panel);
  // Dismissal is armed only after the window is on screen, so the activation
  // that shows it cannot immediately close it again.
  g_dismiss_armed = true;
  SetTimer(g_panel, kDismissTimer, kDismissPollMs, nullptr);
  InvalidateRect(g_panel, nullptr, TRUE);
}

}  // namespace mtk::downloads_panel
