#include "download_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlobj_core.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <cwctype>

#include "policy.h"
#include "preferences.h"

namespace mtk::downloads {
namespace {

constexpr size_t kMaxItems = 200;
constexpr size_t kMaxFileNameLength = 120;

// Only these types are ever written to disk. An allowlist is used instead of a
// blocklist so a new dangerous extension does not become downloadable by
// default.
constexpr std::array kAllowedExtensions{
    // Documents and text
    L"pdf", L"txt", L"csv", L"log", L"md", L"json", L"xml", L"rtf",
    L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx",
    L"odt", L"ods", L"odp", L"epub",
    // Images
    L"jpg", L"jpeg", L"png", L"gif", L"webp", L"bmp", L"tif", L"tiff", L"ico",
    // Audio and video
    L"mp3", L"wav", L"flac", L"m4a", L"aac", L"ogg", L"opus",
    L"mp4", L"m4v", L"webm", L"mov", L"mkv", L"avi",
    // Archives
    L"zip", L"7z", L"rar", L"tar", L"gz",
};

// Refused anywhere in the file name, which also stops "invoice.pdf.exe" style
// disguises and macro-enabled Office documents.
constexpr std::array kDeniedExtensions{
    L"exe", L"msi", L"msix", L"appx", L"appxbundle", L"bat", L"cmd", L"com",
    L"scr", L"pif", L"ps1", L"psm1", L"vbs", L"vbe", L"js", L"jse", L"mjs",
    L"wsf", L"wsh", L"hta", L"dll", L"ocx", L"sys", L"drv", L"cpl", L"msc",
    L"reg", L"lnk", L"url", L"inf", L"scf", L"jar", L"apk", L"ipa",
    L"iso", L"img", L"vhd", L"vhdx", L"dmg", L"gadget", L"application",
    L"py", L"pyw", L"rb", L"pl", L"sh", L"chm", L"hlp",
    L"docm", L"xlsm", L"pptm", L"dotm", L"xltm", L"xlam", L"accdb", L"mde",
};

constexpr std::array kDeniedMimeTypes{
    "application/x-msdownload",
    "application/x-msdos-program",
    "application/x-ms-installer",
    "application/vnd.microsoft.portable-executable",
    "application/x-executable",
    "application/x-dosexec",
    "application/x-sh",
    "application/x-shellscript",
    "application/x-bat",
    "application/java-archive",
    "application/vnd.android.package-archive",
};

constexpr std::array kReservedNames{
    L"con", L"prn", L"aux", L"nul",
    L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8",
    L"com9", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7",
    L"lpt8", L"lpt9",
};

std::wstring ToLower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t character) {
                   return static_cast<wchar_t>(
                       towlower(static_cast<wint_t>(character)));
                 });
  return value;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(tolower(character));
                 });
  return value;
}

bool Contains(const auto& list, const std::wstring& value) {
  for (const wchar_t* entry : list) {
    if (value == entry) return true;
  }
  return false;
}

std::wstring ExtensionOf(const std::wstring& name) {
  const size_t dot = name.find_last_of(L'.');
  if (dot == std::wstring::npos || dot + 1 >= name.size()) return {};
  return ToLower(name.substr(dot + 1));
}

// Every dot-separated token after the first is treated as a possible extension.
bool HasDeniedExtension(const std::wstring& name) {
  const std::wstring lower = ToLower(name);
  size_t start = lower.find(L'.');
  while (start != std::wstring::npos) {
    const size_t next = lower.find(L'.', start + 1);
    const std::wstring token = lower.substr(
        start + 1, (next == std::wstring::npos ? lower.size() : next) - start - 1);
    if (!token.empty() && Contains(kDeniedExtensions, token)) return true;
    start = next;
  }
  return false;
}

bool IsDeniedMimeType(const std::string& mime_type) {
  const std::string lower = ToLowerAscii(mime_type);
  for (const char* entry : kDeniedMimeTypes) {
    if (lower == entry) return true;
  }
  return false;
}

