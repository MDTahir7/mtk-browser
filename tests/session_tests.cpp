#include "tab_preview.h"
#include "test_support.h"

#include <windows.h>

#include <string>

#include "memory_saver.h"
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

MTK_TEST(MultiWindowSessionRoundTripKeepsTabsPinsAndBounds) {
  ScopedProfile profile;
  mtk::session::Window first;
  first.urls = {"https://example.com/", "https://www.iana.org/"};
  first.pinned = {true, false};
  first.active_index = 1;
  first.left = -1200;
  first.top = 40;
  first.width = 1100;
  first.height = 800;
  first.maximized = false;
  mtk::session::Window second;
  second.urls = {"https://example.org/"};
  second.active_index = 0;
  second.left = 30;
  second.top = 70;
  second.width = 1200;
  second.height = 900;
  second.maximized = true;
  MTK_EXPECT_TRUE(mtk::session::SaveWindows({first, second}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.windows.size(), 2);
  if (snapshot.windows.size() != 2) return;
  MTK_EXPECT_EQ_INT(snapshot.windows[0].urls.size(), 2);
  MTK_EXPECT_TRUE(snapshot.windows[0].pinned[0]);
  MTK_EXPECT_EQ_INT(snapshot.windows[0].active_index, 1);
  MTK_EXPECT_EQ_INT(snapshot.windows[0].left, -1200);
  MTK_EXPECT_EQ_INT(snapshot.windows[0].width, 1100);
  MTK_EXPECT_TRUE(snapshot.windows[1].maximized);
  MTK_EXPECT_EQ_STR(snapshot.windows[1].urls[0], "https://example.org/");
  // Legacy callers still observe the first restored window without needing a
  // second code path.
  MTK_EXPECT_EQ_INT(snapshot.urls.size(), 2);
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

MTK_TEST(SessionRejectedRecordDoesNotChangePreviousTabGroupOrPin) {
  ScopedProfile profile;
  const std::wstring path = mtk::preferences::ApplicationDataDirectory() + L"\\session.v1";
  // First URL is https://example.com/, second is the rejected data:x URL.
  const std::string data = "MTK_SESSION_V3\nW\nU\t68747470733A2F2F6578616D706C652E636F6D2F\nG\t576F726B\n"
                           "U\t646174613A78\nP\t1\nG\t57726F6E67\n";
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  MTK_EXPECT_TRUE(file != INVALID_HANDLE_VALUE);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
  CloseHandle(file);
  MTK_EXPECT_TRUE(ok && written == data.size());
  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.windows.size(), 1);
  if (snapshot.windows.empty()) return;
  const auto& saved = snapshot.windows[0];
  MTK_EXPECT_EQ_INT(saved.urls.size(), 1);
  MTK_EXPECT_EQ_STR(saved.groups[0], "Work");
  MTK_EXPECT_FALSE(saved.pinned[0]);
}

MTK_TEST(SessionGroupsSurviveRestartAndUrlFiltering) {
  ScopedProfile profile;
  mtk::session::Window window;
  window.urls = {"https://example.com/", "data:text/html,x", "https://example.org/"};
  window.groups = {"Work", "Rejected", "Research"};
  MTK_EXPECT_TRUE(mtk::session::SaveWindows({window}));
  const auto loaded = mtk::session::Load();
  MTK_EXPECT_EQ_INT(loaded.windows.size(), 1);
  if (loaded.windows.empty()) return;
  const auto& groups = loaded.windows[0].groups;
  MTK_EXPECT_EQ_INT(groups.size(), 2);
  if (groups.size() != 2) return;
  MTK_EXPECT_EQ_STR(groups[0], "Work");
  MTK_EXPECT_EQ_STR(groups[1], "Research");
  window.groups = {"bad\nname", "", std::string(129, 'x')};
  MTK_EXPECT_TRUE(mtk::session::SaveWindows({window}));
  const auto sanitized = mtk::session::Load();
  MTK_EXPECT_TRUE(sanitized.windows[0].groups[0].empty());
  MTK_EXPECT_TRUE(sanitized.windows[0].groups[1].empty());
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

MTK_TEST(MemorySaverTabDiscardingEligibilityRules) {
  mtk::memory::TabState state;
  state.last_active_tick = 1000;
  const uint64_t now = 1000 + mtk::memory::kInactiveDiscardThresholdMs + 500;

  // Normal inactive background tab past threshold is eligible
  MTK_EXPECT_TRUE(mtk::memory::IsEligibleForDiscard(state, now));

  // Active tab cannot be discarded
  state.active = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
  state.active = false;

  // Split tab cannot be discarded
  state.split = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
  state.split = false;

  // Pinned tab cannot be discarded
  state.pinned = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
  state.pinned = false;

  // Loading tab cannot be discarded
  state.loading = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
  state.loading = false;

  // Media active tab cannot be discarded
  state.camera_active = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
  state.camera_active = false;

  state.microphone_active = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
  state.microphone_active = false;

  // Recently active tab (not past threshold) cannot be discarded automatically
  const uint64_t recent = 1000 + 5000;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, recent));

  // But with threshold = 0 (manual user action), recent tab can be discarded
  MTK_EXPECT_TRUE(mtk::memory::IsEligibleForDiscard(state, recent, 0));

  // Already discarded tab cannot be discarded again
  state.discarded = true;
  MTK_EXPECT_TRUE(!mtk::memory::IsEligibleForDiscard(state, now));
}

