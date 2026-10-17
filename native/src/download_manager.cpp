#include "download_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <shlobj_core.h>
#include <wincodec.h>
#include <winhttp.h>
#include <wrl/client.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winhttp.lib")

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
    // SVG is intentionally excluded in safe mode: standalone SVG documents can
    // execute script when opened, unlike passive raster image formats.
    L"avif",
    // Audio and video
    L"mp3", L"wav", L"flac", L"m4a", L"aac", L"ogg", L"opus", L"weba",
    L"mp4", L"m4v", L"webm", L"mov", L"mkv", L"avi", L"ogv",
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

bool MatchesSearch(const Item& item, const std::wstring& query) {
  if (query.empty()) return true;
  const std::wstring haystack = ToLower(
      item.file_name + L"\n" + item.full_path + L"\n" +
      std::wstring(item.url.begin(), item.url.end()) + L"\n" + item.detail);
  return haystack.find(ToLower(query)) != std::wstring::npos;
}

bool CanRetry(const Item& item) {
  return item.state == State::kFailed || item.state == State::kCanceled ||
         item.state == State::kPaused;
}

bool CanResume(const Item& item) {
  if (item.state != State::kPaused && item.state != State::kFailed) return false;
  if (item.received_bytes <= 0) return false;
  if (item.url.empty()) return false;
  return item.url.starts_with("http://") || item.url.starts_with("https://");
}

std::wstring PartialFilePath(const std::wstring& full_path) {
  if (full_path.empty()) return {};
  return full_path + L".mtkpart";
}

std::string BuildRangeHeader(int64_t received_bytes) {
  if (received_bytes <= 0) return {};
  return "bytes=" + std::to_string(received_bytes) + "-";
}

std::string BuildIfRangeHeader(const std::string& etag,
                               const std::string& last_modified) {
  if (!etag.empty()) return etag;
  if (!last_modified.empty()) return last_modified;
  return {};
}

