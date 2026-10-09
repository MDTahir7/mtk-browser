#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace mtk::web_app {

struct InstallOptions {
  std::wstring display_name;
  bool desktop_shortcut = true;
  bool start_menu_shortcut = true;
};

// Stable filesystem/command-line-safe id for a policy-approved web origin.
// Empty result means the URL cannot become an app.
std::string IdForUrl(const std::string& url);

// Quotes one complete Windows command-line argument. Keeping this beside the
// web-app id generation makes the cross-process launch path testable without
// making an address a shell fragment.
std::wstring QuoteCommandLineArgument(std::wstring_view argument);

// Returns the complete, safe command-line arguments for a per-origin app
// window, or empty when the URL cannot become an app.
std::wstring LaunchArguments(const std::string& url);

// Shows the modal "Install web app" dialog. Returns true if user clicked Install.
bool PromptInstallApp(HINSTANCE instance, HWND parent,
                      const std::string& url,
                      const std::wstring& default_title,
                      InstallOptions* out_options);

// Adds or refreshes shortcuts (Desktop and/or Start Menu).
bool CreateAppShortcuts(const std::wstring& executable,
                        const std::string& url,
                        const std::wstring& display_name,
                        bool desktop_shortcut,
                        bool start_menu_shortcut);

// Adds or refreshes this app's Start-menu shortcut. The shortcut launches the
// browser with the same isolated profile as the immediate app-window command.
bool CreateStartMenuShortcut(const std::wstring& executable,
                             const std::string& url,
                             const std::wstring& display_name = L"");

// Adds a desktop shortcut for an already installed app.
bool CreateDesktopShortcutForInstalledApp(const std::wstring& executable,
                                         const std::string& url,
                                         const std::wstring& display_name);

struct AppInfo {
  std::string id;
  std::string origin;
  std::wstring display_name;
  std::wstring shortcut_path;
};

// Returns all currently installed web apps by inspecting the user's Start Menu app folder.
std::vector<AppInfo> ListInstalledApps();

// Removes an installed app: deletes its Start Menu shortcut and its isolated profile folder.
bool RemoveInstalledApp(const std::string& id);

// Opens a modal management dialog to view and uninstall web apps.
void ShowAppManager(HINSTANCE instance, HWND parent);
}
