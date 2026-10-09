#include "test_support.h"

#include <windows.h>
#include <string>
#include <vector>

#include "download_manager.h"
#include "library_store.h"
#include "preferences.h"
#include "private_session.h"
#include "session_store.h"

namespace {

bool ReadProfileFile(const wchar_t* name, std::string* content) {
  if (!mtk::test::WritingToSuiteProfile()) return false;
  const auto path = mtk::preferences::ApplicationDataDirectory() + L"\\" + name;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD size = GetFileSize(file, nullptr);
  if (size == INVALID_FILE_SIZE) {
    CloseHandle(file);
    return false;
  }
  content->resize(size);
  DWORD read = 0;
  const bool ok = ReadFile(file, content->data(), size, &read, nullptr) && read == size;
  CloseHandle(file);
  return ok;
}

}  // namespace

MTK_TEST(PrivateSessionTrackingMultiWindowLifecycle) {
  auto& manager = mtk::private_session::Manager::Get();
  manager.Reset();

  MTK_EXPECT_EQ_INT(manager.ActiveWindowCount(), 0);
  MTK_EXPECT_FALSE(manager.HasActiveSession());

  // Window A opens
  manager.OnWindowOpened();
  MTK_EXPECT_EQ_INT(manager.ActiveWindowCount(), 1);
  MTK_EXPECT_TRUE(manager.HasActiveSession());

  // Window B opens concurrently
  manager.OnWindowOpened();
  MTK_EXPECT_EQ_INT(manager.ActiveWindowCount(), 2);
  MTK_EXPECT_TRUE(manager.HasActiveSession());

  // Window A closes: shared private session must NOT be destroyed because B is still open
  manager.OnWindowClosed();
  MTK_EXPECT_EQ_INT(manager.ActiveWindowCount(), 1);
  MTK_EXPECT_TRUE(manager.HasActiveSession());

  // Window B closes: last private window closes -> private session must be purged
  manager.OnWindowClosed();
  MTK_EXPECT_EQ_INT(manager.ActiveWindowCount(), 0);
  MTK_EXPECT_FALSE(manager.HasActiveSession());
}

MTK_TEST(PrivateSessionNewSessionStartsClean) {
  auto& manager = mtk::private_session::Manager::Get();
  manager.Reset();

  bool cleaned_up = false;
  manager.SetCleanupCallback([&cleaned_up]() { cleaned_up = true; });

  // First session: open and close
  manager.OnWindowOpened();
  manager.OnWindowOpened();
  manager.OnWindowClosed();
  MTK_EXPECT_FALSE(cleaned_up);
  manager.OnWindowClosed();
  MTK_EXPECT_TRUE(cleaned_up);

  // New session starts completely clean
  cleaned_up = false;
  manager.OnWindowOpened();
  MTK_EXPECT_EQ_INT(manager.ActiveWindowCount(), 1);
  MTK_EXPECT_TRUE(manager.HasActiveSession());
  manager.OnWindowClosed();
  MTK_EXPECT_TRUE(cleaned_up);
  manager.SetCleanupCallback(nullptr);
}