int64_t GetExistingPartialBytes(const std::wstring& full_path) {
  if (full_path.empty()) return 0;
  const std::wstring part = PartialFilePath(full_path);
  HANDLE file = CreateFileW(part.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    file = CreateFileW(full_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  if (file == INVALID_HANDLE_VALUE) return 0;
  LARGE_INTEGER size{};
  const bool ok = GetFileSizeEx(file, &size);
  CloseHandle(file);
  return (ok && size.QuadPart > 0) ? static_cast<int64_t>(size.QuadPart) : 0;
}

bool ResumeDownloadWithRange(uint64_t id, std::function<void()> on_progress) {
  Item* item = Manager::Get().FindItem(id);
  if (!item || !CanResume(*item) || item->full_path.empty()) return false;

  const auto nav = mtk::policy::EvaluateNavigation(item->url);
  if (!nav.allow ||
      (!nav.url.starts_with("https://") && !nav.url.starts_with("http://"))) {
    item->state = State::kFailed;
    item->detail = L"blocked by safety policy";
    Manager::Get().Flush();
    if (on_progress) on_progress();
    return false;
  }

  const int64_t disk_bytes = GetExistingPartialBytes(item->full_path);
  int64_t offset = disk_bytes > 0 ? disk_bytes : item->received_bytes;
  if (offset <= 0) return false;

  const std::wstring wide_url = Utf8ToWide(nav.url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256]{};
  wchar_t path[4096]{};
  parts.lpszHostName = host;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts) ||
      (parts.nScheme != INTERNET_SCHEME_HTTPS &&
       parts.nScheme != INTERNET_SCHEME_HTTP)) {
    return false;
  }

  HINTERNET session = WinHttpOpen(L"MTKBrowser-Downloader/0.3",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  DWORD protocols =
      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                   sizeof(protocols));
  DWORD timeout = 30000;
  WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

  HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
  const DWORD req_flags =
      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request =
      connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags)
                 : nullptr;
  if (!request) {
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  std::wstring headers =
      L"Range: " + Utf8ToWide(BuildRangeHeader(offset)) + L"\r\n";
  const std::string if_range =
      BuildIfRangeHeader(item->etag, item->last_modified);
  if (!if_range.empty()) {
    headers += L"If-Range: " + Utf8ToWide(if_range) + L"\r\n";
  }

  item->state = State::kInProgress;
  item->detail = L"resuming from " + FormatBytes(offset);
  if (on_progress) on_progress();

  bool ok = false;
  if (WinHttpSendRequest(request, headers.c_str(),
                         static_cast<DWORD>(headers.size()),
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);

    const std::wstring part_path = PartialFilePath(item->full_path);
    const std::wstring active_path =
        FileExists(part_path) ? part_path : item->full_path;

    if (status == 206 || status == 200) {
      const bool partial = (status == 206);
      HANDLE file = CreateFileW(
          active_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
          partial ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file != INVALID_HANDLE_VALUE) {
        if (partial) {
          LARGE_INTEGER move_to{};
          move_to.QuadPart = offset;
          SetFilePointerEx(file, move_to, nullptr, FILE_BEGIN);
          SetEndOfFile(file);
          item->received_bytes = offset;
        } else {
          offset = 0;
          item->received_bytes = 0;
        }

        wchar_t etag_buf[256]{};
        DWORD etag_len = sizeof(etag_buf);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_ETAG,
                                WINHTTP_HEADER_NAME_BY_INDEX, etag_buf,
                                &etag_len, WINHTTP_NO_HEADER_INDEX)) {
          item->etag = WideToUtf8(etag_buf);
        }
        wchar_t lm_buf[256]{};
        DWORD lm_len = sizeof(lm_buf);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LAST_MODIFIED,
                                WINHTTP_HEADER_NAME_BY_INDEX, lm_buf, &lm_len,
                                WINHTTP_NO_HEADER_INDEX)) {
          item->last_modified = WideToUtf8(lm_buf);
        }

        ok = true;
        std::vector<char> chunk(64 * 1024);
        DWORD read = 0;
        while (WinHttpReadData(request, chunk.data(),
                               static_cast<DWORD>(chunk.size()), &read) &&
               read > 0) {
          DWORD written = 0;
          if (!WriteFile(file, chunk.data(), read, &written, nullptr) ||
              written != read) {
            ok = false;
            break;
          }
          item->received_bytes += written;
          if (item->total_bytes > 0) {
            item->percent = static_cast<int>(
                std::clamp<int64_t>((item->received_bytes * 100) /
                                        std::max<int64_t>(1, item->total_bytes),
                                    0, 100));
          }
          if (on_progress) on_progress();
        }
        FlushFileBuffers(file);
        CloseHandle(file);

        if (ok) {
          if (active_path != item->full_path) {
            MoveFileExW(active_path.c_str(), item->full_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
          }
          item->total_bytes = item->received_bytes;
          item->percent = 100;
          item->state = State::kComplete;
          item->can_resume = false;
          item->detail.clear();
          ApplyMarkOfTheWeb(item->full_path, item->url);
        }
      }
    }
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);

  if (!ok) {
    item->state = State::kFailed;
    item->can_resume = CanResume(*item);
    item->detail = L"resume failed — partial progress kept";
  }
  Manager::Get().Upsert(*item);
  Manager::Get().Flush();
  if (on_progress) on_progress();
  return ok;
}

std::wstring CleanSuggestedPrefixes(const std::wstring& raw) {
  std::wstring s = raw;
  static const wchar_t* const kPrefixes[] = {
      L"objecttype-raw-",
      L"objecttype-",
      L"response-content-disposition=",
      L"filename*=",
      L"filename=",
      L"attachment; filename=",
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto* prefix : kPrefixes) {
      const size_t len = wcslen(prefix);
      if (s.size() > len) {
        bool match = true;
        for (size_t i = 0; i < len; ++i) {
          if (towlower(s[i]) != towlower(prefix[i])) {
            match = false;
            break;
          }
        }
        if (match) {
          s.erase(0, len);
          changed = true;
        }
      }
    }
    while (!s.empty() && (s.front() == L'"' || s.front() == L'\'')) {
      s.erase(s.begin());
      changed = true;
    }
    while (!s.empty() && (s.back() == L'"' || s.back() == L'\'')) {
      s.pop_back();
      changed = true;
    }
  }
  if (!s.empty() && (s.front() == L'_' || s.front() == L'-') && s.size() > 1 && iswalnum(s[1])) {
    s.erase(s.begin());
  }
  return s;
}

