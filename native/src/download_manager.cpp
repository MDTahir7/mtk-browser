#include "download_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlobj_core.h>

#include <algorithm>
#include <array>
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

Manager& Manager::Get() {
  static Manager manager;
  return manager;
}

void Manager::Upsert(const Item& item) {
  for (auto& existing : items_) {
    if (existing.id == item.id) {
      existing = item;
      return;
    }
  }
  if (items_.size() >= kMaxItems) items_.erase(items_.begin());
  items_.push_back(item);
}

bool Manager::Remove(uint32_t id) {
  const auto match = std::find_if(items_.begin(), items_.end(),
                                  [id](const Item& item) { return item.id == id; });
  if (match == items_.end()) return false;
  items_.erase(match);
  return true;
}

size_t Manager::ClearFinished() {
  const size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [](const Item& item) {
                                return item.state != State::kStarting &&
                                       item.state != State::kInProgress;
                              }),
               items_.end());
  return before - items_.size();
}

void Manager::RequestCancel(uint32_t id) {
  if (std::find(cancel_requests_.begin(), cancel_requests_.end(), id) ==
      cancel_requests_.end()) {
    cancel_requests_.push_back(id);
  }
}

bool Manager::TakeCancelRequest(uint32_t id) {
  const auto match =
      std::find(cancel_requests_.begin(), cancel_requests_.end(), id);
  if (match == cancel_requests_.end()) return false;
  cancel_requests_.erase(match);
  return true;
}

size_t Manager::ActiveCount() const {
  return static_cast<size_t>(
      std::count_if(items_.begin(), items_.end(), [](const Item& item) {
        return item.state == State::kStarting || item.state == State::kInProgress;
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
