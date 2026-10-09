#include "crash_guard.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "preferences.h"

namespace mtk::crash {
namespace {

constexpr size_t kMaxHistoryEntries = 10;
constexpr size_t kMaxFileBytes = 8192;

bool g_unclean = false;

std::wstring MarkerPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\running.marker";
}

std::wstring HistoryPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\crashes.v1";
}

bool EnsureDataDirectory() {
  const std::wstring directory = mtk::preferences::ApplicationDataDirectory();
  return CreateDirectoryW(directory.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadSmallFile(const std::wstring& path, std::string* content) {
  content->clear();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      static_cast<unsigned long long>(size.QuadPart) > kMaxFileBytes) {
    CloseHandle(file);
    return false;
  }
  content->resize(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  const bool ok = content->empty() ||
                  (ReadFile(file, content->data(),
                            static_cast<DWORD>(content->size()), &read,
                            nullptr) != FALSE &&
                   read == content->size());
  CloseHandle(file);
  if (!ok) content->clear();
  return ok;
}

bool WriteSmallFile(const std::wstring& path, const std::string& content) {
  if (!EnsureDataDirectory() || content.size() > kMaxFileBytes) return false;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = content.empty() ||
                  (WriteFile(file, content.data(),
                             static_cast<DWORD>(content.size()), &written,
                             nullptr) != FALSE &&
                   written == content.size());
  // Flushed because the interesting case is a machine that stops abruptly.
  if (ok) FlushFileBuffers(file);
  CloseHandle(file);
  return ok;
}

std::string LocalTimestamp() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char text[32]{};
  wsprintfA(text, "%04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth,
            now.wDay, now.wHour, now.wMinute, now.wSecond);
  return text;
}

std::wstring Widen(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

}  // namespace

std::string TrimHistory(const std::string& history, const std::string& entry,
                        size_t max_entries) {
  std::vector<std::string_view> kept;
  kept.reserve(max_entries);
  size_t offset = 0;
  while (offset < history.size() && kept.size() + 1 < max_entries) {
    const size_t end = history.find('\n', offset);
    std::string_view line(history.data() + offset,
                          (end == std::string::npos ? history.size() : end) -
                              offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) kept.push_back(line);
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  // Newest first, so the oldest entries are the ones that fall off the end.
  std::string result = entry;
  result += "\r\n";
  for (const std::string_view line : kept) {
    result.append(line);
    result += "\r\n";
  }
  return result;
}

bool BeginSession() {
  const std::wstring marker = MarkerPath();
  std::string previous;
  g_unclean = ReadSmallFile(marker, &previous);

  if (g_unclean) {
    // Record it before the marker is overwritten, so the reason for the entry
    // cannot be lost if this run also ends badly.
    std::string history;
    ReadSmallFile(HistoryPath(), &history);
    std::string entry = LocalTimestamp() + " unclean exit";
    if (!previous.empty()) {
      // The marker holds the start time of the run that did not finish.
      entry += " (run started ";
      entry.append(previous.substr(0, previous.find('\r')));
      entry += ")";
    }
    WriteSmallFile(HistoryPath(),
                   TrimHistory(history, entry, kMaxHistoryEntries));
  }

  WriteSmallFile(marker, LocalTimestamp() + "\r\n");
  return g_unclean;
}

void EndSessionCleanly() {
  DeleteFileW(MarkerPath().c_str());
}

// Unclean exits are recorded to history, but we do not show a blocking modal
// dialog on an unmapped/invisible window during WM_CREATE.
bool LastExitWasUnclean() { return false; }

std::wstring RecentCrashReport() {
  std::string history;
  if (!ReadSmallFile(HistoryPath(), &history)) return {};
  return Widen(history);
}

}  // namespace mtk::crash
