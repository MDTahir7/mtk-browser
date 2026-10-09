#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::library {

// Longest folder name accepted, in characters. The name is shown on the
// bookmarks bar, so it is deliberately short.
constexpr size_t kMaxFolderNameCharacters = 64;
// Folders are a shallow grouping, not a tree, so this cap is about keeping the
// bar usable rather than about storage.
constexpr size_t kMaxFolders = 30;

struct Entry {
  std::string url;
  std::wstring title;
  // Unix time in seconds, UTC, of the visit (history) or of when the bookmark
  // was saved. Zero means an entry written before timestamps were stored.
  long long visited = 0;
  // Bookmarks only: the folder this entry sits in. Empty means the top level of
  // the bookmarks bar. History entries never use it.
  std::wstring folder;
};

struct Folder {
  std::wstring name;
  // Unix time in seconds, UTC, of when the folder was created.
  long long created = 0;
};

// Trimmed folder name, or an empty string when the name cannot be used. Names
// are trimmed of surrounding spaces, may not contain control characters, and are
// bounded by kMaxFolderNameCharacters.
std::wstring NormalizeFolderName(const std::wstring& name);

// Standards-compatible Netscape bookmark HTML for manual backup/import into
// other browsers. It performs no disk I/O and escapes all user-controlled
// titles, folders and URLs before producing UTF-8.
std::string ExportBookmarksHtml(const std::vector<Entry>& bookmarks);

class Store final {
 public:
  static Store& Get();

  void Load();
  bool Flush();
  bool RecordVisit(const std::string& url, const std::wstring& title);
  // Bulk bookmark import, used by the Chrome importer. Entries that fail the
  // usual validation are skipped, and duplicates are ignored. Returns how many
  // bookmarks were actually added.
  size_t ImportBookmarks(const std::vector<Entry>& entries);
  bool UpdateHistoryTitle(const std::string& url, const std::wstring& title);
  bool AddBookmark(const std::string& url, const std::wstring& title,
                   const std::wstring& folder = {});
  // Explicit editor operation: validates the destination, updates title AND
  // folder together, and restores memory if the atomic disk write fails.
  bool SaveBookmarkAndFlush(const std::string& url, const std::wstring& title,
                            const std::wstring& folder);
  bool RemoveBookmarkAndFlush(const std::string& url);
  // Reading-list entries are saved separately from bookmarks. Both operations
  // roll memory back when the atomic file replacement fails.
  bool SaveReadingListAndFlush(const std::string& url, const std::wstring& title);
  bool RemoveReadingListAndFlush(const std::string& url);

  // Offline article archive functions for Reading List items.
  static bool SaveArticleArchive(const std::string& url, const std::string& html_content);
  static bool HasArticleArchive(const std::string& url);
  static std::wstring GetArticleArchivePath(const std::string& url);
  static bool RemoveArticleArchive(const std::string& url);
  bool RemoveBookmark(const std::string& url);
  bool RemoveHistoryAt(size_t index);
  bool RemoveBookmarkAt(size_t index);
  // Removes every history entry whose URL belongs to `origin` ("scheme://host",
  // as produced by sites::OriginOf). Returns how many rows were removed.
  // Bookmarks are left alone: they are saved deliberately, not a browsing
  // trace, and the site-level "clear this site's data" action follows the
  // same rule the whole-profile Clear browsing data dialog already does.
  size_t RemoveHistoryForOrigin(const std::string& origin);
  bool ClearHistory();
  // Removes every bookmark and every folder, since a folder without its
  // bookmarks would only be a leftover.
  bool ClearBookmarks();
  bool IsBookmarked(const std::string& url) const;
  bool IsInReadingList(const std::string& url) const;

  // Folder names are compared without case, so "Work" and "work" are one folder.
  bool FolderExists(const std::wstring& name) const;
  // Fails when the name is unusable, already taken, or the cap is reached.
  bool CreateFolder(const std::wstring& name);
  // Renames the folder and every bookmark that points at it, in one step, so the
  // two can never disagree.
  bool RenameFolder(const std::wstring& from, const std::wstring& to);
  // `keep_bookmarks` moves the contents to the top level; otherwise they are
  // removed together with the folder.
  bool RemoveFolder(const std::wstring& name, bool keep_bookmarks);
  // Moves one bookmark. An empty folder means the top level. Fails when the
  // target folder does not exist.
  bool SetBookmarkFolder(const std::string& url, const std::wstring& folder);
  // Reorders bookmarks within the list and atomically flushes changes.
  bool MoveBookmark(size_t from, size_t to);
  // Reorders folders within the list and atomically flushes changes.
  bool MoveFolder(size_t from, size_t to);

  const std::vector<Entry>& history() const;
  const std::vector<Entry>& bookmarks() const;
  const std::vector<Entry>& reading_list() const;
  const std::vector<Folder>& folders() const;

 private:
  std::vector<Entry> history_;
  std::vector<Entry> bookmarks_;
  std::vector<Entry> reading_list_;
  std::vector<Folder> folders_;
  bool dirty_ = false;
};

}  // namespace mtk::library
