#include "library_store.h"

#include <windows.h>

#include <algorithm>
#include <string_view>
#include <utility>

#include "preferences.h"

namespace mtk::library {
namespace {

constexpr size_t kMaxHistoryEntries = 500;
constexpr size_t kMaxBookmarkEntries = 500;
constexpr size_t kMaxUrlBytes = 4096;
constexpr size_t kMaxTitleCharacters = 512;
constexpr size_t kMaxFileBytes = 512 * 1024;
constexpr char kHeader[] = "MTK_LIBRARY_V1";

std::wstring LibraryPath() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\library.v1";
}

bool EnsureApplicationDataDirectory() {
  if (CreateDirectoryW(mtk::preferences::ApplicationDataDirectory().c_str(),
                       nullptr)) {
    return true;
  }
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

std::string HexEncode(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(value.size() * 2);
  for (const unsigned char character : value) {
    result.push_back(kHex[character >> 4]);
    result.push_back(kHex[character & 0x0F]);
  }
  return result;
}

int HexValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  return -1;
}

bool HexDecode(std::string_view value, std::string* output) {
  if (value.size() % 2 != 0 || value.size() / 2 > kMaxFileBytes) return false;
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

bool ReadFile(const std::wstring& path, std::string* content) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  const bool valid_size = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                          static_cast<size_t>(size.QuadPart) <= kMaxFileBytes;
  if (!valid_size) {
    CloseHandle(file);
    return false;
  }
  content->assign(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const bool read_ok = content->empty() || ::ReadFile(
      file, content->data(), static_cast<DWORD>(content->size()), &read, nullptr);
  CloseHandle(file);
  if (!read_ok || read != content->size()) {
    content->clear();
    return false;
  }
  return true;
}

bool WriteFileAtomically(const std::string& content) {
  if (!EnsureApplicationDataDirectory()) return false;
  const std::wstring path = LibraryPath();
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool write_ok = content.empty() || ::WriteFile(
      file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
  const bool flush_ok = write_ok && written == content.size() &&
                        FlushFileBuffers(file);
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

bool IsUsableEntry(const Entry& entry) {
  return !entry.url.empty() && entry.url.size() <= kMaxUrlBytes &&
         entry.title.size() <= kMaxTitleCharacters;
}

void AddLoadedEntry(std::vector<Entry>* entries, const Entry& entry,
                    size_t maximum) {
  if (!IsUsableEntry(entry) || entries->size() >= maximum) return;
  entries->push_back(entry);
}

// Current Unix time in seconds, UTC. FILETIME counts 100 ns ticks from 1601.
long long UnixTime() {
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = now.dwLowDateTime;
  ticks.HighPart = now.dwHighDateTime;
  constexpr unsigned long long kTicksToUnixEpoch = 116444736000000000ULL;
  if (ticks.QuadPart <= kTicksToUnixEpoch) return 0;
  return static_cast<long long>((ticks.QuadPart - kTicksToUnixEpoch) / 10000000ULL);
}

// Optional trailing timestamp field. Anything that is not a bounded run of
// digits is treated as "no timestamp" rather than as a parse failure, so a file
// written by an older build still loads.
long long ParseTimestamp(std::string_view value) {
  if (value.empty() || value.size() > 12) return 0;
  long long parsed = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') return 0;
    parsed = parsed * 10 + (character - '0');
  }
  return parsed;
}

// Tab-separated fields of one record. Trailing fields are optional, which is how
// a file written by an older build still loads: it simply has fewer of them.
std::vector<std::string_view> SplitFields(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t offset = 0;
  while (fields.size() < 6) {
    const size_t tab = line.find('\t', offset);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(offset));
      break;
    }
    fields.push_back(line.substr(offset, tab - offset));
    offset = tab + 1;
  }
  return fields;
}

void ParseLine(std::string_view line, std::vector<Entry>* history,
               std::vector<Entry>* bookmarks, std::vector<Folder>* folders) {
  const std::vector<std::string_view> fields = SplitFields(line);
  if (fields.size() < 2 || fields[0].size() != 1) return;
  const char type = fields[0].front();

  if (type == 'F') {
    if (folders->size() >= kMaxFolders) return;
    std::string name;
    if (!HexDecode(fields[1], &name)) return;
    const std::wstring folder = NormalizeFolderName(Utf8ToWide(name));
    if (folder.empty()) return;
    const bool present = std::any_of(folders->begin(), folders->end(),
        [&folder](const Folder& existing) {
          return _wcsicmp(existing.name.c_str(), folder.c_str()) == 0;
        });
    if (present) return;
    folders->push_back(Folder{folder,
        fields.size() >= 3 ? ParseTimestamp(fields[2]) : 0});
    return;
  }

  if (type != 'H' && type != 'B') return;
  if (fields.size() < 3) return;
  std::string url;
  std::string title;
  if (!HexDecode(fields[1], &url) || !HexDecode(fields[2], &title) ||
      url.size() > kMaxUrlBytes) {
    return;
  }
  const std::wstring wide_title = Utf8ToWide(title);
  if (!title.empty() && wide_title.empty()) return;

  Entry entry;
  entry.url = std::move(url);
  entry.title = wide_title;
  entry.visited = fields.size() >= 4 ? ParseTimestamp(fields[3]) : 0;
  if (type == 'H') {
    AddLoadedEntry(history, entry, kMaxHistoryEntries);
    return;
  }
  if (fields.size() >= 5) {
    std::string folder;
    if (HexDecode(fields[4], &folder)) {
      entry.folder = NormalizeFolderName(Utf8ToWide(folder));
    }
  }
  AddLoadedEntry(bookmarks, entry, kMaxBookmarkEntries);
}

std::string Serialize(const std::vector<Entry>& history,
                      const std::vector<Entry>& bookmarks,
                      const std::vector<Folder>& folders) {
  std::string content = std::string(kHeader) + "\r\n";
  // Folder records come first so the list exists before anything refers to it.
  for (const Folder& folder : folders) {
    content += "F\t";
    content += HexEncode(WideToUtf8(folder.name));
    content.push_back('\t');
    content += std::to_string(folder.created < 0 ? 0 : folder.created);
    content += "\r\n";
  }
  const auto append = [&content](char type, const Entry& entry) {
    content.push_back(type);
    content.push_back('\t');
    content += HexEncode(entry.url);
    content.push_back('\t');
    content += HexEncode(WideToUtf8(entry.title));
    content.push_back('\t');
    content += std::to_string(entry.visited < 0 ? 0 : entry.visited);
    if (type == 'B' && !entry.folder.empty()) {
      content.push_back('\t');
      content += HexEncode(WideToUtf8(entry.folder));
    }
    content += "\r\n";
  };
  for (const Entry& entry : history) append('H', entry);
  for (const Entry& entry : bookmarks) append('B', entry);
  return content;
}

}  // namespace

