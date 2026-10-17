#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace mtk::tab_preview {

struct PreviewCardData {
  std::wstring title;
  std::string url;
  std::wstring group;
  std::wstring workspace;
  bool is_https = false;
  bool is_internal = false;
  bool pinned = false;
  bool muted = false;
  bool sleeping = false;
  bool camera_active = false;
  bool microphone_active = false;
  HWND browser_view = nullptr;
};

// Deterministic accent color for a tab group name. Pure helper covered by tests.
COLORREF GroupAccentColor(std::wstring_view group_name);

// Formats the concise domain/origin subtitle and status badges for a tab
// preview card. Pure helper covered by unit tests.
std::wstring FormatPreviewSubtitle(const PreviewCardData& data);

// Shows or updates the non-activating tab hover preview card below `tab_screen_rect`.
void Show(HINSTANCE instance, HWND owner, const RECT& tab_screen_rect,
          const PreviewCardData& data, int dpi, bool is_light);

// Hides the tab hover preview card if visible.
void Hide();

// True when the preview card is currently displayed.
bool IsVisible();

}  // namespace mtk::tab_preview
