#include "profile.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>

namespace mtk::profile {
namespace {

std::wstring g_active_id = kDefaultId;
bool g_initialized = false;
// Set only by SetRootForTesting; empty in the shipping browser.
std::wstring g_root_override;

std::wstring InstallationRoot() {
  if (!g_root_override.empty()) return g_root_override;
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

std::wstring DirectoryForId(const std::wstring& id) {
  return id == kDefaultId ? InstallationRoot() : ProfilesRoot() + L"\\" + id;
}

std::wstring DisplayNamePath(const std::wstring& id) {
  return DirectoryForId(id) + L"\\profile_name.txt";
}

bool WriteDisplayName(const std::wstring& path, const std::wstring& name) {
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const DWORD bytes = static_cast<DWORD>(name.size() * sizeof(wchar_t));
  DWORD written = 0;
  const bool ok = WriteFile(file, name.data(), bytes, &written, nullptr) != FALSE &&
                  written == bytes && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (!ok) { DeleteFileW(temporary.c_str()); return false; }
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

std::wstring ReadDisplayName(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  const DWORD size = GetFileSize(file, nullptr);
  if (size == INVALID_FILE_SIZE || size == 0 ||
      size > kMaxDisplayNameLength * sizeof(wchar_t) ||
      size % sizeof(wchar_t) != 0) { CloseHandle(file); return {}; }
  std::wstring value(size / sizeof(wchar_t), L'\0');
  DWORD read = 0;
  const bool ok = ReadFile(file, value.data(), size, &read, nullptr) != FALSE && read == size;
  CloseHandle(file);
  return ok && IsValidDisplayName(value) ? value : std::wstring();
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

bool IsValidDisplayName(const std::wstring& name) {
  return !name.empty() && name.size() <= kMaxDisplayNameLength &&
         std::all_of(name.begin(), name.end(), [](wchar_t character) {
           return character >= L' ' && character != 0x7f;
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

std::wstring DisplayName(const std::wstring& id) {
  if (!IsValidId(id)) return {};
  const std::wstring stored = ReadDisplayName(DisplayNamePath(id));
  return stored.empty() ? id : stored;
}

bool SetDisplayName(const std::wstring& id, const std::wstring& name) {
  if (!IsValidId(id) || !IsValidDisplayName(name)) return false;
  if (!DirectoryExists(DirectoryForId(id))) return false;
  return WriteDisplayName(DisplayNamePath(id), name);
}

bool Delete(const std::wstring& id) {
  if (!IsValidId(id) || id == kDefaultId || id == g_active_id) return false;
  const std::wstring directory = ProfilesRoot() + L"\\" + id;
  if (!DirectoryExists(directory)) return false;
  // SHFileOperation requires a double-null terminated source list. `id` is
  // validated before composing the path, so this cannot expand deletion scope
  // outside the dedicated Profiles directory.
  std::wstring source = directory;
  source.append(2, L'\0');
  SHFILEOPSTRUCTW operation{};
  operation.wFunc = FO_DELETE;
  operation.pFrom = source.c_str();
  operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                     FOF_SILENT;
  const int result = SHFileOperationW(&operation);
  return result == 0 && !operation.fAnyOperationsAborted &&
         !DirectoryExists(directory);
}

void SetRootForTesting(const std::wstring& root) { g_root_override = root; }

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