std::wstring ExtensionFromMime(const std::string& mime) {
  std::string lower = ToLowerAscii(mime);
  const size_t semicolon = lower.find(';');
  if (semicolon != std::string::npos) {
    lower = lower.substr(0, semicolon);
  }
  while (!lower.empty() && (lower.back() == ' ' || lower.back() == '\t')) {
    lower.pop_back();
  }
  while (!lower.empty() && (lower.front() == ' ' || lower.front() == '\t')) {
    lower.erase(lower.begin());
  }

  // Images
  if (lower == "image/jpeg" || lower == "image/jpg" || lower == "image/pjpeg") return L"jpg";
  if (lower == "image/png") return L"png";
  if (lower == "image/webp") return L"webp";
  if (lower == "image/gif") return L"gif";
  if (lower == "image/svg+xml") return L"svg";
  if (lower == "image/bmp" || lower == "image/x-ms-bmp") return L"bmp";
  if (lower == "image/avif") return L"avif";
  if (lower == "image/x-icon" || lower == "image/vnd.microsoft.icon") return L"ico";
  if (lower == "image/tiff") return L"tiff";

  // Documents
  if (lower == "application/pdf") return L"pdf";
  if (lower == "application/zip" || lower == "application/x-zip-compressed") return L"zip";
  if (lower == "application/x-7z-compressed") return L"7z";
  if (lower == "application/x-rar-compressed" || lower == "application/vnd.rar") return L"rar";
  if (lower == "application/x-tar") return L"tar";
  if (lower == "application/gzip") return L"gz";

  if (lower == "text/plain") return L"txt";
  if (lower == "text/html") return L"html";
  if (lower == "text/csv") return L"csv";
  if (lower == "text/markdown" || lower == "text/x-markdown") return L"md";
  if (lower == "application/json") return L"json";
  if (lower == "application/xml" || lower == "text/xml") return L"xml";
  if (lower == "application/rtf" || lower == "text/rtf") return L"rtf";

  if (lower == "application/msword") return L"doc";
  if (lower == "application/vnd.openxmlformats-officedocument.wordprocessingml.document") return L"docx";
  if (lower == "application/vnd.ms-excel") return L"xls";
  if (lower == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") return L"xlsx";
  if (lower == "application/vnd.ms-powerpoint") return L"ppt";
  if (lower == "application/vnd.openxmlformats-officedocument.presentationml.presentation") return L"pptx";
  if (lower == "application/epub+zip") return L"epub";

  // Audio / Video
  if (lower == "audio/mpeg" || lower == "audio/mp3") return L"mp3";
  if (lower == "audio/wav" || lower == "audio/x-wav") return L"wav";
  if (lower == "audio/flac" || lower == "audio/x-flac") return L"flac";
  if (lower == "audio/aac") return L"aac";
  if (lower == "audio/ogg") return L"ogg";
  if (lower == "audio/webm") return L"weba";

  if (lower == "video/mp4") return L"mp4";
  if (lower == "video/webm") return L"webm";
  if (lower == "video/ogg") return L"ogv";
  if (lower == "video/quicktime") return L"mov";
  if (lower == "video/x-matroska") return L"mkv";
  if (lower == "video/x-msvideo") return L"avi";
  return {};
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
  const std::wstring cleaned_suggested = CleanSuggestedPrefixes(suggested_name);
  std::wstring file_name = SanitizeFileName(cleaned_suggested);
  if (ExtensionOf(file_name).empty()) {
    std::wstring mime_ext = ExtensionFromMime(mime_type);
    if (mime_ext.empty() && !url.empty()) {
      // Fallback: check if the URL path contains a valid extension before query/hash
      const size_t q = url.find_first_of("?#");
      const std::string path_only = (q != std::string::npos) ? url.substr(0, q) : url;
      const size_t slash = path_only.find_last_of("/\\");
      const std::string segment = (slash != std::string::npos) ? path_only.substr(slash + 1) : path_only;
      const size_t dot = segment.find_last_of('.');
      if (dot != std::string::npos && dot + 1 < segment.size()) {
        const std::string raw_ext = segment.substr(dot + 1);
        if (raw_ext.size() >= 1 && raw_ext.size() <= 8 &&
            std::all_of(raw_ext.begin(), raw_ext.end(), [](unsigned char c) { return std::isalnum(c); })) {
          mime_ext = ToLower(std::wstring(raw_ext.begin(), raw_ext.end()));
        }
      }
    }
    if (!mime_ext.empty()) {
      if (file_name.empty()) file_name = L"download";
      file_name += L"." + mime_ext;
    }
  }
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

static std::wstring g_last_download_directory;

bool PromptDownloadLocation(HWND owner, const std::wstring& suggested_name,
                            std::wstring* out_full_path,
                            std::wstring* out_file_name) {
  if (out_full_path == nullptr || out_file_name == nullptr) return false;

  std::wstring initial_dir = g_last_download_directory;
  if (initial_dir.empty() || !FileExists(initial_dir)) {
    initial_dir = DownloadDirectory();
  }

  wchar_t file_path[MAX_PATH * 2]{};
  wcsncpy_s(file_path, suggested_name.c_str(), _TRUNCATE);

  // Build dynamic filter based on file extension
  std::wstring filter;
  const std::wstring ext = ExtensionOf(suggested_name);
  if (!ext.empty()) {
    std::wstring upper_ext = ext;
    for (auto& ch : upper_ext) {
      ch = static_cast<wchar_t>(towupper(ch));
    }
    filter += upper_ext + L" Files (*." + ext + L")";
    filter.push_back(L'\0');
    filter += L"*." + ext;
    filter.push_back(L'\0');
  }
  filter += L"All Files (*.*)";
  filter.push_back(L'\0');
  filter += L"*.*";
  filter.push_back(L'\0');
  filter.push_back(L'\0');

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter.data();
  ofn.lpstrFile = file_path;
  ofn.nMaxFile = static_cast<DWORD>(std::size(file_path));
  ofn.lpstrInitialDir = initial_dir.empty() ? nullptr : initial_dir.c_str();
  ofn.lpstrTitle = L"Save As";
  ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str();
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

  if (!GetSaveFileNameW(&ofn)) {
    return false;
  }

  *out_full_path = file_path;
  const size_t slash = out_full_path->find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    *out_file_name = out_full_path->substr(slash + 1);
    g_last_download_directory = out_full_path->substr(0, slash);
  } else {
    *out_file_name = *out_full_path;
  }

  // If the user did not specify an extension in Save As, preserve the suggested extension
  if (ExtensionOf(*out_file_name).empty() && !ext.empty()) {
    *out_file_name += L"." + ext;
    *out_full_path += L"." + ext;
  }
  return true;
}

std::wstring SuggestPdfFileName(const std::wstring& page_title) {
  std::wstring base = page_title;
  if (base.ends_with(L".html") || base.ends_with(L".htm") ||
      base.ends_with(L".php") || base.ends_with(L".asp") ||
      base.ends_with(L".aspx")) {
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
  }
  std::wstring sanitized = SanitizeFileName(base);
  if (sanitized.empty()) sanitized = L"Saved page";
  if (!sanitized.ends_with(L".pdf") && !sanitized.ends_with(L".PDF")) {
    sanitized += L".pdf";
  }
  return sanitized;
}

std::wstring SuggestScreenshotFileName(const std::wstring& page_title) {
  std::wstring base = page_title;
  if (base.ends_with(L".html") || base.ends_with(L".htm") ||
      base.ends_with(L".php") || base.ends_with(L".asp") ||
      base.ends_with(L".aspx")) {
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
  }
  std::wstring sanitized = SanitizeFileName(base);
  if (sanitized.empty()) sanitized = L"Webpage";

  const size_t dot = sanitized.find_last_of(L'.');
  if (dot != std::wstring::npos) sanitized = sanitized.substr(0, dot);
  if (sanitized.size() > 40) sanitized.resize(40);
  for (auto& ch : sanitized) {
    if (ch == L' ') ch = L'_';
  }
  while (!sanitized.empty() && (sanitized.back() == L' ' || sanitized.back() == L'_' || sanitized.back() == L'.')) {
    sanitized.pop_back();
  }
  if (sanitized.empty()) sanitized = L"Webpage";

  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t time_suffix[64]{};
  swprintf_s(time_suffix, L"_%04d-%02d-%02d_%02d%02d%02d.png",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return L"Screenshot_" + sanitized + time_suffix;
}

bool SaveBitmapToPng(HBITMAP hbitmap, const std::wstring& file_path) {
  if (!hbitmap || file_path.empty()) return false;

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
  if (FAILED(hr)) return false;

  Microsoft::WRL::ComPtr<IWICBitmap> wic_bitmap;
  hr = factory->CreateBitmapFromHBITMAP(hbitmap, nullptr, WICBitmapIgnoreAlpha, &wic_bitmap);
  if (FAILED(hr)) return false;

  Microsoft::WRL::ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) return false;

  hr = stream->InitializeFromFilename(file_path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) return false;

  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) return false;

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) return false;

  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  hr = encoder->CreateNewFrame(&frame, nullptr);
  if (FAILED(hr)) return false;

  hr = frame->Initialize(nullptr);
  if (FAILED(hr)) return false;

  UINT width = 0, height = 0;
  wic_bitmap->GetSize(&width, &height);
  frame->SetSize(width, height);

  WICPixelFormatGUID format = GUID_WICPixelFormatDontCare;
  frame->SetPixelFormat(&format);

  hr = frame->WriteSource(wic_bitmap.Get(), nullptr);
  if (FAILED(hr)) return false;

  hr = frame->Commit();
  if (FAILED(hr)) return false;

  hr = encoder->Commit();
  return SUCCEEDED(hr);
}