MTK_TEST(SessionWorkspacesSurviveRestartAndUrlFiltering) {
  ScopedProfile profile;
  mtk::session::Window window;
  window.urls = {"https://example.com/work", "https://example.com/personal"};
  window.pinned = {false, false};
  window.workspaces = {"Work", "Personal"};
  window.groups = {"Dev", "Finance"};
  window.active_index = 0;

  MTK_EXPECT_TRUE(mtk::session::SaveWindows({window}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.windows.size(), 1);
  if (!snapshot.windows.empty()) {
    const auto& restored = snapshot.windows.front();
    MTK_EXPECT_EQ_INT(restored.urls.size(), 2);
    MTK_EXPECT_EQ_INT(restored.workspaces.size(), 2);
    if (restored.workspaces.size() == 2) {
      MTK_EXPECT_EQ_STR(restored.workspaces[0], "Work");
      MTK_EXPECT_EQ_STR(restored.workspaces[1], "Personal");
    }
  }
}

MTK_TEST(SessionWorkspacesMultipleTabsAndEmptyWorkspacesPreserved) {
  ScopedProfile profile;
  mtk::session::Window window;
  window.urls = {
      "https://example.com/w1",
      "https://example.com/none",
      "https://example.com/w2",
      "https://example.com/w1_second"
  };
  window.pinned = {true, false, false, false};
  window.workspaces = {"Research", "", "Finance", "Research"};
  window.groups = {"G1", "", "G2", "G1"};
  window.active_index = 2;

  MTK_EXPECT_TRUE(mtk::session::SaveWindows({window}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.windows.size(), 1);
  if (!snapshot.windows.empty()) {
    const auto& restored = snapshot.windows.front();
    MTK_EXPECT_EQ_INT(restored.urls.size(), 4);
    MTK_EXPECT_EQ_INT(restored.workspaces.size(), 4);
    if (restored.workspaces.size() == 4) {
      MTK_EXPECT_EQ_STR(restored.workspaces[0], "Research");
      MTK_EXPECT_EQ_STR(restored.workspaces[1], "");
      MTK_EXPECT_EQ_STR(restored.workspaces[2], "Finance");
      MTK_EXPECT_EQ_STR(restored.workspaces[3], "Research");
    }
  }
}

MTK_TEST(SessionSplitViewPreservedAndClamped) {
  ScopedProfile profile;
  mtk::session::Window window;
  window.urls = {
      "https://example.com/left",
      "https://example.com/right"
  };
  window.pinned = {false, false};
  window.active_index = 0;
  window.split_index = 1;
  window.split_ratio = 65;

  MTK_EXPECT_TRUE(mtk::session::SaveWindows({window}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.windows.size(), 1);
  if (!snapshot.windows.empty()) {
    const auto& restored = snapshot.windows.front();
    MTK_EXPECT_EQ_INT(restored.urls.size(), 2);
    MTK_EXPECT_EQ_INT(restored.active_index, 0);
    MTK_EXPECT_EQ_INT(restored.split_index, 1);
    MTK_EXPECT_EQ_INT(restored.split_ratio, 65);
    MTK_EXPECT_EQ_INT(snapshot.split_index, 1);
    MTK_EXPECT_EQ_INT(snapshot.split_ratio, 65);
  }
}

MTK_TEST(SessionSplitViewInvalidIndexDiscarded) {
  ScopedProfile profile;
  mtk::session::Window window;
  window.urls = {
      "https://example.com/page1",
      "https://example.com/page2"
  };
  window.pinned = {false, false};
  window.active_index = 0;
  // Pointing to active_index or out-of-range must not restore as split.
  window.split_index = 0;
  window.split_ratio = 50;

  MTK_EXPECT_TRUE(mtk::session::SaveWindows({window}));

  const auto snapshot = mtk::session::Load();
  MTK_EXPECT_EQ_INT(snapshot.windows.size(), 1);
  if (!snapshot.windows.empty()) {
    const auto& restored = snapshot.windows.front();
    MTK_EXPECT_EQ_INT(restored.split_index, static_cast<size_t>(-1));
  }
}


MTK_TEST(TabPreviewFormattingAndGroupAccentColors) {
  mtk::tab_preview::PreviewCardData data;
  data.title = L"Research Paper";
  data.url = "https://scholar.example.com/paper?id=42";
  data.group = L"Study";
  data.is_https = true;
  data.sleeping = true;

  const std::wstring sub = mtk::tab_preview::FormatPreviewSubtitle(data);
  MTK_EXPECT_TRUE(sub.find(L"HTTPS") != std::wstring::npos);
  MTK_EXPECT_TRUE(sub.find(L"scholar.example.com") != std::wstring::npos);
  MTK_EXPECT_TRUE(sub.find(L"Sleeping") != std::wstring::npos);

  const COLORREF c1 = mtk::tab_preview::GroupAccentColor(L"Study");
  const COLORREF c2 = mtk::tab_preview::GroupAccentColor(L"study");
  MTK_EXPECT_EQ_INT(c1, c2);
}
