#pragma once

#include <string>
#include <string_view>

namespace mtk::preferences {

// How downloads are handled. Safe mode is the default and only saves
// allowlisted file types; standard mode behaves like a mainstream browser.
enum class DownloadMode {
  kSafe,
  kStandard,
};

enum class StartupMode {
  kHomePage,
  kContinueSession,
};

// New profiles use MTK's local dashboard. A separate mode lets old and future
// custom home URLs survive without pretending the internal page is a web URL.
enum class HomePageMode {
  kBrowserHome,
  kCustom,
};

// Native chrome follows Windows by default; page content is never force-styled.
enum class ThemeMode {
  kSystem,
  kLight,
  kDark,
};

// Returns true when light appearance should be used.
// If mode is kSystem, queries Windows registry AppsUseLightTheme.
bool ShouldUseLightAppearance(ThemeMode mode);

// The last normal (non-full-screen) browser window rectangle. A zero-sized
// value means the user has not established a preferred window position yet.
// It is deliberately kept per profile with the other UI preferences.
struct WindowBounds {
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
};

// Reject corrupt preference values before they reach CreateWindow. The UI does
// the final monitor-aware clamping, because only it knows the live displays.
bool IsValidWindowBounds(const WindowBounds& bounds);

// Preferences written before home_page_mode existed are migrated
// conservatively: only the old built-in Google default becomes MTK home.
HomePageMode InferLegacyHomePageMode(std::wstring_view home_page);

// DNS-over-HTTPS resolver. kSystem leaves DNS to Windows, which is the only
// choice that can use a resolver configured for the whole machine.
enum class DohProvider {
  kSystem,
  kCloudflareFamily,
  kCloudflare,
  kQuad9,
  kGoogle,
};

// Resolver template for a provider, empty for kSystem.
const char* DohTemplate(DohProvider provider);
// Short label for the Settings list.
const wchar_t* DohLabel(DohProvider provider);

struct Values {
  // The built-in dashboard is the default. `home_page_custom` records whether
  // the user explicitly chose a web address, so an old default can be migrated
  // without overwriting a real custom choice.
  std::wstring home_page;
  bool home_page_custom = false;
  std::string accept_language;
  std::string search_engine = "google";
  ThemeMode theme_mode = ThemeMode::kSystem;
  DownloadMode download_mode = DownloadMode::kSafe;
  // Whether to prompt for download location and file name using Save As dialog.
  // Enabled by default like other mainstream browsers when configured.
  bool download_ask_location = true;
  StartupMode startup_mode = StartupMode::kHomePage;
  // Keep MTK's Alt shortcuts by default, but let people migrating from another
  // browser opt into the familiar non-editing Ctrl shortcuts.
  bool familiar_shortcuts = false;
  // Bookmarks strip under the toolbar.
  bool show_bookmarks_bar = false;
  // A left tab rail is opt-in. Keeping the horizontal strip as the default
  // avoids changing existing users' muscle memory on upgrade.
  bool vertical_tabs = false;
  // Whether the vertical tabs rail is collapsed to a narrow icon-only strip.
  bool vertical_tabs_collapsed = false;
  // Lets Chromium deprioritize background tabs. Pinned tabs deliberately stay
  // awake as the built-in exception for important work.
  bool memory_saver = true;
  // Focus Mode: blocks suggested and recommended videos on YouTube and distraction rabbit holes.
  // On by default.
  bool focus_mode = true;
  // HTTPS-only: refuse plain-HTTP subresources. Main-frame addresses are already
  // upgraded to https before they are fetched, so this is about the resources a
  // page then pulls in. On by default.
  bool https_only = true;
  // Fingerprinting protection: strips the high-entropy User-Agent Client Hint
  // request headers (Sec-CH-UA-Platform-Version, -Full-Version-List, -Arch,
  // -Bitness, -Model, -WoW64) and, in the renderer, adds session-consistent
  // noise to Canvas readback and spoofs the WebGL vendor/renderer strings.
  // Applies globally; there is currently no per-site exemption for the
  // renderer-side part, because that would need a channel to carry a
  // per-navigation decision into the render process, which does not exist yet.
  // On by default.
  bool fingerprint_protection = true;
  // On a normal browser exit, discard local activity records that are safe to
  // recreate: browsing history, completed-download rows and the saved tab
  // snapshot. Deliberately does not erase cookies, site settings, passwords,
  // bookmarks or downloaded files; those require the explicit clear dialog.
  bool clear_activity_on_exit = false;
  // Encrypted DNS. The default resolves through Cloudflare's family resolver,
  // which refuses malware and adult domains at the DNS layer as well.
  DohProvider doh_provider = DohProvider::kCloudflareFamily;
  // Parent PIN, stored only as a PBKDF2-SHA256 verifier. Both fields are
  // lowercase hex, and both are empty when no PIN is set.
  std::string pin_salt;
  std::string pin_hash;
  // Signed update checks. On by default so security fixes reach users.
  bool auto_update = true;
  // Optional second factor before the saved-password manager is shown. Windows
  // handles the verification; MTK Browser stores only this on/off preference.
  bool windows_hello_unlock = false;
  WindowBounds window_bounds;
  bool window_maximized = false;
};

// Returns the browser's safe default settings while retaining the parent PIN
// verifier. Resetting ordinary preferences must not silently remove the lock
// that protects a child or shared profile. Browsing data is deliberately not
// part of this operation.
Values ResetSettings(const Values& current);

// Discards local browsing activity (session tabs, history, finished downloads)
// if clear_activity_on_exit is enabled. Returns true if cleanup was performed.
bool ExecuteClearActivityOnExit();

// Parent PIN helpers. The PIN itself is never stored or logged; only a salted
// derived verifier is kept in preferences.
bool PinIsSet();
// Derives a fresh salt/verifier pair for `pin`. Returns false when the PIN is
// too short or the platform RNG/KDF fails.
bool DerivePin(const std::wstring& pin, std::string* salt, std::string* hash);
// Constant-time comparison of `pin` against the stored verifier. Returns false
// when no PIN is set.
bool VerifyPin(const std::wstring& pin);

// Shortest accepted parent PIN.
constexpr size_t kMinPinLength = 4;

// MTK-owned data is kept separate from CEF's User Data directory.
// ApplicationDataDirectory() is per-profile; InstallationDataDirectory() is the
// one shared root and is for data that must not be duplicated per profile, such
// as downloaded update installers.
std::wstring ApplicationDataDirectory();
std::wstring InstallationDataDirectory();
std::wstring UserDataDirectory();

class Store final {
 public:
  static Store& Get();

  void Load();
  const Values& values() const;
  bool Save(const Values& values);

 private:
  Values values_;
};

}  // namespace mtk::preferences