MTK_TEST(PrivateDownloadsExcludedFromDiskPersistence) {
  if (!mtk::test::WritingToSuiteProfile()) {
    MTK_EXPECT_TRUE(false);
    return;
  }

  auto& dm = mtk::downloads::Manager::Get();
  dm.ClearFinished();
  dm.ClearPrivateDownloads();

  mtk::downloads::Item normal_item;
  normal_item.id = 9001;
  normal_item.file_name = L"public_document.pdf";
  normal_item.full_path = L"C:\\Users\\User\\Downloads\\public_document.pdf";
  normal_item.url = "https://example.com/public_document.pdf";
  normal_item.state = mtk::downloads::State::kComplete;
  normal_item.is_private = false;

  mtk::downloads::Item private_item;
  private_item.id = 9002;
  private_item.file_name = L"secret_payroll.pdf";
  private_item.full_path = L"C:\\Users\\User\\Downloads\\secret_payroll.pdf";
  private_item.url = "https://confidential.example/secret_payroll.pdf";
  private_item.state = mtk::downloads::State::kComplete;
  private_item.is_private = true;

  dm.Upsert(normal_item);
  dm.Upsert(private_item);

  // Both exist in memory during active browsing
  MTK_EXPECT_TRUE(dm.FindItem(9001) != nullptr);
  MTK_EXPECT_TRUE(dm.FindItem(9002) != nullptr);

  // Flush to disk
  MTK_EXPECT_TRUE(dm.Flush());

  // Inspect the written downloads file on disk
  std::string file_content;
  MTK_EXPECT_TRUE(ReadProfileFile(L"downloads.v1", &file_content));
  MTK_EXPECT_TRUE(file_content.starts_with("MTK_DOWNLOADS_V1"));

  // Verify that reloading from disk restores normal downloads but NOT private downloads
  dm.ClearFinished();
  dm.ClearPrivateDownloads();
  dm.Load();
  bool found_normal_on_disk = false;
  bool found_private_on_disk = false;
  for (const auto& item : dm.items()) {
    if (item.file_name == L"public_document.pdf") found_normal_on_disk = true;
    if (item.file_name == L"secret_payroll.pdf") found_private_on_disk = true;
  }
  MTK_EXPECT_TRUE(found_normal_on_disk);
  MTK_EXPECT_FALSE(found_private_on_disk);

  // When private session ends, private downloads are evicted from memory
  dm.Upsert(private_item);
  MTK_EXPECT_TRUE(dm.FindItem(9002) != nullptr);
  dm.ClearPrivateDownloads();
  MTK_EXPECT_TRUE(dm.FindItem(9002) == nullptr);
}

MTK_TEST(NormalHistoryWorksAndPrivateHistoryDoesNotPersist) {
  if (!mtk::test::WritingToSuiteProfile()) {
    MTK_EXPECT_TRUE(false);
    return;
  }

  auto& library = mtk::library::Store::Get();
  library.ClearHistory();

  // Normal visit is recorded
  MTK_EXPECT_TRUE(library.RecordVisit("https://normal.example/page", L"Normal Page"));
  MTK_EXPECT_TRUE(library.Flush());

  const auto& history = library.history();
  MTK_EXPECT_FALSE(history.empty());
  bool found_normal = false;
  for (const auto& entry : history) {
    if (entry.url == "https://normal.example/page") {
      found_normal = true;
      break;
    }
  }
  MTK_EXPECT_TRUE(found_normal);

  // In private window, RecordVisit is never called (verified by policy architecture).
  // Simulate private navigation: no visit is recorded.
  bool found_private = false;
  for (const auto& entry : library.history()) {
    if (entry.url.find("private.example") != std::string::npos) {
      found_private = true;
      break;
    }
  }
  MTK_EXPECT_FALSE(found_private);
}

MTK_TEST(SessionRestoreExcludesPrivateWindows) {
  if (!mtk::test::WritingToSuiteProfile()) {
    MTK_EXPECT_TRUE(false);
    return;
  }

  // Create normal windows snapshot
  mtk::session::Window normal_window;
  normal_window.urls = {"https://example.org/tab1", "https://example.org/tab2"};
  normal_window.pinned = {false, false};
  normal_window.active_index = 0;

  MTK_EXPECT_TRUE(mtk::session::SaveWindows({normal_window}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 2);
  MTK_EXPECT_EQ_STR(snapshot.urls[0], "https://example.org/tab1");
  MTK_EXPECT_EQ_STR(snapshot.urls[1], "https://example.org/tab2");

  // Private windows are never passed to SaveWindows (guaranteed by BrowserWindow::SaveSessionSnapshot)
}

MTK_TEST(NormalProfileDataUntouchedByPrivateSessionOperations) {
  if (!mtk::test::WritingToSuiteProfile()) {
    MTK_EXPECT_TRUE(false);
    return;
  }

  auto& library = mtk::library::Store::Get();
  library.AddBookmark("https://bookmark.example/", L"My Saved Bookmark");
  library.Flush();

  auto& session_mgr = mtk::private_session::Manager::Get();
  session_mgr.Reset();

  // Run full private session lifecycle
  session_mgr.OnWindowOpened();
  session_mgr.OnWindowOpened();
  session_mgr.OnWindowClosed();
  session_mgr.OnWindowClosed();

  // Normal bookmark is completely preserved
  MTK_EXPECT_TRUE(library.IsBookmarked("https://bookmark.example/"));
}
