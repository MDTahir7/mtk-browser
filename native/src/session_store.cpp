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
constexpr char kHeaderV3[] = "MTK_SESSION_V3\n";
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

bool ParseInt(std::string_view value, int* output) {
  const auto result = std::from_chars(value.data(), value.data() + value.size(),
                                      *output);
  return result.ec == std::errc() && result.ptr == value.data() + value.size();
}

bool ValidBounds(const Window& window) {
  return window.width >= 480 && window.height >= 320 &&
         window.width <= 20000 && window.height <= 20000 &&
         window.left >= -30000 && window.left <= 30000 &&
         window.top >= -30000 && window.top <= 30000;
}

void MirrorFirstWindow(Snapshot* snapshot) {
  if (snapshot->windows.empty()) return;
  snapshot->urls = snapshot->windows.front().urls;
  snapshot->pinned = snapshot->windows.front().pinned;
  snapshot->active_index = snapshot->windows.front().active_index;
  snapshot->split_index = snapshot->windows.front().split_index;
  snapshot->split_ratio = snapshot->windows.front().split_ratio;
}

Snapshot LoadV3(std::string_view content, size_t offset) {
  Snapshot result;
  Window* current = nullptr;
  bool accepted_tab = false;
  size_t total_tabs = 0;
  while (offset < content.size()) {
    const size_t end = content.find('\n', offset);
    std::string_view line = content.substr(
        offset, (end == std::string_view::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line == "W") {
      accepted_tab = false;
      if (result.windows.size() < kMaxRestoredWindows) {
        result.windows.emplace_back();
        current = &result.windows.back();
      } else {
        current = nullptr;
      }
    } else if (current && line.starts_with("B\t")) {
      const std::string_view fields = line.substr(2);
      int values[5]{};
      size_t field = 0;
      size_t begin = 0;
      while (field < 5) {
        const size_t tab = fields.find('\t', begin);
        const std::string_view value = fields.substr(
            begin, (tab == std::string_view::npos ? fields.size() : tab) - begin);
        if (!ParseInt(value, &values[field++])) break;
        if (tab == std::string_view::npos) break;
        begin = tab + 1;
      }
      if (field == 5) {
        current->left = values[0]; current->top = values[1];
        current->width = values[2]; current->height = values[3];
        current->maximized = values[4] != 0;
        if (!ValidBounds(*current)) {
          current->left = current->top = current->width = current->height = 0;
          current->maximized = false;
        }
      }
    } else if (current && line.starts_with("A\t")) {
      size_t active = 0;
      const auto value = line.substr(2);
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), active);
      if (parsed.ec == std::errc() && parsed.ptr == value.data() + value.size()) {
        current->active_index = active;
      }
    } else if (current && line.starts_with("U\t")) {
      accepted_tab = false;
      std::string decoded;
      std::string normalized;
      if (total_tabs < kMaxRestoredTabs && HexDecode(line.substr(2), &decoded) && IsRestorable(decoded, &normalized)) {
        accepted_tab = true;
        current->urls.push_back(std::move(normalized));
        current->pinned.push_back(false);
        current->groups.emplace_back();
        current->workspaces.emplace_back();
        ++total_tabs;
      }
    } else if (current && accepted_tab && line == "P\t1" && !current->pinned.empty()) {
      current->pinned.back() = true;
    } else if (current && accepted_tab && line.starts_with("G\t") && !current->groups.empty()) {
      std::string group;
      if (HexDecode(line.substr(2), &group) && group.size() <= 128 &&
          std::none_of(group.begin(), group.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
        current->groups.back() = std::move(group);
      }
    } else if (current && accepted_tab && line.starts_with("K\t") && !current->workspaces.empty()) {
      std::string ws;
      if (HexDecode(line.substr(2), &ws) && ws.size() <= 64 &&
          std::none_of(ws.begin(), ws.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
        current->workspaces.back() = std::move(ws);
      }
    } else if (current && line.starts_with("S\t")) {
      const std::string_view fields = line.substr(2);
      const size_t tab = fields.find('\t');
      size_t split_idx = 0;
      int ratio = 50;
      if (tab != std::string_view::npos) {
        const auto idx_part = fields.substr(0, tab);
        const auto ratio_part = fields.substr(tab + 1);
        const auto parsed = std::from_chars(idx_part.data(), idx_part.data() + idx_part.size(), split_idx);
        if (parsed.ec == std::errc() && parsed.ptr == idx_part.data() + idx_part.size()) {
          ParseInt(ratio_part, &ratio);
          current->split_index = split_idx;
          current->split_ratio = std::clamp(ratio, 20, 80);
        }
      } else {
        const auto parsed = std::from_chars(fields.data(), fields.data() + fields.size(), split_idx);
        if (parsed.ec == std::errc() && parsed.ptr == fields.data() + fields.size()) {
          current->split_index = split_idx;
          current->split_ratio = 50;
        }
      }
    }
    if (end == std::string_view::npos) break;
    offset = end + 1;
  }
  result.windows.erase(std::remove_if(result.windows.begin(), result.windows.end(),
                                      [](const Window& window) {
                                        return window.urls.empty();
                                      }), result.windows.end());
  for (auto& window : result.windows) {
    window.active_index = std::min(window.active_index, window.urls.size() - 1);
    if (window.split_index >= window.urls.size() || window.split_index == window.active_index) {
      window.split_index = static_cast<size_t>(-1);
    }
  }
  MirrorFirstWindow(&result);
  return result;
}

}  // namespace

