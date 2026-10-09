#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>

namespace mtk::screenshot_panel {

enum class Mode {
  kVisibleArea,
  kSnipRegion,
  kFullPage,
};

struct Callbacks {
  std::function<void(Mode)> on_select;
};

// Opens or closes the screenshot flyout under anchor button.
void TogglePanel(HINSTANCE instance, HWND owner, HWND anchor, Callbacks callbacks);
void ClosePanel();
bool IsOpen();

}  // namespace mtk::screenshot_panel