bool CopyBitmapToClipboard(HWND owner, HBITMAP hbitmap) {
  if (!hbitmap) return false;
  if (!OpenClipboard(owner)) return false;
  EmptyClipboard();

  BITMAP bm{};
  if (!GetObject(hbitmap, sizeof(BITMAP), &bm)) {
    CloseClipboard();
    return false;
  }

  HDC screen_dc = GetDC(nullptr);
  if (!screen_dc) {
    CloseClipboard();
    return false;
  }
  HDC src_dc = CreateCompatibleDC(screen_dc);
  HDC dst_dc = CreateCompatibleDC(screen_dc);
  if (!src_dc || !dst_dc) {
    if (src_dc) DeleteDC(src_dc);
    if (dst_dc) DeleteDC(dst_dc);
    ReleaseDC(nullptr, screen_dc);
    CloseClipboard();
    return false;
  }

  HBITMAP hbm_copy = CreateCompatibleBitmap(screen_dc, bm.bmWidth, bm.bmHeight);
  HBITMAP old_src = static_cast<HBITMAP>(SelectObject(src_dc, hbitmap));
  HBITMAP old_dst = static_cast<HBITMAP>(SelectObject(dst_dc, hbm_copy));

  BitBlt(dst_dc, 0, 0, bm.bmWidth, bm.bmHeight, src_dc, 0, 0, SRCCOPY);

  SelectObject(src_dc, old_src);
  SelectObject(dst_dc, old_dst);
  DeleteDC(src_dc);
  DeleteDC(dst_dc);
  ReleaseDC(nullptr, screen_dc);

  SetClipboardData(CF_BITMAP, hbm_copy);
  CloseClipboard();
  return true;
}

