#pragma once

#include <windows.h>

// The Shields summary panel: a frameless popup, anchored under the toolbar
// shield button, that hosts a small CEF browser showing a self-contained
// data: URL. It replaced a plain MessageBoxW so the summary can carry the
// rendered beacon loop and the product's own styling.
namespace mtk::shields {

// Shows the panel for `owner`, anchored under `anchor`. Any panel already on
// screen is replaced. The panel dismisses itself when it loses activation or
// when Esc is pressed, so callers do not manage its lifetime.
//
// Purely presentational: it sends nothing back to the caller and exposes no
// script bridge. If the popup or its browser cannot be created, the call falls
// back to the plain message-box summary so the button is never dead.
void ShowPanel(HINSTANCE instance, HWND owner, HWND anchor,
               unsigned long blocked_count, bool standard_downloads);

// Closes the panel if one is open. Called when the owner window moves, resizes
// or loses focus, so the popup never floats detached from its anchor.
void ClosePanel();

}  // namespace mtk::shields
