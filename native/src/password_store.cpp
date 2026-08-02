#include "password_store.h"

#include <windows.h>
#include <dpapi.h>

#include <algorithm>
#include <string_view>

#include "policy.h"
#include "preferences.h"
#include "profile.h"
#include "site_settings.h"

namespace mtk::passwords {
namespace {

constexpr char kHeader[] = "MTK_PASSWORDS_V1";
constexpr size_t kMaxFileBytes = 1024u * 1024u;
// A DPAPI blob for a short password is a few hundred bytes; this leaves ample
// room while still bounding what will be parsed.
constexpr size_t kMaxBlobBytes = 8192;

std::wstring PasswordsPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\passwords.v1";
}

bool EnsureDataDirectory() {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  return CreateDirectoryW(directory.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

// Extra DPAPI entropy. Including the profile id means a blob written by one
// profile cannot be decrypted by another, even though both run as the same
// Windows user, which keeps profiles as separate as the rest of their data.
std::string Entropy() {
  std::string entropy = "MTK Browser passwords v1|";
  const std::wstring& id = mtk::profile::ActiveId();
  for (const wchar_t character : id) {
    entropy.push_back(static_cast<char>(character & 0x7F));
  }
  return entropy;
}

std::string HexEncode(std::string_view value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const unsigned char byte : value) {
    encoded.push_back(kDigits[byte >> 4]);
    encoded.push_back(kDigits[byte & 0x0F]);
  }
  return encoded;
}

int HexValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

bool HexDecode(std::string_view value, size_t max_bytes, std::string* output) {
  if (value.empty() || value.size() % 2 != 0 || value.size() / 2 > max_bytes) {
    return false;
  }
  output->clear();
  output->reserve(value.size() / 2);
  for (size_t index = 0; index < value.size(); index += 2) {
    const int high = HexValue(value[index]);
    const int low = HexValue(value[index + 1]);
    if (high < 0 || low < 0) {
      output->clear();
      return false;
    }
    output->push_back(static_cast<char>((high << 4) | low));
  }
  return true;
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

// DPAPI, user scope. CRYPTPROTECT_UI_FORBIDDEN means a blob is never allowed to
// raise a prompt while being unwrapped, which matters because unwrapping happens
// during page load.
bool Protect(const std::wstring& secret, std::string* blob) {
  blob->clear();
  if (secret.empty()) return false;
  std::string plain = WideToUtf8(secret);
  if (plain.empty()) return false;

  std::string entropy = Entropy();
  DATA_BLOB input{static_cast<DWORD>(plain.size()),
                  reinterpret_cast<BYTE*>(plain.data())};
  DATA_BLOB salt{static_cast<DWORD>(entropy.size()),
                 reinterpret_cast<BYTE*>(entropy.data())};
  DATA_BLOB output{};
  const bool ok = CryptProtectData(&input, L"MTK Browser saved password", &salt,
                                   nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                                   &output) != FALSE;
  // The plaintext copy is wiped whether or not encryption succeeded.
  SecureZeroMemory(plain.data(), plain.size());
  if (!ok) return false;
  blob->assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return true;
}

bool Unprotect(const std::string& blob, std::wstring* secret) {
  secret->clear();
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
  *secret = Utf8ToWide(std::string_view(reinterpret_cast<const char*>(output.pbData),
                                        output.cbData));
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return !secret->empty();
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
  const std::wstring path = PasswordsPath();
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

}  // namespace

void Scrub(std::wstring* text) {
  if (!text || text->empty()) return;
  SecureZeroMemory(text->data(), text->size() * sizeof(wchar_t));
  text->clear();
}

bool IsStorableUrl(const std::string& url) {
  // https only. A password typed over plain http is already exposed, and
  // remembering it would quietly encourage repeating that.
  const std::string origin = mtk::sites::OriginOf(url);
  if (origin.empty() || !origin.starts_with("https://")) return false;
  return mtk::policy::EvaluateNavigation(url).allow;
}

Store& Store::Get() {
  static Store store;
  return store;
}

void Store::Load() {
  entries_.clear();
  dirty_ = false;
  std::string content;
  if (!ReadFileBounded(PasswordsPath(), &content)) return;
  const std::string_view header(kHeader);
  const size_t header_end = content.find('\n');
  if (header_end == std::string::npos || header_end < header.size() ||
      std::string_view(content.data(), header.size()) != header) {
    return;
  }
  size_t offset = header_end + 1;
  while (offset < content.size() && entries_.size() < kMaxCredentials) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
                          (end == std::string::npos ? content.size() : end) -
                              offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() > 6 && line.front() == 'P' && line[1] == '\t') {
      line.remove_prefix(2);
      const size_t first = line.find('\t');
      const size_t second =
          first == std::string_view::npos ? first : line.find('\t', first + 1);
      if (second != std::string_view::npos) {
        std::string origin;
        std::string user_utf8;
        Entry entry;
        if (HexDecode(line.substr(0, first), 300, &origin) &&
            HexDecode(line.substr(first + 1, second - first - 1),
                      kMaxUserNameLength * 4, &user_utf8) &&
            HexDecode(line.substr(second + 1), kMaxBlobBytes, &entry.encrypted) &&
            mtk::sites::OriginOf(origin) == origin &&
            origin.starts_with("https://")) {
          entry.credential.origin = std::move(origin);
          entry.credential.user_name = Utf8ToWide(user_utf8);
          if (!entry.credential.user_name.empty() &&
              entry.credential.user_name.size() <= kMaxUserNameLength) {
            entries_.push_back(std::move(entry));
          }
        }
      }
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
}

bool Store::Flush() {
  if (!dirty_) return true;
  std::string content = std::string(kHeader) + "\r\n";
  for (const Entry& entry : entries_) {
    content += "P\t";
    content += HexEncode(entry.credential.origin);
    content += '\t';
    content += HexEncode(WideToUtf8(entry.credential.user_name));
    content += '\t';
    content += HexEncode(entry.encrypted);
    content += "\r\n";
  }
  if (!WriteAtomically(content)) return false;
  dirty_ = false;
  return true;
}

bool Store::Save(const std::string& origin, const std::wstring& user_name,
                 const std::wstring& password) {
  if (mtk::sites::OriginOf(origin) != origin || !origin.starts_with("https://")) {
    return false;
  }
  if (user_name.empty() || user_name.size() > kMaxUserNameLength) return false;
  if (password.empty() || password.size() > kMaxPasswordLength) return false;

  std::string blob;
  if (!Protect(password, &blob) || blob.size() > kMaxBlobBytes) return false;

  const auto match = std::find_if(
      entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.credential.origin == origin &&
               entry.credential.user_name == user_name;
      });
  if (match != entries_.end()) {
    match->encrypted = std::move(blob);
  } else {
    if (entries_.size() >= kMaxCredentials) return false;
    Entry entry;
    entry.credential.origin = origin;
    entry.credential.user_name = user_name;
    entry.encrypted = std::move(blob);
    entries_.push_back(std::move(entry));
  }
  dirty_ = true;
  return true;
}

bool Store::SingleFor(const std::string& origin, Credential* credential) const {
  const Entry* found = nullptr;
  for (const Entry& entry : entries_) {
    if (entry.credential.origin != origin) continue;
    // Several accounts for one site: filling would be a guess, so nothing is
    // filled and the person picks from the saved list instead.
    if (found) return false;
    found = &entry;
  }
  if (!found) return false;
  if (credential) *credential = found->credential;
  return true;
}

bool Store::Reveal(const std::string& origin, const std::wstring& user_name,
                   std::wstring* password) const {
  if (!password) return false;
  Scrub(password);
  const auto match = std::find_if(
      entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.credential.origin == origin &&
               entry.credential.user_name == user_name;
      });
  if (match == entries_.end()) return false;
  return Unprotect(match->encrypted, password);
}

bool Store::Remove(const std::string& origin, const std::wstring& user_name) {
  const auto match = std::find_if(
      entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.credential.origin == origin &&
               entry.credential.user_name == user_name;
      });
  if (match == entries_.end()) return false;
  SecureZeroMemory(match->encrypted.data(), match->encrypted.size());
  entries_.erase(match);
  dirty_ = true;
  return true;
}

bool Store::Clear() {
  if (entries_.empty()) return true;
  for (Entry& entry : entries_) {
    SecureZeroMemory(entry.encrypted.data(), entry.encrypted.size());
  }
  entries_.clear();
  dirty_ = true;
  return true;
}

const std::vector<Credential>& Store::credentials() const {
  // The public list is rebuilt on demand so the encrypted blobs never leave
  // this class.
  static thread_local std::vector<Credential> view;
  view.clear();
  view.reserve(entries_.size());
  for (const Entry& entry : entries_) view.push_back(entry.credential);
  return view;
}

}  // namespace mtk::passwords
