#include "autofill_store.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <sstream>
#include <string_view>

#include "preferences.h"
#include "profile.h"

namespace mtk::autofill {

namespace {

constexpr size_t kMaxFileBytes = 512 * 1024;  // 512 KB

bool EnsureDataDirectory() {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  return CreateDirectoryW(directory.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring AutofillPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\autofill_profiles.dat";
}

std::string Entropy() {
  std::string entropy = "MTK Browser autofill v1|";
  const std::wstring& id = mtk::profile::ActiveId();
  for (const wchar_t character : id) {
    entropy.push_back(static_cast<char>(character & 0x7F));
  }
  return entropy;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

bool Protect(const std::string& plain, std::string* blob) {
  blob->clear();
  if (plain.empty()) return false;

  std::string entropy = Entropy();
  DATA_BLOB input{static_cast<DWORD>(plain.size()),
                  reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
  DATA_BLOB salt{static_cast<DWORD>(entropy.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))};
  DATA_BLOB output{};
  const bool ok = CryptProtectData(&input, L"MTK Browser saved autofill", &salt,
                                   nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                                   &output) != FALSE;
  if (!ok) return false;
  blob->assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return true;
}

bool Unprotect(const std::string& blob, std::string* plain) {
  plain->clear();
  if (blob.empty()) return false;
  std::string entropy = Entropy();
  std::string copy = blob;
  DATA_BLOB input{static_cast<DWORD>(copy.size()),
                  reinterpret_cast<BYTE*>(copy.data())};
  DATA_BLOB salt{static_cast<DWORD>(entropy.size()),
                 reinterpret_cast<BYTE*>(entropy.data())};
  DATA_BLOB output{};
  if (!CryptUnprotectData(&input, nullptr, &salt, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    return false;
  }
  plain->assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return true;
}

bool ReadFileBounded(const std::wstring& path, std::string* content) {
  content->clear();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      static_cast<unsigned long long>(size.QuadPart) > kMaxFileBytes) {
    CloseHandle(file);
    return false;
  }
  content->resize(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  const bool ok = ReadFile(file, content->data(),
                           static_cast<DWORD>(content->size()), &read,
                           nullptr) != FALSE;
  CloseHandle(file);
  if (!ok || read != content->size()) {
    content->clear();
    return false;
  }
  return true;
}

bool WriteAtomically(const std::string& content) {
  if (!EnsureDataDirectory() || content.size() > kMaxFileBytes) return false;
  const std::wstring path = AutofillPath();
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, content.data(),
                            static_cast<DWORD>(content.size()), &written,
                            nullptr) != FALSE &&
                  written == content.size() && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

bool ContainsControlChars(std::wstring_view text) {
  for (const wchar_t c : text) {
    if (c < 32 || c == 127 || c == L'\t' || c == L'\r' || c == L'\n') return true;
  }
  return false;
}

std::string EscapeField(const std::wstring& value) {
  std::string utf8 = WideToUtf8(value);
  std::string escaped;
  escaped.reserve(utf8.size() + 8);
  for (char c : utf8) {
    if (c == '\\') {
      escaped += "\\\\";
    } else if (c == '|') {
      escaped += "\\p";
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else {
      escaped.push_back(c);
    }
  }
  return escaped;
}

std::wstring UnescapeField(std::string_view escaped) {
  std::string utf8;
  utf8.reserve(escaped.size());
  for (size_t i = 0; i < escaped.size(); ++i) {
    if (escaped[i] == '\\' && i + 1 < escaped.size()) {
      char next = escaped[++i];
      if (next == '\\') utf8.push_back('\\');
      else if (next == 'p') utf8.push_back('|');
      else if (next == 'n') utf8.push_back('\n');
      else if (next == 'r') utf8.push_back('\r');
      else {
        utf8.push_back('\\');
        utf8.push_back(next);
      }
    } else {
      utf8.push_back(escaped[i]);
    }
  }
  return Utf8ToWide(utf8);
}

}  // namespace

Store& Store::Get() {
  static Store store;
  return store;
}

void Store::Load() {
  profiles_.clear();
  dirty_ = false;

  std::string blob;
  if (!ReadFileBounded(AutofillPath(), &blob)) return;

  std::string plain;
  if (!Unprotect(blob, &plain)) return;

  std::istringstream stream(plain);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line.starts_with("#")) continue;
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream line_stream(line);
    while (std::getline(line_stream, token, '|')) {
      tokens.push_back(token);
    }
    if (tokens.size() < 12) continue;

    AddressProfile profile;
    profile.id = tokens[0];
    profile.label = UnescapeField(tokens[1]);
    profile.full_name = UnescapeField(tokens[2]);
    profile.email = UnescapeField(tokens[3]);
    profile.phone = UnescapeField(tokens[4]);
    profile.street_address = UnescapeField(tokens[5]);
    profile.city = UnescapeField(tokens[6]);
    profile.state = UnescapeField(tokens[7]);
    profile.postal_code = UnescapeField(tokens[8]);
    profile.country = UnescapeField(tokens[9]);
    profile.consent_granted = tokens[10] == "1";
    try {
      profile.last_used_timestamp = std::stoull(tokens[11]);
    } catch (...) {
      profile.last_used_timestamp = 0;
    }

    if (!profile.id.empty() && profiles_.size() < kMaxProfiles) {
      profiles_.push_back(std::move(profile));
    }
  }
}

bool Store::Flush() {
  if (!dirty_) return true;

  std::ostringstream stream;
  stream << "# MTK Browser Autofill Profiles v1\n";
  for (const auto& p : profiles_) {
    stream << p.id << "|"
           << EscapeField(p.label) << "|"
           << EscapeField(p.full_name) << "|"
           << EscapeField(p.email) << "|"
           << EscapeField(p.phone) << "|"
           << EscapeField(p.street_address) << "|"
           << EscapeField(p.city) << "|"
           << EscapeField(p.state) << "|"
           << EscapeField(p.postal_code) << "|"
           << EscapeField(p.country) << "|"
           << (p.consent_granted ? "1" : "0") << "|"
           << p.last_used_timestamp << "\n";
  }

  std::string plain = stream.str();
  std::string blob;
  if (!Protect(plain, &blob)) return false;

  if (!WriteAtomically(blob)) return false;

  dirty_ = false;
  return true;
}

bool Store::SaveProfile(const AddressProfile& profile) {
  if (profile.full_name.size() > kMaxFieldLength ||
      profile.email.size() > kMaxFieldLength ||
      profile.phone.size() > kMaxFieldLength ||
      profile.street_address.size() > kMaxStreetAddressLength ||
      profile.city.size() > kMaxFieldLength ||
      profile.state.size() > kMaxFieldLength ||
      profile.postal_code.size() > kMaxFieldLength ||
      profile.country.size() > kMaxFieldLength ||
      profile.label.size() > kMaxFieldLength) {
    return false;
  }

  // Must have at least one identifying field
  if (profile.full_name.empty() && profile.email.empty() &&
      profile.street_address.empty()) {
    return false;
  }

  AddressProfile to_save = profile;
  if (to_save.id.empty()) {
    static uint64_t next_id = 1;
    to_save.id = "addr_" + std::to_string(GetTickCount64()) + "_" +
                 std::to_string(next_id++);
  }

  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const AddressProfile& p) { return p.id == to_save.id; });
  if (it != profiles_.end()) {
    *it = to_save;
  } else {
    if (profiles_.size() >= kMaxProfiles) return false;
    profiles_.push_back(to_save);
  }

