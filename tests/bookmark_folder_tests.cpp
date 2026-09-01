#include "test_support.h"

#include <windows.h>

#include <string>
#include <vector>

#include "library_store.h"
#include "preferences.h"
#include "profile.h"

namespace {

// Starts each case from an empty library inside the suite's throwaway profile.
// The redirection itself is set up once for the whole suite.
class ScopedProfile {
 public:
  ScopedProfile() {
    DeleteFileW(LibraryFile().c_str());
    Reload();
  }

  ~ScopedProfile() { DeleteFileW(LibraryFile().c_str()); }

  std::wstring LibraryFile() const {
    return mtk::preferences::ApplicationDataDirectory() + L"\\library.v1";
  }

  // Guards every case that touches the file: if the redirection ever stopped
  // working, the case must stop rather than operate on real browsing data.
  bool RedirectedAwayFromRealProfile() const {
    return mtk::test::WritingToSuiteProfile();
  }

  // Writes a library file by hand, which is how an older format is simulated.
  void WriteLibrary(const std::string& content) const {
    HANDLE file = CreateFileW(LibraryFile().c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
              &written, nullptr);
    CloseHandle(file);
  }

  void Reload() const { mtk::library::Store::Get().Load(); }
  void SaveAndReload() const {
    mtk::library::Store::Get().Flush();
    mtk::library::Store::Get().Load();
  }
};

const mtk::library::Entry* FindBookmark(const std::string& url) {
  for (const auto& bookmark : mtk::library::Store::Get().bookmarks()) {
    if (bookmark.url == url) return &bookmark;
  }
  return nullptr;
}

// Folder of a saved bookmark, or a sentinel when the bookmark is not there at
// all, so a missing row is reported rather than crashing the run.
std::wstring FolderOf(const std::string& url) {
  const mtk::library::Entry* bookmark = FindBookmark(url);
  return bookmark ? bookmark->folder : L"<bookmark missing>";
}

std::string HexOf(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string result;
  for (const unsigned char character : value) {
    result.push_back(kHex[character >> 4]);
    result.push_back(kHex[character & 0x0F]);
  }
  return result;
}

}  // namespace

MTK_TEST(FolderNamesAreTrimmedBoundedAndFreeOfControlCharacters) {
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(L"  Work  "), L"Work");
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(L"Work"), L"Work");
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(L""), L"");
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(L"   "), L"");
  // A tab or newline would break the line-based file format.
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(L"a\tb"), L"");
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(L"a\nb"), L"");
  const std::wstring too_long(mtk::library::kMaxFolderNameCharacters + 1, L'x');
  MTK_EXPECT_EQ_WSTR(mtk::library::NormalizeFolderName(too_long), L"");
}

MTK_TEST(FoldersAreUniqueWithoutCaseAndAreCapped) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_FALSE(store.CreateFolder(L"work"));
  MTK_EXPECT_FALSE(store.CreateFolder(L"  WORK "));
  MTK_EXPECT_TRUE(store.FolderExists(L"wOrK"));
  MTK_EXPECT_FALSE(store.FolderExists(L"Reading"));
  MTK_EXPECT_FALSE(store.CreateFolder(L""));

  while (store.folders().size() < mtk::library::kMaxFolders) {
    MTK_EXPECT_TRUE(
        store.CreateFolder(L"f" + std::to_wstring(store.folders().size())));
  }
  MTK_EXPECT_FALSE(store.CreateFolder(L"one too many"));
  MTK_EXPECT_EQ_INT(store.folders().size(), mtk::library::kMaxFolders);
}

MTK_TEST(BookmarksMoveBetweenFoldersAndOnlyIntoExistingOnes) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_TRUE(store.AddBookmark("https://www.example.com/", L"Example"));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/"), L"");

  MTK_EXPECT_TRUE(
      store.SetBookmarkFolder("https://www.example.com/", L"Work"));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/"), L"Work");

  // An unknown folder is refused rather than created as a side effect.
  MTK_EXPECT_FALSE(
      store.SetBookmarkFolder("https://www.example.com/", L"Nowhere"));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/"), L"Work");

  // An empty name means the top level.
  MTK_EXPECT_TRUE(store.SetBookmarkFolder("https://www.example.com/", L""));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/"), L"");

  MTK_EXPECT_FALSE(store.SetBookmarkFolder("https://not.saved/", L"Work"));
}

MTK_TEST(SavingIntoAnUnknownFolderKeepsTheBookmarkAtTheTopLevel) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://www.example.org/", L"Example", L"Missing"));
  // The bookmark is still saved; it simply is not filed away somewhere invisible.
  MTK_EXPECT_TRUE(FindBookmark("https://www.example.org/") != nullptr);
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.org/"), L"");
}

MTK_TEST(RenamingAFolderMovesItsBookmarksWithIt) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_TRUE(store.CreateFolder(L"Reading"));
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://www.example.com/", L"Example", L"Work"));

  MTK_EXPECT_TRUE(store.RenameFolder(L"Work", L"Office"));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/"),
                     L"Office");
  MTK_EXPECT_FALSE(store.FolderExists(L"Work"));

  // Colliding with a different folder is refused; changing only case is not.
  MTK_EXPECT_FALSE(store.RenameFolder(L"Office", L"reading"));
  MTK_EXPECT_TRUE(store.RenameFolder(L"Office", L"OFFICE"));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/"),
                     L"OFFICE");
  MTK_EXPECT_FALSE(store.RenameFolder(L"Missing", L"Anything"));
}