std::wstring NormalizeFolderName(const std::wstring& name) {
  size_t begin = 0;
  size_t end = name.size();
  while (begin < end && (name[begin] == L' ' || name[begin] == L'\t')) ++begin;
  while (end > begin && (name[end - 1] == L' ' || name[end - 1] == L'\t')) --end;
  const std::wstring trimmed = name.substr(begin, end - begin);
  if (trimmed.empty() || trimmed.size() > kMaxFolderNameCharacters) return {};
  // A control character would let a name break the line-based file format or the
  // menu it is drawn in.
  for (const wchar_t character : trimmed) {
    if (character < 0x20 || character == 0x7f) return {};
  }
  return trimmed;
}

Store& Store::Get() {
  static Store store;
  return store;
}

void Store::Load() {
  history_.clear();
  bookmarks_.clear();
  folders_.clear();
  dirty_ = false;
  std::string content;
  if (!ReadFile(LibraryPath(), &content)) return;
  const size_t header_end = content.find('\n');
  const std::string_view header(kHeader);
  if (header_end == std::string::npos || header_end < header.size() ||
      std::string_view(content.data(), header.size()) != header) {
    return;
  }
  size_t offset = header_end + 1;
  while (offset < content.size()) {
    const size_t end = content.find('\n', offset);
    std::string_view line(content.data() + offset,
        (end == std::string::npos ? content.size() : end) - offset);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    ParseLine(line, &history_, &bookmarks_, &folders_);
    if (end == std::string::npos) break;
    offset = end + 1;
  }

  // A bookmark may name a folder whose own record is missing, for instance after
  // a file was edited by hand. The folder is registered rather than the bookmark
  // being moved silently, so nothing appears to have been reorganised.
  for (Entry& bookmark : bookmarks_) {
    if (bookmark.folder.empty() || FolderExists(bookmark.folder)) continue;
    if (folders_.size() < kMaxFolders) {
      folders_.push_back(Folder{bookmark.folder, bookmark.visited});
    } else {
      bookmark.folder.clear();
    }
  }
}

