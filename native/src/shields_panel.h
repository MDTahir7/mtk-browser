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
// Called when the panel actions are activated. Reached through
// navigation-intercept pattern, keeping CEF architecture stable and crash-free.
struct Actions {
  std::function<void()> on_toggle_shield;
  std::function<void()> on_toggle_cookies;
  std::function<void()> on_toggle_downloads;
  std::function<void()> on_open_site_settings;
  std::function<void()> on_open_user_filters;
  std::function<void()> on_manage_sites;
};

// Site state passed to the panel for display and toggle binding.
struct SiteState {
  std::string site;
  unsigned long site_blocked_count = 0;
  bool site_filtering = true;
  unsigned long custom_rule_count = 0;
  bool cookies = true;
};

void ShowPanel(HINSTANCE instance, HWND owner, HWND anchor,
               unsigned long blocked_count, bool standard_downloads,
               const SiteState& site_state, Actions actions);

// Re-renders the open panel with updated state in-place without flicker or closing.
void UpdatePanel(unsigned long blocked_count, bool standard_downloads,
                 const SiteState& site_state);

// Closes the panel if one is open.
void ClosePanel();

// True while the shields popup window is open.
bool IsPanelOpen();

}  // namespace mtk::shields
