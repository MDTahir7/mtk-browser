#include "session_store.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <string_view>

#include "home_page.h"
#include "policy.h"
#include "preferences.h"

namespace mtk::session {
namespace {

// V1 files (written before pinning was persisted) carry no P lines, so every
// tab from one of those restores unpinned; V2 adds an optional "P\t1" line
// right after the "U" line of any tab that was pinned when saved.
constexpr char kHeaderV1[] = "MTK_SESSION_V1\n";
constexpr char kHeaderV2[] = "MTK_SESSION_V2\n";
constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxUrlBytes = 2048;

std::wstring SessionPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\session.v1";
}

bool EnsureDataDirectory() {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  return CreateDirectoryW(directory.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

int HexValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

std::string HexEncode(std::string_view value) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const unsigned char character : value) {
    encoded.push_back(digits[character >> 4]);
    encoded.push_back(digits[character & 15]);
  }
  return encoded;
}

bool HexDecode(std::string_view value, std::string* output) {
  if (value.size() % 2 != 0 || value.size() > kMaxUrlBytes * 2) return false;
  output->clear();
  output->reserve(value.size() / 2);
  for (size_t index = 0; index < value.size(); index += 2) {
    const int high = HexValue(value[index]);
    const int low = HexValue(value[index + 1]);
    if (high < 0 || low < 0) return false;
    output->push_back(static_cast<char>((high << 4) | low));
  }
  return true;
}
bool ReadBoundedFile(std::string* content) {
  HANDLE file = CreateFileW(SessionPath().c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  const bool valid = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                     size.QuadPart <= static_cast<LONGLONG>(kMaxFileBytes);
  if (!valid) {
    CloseHandle(file);
    return false;
  }
  content->assign(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const bool ok = content->empty() ||
      ReadFile(file, content->data(), static_cast<DWORD>(content->size()),
               &read, nullptr);
  CloseHandle(file);
  if (!ok || read != content->size()) {
    content->clear();
    return false;
  }
  return true;
}

bool WriteAtomically(const std::string& content) {
  if (!EnsureDataDirectory() || content.size() > kMaxFileBytes) return false;
  const std::wstring path = SessionPath();
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool wrote = content.empty() ||
      WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
                &written, nullptr);
  const bool flushed = wrote && written == content.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (flushed && MoveFileExW(temporary.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING |
                                 MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  DeleteFileW(temporary.c_str());
  return false;
}

bool IsRestorable(const std::string& raw_url, std::string* normalized) {
  if (raw_url.empty() || raw_url.size() > kMaxUrlBytes) return false;
  if (mtk::home::IsHomeUrl(raw_url)) {
    *normalized = mtk::home::kHomeUrl;
    return true;
  }
  const auto decision = mtk::policy::EvaluateNavigation(raw_url);
  if (!decision.allow ||
      (!decision.url.starts_with("https://") &&
       !decision.url.starts_with("http://"))) {
    return false;
  }
  *normalized = decision.url;
  return true;
}

}  // namespace

Snapshot Load() {
  Snapshot result;
  std::string content;
  if (!ReadBoundedFile(&content)) return result;
  size_t offset;
  if (content.starts_with(kHeaderV2)) {
    offset = sizeof(kHeaderV2) - 1;
  } else if (content.starts_with(kHeaderV1)) {
    offset = sizeof(kHeaderV1) - 1;
  } else {
    return result;
  }

  size_t requested_active = 0;
  while (offset < content.size() && result.urls.size() < kMaxRestoredTabs) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
                          (end == std::string::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.starts_with("A\t")) {
      const auto value = line.substr(2);
      std::from_chars(value.data(), value.data() + value.size(), requested_active);
    } else if (line.starts_with("U\t")) {
      std::string decoded;
      std::string normalized;
      if (HexDecode(line.substr(2), &decoded) &&
          IsRestorable(decoded, &normalized)) {
        result.urls.push_back(std::move(normalized));
        result.pinned.push_back(false);
      }
    } else if (line == "P\t1" && !result.pinned.empty()) {
      // Applies to the tab from the immediately preceding "U" line.
      result.pinned.back() = true;
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  result.active_index = result.urls.empty()
                            ? 0
                            : std::min(requested_active, result.urls.size() - 1);
  return result;
}

bool Save(const std::vector<std::string>& urls, size_t active_index,
          const std::vector<bool>& pinned) {
  std::vector<std::string> safe_urls;
  std::vector<bool> safe_pinned;
  safe_urls.reserve(std::min(urls.size(), kMaxRestoredTabs));
  safe_pinned.reserve(safe_urls.capacity());
  size_t safe_active = 0;
  for (size_t index = 0;
       index < urls.size() && safe_urls.size() < kMaxRestoredTabs; ++index) {
    std::string normalized;
    if (!IsRestorable(urls[index], &normalized)) continue;
    if (index <= active_index) safe_active = safe_urls.size();
    safe_urls.push_back(std::move(normalized));
    safe_pinned.push_back(index < pinned.size() && pinned[index]);
  }
  if (safe_urls.empty()) return Clear();

  safe_active = std::min(safe_active, safe_urls.size() - 1);
  std::string content = kHeaderV2;
  content += "A\t" + std::to_string(safe_active) + "\n";
  for (size_t index = 0; index < safe_urls.size(); ++index) {
    content += "U\t" + HexEncode(safe_urls[index]) + "\n";
    if (safe_pinned[index]) content += "P\t1\n";
  }
  return WriteAtomically(content);
}

bool Clear() {
  if (DeleteFileW(SessionPath().c_str())) return true;
  return GetLastError() == ERROR_FILE_NOT_FOUND;
}

}  // namespace mtk::session