// Keeps only characters that are valid in a Windows file name and strips the
// path traversal, control and trailing-dot tricks used to escape the target
// directory or hide the real extension.
std::wstring SanitizeFileName(const std::wstring& suggested) {
  std::wstring name;
  name.reserve(std::min(suggested.size(), kMaxFileNameLength));
  for (const wchar_t character : suggested) {
    if (character < 0x20 || character == 0x7f) continue;
    if (wcschr(L"<>:\"/\\|?*", character) != nullptr) {
      name.push_back(L'_');
      continue;
    }
    name.push_back(character);
  }
  while (!name.empty() && (name.front() == L' ' || name.front() == L'.')) {
    name.erase(name.begin());
  }
  while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) {
    name.pop_back();
  }
  if (name.empty()) return {};

  const std::wstring extension = ExtensionOf(name);
  const size_t dot = name.find_last_of(L'.');
  std::wstring base = dot == std::wstring::npos ? name : name.substr(0, dot);
  if (base.empty()) return {};
  if (Contains(kReservedNames, ToLower(base))) base.insert(base.begin(), L'_');

  const size_t suffix = extension.empty() ? 0 : extension.size() + 1;
  if (base.size() + suffix > kMaxFileNameLength) {
    base.resize(kMaxFileNameLength > suffix ? kMaxFileNameLength - suffix : 1);
  }
  return extension.empty() ? base : base + L'.' + extension;
}

bool EnsureDirectory(const std::wstring& path) {
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool FileExists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring FormatBytes(int64_t bytes) {
  if (bytes < 0) return L"unknown size";
  const double value = static_cast<double>(bytes);
  wchar_t text[64]{};
  if (bytes < 1024) {
    swprintf(text, 64, L"%lld B", static_cast<long long>(bytes));
  } else if (bytes < 1024 * 1024) {
    swprintf(text, 64, L"%.1f KB", value / 1024.0);
  } else if (bytes < 1024LL * 1024 * 1024) {
    swprintf(text, 64, L"%.1f MB", value / (1024.0 * 1024.0));
  } else {
    swprintf(text, 64, L"%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
  }
  return text;
}

}  // namespace

Mode CurrentMode() {
  return mtk::preferences::Store::Get().values().download_mode ==
                 mtk::preferences::DownloadMode::kStandard
             ? Mode::kStandard
             : Mode::kSafe;
}

bool IsRiskyFileName(const std::wstring& file_name) {
  return HasDeniedExtension(file_name);
}

Decision Evaluate(const std::string& url, const std::wstring& suggested_name,
                  const std::string& mime_type, Mode mode) {
  // Address safety is never relaxed by the download mode: a blocked site cannot
  // hand over a file in either mode.
  const auto navigation = mtk::policy::EvaluateNavigation(url);
  if (!navigation.allow) {
    return {false,
            L"This download comes from an address blocked by your safety "
            L"policy.", {}, false};
  }
  if (!navigation.url.starts_with("http://") &&
      !navigation.url.starts_with("https://")) {
    return {false, L"MTK Browser only saves downloads from web addresses.", {},
            false};
  }

  // File name hardening also applies in both modes: it prevents writes outside
  // the download folder, not a category of content.
  std::wstring file_name = SanitizeFileName(suggested_name);
  if (file_name.empty()) {
    if (mode == Mode::kSafe) {
      return {false, L"This download has no usable file name.", {}, false};
    }
    file_name = L"download";
  }

  const bool risky =
      IsRiskyFileName(file_name) || IsDeniedMimeType(mime_type);
  if (mode == Mode::kStandard) return {true, {}, file_name, risky};

  if (HasDeniedExtension(file_name)) {
    return {false,
            L"Safe downloads block programs, scripts, installers and disk "
            L"images.\n\nSwitch Downloads to standard mode in Settings if you "
            L"need this file.", {}, true};
  }
  const std::wstring extension = ExtensionOf(file_name);
  if (extension.empty() || !Contains(kAllowedExtensions, extension)) {
    return {false,
            L"Safe downloads only save known file types such as documents, "
            L"images, audio, video and archives.\n\nSwitch Downloads to "
            L"standard mode in Settings if you need this file.", {}, false};
  }
  if (IsDeniedMimeType(mime_type)) {
    return {false, L"The server offered this file as a program, so safe "
                   L"downloads did not save it.", {}, true};
  }
  return {true, {}, file_name, false};
}

std::wstring DownloadDirectory() {
  std::wstring root;
  PWSTR known_path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE,
                                     nullptr, &known_path)) &&
      known_path != nullptr) {
    root = known_path;
  }
  if (known_path) CoTaskMemFree(known_path);
  if (root.empty()) root = mtk::preferences::ApplicationDataDirectory();
  if (root.empty()) return {};
  if (root.back() == L'\\') root.pop_back();

  const std::wstring directory = root + L"\\MTK Browser";
  if (!EnsureDirectory(directory)) return root;
  return directory;
}

