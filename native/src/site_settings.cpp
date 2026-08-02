#include "site_settings.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string_view>

#include "preferences.h"

namespace mtk::sites {
namespace {

constexpr char kHeader[] = "MTK_SITES_V1";
// Bounded so a long browsing history cannot grow this file without limit. The
// oldest rule is dropped when the limit is reached.
constexpr size_t kMaxRules = 500;
constexpr size_t kMaxOriginLength = 300;
constexpr size_t kMaxFileBytes = 256u * 1024u;

std::wstring SitesPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\sites.v1";
}

bool EnsureDataDirectory() {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  return CreateDirectoryW(directory.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
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

bool HexDecode(std::string_view value, std::string* output) {
  if (value.empty() || value.size() % 2 != 0 ||
      value.size() / 2 > kMaxOriginLength) {
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
  const std::wstring path = SitesPath();
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

char PermissionCode(Permission value) {
  switch (value) {
    case Permission::kAllow: return 'a';
    case Permission::kBlock: return 'b';
    case Permission::kAsk: break;
  }
  return '?';
}

Permission PermissionFromCode(char code) {
  if (code == 'a') return Permission::kAllow;
  if (code == 'b') return Permission::kBlock;
  return Permission::kAsk;
}

bool IsDefault(const Rule& rule) {
  return rule.zoom_percent == kDefaultZoomPercent &&
         rule.camera == Permission::kAsk &&
         rule.microphone == Permission::kAsk &&
         rule.location == Permission::kAsk &&
         rule.notifications == Permission::kAsk && rule.ad_filtering &&
         rule.cookies;
}

// Field layout, tab separated: origin, zoom, four permission codes, ad
// filtering, cookies. Any malformed line is skipped rather than failing the
// whole file, so one bad row cannot lose every other rule.
std::string Serialize(const std::vector<Rule>& rules) {
  std::string content = std::string(kHeader) + "\r\n";
  for (const Rule& rule : rules) {
    content += "S\t";
    content += HexEncode(rule.origin);
    content += '\t';
    content += std::to_string(rule.zoom_percent);
    content += '\t';
    content += PermissionCode(rule.camera);
    content += PermissionCode(rule.microphone);
    content += PermissionCode(rule.location);
    content += PermissionCode(rule.notifications);
    content += '\t';
    content += rule.ad_filtering ? '1' : '0';
    content += '\t';
    content += rule.cookies ? '1' : '0';
    content += "\r\n";
  }
  return content;
}

std::vector<std::string_view> SplitFields(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (start <= line.size() && fields.size() < 8) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  return fields;
}

int ParseZoom(std::string_view value) {
  if (value.empty() || value.size() > 4) return kDefaultZoomPercent;
  int parsed = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') return kDefaultZoomPercent;
    parsed = parsed * 10 + (character - '0');
  }
  return std::clamp(parsed, kMinZoomPercent, kMaxZoomPercent);
}

}  // namespace

std::string OriginOf(const std::string& url) {
  std::string lowered;
  lowered.reserve(url.size());
  for (const char character : url) {
    lowered.push_back(static_cast<char>(
        character >= 'A' && character <= 'Z' ? character + 32 : character));
  }
  const bool https = lowered.starts_with("https://");
  const bool http = lowered.starts_with("http://");
  if (!https && !http) return {};
  const size_t host_start = https ? 8 : 7;
  // The authority runs to the first path, query or fragment character. The port
  // is found inside it, after any credentials, so the whole authority has to be
  // isolated before looking for ':'.
  size_t authority_end = lowered.size();
  for (size_t index = host_start; index < lowered.size(); ++index) {
    const char character = lowered[index];
    if (character == '/' || character == '?' || character == '#') {
      authority_end = index;
      break;
    }
  }
  const std::string authority =
      lowered.substr(host_start, authority_end - host_start);
  // Credentials would let two different strings name the same site, so a URL
  // carrying them has no usable origin here.
  if (authority.find('@') != std::string::npos) return {};
  const size_t port = authority.find(':');
  const std::string host =
      port == std::string::npos ? authority : authority.substr(0, port);
  if (host.empty() || host.size() > kMaxOriginLength) return {};
  return (https ? "https://" : "http://") + host;
}

double ZoomLevelForPercent(int percent) {
  const int clamped = std::clamp(percent, kMinZoomPercent, kMaxZoomPercent);
  if (clamped == kDefaultZoomPercent) return 0.0;
  // Chromium's zoom level is log base 1.2 of the scale factor.
  return std::log(static_cast<double>(clamped) / 100.0) / std::log(1.2);
}

int ZoomPercentForLevel(double level) {
  if (!std::isfinite(level)) return kDefaultZoomPercent;
  const double scale = std::pow(1.2, level) * 100.0;
  const int rounded = static_cast<int>(std::lround(scale));
  return std::clamp(rounded, kMinZoomPercent, kMaxZoomPercent);
}

Store& Store::Get() {
  static Store store;
  return store;
}

void Store::Load() {
  rules_.clear();
  dirty_ = false;
  std::string content;
  if (!ReadFileBounded(SitesPath(), &content)) return;
  const std::string_view header(kHeader);
  const size_t header_end = content.find('\n');
  if (header_end == std::string::npos || header_end < header.size() ||
      std::string_view(content.data(), header.size()) != header) {
    return;
  }
  size_t offset = header_end + 1;
  while (offset < content.size() && rules_.size() < kMaxRules) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
                          (end == std::string::npos ? content.size() : end) -
                              offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() > 4 && line.front() == 'S' && line[1] == '\t') {
      const auto fields = SplitFields(line.substr(2));
      Rule rule;
      if (fields.size() >= 4 && HexDecode(fields[0], &rule.origin) &&
          OriginOf(rule.origin) == rule.origin) {
        rule.zoom_percent = ParseZoom(fields[1]);
        if (fields[2].size() == 4) {
          rule.camera = PermissionFromCode(fields[2][0]);
          rule.microphone = PermissionFromCode(fields[2][1]);
          rule.location = PermissionFromCode(fields[2][2]);
          rule.notifications = PermissionFromCode(fields[2][3]);
        }
        rule.ad_filtering = fields[3] != "0";
        rule.cookies = fields.size() < 5 || fields[4] != "0";
        if (!HasRule(rule.origin)) rules_.push_back(std::move(rule));
      }
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
}

bool Store::Flush() {
  if (!dirty_) return true;
  if (!WriteAtomically(Serialize(rules_))) return false;
  dirty_ = false;
  return true;
}

Rule Store::For(const std::string& origin) const {
  const auto match = std::find_if(
      rules_.begin(), rules_.end(),
      [&origin](const Rule& rule) { return rule.origin == origin; });
  if (match != rules_.end()) return *match;
  Rule rule;
  rule.origin = origin;
  return rule;
}

bool Store::HasRule(const std::string& origin) const {
  return std::any_of(rules_.begin(), rules_.end(),
                     [&origin](const Rule& rule) {
                       return rule.origin == origin;
                     });
}

bool Store::Set(const Rule& rule) {
  if (rule.origin.empty() || OriginOf(rule.origin) != rule.origin) return false;
  // A rule that says nothing is removed instead of stored, so the file only
  // ever holds real decisions.
  if (IsDefault(rule)) return Forget(rule.origin);
  Rule stored = rule;
  stored.zoom_percent =
      std::clamp(stored.zoom_percent, kMinZoomPercent, kMaxZoomPercent);
  const auto match = std::find_if(
      rules_.begin(), rules_.end(),
      [&stored](const Rule& existing) { return existing.origin == stored.origin; });
  if (match != rules_.end()) {
    *match = std::move(stored);
  } else {
    if (rules_.size() >= kMaxRules) rules_.erase(rules_.begin());
    rules_.push_back(std::move(stored));
  }
  dirty_ = true;
  return true;
}

bool Store::Forget(const std::string& origin) {
  const auto match = std::find_if(
      rules_.begin(), rules_.end(),
      [&origin](const Rule& rule) { return rule.origin == origin; });
  if (match == rules_.end()) return true;
  rules_.erase(match);
  dirty_ = true;
  return true;
}

bool Store::Clear() {
  if (rules_.empty()) return true;
  rules_.clear();
  dirty_ = true;
  return true;
}

const std::vector<Rule>& Store::rules() const { return rules_; }

}  // namespace mtk::sites