  dirty_ = true;
  return Flush();
}

bool Store::GetProfile(const std::string& id, AddressProfile* profile) const {
  if (id.empty() || !profile) return false;
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const AddressProfile& p) { return p.id == id; });
  if (it == profiles_.end()) return false;
  *profile = *it;
  return true;
}

std::vector<AddressProfile> Store::ListProfiles() const {
  return profiles_;
}

bool Store::CanAutofill(const std::string& id) const {
  if (id.empty()) return false;
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const AddressProfile& p) { return p.id == id; });
  if (it == profiles_.end()) return false;
  return it->consent_granted;
}

bool Store::SetConsent(const std::string& id, bool consent) {
  if (id.empty()) return false;
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const AddressProfile& p) { return p.id == id; });
  if (it == profiles_.end()) return false;
  if (it->consent_granted != consent) {
    it->consent_granted = consent;
    dirty_ = true;
    return Flush();
  }
  return true;
}

bool Store::RemoveProfile(const std::string& id) {
  if (id.empty()) return false;
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const AddressProfile& p) { return p.id == id; });
  if (it == profiles_.end()) return false;
  profiles_.erase(it);
  dirty_ = true;
  return Flush();
}

bool Store::Clear() {
  profiles_.clear();
  dirty_ = true;
  return Flush();
}

size_t Store::Count() const {
  return profiles_.size();
}

}  // namespace mtk::autofill