Snapshot Load() {
  Snapshot result;
  std::string content;
  if (!ReadBoundedFile(&content)) return result;
  size_t offset;
  if (content.starts_with(kHeaderV3)) {
    return LoadV3(content, sizeof(kHeaderV3) - 1);
  } else if (content.starts_with(kHeaderV2)) {
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
  if (!result.urls.empty()) {
    Window window;
    window.urls = result.urls;
    window.pinned = result.pinned;
    window.active_index = result.active_index;
    result.windows.push_back(std::move(window));
  }
  return result;
}

bool Save(const std::vector<std::string>& urls, size_t active_index,
          const std::vector<bool>& pinned) {
  Window window;
  window.urls = urls;
  window.pinned = pinned;
  window.active_index = active_index;
  return SaveWindows({window});
}

bool SaveWindows(const std::vector<Window>& windows) {
  std::vector<Window> safe_windows;
  safe_windows.reserve(std::min(windows.size(), kMaxRestoredWindows));
  size_t total_tabs = 0;
  for (const Window& source : windows) {
    if (safe_windows.size() >= kMaxRestoredWindows || total_tabs >= kMaxRestoredTabs) break;
    Window window;
    if (ValidBounds(source)) {
      window.left = source.left; window.top = source.top;
      window.width = source.width; window.height = source.height;
      window.maximized = source.maximized;
    }
    size_t target_split_index = static_cast<size_t>(-1);
    for (size_t index = 0; index < source.urls.size() &&
                           total_tabs < kMaxRestoredTabs; ++index) {
      std::string normalized;
      if (!IsRestorable(source.urls[index], &normalized)) continue;
      if (index <= source.active_index) window.active_index = window.urls.size();
      if (index == source.split_index) target_split_index = window.urls.size();
      window.urls.push_back(std::move(normalized));
      window.pinned.push_back(index < source.pinned.size() && source.pinned[index]);
      const std::string group = index < source.groups.size() ? source.groups[index] : "";
      window.groups.push_back(group.size() <= 128 &&
          std::none_of(group.begin(), group.end(), [](unsigned char c) { return c < 32 || c == 127; }) ? group : "");
      const std::string ws = index < source.workspaces.size() ? source.workspaces[index] : "";
      window.workspaces.push_back(ws.size() <= 64 &&
          std::none_of(ws.begin(), ws.end(), [](unsigned char c) { return c < 32 || c == 127; }) ? ws : "");
      ++total_tabs;
    }
    if (!window.urls.empty()) {
      window.active_index = std::min(window.active_index, window.urls.size() - 1);
      if (target_split_index < window.urls.size() && target_split_index != window.active_index) {
        window.split_index = target_split_index;
        window.split_ratio = std::clamp(source.split_ratio, 20, 80);
      }
      safe_windows.push_back(std::move(window));
    }
  }
  if (safe_windows.empty()) return Clear();

  std::string content = kHeaderV3;
  for (const Window& window : safe_windows) {
    content += "W\nB\t" + std::to_string(window.left) + "\t" +
               std::to_string(window.top) + "\t" + std::to_string(window.width) +
               "\t" + std::to_string(window.height) + "\t" +
               (window.maximized ? "1" : "0") + "\nA\t" +
               std::to_string(window.active_index) + "\n";
    if (window.split_index < window.urls.size() && window.split_index != window.active_index) {
      content += "S\t" + std::to_string(window.split_index) + "\t" +
                 std::to_string(window.split_ratio) + "\n";
    }
    for (size_t index = 0; index < window.urls.size(); ++index) {
      content += "U\t" + HexEncode(window.urls[index]) + "\n";
      if (window.pinned[index]) content += "P\t1\n";
      if (!window.groups[index].empty()) content += "G\t" + HexEncode(window.groups[index]) + "\n";
      if (!window.workspaces[index].empty()) content += "K\t" + HexEncode(window.workspaces[index]) + "\n";
    }
  }
  return WriteAtomically(content);
}

bool Clear() {
  if (DeleteFileW(SessionPath().c_str())) return true;
  return GetLastError() == ERROR_FILE_NOT_FOUND;
}

}  // namespace mtk::session
