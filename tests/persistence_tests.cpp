#include "test_support.h"
#include <windows.h>
#include <string>
#include <vector>
#include "download_manager.h"
#include "library_store.h"
#include "password_store.h"
#include "preferences.h"
#include "session_store.h"
#include "tab_search.h"

namespace {
bool WriteFixture(const wchar_t* name, const std::string& content) {
  if (!mtk::test::WritingToSuiteProfile()) return false;
  const auto path = mtk::preferences::ApplicationDataDirectory() + L"\\" + name;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
                            &written, nullptr) && written == content.size();
  CloseHandle(file);
  return ok;
}

MTK_TEST(TabSearchKeepsOriginalIndexesAndMatchesTitleOrUrl) {
  const std::vector<mtk::tabs::SearchEntry> entries = {
      {L"Mail", L"https://mail.example/"},
      {L"Work notes", L"https://example.com/"},
      {L"Mail", L"https://another.example/"}};
  const auto title = mtk::tabs::Search(entries, L"MAIL");
  MTK_EXPECT_EQ_INT(title.size(), 2);
  if (title.size() == 2) { MTK_EXPECT_EQ_INT(title[0], 0); MTK_EXPECT_EQ_INT(title[1], 2); }
  const auto url = mtk::tabs::Search(entries, L"EXAMPLE.COM");
  MTK_EXPECT_EQ_INT(url.size(), 1);
  if (url.size() == 1) MTK_EXPECT_EQ_INT(url[0], 1);
  MTK_EXPECT_EQ_INT(mtk::tabs::Search(entries, L"").size(), 3);
  MTK_EXPECT_TRUE(mtk::tabs::Search(entries, L"nothing").empty());
  MTK_EXPECT_TRUE(mtk::tabs::Search({}, L"mail").empty());
}
}

MTK_TEST(WindowPreferencesRoundTripAndMalformedFieldsFailClosed) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  auto values = store.values();
  values.window_bounds = {-1920, 38, 1200, 800};
  values.window_maximized = true;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_EQ_INT(store.values().window_bounds.left, -1920);
  MTK_EXPECT_EQ_INT(store.values().window_bounds.top, 38);
  MTK_EXPECT_EQ_INT(store.values().window_bounds.width, 1200);
  MTK_EXPECT_EQ_INT(store.values().window_bounds.height, 800);
  MTK_EXPECT_TRUE(store.values().window_maximized);
  for (const std::string width : {"0", "479", "99999999999999", "1200x", ""}) {
    MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini",
        "window_left=5\nwindow_top=8\nwindow_width=" + width +
        "\nwindow_height=800\nwindow_maximized=on\n"));
    store.Load();
    MTK_EXPECT_EQ_INT(store.values().window_bounds.width, 0);
    MTK_EXPECT_FALSE(store.values().window_maximized);
  }
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini",
      "window_width=1200\nwindow_height=800\nwindow_maximized=on\n"));
  store.Load();
  MTK_EXPECT_EQ_INT(store.values().window_bounds.width, 0);
  MTK_EXPECT_FALSE(store.values().window_maximized);
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
}

MTK_TEST(SettingsResetRestoresSafeDefaultsButKeepsParentLock) {
  mtk::preferences::Values changed;
  changed.home_page = L"https://example.com/";
  changed.home_page_custom = true;
  changed.accept_language = "hi,en-US,en";
  changed.search_engine = "duckduckgo";
  changed.download_mode = mtk::preferences::DownloadMode::kStandard;
  changed.startup_mode = mtk::preferences::StartupMode::kContinueSession;
  changed.familiar_shortcuts = true;
  changed.vertical_tabs = true;
  changed.memory_saver = false;
  changed.https_only = false;
  changed.fingerprint_protection = false;
  changed.doh_provider = mtk::preferences::DohProvider::kSystem;
  changed.auto_update = false;
  changed.windows_hello_unlock = true;
  changed.window_bounds = {12, 24, 1200, 800};
  changed.window_maximized = true;
  changed.pin_salt = "0123456789abcdef0123456789abcdef";
  changed.pin_hash =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto reset = mtk::preferences::ResetSettings(changed);
  MTK_EXPECT_EQ_WSTR(reset.home_page, L"mtk://home/");
  MTK_EXPECT_FALSE(reset.home_page_custom);
  MTK_EXPECT_EQ_STR(reset.accept_language, "en-US,en,hi");
  MTK_EXPECT_EQ_STR(reset.search_engine, "google");
  MTK_EXPECT_TRUE(reset.download_mode == mtk::preferences::DownloadMode::kSafe);
  MTK_EXPECT_TRUE(reset.download_ask_location);
  MTK_EXPECT_TRUE(reset.startup_mode == mtk::preferences::StartupMode::kHomePage);
  MTK_EXPECT_FALSE(reset.familiar_shortcuts);
  MTK_EXPECT_FALSE(reset.vertical_tabs);
  MTK_EXPECT_TRUE(reset.memory_saver);
  MTK_EXPECT_TRUE(reset.https_only);
  MTK_EXPECT_TRUE(reset.fingerprint_protection);
  MTK_EXPECT_TRUE(reset.doh_provider ==
                  mtk::preferences::DohProvider::kCloudflareFamily);
  MTK_EXPECT_TRUE(reset.auto_update);
  MTK_EXPECT_FALSE(reset.windows_hello_unlock);
  MTK_EXPECT_EQ_INT(reset.window_bounds.width, 0);
  MTK_EXPECT_FALSE(reset.window_maximized);
  MTK_EXPECT_EQ_STR(reset.pin_salt, changed.pin_salt);
  MTK_EXPECT_EQ_STR(reset.pin_hash, changed.pin_hash);
}