bool Store::Flush() {
  if (!dirty_) return true;
  if (!WriteFileAtomically(Serialize(history_, bookmarks_, folders_))) {
    return false;
  }
  dirty_ = false;
  return true;
}

bool Store::RecordVisit(const std::string& url, const std::wstring& title) {
  const Entry entry{url, title.empty() ? Utf8ToWide(url) : title, UnixTime()};
  if (!IsUsableEntry(entry)) return false;
  if (!history_.empty() && history_.front().url == url) {
    history_.front().visited = entry.visited;
    dirty_ = true;
    if (history_.front().title != entry.title) {
      history_.front().title = entry.title;
    }
    return true;
  }
  history_.insert(history_.begin(), entry);
  if (history_.size() > kMaxHistoryEntries) history_.pop_back();
  dirty_ = true;
  return true;
}

bool Store::UpdateHistoryTitle(const std::string& url, const std::wstring& title) {
  if (title.empty() || title.size() > kMaxTitleCharacters) return false;
  const auto match = std::find_if(history_.begin(), history_.end(),
      [&url](const Entry& entry) { return entry.url == url; });
  if (match == history_.end() || match->title == title) return false;
  match->title = title;
  dirty_ = true;
  return true;
}

size_t Store::ImportBookmarks(const std::vector<Entry>& entries) {
  size_t added = 0;
  for (const Entry& candidate : entries) {
    if (bookmarks_.size() >= kMaxBookmarkEntries) break;
    if (!IsUsableEntry(candidate)) continue;
    if (IsBookmarked(candidate.url)) continue;
    Entry entry = candidate;
    if (entry.title.empty()) entry.title = Utf8ToWide(entry.url);
    if (entry.visited <= 0) entry.visited = UnixTime();
    // Imported bookmarks go after the existing ones, so a large import cannot
    // bury what the user saved here themselves.
    bookmarks_.push_back(std::move(entry));
    dirty_ = true;
    ++added;
  }
  return added;
}

bool Store::AddBookmark(const std::string& url, const std::wstring& title,
                        const std::wstring& folder) {
  Entry entry{url, title.empty() ? Utf8ToWide(url) : title, UnixTime()};
  if (!IsUsableEntry(entry)) return false;
  // An unknown folder would leave the bookmark pointing at nothing, so it is
  // saved at the top level instead of being refused.
  const std::wstring target = NormalizeFolderName(folder);
  if (!target.empty() && FolderExists(target)) entry.folder = target;
  const auto match = std::find_if(bookmarks_.begin(), bookmarks_.end(),
      [&url](const Entry& saved) { return saved.url == url; });
  if (match != bookmarks_.end()) {
    if (match->title != entry.title) {
      match->title = entry.title;
      dirty_ = true;
    }
    return true;
  }
  bookmarks_.insert(bookmarks_.begin(), entry);
  if (bookmarks_.size() > kMaxBookmarkEntries) bookmarks_.pop_back();
  dirty_ = true;
  return true;
}

bool Store::RemoveBookmark(const std::string& url) {
  const auto match = std::find_if(bookmarks_.begin(), bookmarks_.end(),
      [&url](const Entry& entry) { return entry.url == url; });
  if (match == bookmarks_.end()) return false;
  bookmarks_.erase(match);
  dirty_ = true;
  return true;
}

bool Store::RemoveHistoryAt(size_t index) {
  if (index >= history_.size()) return false;
  history_.erase(history_.begin() + static_cast<ptrdiff_t>(index));
  dirty_ = true;
  return true;
}

