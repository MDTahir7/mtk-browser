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
  std::wstring home_page;
  std::string accept_language;
  DownloadMode download_mode = DownloadMode::kSafe;
  StartupMode startup_mode = StartupMode::kHomePage;
  // Bookmarks strip under the toolbar. Off by default so the first run looks
  // the same as before, and toggled with Alt+Shift+B.
  bool show_bookmarks_bar = false;
  // HTTPS-only: refuse plain-HTTP subresources. Main-frame addresses are already
  // upgraded to https before they are fetched, so this is about the resources a
  // page then pulls in. On by default.
  bool https_only = true;
  // Encrypted DNS. The default resolves through Cloudflare's family resolver,
  // which refuses malware and adult domains at the DNS layer as well.
  DohProvider doh_provider = DohProvider::kCloudflareFamily;
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
