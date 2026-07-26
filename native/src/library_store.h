#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::library {

struct Entry {
  std::string url;
  std::wstring title;
};

class Store final {
 public:
  static Store& Get();

  void Load();
  bool Flush();
  bool RecordVisit(const std::string& url, const std::wstring& title);
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