namespace {

constexpr char kDownloadsHeader[] = "MTK_DOWNLOADS_V1\n";
constexpr size_t kMaxDownloadsFileBytes = 512 * 1024;

std::wstring DownloadsHistoryPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\downloads.v1";
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
    if ((fields.size() == 10 || fields.size() == 12) && fields[0] == "D") {
      int state_value = 0;
      Item item;
      int risky = 0;
      int warned = 0;
      std::string file_name;
      std::string full_path;
      std::string url;
      std::string etag;
      std::string last_modified;
      const bool optional_ok =
          fields.size() < 12 ||
          (DecodeField(fields[10], &etag) &&
           DecodeField(fields[11], &last_modified));
      if (optional_ok &&
          ParseNumber(fields[1], &state_value) &&
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
        item.etag = std::move(etag);
        item.last_modified = std::move(last_modified);
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
        if (item.state == State::kFailed || item.state == State::kPaused) {
          const int64_t disk_bytes = GetExistingPartialBytes(item.full_path);
          if (disk_bytes > 0) item.received_bytes = disk_bytes;
          if (item.received_bytes > 0) {
            item.partial_path = PartialFilePath(item.full_path);
            item.can_resume = CanResume(item);
          }
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
    // Private downloads originate in Private Windows and must never be written
    // to the persistent profile on disk.
    if (item.is_private) continue;

    // Persist optional ETag and Last-Modified fields when present so HTTP
    // Range + If-Range resumption works across browser restarts.
    content += "D\t" + std::to_string(static_cast<int>(item.state)) + "\t" +
               std::to_string(item.received_bytes) + "\t" +
               std::to_string(item.total_bytes) + "\t" +
               std::to_string(item.percent) + "\t" +
               (item.risky ? "1" : "0") + "\t" +
               (item.size_warned ? "1" : "0") + "\t" +
               EncodeField(WideToUtf8(item.file_name)) + "\t" +
               EncodeField(WideToUtf8(item.full_path)) + "\t" +
               EncodeField(item.url);
    if (!item.etag.empty() || !item.last_modified.empty()) {
      content += "\t" + EncodeField(item.etag) + "\t" +
                 EncodeField(item.last_modified);
    }
    content += "\n";
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

size_t Manager::ClearPrivateDownloads() {
  const size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [](const Item& item) { return item.is_private; }),
               items_.end());
  return before - items_.size();
}

Item* Manager::FindItem(uint64_t id) {
  for (auto& item : items_) {
    if (item.id == id) return &item;
  }
  return nullptr;
}

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

bool ApplyMarkOfTheWeb(const std::wstring& full_path, const std::string& url) {
  if (full_path.empty()) return false;
  // The marker must never become a way to smuggle extra stream content, so only
  // a policy-approved http(s) address is recorded and it is length-bounded.
  std::string host_url;
  if (const auto decision = mtk::policy::EvaluateNavigation(url);
      decision.allow && (decision.url.starts_with("https://") ||
                         decision.url.starts_with("http://")) &&
      decision.url.size() <= 2048 &&
      decision.url.find_first_of("\r\n") == std::string::npos) {
    host_url = decision.url;
  }

  // ZoneId=3 is URLZONE_INTERNET. Windows reads this stream to decide that the
  // file is untrusted.
  std::string content = "[ZoneTransfer]\r\nZoneId=3\r\n";
  if (!host_url.empty()) content += "HostUrl=" + host_url + "\r\n";

  const std::wstring stream_path = full_path + L":Zone.Identifier";
  HANDLE stream = CreateFileW(stream_path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (stream == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok =
      WriteFile(stream, content.data(), static_cast<DWORD>(content.size()),
                &written, nullptr) &&
      written == content.size();
  CloseHandle(stream);
  return ok;
}

}  // namespace mtk::downloads