bool Store::RemoveBookmarkAt(size_t index) {
  if (index >= bookmarks_.size()) return false;
  bookmarks_.erase(bookmarks_.begin() + static_cast<ptrdiff_t>(index));
  dirty_ = true;
  return true;
}

bool Store::ClearHistory() {
  if (history_.empty()) return true;
  history_.clear();
  dirty_ = true;
  return true;
}

bool Store::ClearBookmarks() {
  if (bookmarks_.empty() && folders_.empty()) return true;
  bookmarks_.clear();
  folders_.clear();
  dirty_ = true;
  return true;
}

bool Store::FolderExists(const std::wstring& name) const {
  const std::wstring target = NormalizeFolderName(name);
  if (target.empty()) return false;
  return std::any_of(folders_.begin(), folders_.end(),
      [&target](const Folder& folder) {
        return _wcsicmp(folder.name.c_str(), target.c_str()) == 0;
      });
}

bool Store::CreateFolder(const std::wstring& name) {
  const std::wstring target = NormalizeFolderName(name);
  if (target.empty() || folders_.size() >= kMaxFolders) return false;
  if (FolderExists(target)) return false;
  folders_.push_back(Folder{target, UnixTime()});
  dirty_ = true;
  return true;
}

bool Store::RenameFolder(const std::wstring& from, const std::wstring& to) {
  const std::wstring source = NormalizeFolderName(from);
  const std::wstring target = NormalizeFolderName(to);
  if (source.empty() || target.empty()) return false;
  const auto match = std::find_if(folders_.begin(), folders_.end(),
      [&source](const Folder& folder) {
        return _wcsicmp(folder.name.c_str(), source.c_str()) == 0;
      });
  if (match == folders_.end()) return false;
  // Changing only the letter case of the same folder stays allowed; colliding
  // with a different folder does not.
  if (_wcsicmp(source.c_str(), target.c_str()) != 0 && FolderExists(target)) {
    return false;
  }
  match->name = target;
  for (Entry& bookmark : bookmarks_) {
    if (_wcsicmp(bookmark.folder.c_str(), source.c_str()) == 0) {
      bookmark.folder = target;
    }
  }
  dirty_ = true;
  return true;
}

bool Store::RemoveFolder(const std::wstring& name, bool keep_bookmarks) {
  const std::wstring target = NormalizeFolderName(name);
  if (target.empty()) return false;
  const auto match = std::find_if(folders_.begin(), folders_.end(),
      [&target](const Folder& folder) {
        return _wcsicmp(folder.name.c_str(), target.c_str()) == 0;
      });
  if (match == folders_.end()) return false;
  folders_.erase(match);
  if (keep_bookmarks) {
    for (Entry& bookmark : bookmarks_) {
      if (_wcsicmp(bookmark.folder.c_str(), target.c_str()) == 0) {
        bookmark.folder.clear();
      }
    }
  } else {
    bookmarks_.erase(std::remove_if(bookmarks_.begin(), bookmarks_.end(),
        [&target](const Entry& bookmark) {
          return _wcsicmp(bookmark.folder.c_str(), target.c_str()) == 0;
        }), bookmarks_.end());
  }
  dirty_ = true;
  return true;
}

bool Store::SetBookmarkFolder(const std::string& url,
                              const std::wstring& folder) {
  const std::wstring target = NormalizeFolderName(folder);
  // An empty name means the top level; any other name has to exist already.
  if (!target.empty() && !FolderExists(target)) return false;
  const auto match = std::find_if(bookmarks_.begin(), bookmarks_.end(),
      [&url](const Entry& bookmark) { return bookmark.url == url; });
  if (match == bookmarks_.end()) return false;
  if (match->folder == target) return true;
  match->folder = target;
  dirty_ = true;
  return true;
}

bool Store::IsBookmarked(const std::string& url) const {
  return std::any_of(bookmarks_.begin(), bookmarks_.end(),
      [&url](const Entry& entry) { return entry.url == url; });
}

const std::vector<Entry>& Store::history() const { return history_; }
const std::vector<Entry>& Store::bookmarks() const { return bookmarks_; }
const std::vector<Folder>& Store::folders() const { return folders_; }

}  // namespace mtk::library
