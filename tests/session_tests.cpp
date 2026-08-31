#include "test_support.h"

#include <windows.h>

#include <string>

#include "preferences.h"
#include "session_store.h"

namespace {

// Session and preference paths are derived from LOCALAPPDATA, so a throwaway
// directory keeps these tests away from any real profile.
class ScopedProfile {
 public:
  ScopedProfile() {
    wchar_t temporary[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporary);
    root_ = std::wstring(temporary) + L"mtk-session-tests-" +
            std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(root_.c_str(), nullptr);
    previous_.resize(32767);
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", previous_.data(), static_cast<DWORD>(previous_.size()));
    previous_.resize(length);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root_.c_str());
    CreateDirectoryW((root_ + L"\\MTK Browser").c_str(), nullptr);
    mtk::session::Clear();
  }

  ~ScopedProfile() {
    mtk::session::Clear();
    SetEnvironmentVariableW(L"LOCALAPPDATA",
                            previous_.empty() ? nullptr : previous_.c_str());
    RemoveDirectoryW((root_ + L"\\MTK Browser").c_str());
    RemoveDirectoryW(root_.c_str());
  }

 private:
  std::wstring root_;
  std::wstring previous_;
};

}  // namespace

MTK_TEST(SessionRoundTripKeepsOrderAndActiveTab) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(mtk::session::Save(
      {"https://example.com/", "https://www.iana.org/"}, 1));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 2);
  if (snapshot.urls.size() == 2) {
    MTK_EXPECT_EQ_STR(snapshot.urls[0], "https://example.com/");
    MTK_EXPECT_EQ_STR(snapshot.urls[1], "https://www.iana.org/");
  }
  MTK_EXPECT_EQ_INT(snapshot.active_index, 1);
}

MTK_TEST(SessionDropsUnsafeUrls) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(mtk::session::Save(
      {"https://example.com/", "data:text/html,<h1>block</h1>",
       "https://www.pornhub.com/", "http://127.0.0.1/", "about:blank"},
      0));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 1);
  if (!snapshot.urls.empty()) {
    MTK_EXPECT_EQ_STR(snapshot.urls[0], "https://example.com/");
  }
}

MTK_TEST(SessionActiveIndexStaysInRangeAfterFiltering) {
  ScopedProfile profile;
  // The third entry is the active one, but the two before it are dropped, so
  // the restored index must clamp to the single surviving tab.
  MTK_EXPECT_TRUE(mtk::session::Save(
      {"data:text/html,x", "http://10.0.0.1/", "https://example.com/"}, 2));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 1);
  MTK_EXPECT_EQ_INT(snapshot.active_index, 0);
}

MTK_TEST(SessionSaveWithNoSafeUrlsClearsSnapshot) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(mtk::session::Save({"https://example.com/"}, 0));
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(), 1);

  // Every URL is unsafe, so the stored snapshot is removed rather than kept.
  MTK_EXPECT_TRUE(mtk::session::Save({"data:text/html,x"}, 0));
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(), 0);
}

MTK_TEST(SessionIsCappedAtTheDocumentedTabLimit) {
  ScopedProfile profile;
  std::vector<std::string> urls;
  for (size_t index = 0; index < mtk::session::kMaxRestoredTabs + 8; ++index) {
    urls.push_back("https://example.com/page" + std::to_string(index));
  }
  MTK_EXPECT_TRUE(mtk::session::Save(urls, 0));
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(),
                    mtk::session::kMaxRestoredTabs);
}

MTK_TEST(SessionClearRemovesSnapshot) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(mtk::session::Save({"https://example.com/"}, 0));
  MTK_EXPECT_TRUE(mtk::session::Clear());
  MTK_EXPECT_EQ_INT(mtk::session::Load().urls.size(), 0);
  // Clearing an absent snapshot is not an error.
  MTK_EXPECT_TRUE(mtk::session::Clear());
}

MTK_TEST(SessionLoadOnEmptyProfileReturnsNothing) {
  ScopedProfile profile;
  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 0);
  MTK_EXPECT_EQ_INT(snapshot.active_index, 0);
}

MTK_TEST(SessionUrlsAreNormalisedThroughPolicy) {
  ScopedProfile profile;
  // http is upgraded, so the restored URL is the https form.
  MTK_EXPECT_TRUE(mtk::session::Save({"http://example.com/page"}, 0));
  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 1);
  if (!snapshot.urls.empty()) {
    MTK_EXPECT_EQ_STR(snapshot.urls[0], "https://example.com/page");
  }
}

MTK_TEST(SessionPinnedStateRoundTrips) {
  ScopedProfile profile;
  MTK_EXPECT_TRUE(mtk::session::Save(
      {"https://example.com/", "https://www.iana.org/", "https://example.org/"},
      1, {true, false, true}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 3);
  MTK_EXPECT_EQ_INT(snapshot.pinned.size(), 3);
  if (snapshot.pinned.size() == 3) {
    MTK_EXPECT_TRUE(snapshot.pinned[0]);
    MTK_EXPECT_TRUE(!snapshot.pinned[1]);
    MTK_EXPECT_TRUE(snapshot.pinned[2]);
  }
}

MTK_TEST(SessionMissingPinnedArgumentRestoresEverythingUnpinned) {
  ScopedProfile profile;
  // Callers that do not pass `pinned` at all (or pass fewer entries than
  // `urls`) must not crash, and every such tab restores unpinned.
  MTK_EXPECT_TRUE(mtk::session::Save({"https://example.com/"}, 0));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 1);
  MTK_EXPECT_EQ_INT(snapshot.pinned.size(), 1);
  if (!snapshot.pinned.empty()) MTK_EXPECT_TRUE(!snapshot.pinned[0]);
}

MTK_TEST(SessionPinnedIndexesFollowSurvivingUrlsAfterFiltering) {
  ScopedProfile profile;
  // The unsafe entry at index 1 is dropped; its neighbour's pinned flag must
  // stay attached to the URL it belongs to, not shift into the gap.
  MTK_EXPECT_TRUE(mtk::session::Save(
      {"https://example.com/", "data:text/html,x", "https://example.org/"}, 0,
      {false, true, true}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 2);
  if (snapshot.urls.size() == 2 && snapshot.pinned.size() == 2) {
    MTK_EXPECT_EQ_STR(snapshot.urls[0], "https://example.com/");
    MTK_EXPECT_TRUE(!snapshot.pinned[0]);
    MTK_EXPECT_EQ_STR(snapshot.urls[1], "https://example.org/");
    MTK_EXPECT_TRUE(snapshot.pinned[1]);
  }
}