MTK_TEST(DeletingAFolderEitherKeepsOrRemovesItsBookmarks) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://www.example.com/a", L"A", L"Work"));
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://www.example.com/b", L"B", L"Work"));
  MTK_EXPECT_TRUE(store.AddBookmark("https://www.example.com/c", L"C"));

  MTK_EXPECT_TRUE(store.RemoveFolder(L"Work", true));
  MTK_EXPECT_FALSE(store.FolderExists(L"Work"));
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 3);
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/a"), L"");

  MTK_EXPECT_TRUE(store.CreateFolder(L"Temp"));
  MTK_EXPECT_TRUE(
      store.SetBookmarkFolder("https://www.example.com/a", L"Temp"));
  MTK_EXPECT_TRUE(store.RemoveFolder(L"Temp", false));
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 2);
  MTK_EXPECT_TRUE(FindBookmark("https://www.example.com/a") == nullptr);
  // Bookmarks outside the folder are untouched either way.
  MTK_EXPECT_TRUE(FindBookmark("https://www.example.com/c") != nullptr);
}

MTK_TEST(FoldersAndTheirBookmarksSurviveASaveAndReload) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_TRUE(store.CreateFolder(L"खाली"));
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://www.example.com/a", L"A page", L"Work"));
  MTK_EXPECT_TRUE(store.AddBookmark("https://www.example.com/b", L"B page"));
  profile.SaveAndReload();

  MTK_EXPECT_EQ_INT(store.folders().size(), 2);
  MTK_EXPECT_TRUE(store.FolderExists(L"Work"));
  // An empty folder is a real folder, so it has to come back too.
  MTK_EXPECT_TRUE(store.FolderExists(L"खाली"));
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/a"), L"Work");
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/b"), L"");
}

MTK_TEST(AFileFromBeforeFoldersLoadsWithEverythingAtTheTopLevel) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  // Exactly the old three-field layout: no folder records, no folder field.
  std::string content = "MTK_LIBRARY_V1\r\n";
  content += "H\t" + HexOf("https://www.example.com/seen") + "\t" +
             HexOf("Seen") + "\t1750000000\r\n";
  content += "B\t" + HexOf("https://www.example.com/saved") + "\t" +
             HexOf("Saved") + "\t1750000001\r\n";
  profile.WriteLibrary(content);
  profile.Reload();

  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_EQ_INT(store.history().size(), 1);
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 1);
  MTK_EXPECT_EQ_INT(store.folders().size(), 0);
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/saved"), L"");
  MTK_EXPECT_EQ_INT(store.history()[0].visited, 1750000000);
}

MTK_TEST(ABookmarkNamingAMissingFolderKeepsThatFolderRatherThanBeingMoved) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  std::string content = "MTK_LIBRARY_V1\r\n";
  // Folder field present, but the matching F record is absent.
  content += "B\t" + HexOf("https://www.example.com/a") + "\t" + HexOf("A") +
             "\t1750000000\t" + HexOf("Work") + "\r\n";
  profile.WriteLibrary(content);
  profile.Reload();

  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/a"), L"Work");
  MTK_EXPECT_TRUE(store.FolderExists(L"Work"));
}

MTK_TEST(UnusableRecordsInTheFileAreSkippedWithoutLosingTheRest) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  std::string content = "MTK_LIBRARY_V1\r\n";
  content += "F\t" + HexOf("Work") + "\t1750000000\r\n";
  // A folder record whose name is not usable, and one duplicate of Work.
  content += "F\t" + HexOf("bad\tname") + "\t1750000000\r\n";
  content += "F\t" + HexOf("work") + "\t1750000000\r\n";
  content += "F\tZZZZ\t1750000000\r\n";
  content += "B\t" + HexOf("https://www.example.com/a") + "\t" + HexOf("A") +
             "\t1750000000\t" + HexOf("Work") + "\r\n";
  content += "X\tgarbage\r\n";
  content += "B\t" + HexOf("https://www.example.com/b") + "\t" + HexOf("B") +
             "\t1750000001\r\n";
  profile.WriteLibrary(content);
  profile.Reload();

  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_EQ_INT(store.folders().size(), 1);
  MTK_EXPECT_EQ_WSTR(store.folders()[0].name, L"Work");
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 2);
  MTK_EXPECT_EQ_WSTR(FolderOf("https://www.example.com/a"), L"Work");
}

MTK_TEST(ClearingBookmarksAlsoClearsTheFolders) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://www.example.com/a", L"A", L"Work"));
  MTK_EXPECT_TRUE(store.ClearBookmarks());
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 0);
  MTK_EXPECT_EQ_INT(store.folders().size(), 0);
}

MTK_TEST(RemoveHistoryForOriginOnlyTouchesThatSiteAndLeavesBookmarks) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(profile.RedirectedAwayFromRealProfile());
  if (!profile.RedirectedAwayFromRealProfile()) return;
  auto& store = mtk::library::Store::Get();
  MTK_EXPECT_TRUE(store.RecordVisit("https://example.com/a", L"A"));
  MTK_EXPECT_TRUE(store.RecordVisit("https://example.com/b", L"B"));
  MTK_EXPECT_TRUE(store.RecordVisit("https://other.example/c", L"C"));
  MTK_EXPECT_TRUE(
      store.AddBookmark("https://example.com/saved", L"Saved page"));

  MTK_EXPECT_EQ_INT(store.RemoveHistoryForOrigin("https://example.com"), 2);
  MTK_EXPECT_EQ_INT(store.history().size(), 1);
  MTK_EXPECT_EQ_STR(store.history()[0].url, "https://other.example/c");
  // A site-scoped history clear must not touch a deliberately saved bookmark.
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 1);
  // An origin with nothing left removes nothing on a second call.
  MTK_EXPECT_EQ_INT(store.RemoveHistoryForOrigin("https://example.com"), 0);
}
