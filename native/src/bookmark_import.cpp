#include "bookmark_import.h"

#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "policy.h"

namespace mtk::bookmark_import {
namespace {

// A bookmarks file is JSON written by another program, so every bound here is
// deliberate: it is untrusted input.
constexpr size_t kMaxFileBytes = 8u * 1024u * 1024u;
constexpr size_t kMaxDepth = 64;
constexpr size_t kMaxEntries = 5000;
constexpr size_t kMaxStringCharacters = 4096;

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

std::wstring LocalAppData() {
  PWSTR path = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
    return {};
  }
  std::wstring result(path);
  CoTaskMemFree(path);
  return result;
}

bool ReadWholeFile(const std::wstring& path, std::string* content) {
  content->clear();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  const bool sized = GetFileSizeEx(file, &size) != FALSE;
  if (!sized || size.QuadPart <= 0 ||
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

int HexDigit(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

// Reads one JSON string starting at the opening quote. `offset` ends up just
// past the closing quote. Returns false on a malformed or oversized string.
bool ReadJsonString(const std::string& json, size_t* offset,
                    std::wstring* value) {
  value->clear();
  if (*offset >= json.size() || json[*offset] != '"') return false;
  size_t index = *offset + 1;
  while (index < json.size()) {
    const char character = json[index];
    if (character == '"') {
      *offset = index + 1;
      return true;
    }
    if (value->size() >= kMaxStringCharacters) return false;
    if (character != '\\') {
      // The file is UTF-8. Decoding is deferred: bytes are collected as-is and
      // converted once the string is complete.
      value->push_back(static_cast<unsigned char>(character));
      ++index;
      continue;
    }
    if (index + 1 >= json.size()) return false;
    const char escape = json[index + 1];
    index += 2;
    switch (escape) {
      case '"': value->push_back(L'"'); break;
      case '\\': value->push_back(L'\\'); break;
      case '/': value->push_back(L'/'); break;
      case 'b': value->push_back(L'\b'); break;
      case 'f': value->push_back(L'\f'); break;
      case 'n': value->push_back(L'\n'); break;
      case 'r': value->push_back(L'\r'); break;
      case 't': value->push_back(L'\t'); break;
      case 'u': {
        if (index + 4 > json.size()) return false;
        int code = 0;
        for (size_t digit = 0; digit < 4; ++digit) {
          const int nibble = HexDigit(json[index + digit]);
          if (nibble < 0) return false;
          code = code * 16 + nibble;
        }
        index += 4;
        value->push_back(static_cast<wchar_t>(code));
        break;
      }
      default:
        return false;
    }
  }
  return false;
}

// The raw bytes collected above are UTF-8, except for characters that arrived
// as \u escapes, which are already UTF-16. Escapes are rare in these files, so
// a string is decoded as UTF-8 only when it contains no escape-produced units
// above 0x7F, which keeps both cases correct without a second parser.
std::wstring DecodeText(const std::wstring& raw) {
  std::string bytes;
  bytes.reserve(raw.size());
  for (const wchar_t unit : raw) {
    if (unit > 0xFF) return raw;
    bytes.push_back(static_cast<char>(unit));
  }
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      bytes.c_str(),
                                      static_cast<int>(bytes.size()), nullptr, 0);
  if (size <= 0) return raw;
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.c_str(),
                      static_cast<int>(bytes.size()), result.data(), size);
  return result;
}

void SkipWhitespace(const std::string& json, size_t* offset) {
  while (*offset < json.size() && (json[*offset] == ' ' || json[*offset] == '\t' ||
                                   json[*offset] == '\r' || json[*offset] == '\n')) {
    ++*offset;
  }
}

struct Frame {
  std::wstring name;
  std::wstring url;
  std::wstring type;
};

}  // namespace

std::vector<Source> AvailableSources() {
  const std::wstring local = LocalAppData();
  if (local.empty()) return {};
  // Only the default profile of each browser is offered. Naming extra profiles
  // reliably needs each browser's "Local State" file, which is more surface
  // than an import feature justifies.
  const std::pair<const wchar_t*, const wchar_t*> candidates[] = {
      {L"Google Chrome", L"\\Google\\Chrome\\User Data\\Default\\Bookmarks"},
      {L"Microsoft Edge", L"\\Microsoft\\Edge\\User Data\\Default\\Bookmarks"},
      {L"Brave", L"\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Bookmarks"},
  };
  std::vector<Source> sources;
  for (const auto& [browser, suffix] : candidates) {
    std::wstring path = local + suffix;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      continue;
    }
    sources.push_back(Source{browser, std::move(path)});
  }
  return sources;
}

std::vector<mtk::library::Entry> Read(const std::wstring& path,
                                     size_t* refused) {
  if (refused) *refused = 0;
  std::vector<mtk::library::Entry> entries;
  std::string json;
  if (!ReadWholeFile(path, &json)) return entries;

  std::vector<Frame> frames;
  size_t offset = 0;
  while (offset < json.size() && entries.size() < kMaxEntries) {
    const char character = json[offset];
    if (character == '{') {
      if (frames.size() >= kMaxDepth) return entries;
      frames.emplace_back();
      ++offset;
      continue;
    }
    if (character == '}') {
      if (!frames.empty()) {
        Frame frame = std::move(frames.back());
        frames.pop_back();
        if (frame.type == L"url" && !frame.url.empty()) {
          // Imported bookmarks are held to the same rules as typed addresses,
          // so an import cannot smuggle in an address the browser refuses.
          const auto decision =
              mtk::policy::EvaluateNavigation(WideToUtf8(DecodeText(frame.url)));
          if (decision.allow && (decision.url.starts_with("https://") ||
                                 decision.url.starts_with("http://"))) {
            mtk::library::Entry entry;
            entry.url = decision.url;
            entry.title = DecodeText(frame.name);
            entries.push_back(std::move(entry));
          } else if (refused) {
            ++*refused;
          }
        }
      }
      ++offset;
      continue;
    }
    if (character != '"') {
      ++offset;
      continue;
    }
    std::wstring key;
    if (!ReadJsonString(json, &offset, &key)) return entries;
    SkipWhitespace(json, &offset);
    if (offset >= json.size() || json[offset] != ':') continue;
    ++offset;
    SkipWhitespace(json, &offset);
    if (offset >= json.size() || json[offset] != '"') continue;
    std::wstring value;
    if (!ReadJsonString(json, &offset, &value)) return entries;
    if (frames.empty()) continue;
    Frame& frame = frames.back();
    if (key == L"name") frame.name = std::move(value);
    else if (key == L"url") frame.url = std::move(value);
    else if (key == L"type") frame.type = std::move(value);
  }
  return entries;
}

}  // namespace mtk::bookmark_import
