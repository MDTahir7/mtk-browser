#pragma once

#include <windows.h>

namespace mtk::default_browser {

// Windows default-browser registration, all under HKEY_CURRENT_USER because the
// installer is per-user and never needs administrator rights.
//
// Windows 10 and 11 do not let a program make itself the default; it can only
// register itself as a candidate and then ask the user to confirm in Settings.
// Anything that claims otherwise is fighting the OS, so this does the two things
// that actually work: register properly, and open the right Settings page.

// Writes the ProgIDs, capabilities and RegisteredApplications entries for the
// running executable. Safe to call repeatedly; it overwrites its own keys and
// touches nothing else. Returns false when a key could not be written.
bool Register();

// True when this executable is the current handler for https.
bool IsDefault();

// Opens the Windows "Default apps" page so the user can pick MTK Browser. Falls
// back to the older per-protocol chooser when that page cannot be opened.
// Returns false when neither could be launched.
bool PromptUserToChoose(HWND owner);

}  // namespace mtk::default_browser
