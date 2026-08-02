#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::library {

struct Entry {
  std::string url;
  std::wstring title;
  // Unix time in seconds, UTC, of the visit (history) or of when the bookmark
  // was saved. Zero means an entry written before timestamps were stored.
  long long visited = 0;
};

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
  bool AddBookmark(const std::string& url, const std::wstring& title);
  bool RemoveBookmark(const std::string& url);
  bool RemoveHistoryAt(size_t index);
  bool RemoveBookmarkAt(size_t index);
  bool ClearHistory();
  bool ClearBookmarks();
  bool IsBookmarked(const std::string& url) const;

  const std::vector<Entry>& history() const;
  const std::vector<Entry>& bookmarks() const;

 private:
  std::vector<Entry> history_;
  std::vector<Entry> bookmarks_;
  bool dirty_ = false;
};

}  // namespace mtk::library
