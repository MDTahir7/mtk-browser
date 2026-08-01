#include "preferences.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <string_view>
#include <vector>

namespace mtk::preferences {
namespace {

constexpr wchar_t kDefaultHomePage[] = L"https://www.google.com/?safe=active";
constexpr char kDefaultAcceptLanguage[] = "en-US,en,hi";
constexpr size_t kMaxHomePageLength = 2048;

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
  size_t offset = 0;
  while (offset < content.size()) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
                          (end == std::string::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    constexpr std::string_view kHomePage = "home_page=";
    constexpr std::string_view kAcceptLanguage = "accept_language=";
    constexpr std::string_view kDownloadMode = "download_mode=";
    constexpr std::string_view kStartupMode = "startup_mode=";
    constexpr std::string_view kPinSalt = "pin_salt=";
    constexpr std::string_view kPinHash = "pin_hash=";
    constexpr std::string_view kAutoUpdate = "auto_update=";
    if (line.starts_with(kHomePage)) {
      const std::wstring home_page = Utf8ToWide(
          std::string(line.substr(kHomePage.size())));
      if (IsSafeHomePage(home_page)) values->home_page = home_page;
    } else if (line.starts_with(kAcceptLanguage)) {
      const std::string language(line.substr(kAcceptLanguage.size()));
      if (IsAllowedLanguage(language)) values->accept_language = language;
    } else if (line.starts_with(kDownloadMode)) {
      // Anything other than the explicit opt-in keeps the safe default.
      const std::string mode(line.substr(kDownloadMode.size()));
      values->download_mode = mode == "standard" ? DownloadMode::kStandard
                                                 : DownloadMode::kSafe;
    } else if (line.starts_with(kStartupMode)) {
      const std::string mode(line.substr(kStartupMode.size()));
      values->startup_mode = mode == "continue"
                                 ? StartupMode::kContinueSession
                                 : StartupMode::kHomePage;
    } else if (line.starts_with(kPinSalt)) {
      // A malformed verifier is ignored rather than trusted, which leaves the
      // browser in the "no PIN set" state instead of an unverifiable one.
      const std::string salt(line.substr(kPinSalt.size()));
      if (IsHexOfLength(salt, kPinSaltBytes)) values->pin_salt = salt;
    } else if (line.starts_with(kPinHash)) {
      const std::string hash(line.substr(kPinHash.size()));
      if (IsHexOfLength(hash, kPinHashBytes)) values->pin_hash = hash;
    } else if (line.starts_with(kAutoUpdate)) {
      // Anything other than an explicit "off" keeps checks enabled.
      values->auto_update = std::string(line.substr(kAutoUpdate.size())) != "off";
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
}

}  // namespace

std::wstring ApplicationDataDirectory() { return LocalAppDataRoot(); }

std::wstring UserDataDirectory() {
  return ApplicationDataDirectory() + L"\\User Data";
}

Store& Store::Get() {
  static Store store;
  return store;
}

void Store::Load() {
  values_ = {kDefaultHomePage, kDefaultAcceptLanguage, DownloadMode::kSafe,
             StartupMode::kHomePage, {}, {}, true};
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
  if (!IsSafeHomePage(values.home_page) ||
      !IsAllowedLanguage(values.accept_language)) {
    return false;
  }
  const bool pin_present =
      !values.pin_salt.empty() || !values.pin_hash.empty();
  if (pin_present && (!IsHexOfLength(values.pin_salt, kPinSaltBytes) ||
                      !IsHexOfLength(values.pin_hash, kPinHashBytes))) {
    return false;
  }
  std::string content =
      "[MTK Browser]\r\nhome_page=" + WideToUtf8(values.home_page) +
      "\r\naccept_language=" + values.accept_language +
      "\r\ndownload_mode=" +
      (values.download_mode == DownloadMode::kStandard ? "standard" : "safe") +
      "\r\nstartup_mode=" +
      (values.startup_mode == StartupMode::kContinueSession ? "continue"
                                                            : "home") +
      "\r\n";
  if (pin_present) {
    content += "pin_salt=" + values.pin_salt + "\r\n";
    content += "pin_hash=" + values.pin_hash + "\r\n";
  }
  content += std::string("auto_update=") +
             (values.auto_update ? "on" : "off") + "\r\n";
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

}  // namespace mtk::preferences
