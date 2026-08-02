#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

namespace mtk::downloads_panel {

// Toolbar popup listing recent downloads with inline actions. It is a plain
// Win32 popup, not a CEF browser, so it needs none of the close handshake the
// Shields panel does.
//
// Pause, resume and cancel are supplied by the caller instead of being called
// here, because those live on the CEF download callbacks and this file stays
// free of CEF headers.
struct Callbacks {
  std::function<bool(uint64_t)> pause;
  std::function<bool(uint64_t)> resume;
  std::function<bool(uint64_t)> cancel;
  // True while a live download can still be controlled in this process.
  std::function<bool(uint64_t)> controllable;
  // Opens the full downloads window.
  std::function<void()> show_all;
};

// Opens the panel under `anchor`, or closes it when it is already open for that
// anchor, so the toolbar button toggles.
void TogglePanel(HINSTANCE instance, HWND owner, HWND anchor,
                 Callbacks callbacks);
void ClosePanel();
bool IsOpen();

// Repaints the panel after a download changes. Safe to call when closed.
void Refresh();

}  // namespace mtk::downloads_panel