MTK_TEST(ThemePreferenceDefaultsSystemAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  MTK_EXPECT_TRUE(store.values().theme_mode == mtk::preferences::ThemeMode::kSystem);
  auto values = store.values();
  values.theme_mode = mtk::preferences::ThemeMode::kLight;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().theme_mode == mtk::preferences::ThemeMode::kLight);
  values = store.values();
  values.theme_mode = mtk::preferences::ThemeMode::kDark;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().theme_mode == mtk::preferences::ThemeMode::kDark);
}

MTK_TEST(ClearActivityOnExitPreferenceDefaultsOffAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  MTK_EXPECT_FALSE(store.values().clear_activity_on_exit);
  auto values = store.values();
  values.clear_activity_on_exit = true;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().clear_activity_on_exit);
}

MTK_TEST(FamiliarShortcutsPreferenceDefaultsOffAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  MTK_EXPECT_FALSE(store.values().familiar_shortcuts);
  auto values = store.values();
  values.familiar_shortcuts = true;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().familiar_shortcuts);
}

MTK_TEST(MemorySaverPreferenceDefaultsOnAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  MTK_EXPECT_TRUE(store.values().memory_saver);
  auto values = store.values();
  values.memory_saver = false;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_FALSE(store.values().memory_saver);
}

MTK_TEST(VerticalTabsPreferenceDefaultsOffAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  MTK_EXPECT_FALSE(store.values().vertical_tabs);
  MTK_EXPECT_FALSE(store.values().vertical_tabs_collapsed);
  auto values = store.values();
  values.vertical_tabs = true;
  values.vertical_tabs_collapsed = true;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().vertical_tabs);
  MTK_EXPECT_TRUE(store.values().vertical_tabs_collapsed);
}

MTK_TEST(WindowsHelloUnlockPreferenceDefaultsOffAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  store.Load();
  MTK_EXPECT_FALSE(store.values().windows_hello_unlock);
  auto values = store.values();
  values.windows_hello_unlock = true;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().windows_hello_unlock);
}

MTK_TEST(PasswordUpdateClassificationDoesNotMutateAndSurvivesReload) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::passwords::Store::Get();
  using D = mtk::passwords::SaveDisposition;
  store.Clear();
  MTK_EXPECT_TRUE(store.ClassifySave("https://example.com", L"user", L"first") == D::kNew);
  MTK_EXPECT_TRUE(store.SaveAndFlush("https://example.com", L"user", L"first"));
  MTK_EXPECT_TRUE(store.ClassifySave("https://example.com", L"user", L"first") == D::kUnchanged);
  MTK_EXPECT_TRUE(store.ClassifySave("https://example.com", L"user", L"second") == D::kUpdate);
  MTK_EXPECT_TRUE(store.ClassifySave("https://example.com", L"other", L"first") == D::kNew);
  MTK_EXPECT_TRUE(store.ClassifySave("https://example.com", L"user", L"") == D::kInvalid);
  std::wstring password;
  MTK_EXPECT_TRUE(store.Reveal("https://example.com", L"user", &password));
  MTK_EXPECT_EQ_WSTR(password, L"first"); // Declining the proposal changes nothing.
  MTK_EXPECT_TRUE(store.SaveAndFlush("https://example.com", L"user", L"second"));
  store.Load();
  MTK_EXPECT_EQ_INT(store.credentials().size(), 1);
  MTK_EXPECT_TRUE(store.Reveal("https://example.com", L"user", &password));
  MTK_EXPECT_EQ_WSTR(password, L"second");
  mtk::passwords::Scrub(&password);
  store.Clear(); store.Flush();
}

