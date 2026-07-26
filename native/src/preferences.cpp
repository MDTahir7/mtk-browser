#include "preferences.h"

#include <windows.h>

#include <string_view>

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
  values_ = {kDefaultHomePage, kDefaultAcceptLanguage, DownloadMode::kSafe};
  std::string content;
  if (ReadFile(PreferencesPath(), &content)) ReadValues(content, &values_);
}

const Values& Store::values() const { return values_; }

bool Store::Save(const Values& values) {
  if (!IsSafeHomePage(values.home_page) ||
      !IsAllowedLanguage(values.accept_language)) {
    return false;
  }
  const std::string content =
      "[MTK Browser]\r\nhome_page=" + WideToUtf8(values.home_page) +
      "\r\naccept_language=" + values.accept_language +
      "\r\ndownload_mode=" +
      (values.download_mode == DownloadMode::kStandard ? "standard" : "safe") +
      "\r\n";
  if (!WriteFileAtomically(content)) return false;
  values_ = values;
  return true;
}

}  // namespace mtk::preferences