std::wstring UniqueTargetPath(const std::wstring& file_name) {
  const std::wstring directory = DownloadDirectory();
  if (directory.empty() || file_name.empty()) return {};

  const size_t dot = file_name.find_last_of(L'.');
  const std::wstring base =
      dot == std::wstring::npos ? file_name : file_name.substr(0, dot);
  const std::wstring extension =
      dot == std::wstring::npos ? std::wstring{} : file_name.substr(dot);

  std::wstring candidate = directory + L"\\" + file_name;
  for (int attempt = 1; attempt <= 999 && FileExists(candidate); ++attempt) {
    candidate = directory + L"\\" + base + L" (" + std::to_wstring(attempt) +
                L")" + extension;
  }
  return FileExists(candidate) ? std::wstring{} : candidate;
}

namespace {

constexpr char kDownloadsHeader[] = "MTK_DOWNLOADS_V1\n";
constexpr size_t kMaxDownloadsFileBytes = 512 * 1024;

std::wstring DownloadsHistoryPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\downloads.v1";
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       value.data(), static_cast<int>(value.size()),
                                       nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string EncodeField(std::string_view value) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string output;
  output.reserve(value.size() * 2);
  for (const unsigned char character : value) {
    output.push_back(digits[character >> 4]);
    output.push_back(digits[character & 15]);
  }
  return output;
}

int DecodeNibble(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  return -1;
}

bool DecodeField(std::string_view value, std::string* output) {
  if (value.size() % 2 != 0 || value.size() > 32768) return false;
  output->clear();
  output->reserve(value.size() / 2);
  for (size_t index = 0; index < value.size(); index += 2) {
    const int high = DecodeNibble(value[index]);
    const int low = DecodeNibble(value[index + 1]);
    if (high < 0 || low < 0) return false;
    output->push_back(static_cast<char>((high << 4) | low));
  }
  return true;
}

std::vector<std::string_view> SplitTabs(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t offset = 0;
  while (offset <= line.size()) {
    const size_t tab = line.find('\t', offset);
    fields.push_back(line.substr(offset, tab == std::string_view::npos
                                            ? line.size() - offset
                                            : tab - offset));
    if (tab == std::string_view::npos) break;
    offset = tab + 1;
  }
  return fields;
}

