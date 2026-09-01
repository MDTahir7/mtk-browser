#pragma once

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>

// The Shields summary panel: a frameless popup, anchored under the toolbar
// shield button, that hosts a small CEF browser showing a self-contained
// data: URL. It replaced a plain MessageBoxW so the summary can carry the
// rendered beacon loop and the product's own styling.
namespace mtk::shields {

// Called when the panel's "Manage sites..." hint is activated. Reached through
// the same fixed-URL navigation-intercept pattern the home and history pages
// use for their own action links, never through a script bridge: the panel
// stays exactly as presentation-only as it always was.
using OnManageSites = std::function<void()>;

// Shows the panel for `owner`, anchored under `anchor`. Any panel already on
// screen is replaced. The panel dismisses itself when it loses activation or
// when Esc is pressed, so callers do not manage its lifetime.
//
// Purely presentational: it sends nothing back to the caller and exposes no
// script bridge. If the popup or its browser cannot be created, the call falls
// back to the plain message-box summary so the button is never dead.
// `site` is what the panel says the current page is; empty for a page that has
// no site of its own. `site_blocked_count` is what was blocked while that page
// was showing, and `site_filtering` is that site's own ad/tracker switch.
// `custom_rule_count` is how many of the user's own rules matched *on this
// page*, not how many rules exist in total.
struct SiteState {
  std::string site;
  unsigned long site_blocked_count = 0;
  bool site_filtering = true;
  unsigned long custom_rule_count = 0;
};

void ShowPanel(HINSTANCE instance, HWND owner, HWND anchor,
               unsigned long blocked_count, bool standard_downloads,
               const SiteState& site_state, OnManageSites on_manage_sites);

// Closes the panel if one is open. Called when the owner window moves, resizes
// or loses focus, so the popup never floats detached from its anchor.
void ClosePanel();

}  // namespace mtk::shields
