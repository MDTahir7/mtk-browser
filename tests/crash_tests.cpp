// Tests for the crash-recovery history trimming. The rest of crash_guard.cpp is
// file and marker handling, which is exercised by running the browser; this is
// the part with logic worth pinning down, and it is the part that decides whether
// an old record can push the newest one out.

#include <string>

#include "crash_guard.h"
#include "test_support.h"

namespace {

size_t CountLines(const std::string& text) {
  size_t lines = 0;
  size_t offset = 0;
  while (offset < text.size()) {
    const size_t end = text.find('\n', offset);
    const size_t length = (end == std::string::npos ? text.size() : end) - offset;
    std::string line = text.substr(offset, length);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) ++lines;
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  return lines;
}

}  // namespace

MTK_TEST(CrashHistoryPutsTheNewestEntryFirst) {
  const std::string history = "older-1\r\nolder-2\r\n";
  const std::string result = mtk::crash::TrimHistory(history, "newest", 10);
  MTK_EXPECT_TRUE(result.starts_with("newest\r\n"));
  MTK_EXPECT_TRUE(result.find("older-1") != std::string::npos);
  MTK_EXPECT_TRUE(result.find("older-2") != std::string::npos);
  MTK_EXPECT_EQ_INT(CountLines(result), 3);
}

MTK_TEST(CrashHistoryStaysBounded) {
  std::string history;
  for (int index = 0; index < 50; ++index) {
    history += "entry-" + std::to_string(index) + "\r\n";
  }
  const std::string result = mtk::crash::TrimHistory(history, "newest", 10);
  MTK_EXPECT_EQ_INT(CountLines(result), 10);
  // The newest entry survives and the oldest ones are the ones dropped.
  MTK_EXPECT_TRUE(result.starts_with("newest\r\n"));
  MTK_EXPECT_TRUE(result.find("entry-0\r\n") != std::string::npos);
  MTK_EXPECT_TRUE(result.find("entry-49") == std::string::npos);
}

MTK_TEST(CrashHistoryHandlesAnEmptyOrOddFile) {
  const std::string from_empty = mtk::crash::TrimHistory("", "first", 10);
  MTK_EXPECT_EQ_INT(CountLines(from_empty), 1);
  MTK_EXPECT_TRUE(from_empty.starts_with("first\r\n"));

  // Blank lines and a missing final newline must not produce empty rows.
  const std::string messy = mtk::crash::TrimHistory("a\r\n\r\n\r\nb", "new", 10);
  MTK_EXPECT_EQ_INT(CountLines(messy), 3);
}

MTK_TEST(CrashHistoryWithARoomOfOneKeepsOnlyTheNewest) {
  const std::string result =
      mtk::crash::TrimHistory("old-1\r\nold-2\r\n", "newest", 1);
  MTK_EXPECT_EQ_INT(CountLines(result), 1);
  MTK_EXPECT_TRUE(result.starts_with("newest"));
}