MTK_TEST(PasswordUpdateDiskFailureKeepsPreviousCredential) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::passwords::Store::Get();
  store.Clear();
  MTK_EXPECT_TRUE(store.SaveAndFlush("https://example.com", L"user", L"first"));
  const auto path = mtk::preferences::ApplicationDataDirectory() + L"\\passwords.v1";
  HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  MTK_EXPECT_TRUE(lock != INVALID_HANDLE_VALUE);
  if (lock == INVALID_HANDLE_VALUE) return;
  MTK_EXPECT_FALSE(store.SaveAndFlush("https://example.com", L"user", L"second"));
  CloseHandle(lock);
  std::wstring password;
  MTK_EXPECT_TRUE(store.Reveal("https://example.com", L"user", &password));
  MTK_EXPECT_EQ_WSTR(password, L"first");
  store.Load();
  MTK_EXPECT_TRUE(store.Reveal("https://example.com", L"user", &password));
  MTK_EXPECT_EQ_WSTR(password, L"first");
  mtk::passwords::Scrub(&password);
  store.Clear(); store.Flush();
}

MTK_TEST(PasswordOriginsKeepNonDefaultPortsSeparate) {
  using mtk::passwords::OriginOf;
  MTK_EXPECT_EQ_STR(OriginOf("https://Example.com:443/login"), "https://example.com");
  MTK_EXPECT_EQ_STR(OriginOf("https://example.com:8443/login"), "https://example.com:8443");
  for (const auto* url : {"https://example.com:0/", "https://example.com:65536/",
       "https://example.com:443oops/", "https://user@example.com/",
       "https://example.com:/", "http://example.com/", "https://example.com\\other/"}) {
    MTK_EXPECT_TRUE(OriginOf(url).empty());
  }
  auto& store = mtk::passwords::Store::Get();
  store.Clear();
  MTK_EXPECT_TRUE(store.Save("https://example.com", L"user", L"normal"));
  MTK_EXPECT_TRUE(store.Save("https://example.com:8443", L"user", L"alternate"));
  MTK_EXPECT_TRUE(store.ClassifySave("https://example.com:9443", L"user", L"normal") ==
                  mtk::passwords::SaveDisposition::kNew);
  std::wstring password;
  MTK_EXPECT_TRUE(store.Reveal("https://example.com:8443", L"user", &password));
  MTK_EXPECT_EQ_WSTR(password, L"alternate");
  mtk::passwords::Scrub(&password);
  store.Clear();
}

MTK_TEST(BookmarkEditorMovesRenamesAndPersistsWithoutDuplicates) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::library::Store::Get();
  store.ClearBookmarks();
  MTK_EXPECT_TRUE(store.CreateFolder(L"Work"));
  MTK_EXPECT_TRUE(store.SaveBookmarkAndFlush("https://example.com/", L"Original", L""));
  MTK_EXPECT_TRUE(store.SaveBookmarkAndFlush("https://example.com/", L"Renamed", L"Work"));
  store.Load();
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 1);
  if (store.bookmarks().size() != 1) return;
  MTK_EXPECT_EQ_WSTR(store.bookmarks()[0].title, L"Renamed");
  MTK_EXPECT_EQ_WSTR(store.bookmarks()[0].folder, L"Work");
  MTK_EXPECT_FALSE(store.SaveBookmarkAndFlush("https://example.com/", L"Lost", L"Missing"));
  MTK_EXPECT_EQ_WSTR(store.bookmarks()[0].title, L"Renamed");
  MTK_EXPECT_TRUE(store.SaveBookmarkAndFlush("https://example.com/", L"Root", L""));
  MTK_EXPECT_TRUE(store.bookmarks()[0].folder.empty());
  MTK_EXPECT_TRUE(store.RemoveBookmarkAndFlush("https://example.com/"));
  store.Load();
  MTK_EXPECT_TRUE(store.bookmarks().empty());
}

MTK_TEST(ReadingListIsSeparateAtomicAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::library::Store::Get();
  store.ClearBookmarks();
  // Remove any rows left by an earlier run of this isolated suite profile.
  std::vector<std::string> stale_urls;
  for (const auto& entry : store.reading_list()) stale_urls.push_back(entry.url);
  for (const auto& url : stale_urls) {
    MTK_EXPECT_TRUE(store.RemoveReadingListAndFlush(url));
  }
  MTK_EXPECT_TRUE(store.SaveReadingListAndFlush("https://example.com/article",
                                                L"Article"));
  MTK_EXPECT_TRUE(store.IsInReadingList("https://example.com/article"));
  MTK_EXPECT_FALSE(store.IsBookmarked("https://example.com/article"));
  store.Load();
  MTK_EXPECT_EQ_INT(store.reading_list().size(), 1);
  if (store.reading_list().size() == 1) {
    MTK_EXPECT_EQ_STR(store.reading_list()[0].url, "https://example.com/article");
    MTK_EXPECT_EQ_WSTR(store.reading_list()[0].title, L"Article");
  }
  MTK_EXPECT_TRUE(store.RemoveReadingListAndFlush("https://example.com/article"));
  store.Load();
  MTK_EXPECT_TRUE(store.reading_list().empty());
}

MTK_TEST(ReadingListArticleArchiveSavesRetrievesAndCleansUp) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  const std::string url = "https://example.com/archive-test";
  const std::string html = "<html><body><h1>Archived Article</h1><p>Offline content</p></body></html>";

  // Initially not archived
  MTK_EXPECT_FALSE(mtk::library::Store::HasArticleArchive(url));

  // Save archive
  MTK_EXPECT_TRUE(mtk::library::Store::SaveArticleArchive(url, html));
  MTK_EXPECT_TRUE(mtk::library::Store::HasArticleArchive(url));
  const std::wstring path = mtk::library::Store::GetArticleArchivePath(url);
  MTK_EXPECT_FALSE(path.empty());

  // Reading the file verifies content
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  MTK_EXPECT_TRUE(file != INVALID_HANDLE_VALUE);
  if (file != INVALID_HANDLE_VALUE) {
    char buffer[256]{};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    MTK_EXPECT_TRUE(std::string(buffer, read).find("Offline content") != std::string::npos);
  }

  // Remove archive
  MTK_EXPECT_TRUE(mtk::library::Store::RemoveArticleArchive(url));
  MTK_EXPECT_FALSE(mtk::library::Store::HasArticleArchive(url));
}

MTK_TEST(BookmarkEditorDiskFailureRestoresPreviousTitleFolderAndRemoval) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::library::Store::Get();
  store.ClearBookmarks();
  MTK_EXPECT_TRUE(store.SaveBookmarkAndFlush("https://example.com/", L"Keep", L""));
  const auto path = mtk::preferences::ApplicationDataDirectory() + L"\\library.v1";
  HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  MTK_EXPECT_TRUE(lock != INVALID_HANDLE_VALUE);
  if (lock == INVALID_HANDLE_VALUE) return;
  MTK_EXPECT_FALSE(store.SaveBookmarkAndFlush("https://example.com/", L"Wrong", L""));
  MTK_EXPECT_FALSE(store.RemoveBookmarkAndFlush("https://example.com/"));
  CloseHandle(lock);
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 1);
  if (store.bookmarks().size() == 1) MTK_EXPECT_EQ_WSTR(store.bookmarks()[0].title, L"Keep");
  store.Load();
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 1);
  if (store.bookmarks().size() == 1) MTK_EXPECT_EQ_WSTR(store.bookmarks()[0].title, L"Keep");
}

