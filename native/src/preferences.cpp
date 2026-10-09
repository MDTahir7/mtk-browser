#include "preferences.h"

#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include "download_manager.h"
#include "home_page.h"
#include "library_store.h"
#include "profile.h"
#include "session_store.h"

namespace mtk::preferences {
namespace {

constexpr wchar_t kLegacyDefaultHomePage[] =
    L"https://www.google.com/?safe=active";
constexpr char kDefaultAcceptLanguage[] = "en-US,en,hi";
constexpr size_t kMaxHomePageLength = 2048;
constexpr int kMinWindowWidth = 480;
constexpr int kMinWindowHeight = 360;
constexpr int kMaxWindowDimension = 10000;
constexpr int kMaxWindowCoordinate = 100000;

std::wstring LocalAppDataRoot() {
  std::wstring path(32767, L'\0');
  DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) {
    length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
  }
  if (length == 0 || length >= path.size()) return L"C:\\MTKBrowserData";
  path.resize(length);
  if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
  return path + L"MTK Browser";
}

std::wstring PreferencesPath() {
  return ApplicationDataDirectory() + L"\\preferences.ini";
}

bool EnsureApplicationDataDirectory() {
  if (CreateDirectoryW(ApplicationDataDirectory().c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

bool IsAllowedLanguage(const std::string& value) {
  return value == "en-US,en,hi" || value == "hi,en-US,en";
}

constexpr size_t kPinSaltBytes = 16;
constexpr size_t kPinHashBytes = 32;
// A parent PIN is short by design, so the derivation is deliberately expensive
// to make offline guessing of the stored verifier slow.
constexpr unsigned long kPinIterations = 200000;

std::string ToHex(const unsigned char* data, size_t size) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(size * 2);
  for (size_t index = 0; index < size; ++index) {
    hex.push_back(kDigits[data[index] >> 4]);
    hex.push_back(kDigits[data[index] & 0x0F]);
  }
  return hex;
}

bool FromHex(const std::string& hex, std::vector<unsigned char>* out) {
  if (hex.empty() || hex.size() % 2 != 0) return false;
  const auto value = [](char character) -> int {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
  };
  out->clear();
  out->reserve(hex.size() / 2);
  for (size_t index = 0; index < hex.size(); index += 2) {
    const int high = value(hex[index]);
    const int low = value(hex[index + 1]);
    if (high < 0 || low < 0) return false;
    out->push_back(static_cast<unsigned char>((high << 4) | low));
  }
  return true;
}

bool IsHexOfLength(const std::string& value, size_t bytes) {
  if (value.size() != bytes * 2) return false;
  for (const char character : value) {
    const bool hex_digit = (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'f');
    if (!hex_digit) return false;
  }
  return true;
}

// PBKDF2-HMAC-SHA256 through Windows CNG. No third-party crypto is linked in.
bool DeriveKey(const std::wstring& pin, const std::vector<unsigned char>& salt,
               std::array<unsigned char, kPinHashBytes>* out) {
  // The PIN is hashed as UTF-8 so the verifier does not depend on wchar layout.
  const int utf8_size =
      WideCharToMultiByte(CP_UTF8, 0, pin.data(), static_cast<int>(pin.size()),
                          nullptr, 0, nullptr, nullptr);
  if (utf8_size <= 0) return false;
  std::string secret(static_cast<size_t>(utf8_size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, pin.data(), static_cast<int>(pin.size()),
                      secret.data(), utf8_size, nullptr, nullptr);

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
          BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
    SecureZeroMemory(secret.data(), secret.size());
    return false;
  }
  const NTSTATUS status = BCryptDeriveKeyPBKDF2(
      algorithm, reinterpret_cast<PUCHAR>(secret.data()),
      static_cast<ULONG>(secret.size()),
      const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
      kPinIterations, out->data(), static_cast<ULONG>(out->size()), 0);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  SecureZeroMemory(secret.data(), secret.size());
  return BCRYPT_SUCCESS(status);
}

bool IsSafeHomePage(const std::wstring& value) {
  return !value.empty() && value.size() <= kMaxHomePageLength &&
         value.find_first_of(L"\r\n") == std::wstring::npos;
}

bool ReadFile(const std::wstring& path, std::string* content) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  const bool valid_size = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                          size.QuadPart <= 64 * 1024;
  if (!valid_size) {
    CloseHandle(file);
    return false;
  }
  content->assign(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const bool read_ok = content->empty() ||
      ::ReadFile(file, content->data(), static_cast<DWORD>(content->size()),
                 &read, nullptr);
  CloseHandle(file);
  if (!read_ok || read != content->size()) {
    content->clear();
    return false;
  }
  return true;
}

bool WriteFileAtomically(const std::string& content) {
  if (!EnsureApplicationDataDirectory()) return false;
  const std::wstring path = PreferencesPath();
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool write_ok = content.empty() ||
      WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
                &written, nullptr);
  const bool flush_ok = write_ok && written == content.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!flush_ok) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  if (MoveFileExW(temporary.c_str(), path.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  DeleteFileW(temporary.c_str());
  return false;
}

void ReadValues(const std::string& content, Values* values) {
  std::optional<std::wstring> loaded_home;
  std::optional<bool> loaded_home_custom;
  WindowBounds loaded_bounds{};
  bool has_left = false;
  bool has_top = false;
  bool has_width = false;
  bool has_height = false;
  std::optional<bool> loaded_maximized;

  auto parse_int = [](std::string_view sv, int* out) -> bool {
    if (sv.empty()) return false;
    int val = 0;
    const auto [ptr, ec] =
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
      *out = val;
      return true;
    }
    return false;
  };

  size_t offset = 0;
  while (offset < content.size()) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
                          (end == std::string::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    constexpr std::string_view kHomePage = "home_page=";
    constexpr std::string_view kHomePageMode = "home_page_mode=";
    constexpr std::string_view kAcceptLanguage = "accept_language=";
    constexpr std::string_view kSearchEngine = "search_engine=";
    constexpr std::string_view kThemeMode = "theme=";
    constexpr std::string_view kDownloadMode = "download_mode=";
    constexpr std::string_view kDownloadAskLocation = "download_ask_location=";
    constexpr std::string_view kStartupMode = "startup_mode=";
    constexpr std::string_view kPinSalt = "pin_salt=";
    constexpr std::string_view kPinHash = "pin_hash=";
    constexpr std::string_view kAutoUpdate = "auto_update=";
    constexpr std::string_view kFamiliarShortcuts = "familiar_shortcuts=";
    constexpr std::string_view kBookmarksBar = "bookmarks_bar=";
    constexpr std::string_view kVerticalTabs = "vertical_tabs=";
    constexpr std::string_view kVerticalTabsCollapsed = "vertical_tabs_collapsed=";
    constexpr std::string_view kMemorySaver = "memory_saver=";
    constexpr std::string_view kFocusMode = "focus_mode=";
    constexpr std::string_view kHttpsOnly = "https_only=";
    constexpr std::string_view kFingerprintProtection =
        "fingerprint_protection=";
    constexpr std::string_view kClearActivityOnExit = "clear_activity_on_exit=";
    constexpr std::string_view kDohProvider = "doh_provider=";
    constexpr std::string_view kWindowsHelloUnlock = "windows_hello_unlock=";
    constexpr std::string_view kWindowLeft = "window_left=";
    constexpr std::string_view kWindowTop = "window_top=";
    constexpr std::string_view kWindowWidth = "window_width=";
    constexpr std::string_view kWindowHeight = "window_height=";
    constexpr std::string_view kWindowMaximized = "window_maximized=";
    if (line.starts_with(kHomePage)) {
      const std::wstring home_page = Utf8ToWide(
          std::string(line.substr(kHomePage.size())));
      if (IsSafeHomePage(home_page)) loaded_home = home_page;
    } else if (line.starts_with(kHomePageMode)) {
      const std::string mode(line.substr(kHomePageMode.size()));
      if (mode == "default") loaded_home_custom = false;
      else if (mode == "custom") loaded_home_custom = true;
    } else if (line.starts_with(kAcceptLanguage)) {
      const std::string language(line.substr(kAcceptLanguage.size()));
      if (IsAllowedLanguage(language)) values->accept_language = language;
    } else if (line.starts_with(kSearchEngine)) {
      const std::string engine(line.substr(kSearchEngine.size()));
      if (engine == "google" || engine == "bing" || engine == "duckduckgo") {
        values->search_engine = engine;
      }
    } else if (line.starts_with(kThemeMode)) {
      const std::string mode(line.substr(kThemeMode.size()));
      if (mode == "light") values->theme_mode = ThemeMode::kLight;
      else if (mode == "dark") values->theme_mode = ThemeMode::kDark;
      else if (mode == "system") values->theme_mode = ThemeMode::kSystem;
    } else if (line.starts_with(kDownloadMode)) {
      const std::string mode(line.substr(kDownloadMode.size()));
      values->download_mode = mode == "standard" ? DownloadMode::kStandard
                                                 : DownloadMode::kSafe;
    } else if (line.starts_with(kDownloadAskLocation)) {
      values->download_ask_location =
          line.substr(kDownloadAskLocation.size()) != "off";
    } else if (line.starts_with(kStartupMode)) {
      const std::string mode(line.substr(kStartupMode.size()));
      values->startup_mode = mode == "continue"
                                 ? StartupMode::kContinueSession
                                 : StartupMode::kHomePage;
    } else if (line.starts_with(kPinSalt)) {
      const std::string salt(line.substr(kPinSalt.size()));
      if (IsHexOfLength(salt, kPinSaltBytes)) values->pin_salt = salt;
    } else if (line.starts_with(kPinHash)) {
      const std::string hash(line.substr(kPinHash.size()));
      if (IsHexOfLength(hash, kPinHashBytes)) values->pin_hash = hash;
    } else if (line.starts_with(kAutoUpdate)) {
      values->auto_update = std::string(line.substr(kAutoUpdate.size())) != "off";
    } else if (line.starts_with(kFamiliarShortcuts)) {
      values->familiar_shortcuts =
          std::string(line.substr(kFamiliarShortcuts.size())) == "on";
    } else if (line.starts_with(kBookmarksBar)) {
      values->show_bookmarks_bar =
          std::string(line.substr(kBookmarksBar.size())) == "on";
    } else if (line.starts_with(kVerticalTabs)) {
      values->vertical_tabs =
          std::string(line.substr(kVerticalTabs.size())) == "on";
    } else if (line.starts_with(kVerticalTabsCollapsed)) {
      values->vertical_tabs_collapsed =
          std::string(line.substr(kVerticalTabsCollapsed.size())) == "on";
    } else if (line.starts_with(kMemorySaver)) {
      values->memory_saver =
          std::string(line.substr(kMemorySaver.size())) != "off";
    } else if (line.starts_with(kFocusMode)) {
      values->focus_mode =
          std::string(line.substr(kFocusMode.size())) != "off";
    } else if (line.starts_with(kHttpsOnly)) {
      values->https_only = std::string(line.substr(kHttpsOnly.size())) != "off";
    } else if (line.starts_with(kFingerprintProtection)) {
      values->fingerprint_protection =
          std::string(line.substr(kFingerprintProtection.size())) != "off";
    } else if (line.starts_with(kClearActivityOnExit)) {
      values->clear_activity_on_exit =
          std::string(line.substr(kClearActivityOnExit.size())) == "on";
    } else if (line.starts_with(kDohProvider)) {
      const std::string name(line.substr(kDohProvider.size()));
      if (name == "system") values->doh_provider = DohProvider::kSystem;
      else if (name == "cloudflare") values->doh_provider = DohProvider::kCloudflare;
      else if (name == "quad9") values->doh_provider = DohProvider::kQuad9;
      else if (name == "google") values->doh_provider = DohProvider::kGoogle;
      else values->doh_provider = DohProvider::kCloudflareFamily;
    } else if (line.starts_with(kWindowsHelloUnlock)) {
      values->windows_hello_unlock =
          std::string(line.substr(kWindowsHelloUnlock.size())) == "on";
    } else if (line.starts_with(kWindowLeft)) {
      has_left = parse_int(line.substr(kWindowLeft.size()), &loaded_bounds.left);
    } else if (line.starts_with(kWindowTop)) {
      has_top = parse_int(line.substr(kWindowTop.size()), &loaded_bounds.top);
    } else if (line.starts_with(kWindowWidth)) {
      has_width = parse_int(line.substr(kWindowWidth.size()), &loaded_bounds.width);
    } else if (line.starts_with(kWindowHeight)) {
      has_height = parse_int(line.substr(kWindowHeight.size()), &loaded_bounds.height);
    } else if (line.starts_with(kWindowMaximized)) {
      loaded_maximized =
          std::string(line.substr(kWindowMaximized.size())) == "on";
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }

  if (has_left && has_top && has_width && has_height &&
      IsValidWindowBounds(loaded_bounds)) {
    values->window_bounds = loaded_bounds;
    if (loaded_maximized.has_value()) {
      values->window_maximized = *loaded_maximized;
    }
  }

  // Old builds had no mode marker. Their exact Google default is migrated to
  // the dashboard; every other saved value is treated as an explicit choice.
  if (loaded_home_custom.value_or(false) && loaded_home &&
      !mtk::home::IsHomeUrl(WideToUtf8(*loaded_home))) {
    values->home_page = *loaded_home;
    values->home_page_custom = true;
  } else if (!loaded_home_custom.has_value() && loaded_home &&
             *loaded_home != kLegacyDefaultHomePage &&
             !mtk::home::IsHomeUrl(WideToUtf8(*loaded_home))) {
    values->home_page = *loaded_home;
    values->home_page_custom = true;
  } else {
    values->home_page = mtk::home::kHomeUrlWide;
    values->home_page_custom = false;
  }
}

}  // namespace

const char* DohTemplate(DohProvider provider) {
  switch (provider) {
    // Cloudflare's family resolver refuses malware and adult domains at the DNS
    // layer, which is a second line behind the browser's own rules.
    case DohProvider::kCloudflareFamily:
      return "https://family.cloudflare-dns.com/dns-query";
    case DohProvider::kCloudflare:
      return "https://cloudflare-dns.com/dns-query";
    case DohProvider::kQuad9:
      return "https://dns.quad9.net/dns-query";
    case DohProvider::kGoogle:
      return "https://dns.google/dns-query";
    case DohProvider::kSystem:
      break;
  }
  return "";
}

const wchar_t* DohLabel(DohProvider provider) {
  switch (provider) {
    case DohProvider::kCloudflareFamily:
      return L"Cloudflare for Families (blocks malware and adult domains)";
    case DohProvider::kCloudflare:
      return L"Cloudflare (1.1.1.1)";
    case DohProvider::kQuad9:
      return L"Quad9 (blocks known malicious domains)";
    case DohProvider::kGoogle:
      return L"Google Public DNS";
    case DohProvider::kSystem:
      break;
  }
  return L"Use the Windows DNS setting";
}

std::wstring InstallationDataDirectory() { return LocalAppDataRoot(); }

// Every store built on this path becomes per-profile at once: preferences, the
// library, the session snapshot, the download history and Chromium's cache.
std::wstring ApplicationDataDirectory() {
  return mtk::profile::ActiveDirectory();
}

std::wstring UserDataDirectory() {
  return ApplicationDataDirectory() + L"\\User Data";
}

Store& Store::Get() {
  static Store store;
  return store;
}

void Store::Load() {
  // Named fields, so adding a preference cannot silently shift the defaults.
  values_ = Values{};
  values_.home_page = mtk::home::kHomeUrlWide;
  values_.home_page_custom = false;
  values_.accept_language = kDefaultAcceptLanguage;
  std::string content;
  if (ReadFile(PreferencesPath(), &content)) ReadValues(content, &values_);
  // Half a verifier is unusable, so treat it as no PIN at all.
  if (values_.pin_salt.empty() || values_.pin_hash.empty()) {
    values_.pin_salt.clear();
    values_.pin_hash.clear();
  }
}

const Values& Store::values() const { return values_; }

bool Store::Save(const Values& values) {
  const std::string home_page = WideToUtf8(values.home_page);
  const bool valid_default = !values.home_page_custom &&
                             mtk::home::IsHomeUrl(home_page);
  const bool valid_custom = values.home_page_custom &&
                            (home_page.starts_with("https://") ||
                             home_page.starts_with("http://"));
  if (!IsSafeHomePage(values.home_page) ||
      (!valid_default && !valid_custom) ||
      !IsAllowedLanguage(values.accept_language) ||
      (values.search_engine != "google" &&
       values.search_engine != "bing" &&
       values.search_engine != "duckduckgo")) {
    return false;
  }
  const bool pin_present =
      !values.pin_salt.empty() || !values.pin_hash.empty();
  if (pin_present && (!IsHexOfLength(values.pin_salt, kPinSaltBytes) ||
                      !IsHexOfLength(values.pin_hash, kPinHashBytes))) {
    return false;
  }
  const bool has_bounds = values.window_bounds.left != 0 ||
                          values.window_bounds.top != 0 ||
                          values.window_bounds.width != 0 ||
                          values.window_bounds.height != 0;
  if (has_bounds && !IsValidWindowBounds(values.window_bounds)) {
    return false;
  }
  std::string content =
      "[MTK Browser]\r\nhome_page=" + home_page +
      "\r\nhome_page_mode=" +
      (values.home_page_custom ? "custom" : "default") +
      "\r\naccept_language=" + values.accept_language +
      "\r\nsearch_engine=" + values.search_engine +
      "\r\ndownload_mode=" +
      (values.download_mode == DownloadMode::kStandard ? "standard" : "safe") +
      "\r\ndownload_ask_location=" +
      (values.download_ask_location ? "on" : "off") +
      "\r\nstartup_mode=" +
      (values.startup_mode == StartupMode::kContinueSession ? "continue"
                                                            : "home") +
      "\r\n";
  if (pin_present) {
    content += "pin_salt=" + values.pin_salt + "\r\n";
    content += "pin_hash=" + values.pin_hash + "\r\n";
  }
  const auto theme_name = [](ThemeMode mode) -> const char* {
    switch (mode) {
      case ThemeMode::kLight: return "light";
      case ThemeMode::kDark: return "dark";
      case ThemeMode::kSystem: break;
    }
    return "system";
  };
  content += std::string("theme=") + theme_name(values.theme_mode) + "\r\n";
  content += std::string("auto_update=") +
             (values.auto_update ? "on" : "off") + "\r\n";
  content += std::string("windows_hello_unlock=") +
             (values.windows_hello_unlock ? "on" : "off") + "\r\n";
  content += std::string("familiar_shortcuts=") +
             (values.familiar_shortcuts ? "on" : "off") + "\r\n";
  content += std::string("bookmarks_bar=") +
             (values.show_bookmarks_bar ? "on" : "off") + "\r\n";
  content += std::string("vertical_tabs=") +
             (values.vertical_tabs ? "on" : "off") + "\r\n";
  content += std::string("vertical_tabs_collapsed=") +
             (values.vertical_tabs_collapsed ? "on" : "off") + "\r\n";
  content += std::string("memory_saver=") +
             (values.memory_saver ? "on" : "off") + "\r\n";
  content += std::string("focus_mode=") +
             (values.focus_mode ? "on" : "off") + "\r\n";
  content += std::string("https_only=") + (values.https_only ? "on" : "off") +
             "\r\n";
  content += std::string("fingerprint_protection=") +
             (values.fingerprint_protection ? "on" : "off") + "\r\n";
  content += std::string("clear_activity_on_exit=") +
             (values.clear_activity_on_exit ? "on" : "off") + "\r\n";
  const auto doh_name = [](DohProvider provider) -> const char* {
    switch (provider) {
      case DohProvider::kSystem: return "system";
      case DohProvider::kCloudflare: return "cloudflare";
      case DohProvider::kQuad9: return "quad9";
      case DohProvider::kGoogle: return "google";
      case DohProvider::kCloudflareFamily: break;
    }
    return "cloudflare-family";
  };
  content += std::string("doh_provider=") + doh_name(values.doh_provider) +
             "\r\n";
  if (IsValidWindowBounds(values.window_bounds)) {
    content += "window_left=" + std::to_string(values.window_bounds.left) + "\r\n";
    content += "window_top=" + std::to_string(values.window_bounds.top) + "\r\n";
    content += "window_width=" + std::to_string(values.window_bounds.width) + "\r\n";
    content += "window_height=" + std::to_string(values.window_bounds.height) + "\r\n";
    content += std::string("window_maximized=") +
               (values.window_maximized ? "on" : "off") + "\r\n";
  }
  if (!WriteFileAtomically(content)) return false;
  values_ = values;
  return true;
}

bool PinIsSet() {
  const Values& values = Store::Get().values();
  return !values.pin_salt.empty() && !values.pin_hash.empty();
}

bool DerivePin(const std::wstring& pin, std::string* salt, std::string* hash) {
  if (!salt || !hash || pin.size() < kMinPinLength) return false;
  std::vector<unsigned char> salt_bytes(kPinSaltBytes);
  if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, salt_bytes.data(),
                                      static_cast<ULONG>(salt_bytes.size()),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
    return false;
  }
  std::array<unsigned char, kPinHashBytes> derived{};
  if (!DeriveKey(pin, salt_bytes, &derived)) return false;
  *salt = ToHex(salt_bytes.data(), salt_bytes.size());
  *hash = ToHex(derived.data(), derived.size());
  SecureZeroMemory(derived.data(), derived.size());
  return true;
}

bool VerifyPin(const std::wstring& pin) {
  if (!PinIsSet() || pin.empty()) return false;
  const Values& values = Store::Get().values();
  std::vector<unsigned char> salt_bytes;
  std::vector<unsigned char> expected;
  if (!FromHex(values.pin_salt, &salt_bytes) ||
      !FromHex(values.pin_hash, &expected) ||
      expected.size() != kPinHashBytes) {
    return false;
  }
  std::array<unsigned char, kPinHashBytes> derived{};
  if (!DeriveKey(pin, salt_bytes, &derived)) return false;
  // Constant-time comparison so a wrong PIN leaks no timing information.
  unsigned char difference = 0;
  for (size_t index = 0; index < kPinHashBytes; ++index) {
    difference |= static_cast<unsigned char>(derived[index] ^ expected[index]);
  }
  SecureZeroMemory(derived.data(), derived.size());
  return difference == 0;
}

bool IsValidWindowBounds(const WindowBounds& bounds) {
  if (bounds.width < kMinWindowWidth || bounds.height < kMinWindowHeight) return false;
  if (bounds.width > kMaxWindowDimension || bounds.height > kMaxWindowDimension) return false;
  if (std::abs(bounds.left) > kMaxWindowCoordinate || std::abs(bounds.top) > kMaxWindowCoordinate) return false;
  return true;
}

Values ResetSettings(const Values& current) {
  Values defaults;
  defaults.home_page = mtk::home::kHomeUrlWide;
  defaults.home_page_custom = false;
  defaults.accept_language = kDefaultAcceptLanguage;
  defaults.pin_salt = current.pin_salt;
  defaults.pin_hash = current.pin_hash;
  return defaults;
}

bool ExecuteClearActivityOnExit() {
  if (!Store::Get().values().clear_activity_on_exit) return false;
  mtk::session::Clear();
  mtk::library::Store::Get().ClearHistory();
  auto& dm = mtk::downloads::Manager::Get();
  dm.ClearFinished();
  dm.Flush();
  return true;
}

bool ShouldUseLightAppearance(ThemeMode mode) {
  if (mode == ThemeMode::kLight) return true;
  if (mode == ThemeMode::kDark) return false;
  // ThemeMode::kSystem: Query Windows personalization registry setting.
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LSTATUS status = RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme",
      RRF_RT_REG_DWORD,
      nullptr,
      &value,
      &size);
  if (status == ERROR_SUCCESS) {
    return value != 0;
  }
  return true;
}

}  // namespace mtk::preferences
