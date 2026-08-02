#pragma once

#include <string>

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

struct Values {
  std::wstring home_page;
  std::string accept_language;
  DownloadMode download_mode = DownloadMode::kSafe;
  StartupMode startup_mode = StartupMode::kHomePage;
  // Bookmarks strip under the toolbar. Off by default so the first run looks
  // the same as before, and toggled with Ctrl+Shift+B.
  bool show_bookmarks_bar = false;
  // Parent PIN, stored only as a PBKDF2-SHA256 verifier. Both fields are
  // lowercase hex, and both are empty when no PIN is set.
  std::string pin_salt;
  std::string pin_hash;
  // Signed update checks. On by default so security fixes reach users.
  bool auto_update = true;
};

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
std::wstring ApplicationDataDirectory();
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