MTK_TEST(MoveBookmarkAndMoveFolderReorderAndPersist) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& store = mtk::library::Store::Get();
  store.ClearBookmarks();
  MTK_EXPECT_TRUE(store.AddBookmark("https://a.example/", L"A"));
  MTK_EXPECT_TRUE(store.AddBookmark("https://b.example/", L"B"));
  MTK_EXPECT_TRUE(store.AddBookmark("https://c.example/", L"C"));
  // Bookmarks are prepended, so order is C, B, A.
  MTK_EXPECT_EQ_STR(store.bookmarks()[0].url, "https://c.example/");
  MTK_EXPECT_EQ_STR(store.bookmarks()[1].url, "https://b.example/");
  MTK_EXPECT_EQ_STR(store.bookmarks()[2].url, "https://a.example/");

  // Move C from index 0 to index 2 → B, A, C
  MTK_EXPECT_TRUE(store.MoveBookmark(0, 2));
  MTK_EXPECT_EQ_STR(store.bookmarks()[0].url, "https://b.example/");
  MTK_EXPECT_EQ_STR(store.bookmarks()[1].url, "https://a.example/");
  MTK_EXPECT_EQ_STR(store.bookmarks()[2].url, "https://c.example/");

  // Same index is rejected.
  MTK_EXPECT_FALSE(store.MoveBookmark(1, 1));
  // Out of bounds is rejected.
  MTK_EXPECT_FALSE(store.MoveBookmark(0, 10));

  // Verify reorder survives reload.
  store.Load();
  MTK_EXPECT_EQ_INT(store.bookmarks().size(), 3);
  if (store.bookmarks().size() == 3) {
    MTK_EXPECT_EQ_STR(store.bookmarks()[0].url, "https://b.example/");
    MTK_EXPECT_EQ_STR(store.bookmarks()[2].url, "https://c.example/");
  }

  // Now test MoveFolder.
  MTK_EXPECT_TRUE(store.CreateFolder(L"Alpha"));
  MTK_EXPECT_TRUE(store.CreateFolder(L"Beta"));
  MTK_EXPECT_TRUE(store.CreateFolder(L"Gamma"));
  MTK_EXPECT_EQ_INT(store.folders().size(), 3);
  MTK_EXPECT_EQ_WSTR(store.folders()[0].name, L"Alpha");
  MTK_EXPECT_EQ_WSTR(store.folders()[1].name, L"Beta");
  MTK_EXPECT_EQ_WSTR(store.folders()[2].name, L"Gamma");

  // Move Alpha from 0 to 2 → Beta, Gamma, Alpha
  MTK_EXPECT_TRUE(store.MoveFolder(0, 2));
  MTK_EXPECT_EQ_WSTR(store.folders()[0].name, L"Beta");
  MTK_EXPECT_EQ_WSTR(store.folders()[2].name, L"Alpha");
  MTK_EXPECT_FALSE(store.MoveFolder(0, 0));  // same index
  MTK_EXPECT_FALSE(store.MoveFolder(0, 99)); // out of bounds

  // Reorder survives reload.
  store.Load();
  MTK_EXPECT_EQ_INT(store.folders().size(), 3);
  if (store.folders().size() == 3) {
    MTK_EXPECT_EQ_WSTR(store.folders()[0].name, L"Beta");
    MTK_EXPECT_EQ_WSTR(store.folders()[2].name, L"Alpha");
  }

  store.ClearBookmarks();
}

MTK_TEST(ClearActivityOnExitPurgesActivityRecordsWhenEnabled) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& prefs = mtk::preferences::Store::Get();
  auto& library = mtk::library::Store::Get();
  auto& dm = mtk::downloads::Manager::Get();
  dm.ClearFinished();

  library.RecordVisit("https://example.com/test", L"Test Page");
  MTK_EXPECT_TRUE(!library.history().empty());

  mtk::downloads::Item item;
  item.id = 10001;
  item.file_name = L"test.pdf";
  item.state = mtk::downloads::State::kComplete;
  dm.Upsert(item);
  MTK_EXPECT_EQ_INT(dm.items().size(), 1);

  mtk::session::Save({"https://example.com/tab"}, 0);
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(), 1);

  auto values = prefs.values();
  values.clear_activity_on_exit = false;
  prefs.Save(values);
  MTK_EXPECT_FALSE(mtk::preferences::ExecuteClearActivityOnExit());
  MTK_EXPECT_TRUE(!library.history().empty());
  MTK_EXPECT_EQ_INT(dm.items().size(), 1);
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(), 1);

  values.clear_activity_on_exit = true;
  prefs.Save(values);
  MTK_EXPECT_TRUE(mtk::preferences::ExecuteClearActivityOnExit());
  MTK_EXPECT_TRUE(library.history().empty());
  MTK_EXPECT_EQ_INT(dm.items().size(), 0);
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(), 0);

  values.clear_activity_on_exit = false;
  prefs.Save(values);
}

MTK_TEST(FocusModePreferenceDefaultsOnAndPersists) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  auto& prefs = mtk::preferences::Store::Get();
  MTK_EXPECT_TRUE(WriteFixture(L"preferences.ini", ""));
  prefs.Load();
  MTK_EXPECT_TRUE(prefs.values().focus_mode);

  auto values = prefs.values();
  values.focus_mode = false;
  MTK_EXPECT_TRUE(prefs.Save(values));
  prefs.Load();
  MTK_EXPECT_FALSE(prefs.values().focus_mode);

  values.focus_mode = true;
  MTK_EXPECT_TRUE(prefs.Save(values));
  prefs.Load();
  MTK_EXPECT_TRUE(prefs.values().focus_mode);
}


MTK_TEST(DownloadAskLocationPreferenceDefaultsOnAndPersists) {
  auto& store = mtk::preferences::Store::Get();
  mtk::preferences::Values values = store.values();
  MTK_EXPECT_TRUE(values.download_ask_location);

  values.download_ask_location = false;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_FALSE(store.values().download_ask_location);

  values.download_ask_location = true;
  MTK_EXPECT_TRUE(store.Save(values));
  store.Load();
  MTK_EXPECT_TRUE(store.values().download_ask_location);
}