template <typename Number>
bool ParseNumber(std::string_view value, Number* output) {
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), *output);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool ReadDownloadsFile(std::string* content) {
  HANDLE file = CreateFileW(DownloadsHistoryPath().c_str(), GENERIC_READ,
                            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  const bool valid = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                     size.QuadPart <= static_cast<LONGLONG>(kMaxDownloadsFileBytes);
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
  return ok && read == content->size();
}

bool WriteDownloadsFile(const std::string& content) {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  if ((!CreateDirectoryW(directory.c_str(), nullptr) &&
       GetLastError() != ERROR_ALREADY_EXISTS) ||
      content.size() > kMaxDownloadsFileBytes) {
    return false;
  }
  const std::wstring path = DownloadsHistoryPath();
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

}  // namespace

Manager& Manager::Get() {
  static Manager manager;
  return manager;
}

void Manager::Load() {
  items_.clear();
  dirty_ = false;

  std::string content;
  if (!ReadDownloadsFile(&content) || !content.starts_with(kDownloadsHeader)) return;
  uint64_t history_id = uint64_t{1} << 32;
  size_t offset = sizeof(kDownloadsHeader) - 1;
  while (offset < content.size() && items_.size() < kMaxItems) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
                          (end == std::string::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto fields = SplitTabs(line);
    if (fields.size() == 10 && fields[0] == "D") {
      int state_value = 0;
      Item item;
      int risky = 0;
      int warned = 0;
      std::string file_name;
      std::string full_path;
      std::string url;
      std::string detail;
      if (ParseNumber(fields[1], &state_value) &&
          ParseNumber(fields[2], &item.received_bytes) &&
          ParseNumber(fields[3], &item.total_bytes) &&
          ParseNumber(fields[4], &item.percent) &&
          ParseNumber(fields[5], &risky) && ParseNumber(fields[6], &warned) &&
          DecodeField(fields[7], &file_name) &&
          DecodeField(fields[8], &full_path) && DecodeField(fields[9], &url)) {
        item.id = history_id++;
        item.file_name = Utf8ToWide(file_name);
        item.full_path = Utf8ToWide(full_path);
        item.url = std::move(url);
        item.risky = risky != 0;
        item.size_warned = warned != 0;
        item.state = state_value >= static_cast<int>(State::kStarting) &&
                             state_value <= static_cast<int>(State::kFailed)
                         ? static_cast<State>(state_value)
                         : State::kFailed;
        if (item.state == State::kStarting ||
            item.state == State::kInProgress || item.state == State::kPaused) {
          item.state = State::kFailed;
          item.detail = L"interrupted when MTK Browser closed";
          dirty_ = true;
        }
        if (!item.file_name.empty() && item.file_name.size() <= 260 &&
            item.full_path.size() <= 32767 && item.url.size() <= 4096) {
          items_.push_back(std::move(item));
        }
      }
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  if (dirty_) Flush();
}

bool Manager::Flush() {
  if (!dirty_) return true;
  std::string content = kDownloadsHeader;
  for (const auto& item : items_) {
    // Active callbacks are not resumable across a process restart; Load marks
    // these records as interrupted while preserving the history row.
    content += "D\t" + std::to_string(static_cast<int>(item.state)) + "\t" +
               std::to_string(item.received_bytes) + "\t" +
               std::to_string(item.total_bytes) + "\t" +
               std::to_string(item.percent) + "\t" +
               (item.risky ? "1" : "0") + "\t" +
               (item.size_warned ? "1" : "0") + "\t" +
               EncodeField(WideToUtf8(item.file_name)) + "\t" +
               EncodeField(WideToUtf8(item.full_path)) + "\t" +
               EncodeField(item.url) + "\n";
  }
  if (!WriteDownloadsFile(content)) return false;
  dirty_ = false;
  return true;
}

void Manager::Upsert(const Item& item) {
  for (auto& existing : items_) {
    if (existing.id == item.id) {
      existing = item;
      dirty_ = true;
      return;
    }
  }
  if (items_.size() >= kMaxItems) items_.erase(items_.begin());
  items_.push_back(item);
  dirty_ = true;
}

bool Manager::Remove(uint64_t id) {
  const auto match = std::find_if(items_.begin(), items_.end(),
                                  [id](const Item& item) { return item.id == id; });
  if (match == items_.end()) return false;
  items_.erase(match);
  dirty_ = true;
  return true;
}

size_t Manager::ClearFinished() {
  const size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [](const Item& item) {
                                return item.state != State::kStarting &&
                                       item.state != State::kInProgress &&
                                       item.state != State::kPaused;
                              }),
               items_.end());
  const size_t removed = before - items_.size();
  if (removed > 0) dirty_ = true;
  return removed;
}

size_t Manager::ActiveCount() const {
  return static_cast<size_t>(
      std::count_if(items_.begin(), items_.end(), [](const Item& item) {
        return item.state == State::kStarting ||
               item.state == State::kInProgress || item.state == State::kPaused;
      }));
}

const std::vector<Item>& Manager::items() const { return items_; }

std::wstring DescribeItem(const Item& item) {
  std::wstring text = item.file_name;
  switch (item.state) {
    case State::kStarting:
      text += L"  —  starting";
      break;
    case State::kInProgress:
      text += L"  —  " + FormatBytes(item.received_bytes);
      if (item.total_bytes > 0) {
        text += L" of " + FormatBytes(item.total_bytes);
      }
      if (item.percent >= 0) {
        text += L" (" + std::to_wstring(item.percent) + L"%)";
      }
      break;
    case State::kPaused:
      text += L"  —  paused at " + FormatBytes(item.received_bytes);
      break;
    case State::kComplete:
      text += L"  —  saved, " + FormatBytes(item.received_bytes);
      break;
    case State::kCanceled:
      text += L"  —  canceled";
      break;
    case State::kFailed:
      text += L"  —  failed";
      break;
  }
  if (!item.detail.empty()) text += L"  —  " + item.detail;
  return text;
}

}  // namespace mtk::downloads
