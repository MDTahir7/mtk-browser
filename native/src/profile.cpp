#include "profile.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

namespace mtk::profile {
namespace {

std::wstring g_active_id = kDefaultId;
bool g_initialized = false;

std::wstring InstallationRoot() {
  PWSTR path = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
    return {};
  }
  std::wstring root(path);
  CoTaskMemFree(path);
  if (!root.empty() && root.back() == L'\\') root.pop_back();
  return root + L"\\MTK Browser";
}

std::wstring ProfilesRoot() { return InstallationRoot() + L"\\Profiles"; }

bool EnsureDirectory(const std::wstring& path) {
  if (path.empty()) return false;
  return CreateDirectoryW(path.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

bool DirectoryExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

}  // namespace

bool IsValidId(const std::wstring& id) {
  if (id.empty() || id.size() > kMaxIdLength) return false;
  // Only these characters, so an id can never escape the profiles directory or
  // name a device. A leading '-' is refused as well, because an id also travels
  // on a command line.
  if (id.front() == L'-') return false;
  return std::all_of(id.begin(), id.end(), [](wchar_t character) {
    return (character >= L'a' && character <= L'z') ||
           (character >= L'A' && character <= L'Z') ||
           (character >= L'0' && character <= L'9') || character == L'-' ||
           character == L'_';
  });
}

bool Initialize(const std::wstring& id) {
  // Selecting a profile after a store has been read would leave that store
  // pointing at the previous directory, so this only ever runs once.
  if (g_initialized) return false;
  g_initialized = true;
  if (id.empty()) return true;
  if (!IsValidId(id) || id == kDefaultId) {
    return id == kDefaultId;
  }
  const std::wstring directory = ProfilesRoot() + L"\\" + id;
  if (!DirectoryExists(directory)) {
    if (!EnsureDirectory(InstallationRoot()) || !EnsureDirectory(ProfilesRoot()) ||
        !EnsureDirectory(directory)) {
      return false;
    }
  }
  g_active_id = id;
  return true;
}

const std::wstring& ActiveId() { return g_active_id; }

bool ActiveIsDefault() { return g_active_id == kDefaultId; }

std::wstring ActiveDirectory() {
  // The default profile keeps the original location, so an existing install
  // needs no migration.
  if (ActiveIsDefault()) return InstallationRoot();
  return ProfilesRoot() + L"\\" + g_active_id;
}

std::vector<std::wstring> List() {
  std::vector<std::wstring> profiles{kDefaultId};
  const std::wstring pattern = ProfilesRoot() + L"\\*";
  WIN32_FIND_DATAW entry{};
  HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
  if (search == INVALID_HANDLE_VALUE) return profiles;
  do {
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
    const std::wstring name = entry.cFileName;
    // Only directories that could have been created by this code are listed, so
    // anything dropped into the folder by hand is ignored rather than launched.
    if (!IsValidId(name) || name == kDefaultId) continue;
    profiles.push_back(name);
  } while (FindNextFileW(search, &entry));
  FindClose(search);
  return profiles;
}

bool Create(const std::wstring& id) {
  if (!IsValidId(id) || id == kDefaultId) return false;
  return EnsureDirectory(InstallationRoot()) && EnsureDirectory(ProfilesRoot()) &&
         EnsureDirectory(ProfilesRoot() + L"\\" + id);
}

std::wstring IdFromCommandLine(const std::wstring& command_line) {
  constexpr std::wstring_view kSwitch = L"--profile=";
  size_t offset = command_line.find(kSwitch);
  if (offset == std::wstring::npos) return {};
  // The switch has to start a token, otherwise "--not-a-profile=x" would match.
  if (offset > 0 && command_line[offset - 1] != L' ' &&
      command_line[offset - 1] != L'\t' && command_line[offset - 1] != L'"') {
    return {};
  }
  offset += kSwitch.size();
  std::wstring value;
  const bool quoted = offset < command_line.size() && command_line[offset] == L'"';
  if (quoted) ++offset;
  while (offset < command_line.size()) {
    const wchar_t character = command_line[offset];
    if (quoted ? character == L'"'
               : (character == L' ' || character == L'\t')) {
      break;
    }
    if (value.size() >= kMaxIdLength) return {};
    value.push_back(character);
    ++offset;
  }
  return value;
}

}  // namespace mtk::profile
