#include "user_filters.h"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <string_view>

#include "preferences.h"

namespace mtk::user_filters {
namespace {

constexpr size_t kMaxFileBytes = 256u * 1024u;

struct Rules {
  // Authored lines, kept so the editor shows exactly what was typed.
  std::vector<std::wstring> lines;
  // Lowercase ASCII patterns, split for matching.
  std::vector<std::string> block;
  std::vector<std::string> allow;
};

// Read from the request path and written from the UI thread, so a shared lock is
// what keeps the two apart. A read is a handful of substring tests.
std::shared_mutex g_mutex;
Rules g_rules;

std::wstring FiltersPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\user_filters.txt";
}

bool EnsureDataDirectory() {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  return CreateDirectoryW(directory.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
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

std::string Trim(std::string value) {
  const auto not_space = [](unsigned char character) {
    return character != ' ' && character != '\t' && character != '\r';
  };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(character >= 'A' && character <= 'Z'
                                                ? character + 32
                                                : character);
                 });
  return value;
}

// Builds the matching lists from authored lines. A rule that is not plain ASCII
// is skipped for matching: an address is ASCII by the time it reaches the
// request path, so a non-ASCII rule could never match and keeping it would only
// look like it worked.
Rules Compile(const std::vector<std::wstring>& lines) {
  Rules rules;
  rules.lines = lines;
  for (const std::wstring& line : lines) {
    const std::string narrow = Trim(WideToUtf8(line));
    if (narrow.empty() || narrow.front() == '#') continue;
    const bool allow = narrow.starts_with("@@");
    std::string pattern = Lower(allow ? narrow.substr(2) : narrow);
    if (pattern.empty() || pattern.size() > kMaxRuleLength) continue;
    if (std::any_of(pattern.begin(), pattern.end(), [](unsigned char character) {
          return character > 0x7F;
        })) {
      continue;
    }
    (allow ? rules.allow : rules.block).push_back(std::move(pattern));
  }
  return rules;
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
  const std::wstring path = FiltersPath();
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = content.empty() ||
                  (WriteFile(file, content.data(),
                             static_cast<DWORD>(content.size()), &written,
                             nullptr) != FALSE &&
                   written == content.size());
  if (ok) FlushFileBuffers(file);
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

Validation Validate(const std::vector<std::wstring>& lines) {
  Validation validation;
  if (lines.size() > kMaxRules) {
    validation.valid = false;
    validation.line = kMaxRules + 1;
    validation.error = L"There can be at most " + std::to_wstring(kMaxRules) +
                       L" lines.";
    return validation;
  }

  for (size_t index = 0; index < lines.size(); ++index) {
    const std::wstring& line = lines[index];
    const size_t line_number = index + 1;
    if (line.size() > kMaxRuleLength) {
      validation.valid = false;
      validation.line = line_number;
      validation.error = L"A line can be at most " +
                         std::to_wstring(kMaxRuleLength) + L" characters.";
      return validation;
    }
    const std::string narrow = Trim(WideToUtf8(line));
    if (!line.empty() && narrow.empty()) {
      validation.valid = false;
      validation.line = line_number;
      validation.error = L"This rule cannot be converted to UTF-8.";
      return validation;
    }
    if (narrow.empty() || narrow.front() == '#') continue;

    const bool allow = narrow.starts_with("@@");
    const std::string_view pattern = allow ? std::string_view(narrow).substr(2)
                                           : std::string_view(narrow);
    if (pattern.empty()) {
      validation.valid = false;
      validation.line = line_number;
      validation.error = allow ? L"@@ needs an address pattern after it."
                               : L"A rule needs an address pattern.";
      return validation;
    }
    if (std::any_of(pattern.begin(), pattern.end(), [](unsigned char character) {
          return character < 0x21 || character > 0x7E;
        })) {
      validation.valid = false;
      validation.line = line_number;
      validation.error = L"Use visible ASCII characters only; web addresses use ASCII.";
      return validation;
    }
    ++validation.active_rule_count;
  }
  return validation;
}

void Load() {
  std::string content;
  std::vector<std::wstring> lines;
  if (ReadFileBounded(FiltersPath(), &content)) {
    size_t offset = 0;
    while (offset < content.size() && lines.size() < kMaxRules) {
      const size_t end = content.find('\n', offset);
      std::string_view line(content.data() + offset,
                            (end == std::string::npos ? content.size() : end) -
                                offset);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      if (line.size() <= kMaxRuleLength) lines.push_back(Utf8ToWide(line));
      if (end == std::string::npos) break;
      offset = end + 1;
    }
  }
  ApplyWithoutSaving(lines);
}

void ApplyWithoutSaving(const std::vector<std::wstring>& lines) {
  Rules compiled = Compile(lines);
  std::unique_lock lock(g_mutex);
  g_rules = std::move(compiled);
}

bool Save(const std::vector<std::wstring>& lines, std::wstring* error) {
  const Validation validation = Validate(lines);
  if (!validation.valid) {
    if (error) {
      *error = L"Line " + std::to_wstring(validation.line) + L": " +
               validation.error;
    }
    return false;
  }
  std::string content;
  for (const std::wstring& line : lines) {
    content += WideToUtf8(line);
    content += "\r\n";
  }
  if (!WriteAtomically(content)) {
    if (error) *error = L"The rules could not be written to disk.";
    return false;
  }
  ApplyWithoutSaving(lines);
  return true;
}

std::vector<std::wstring> Lines() {
  std::shared_lock lock(g_mutex);
  return g_rules.lines;
}

size_t ActiveRuleCount() {
  std::shared_lock lock(g_mutex);
  return g_rules.block.size() + g_rules.allow.size();
}

Decision Evaluate(const std::string& lowercase_url) {
  if (lowercase_url.empty()) return Decision::kNone;
  std::shared_lock lock(g_mutex);
  if (g_rules.block.empty() && g_rules.allow.empty()) return Decision::kNone;
  // Allow rules are checked first so one of them can rescue an address that a
  // broader block rule would otherwise catch.
  for (const std::string& pattern : g_rules.allow) {
    if (lowercase_url.find(pattern) != std::string::npos) {
      return Decision::kAllow;
    }
  }
  for (const std::string& pattern : g_rules.block) {
    if (lowercase_url.find(pattern) != std::string::npos) {
      return Decision::kBlock;
    }
  }
  return Decision::kNone;
}

}  // namespace mtk::user_filters
