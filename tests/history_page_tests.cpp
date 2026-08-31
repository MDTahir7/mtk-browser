#include "test_support.h"

#include <windows.h>

#include <string>
#include <vector>

#include "history_page.h"

namespace {

using mtk::history::Action;
using mtk::history::ActionKind;

// A visit recorded at a known local time, so grouping and the day key can be
// checked without depending on when the suite runs.
long long LocalUnixTime(WORD year, WORD month, WORD day, WORD hour,
                        WORD minute) {
  SYSTEMTIME local{};
  local.wYear = year;
  local.wMonth = month;
  local.wDay = day;
  local.wHour = hour;
  local.wMinute = minute;
  FILETIME local_file{};
  FILETIME utc{};
  if (!SystemTimeToFileTime(&local, &local_file)) return 0;
  if (!LocalFileTimeToFileTime(&local_file, &utc)) return 0;
  ULARGE_INTEGER value{};
  value.LowPart = utc.dwLowDateTime;
  value.HighPart = utc.dwHighDateTime;
  constexpr unsigned long long kEpochDelta = 116444736000000000ULL;
  if (value.QuadPart < kEpochDelta) return 0;
  return static_cast<long long>((value.QuadPart - kEpochDelta) / 10000000ULL);
}

long long TodayAt(WORD hour, WORD minute) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  return LocalUnixTime(now.wYear, now.wMonth, now.wDay, hour, minute);
}

std::vector<mtk::library::Entry> SampleHistory() {
  return {
      {"https://www.example.com/docs", L"Example Docs", TodayAt(9, 30)},
      {"https://news.example.org/story", L"A Story", TodayAt(8, 15)},
      {"https://www.example.net/old",
       L"Older Page", LocalUnixTime(2026, 1, 14, 20, 5)},
  };
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

MTK_TEST(HistoryUrlAcceptsOnlyTheExactDocumentAndOneSearchParameter) {
  MTK_EXPECT_TRUE(mtk::history::IsHistoryUrl("mtk://history/"));
  MTK_EXPECT_TRUE(mtk::history::IsHistoryUrl("mtk://history/?q="));
  MTK_EXPECT_TRUE(mtk::history::IsHistoryUrl("mtk://history/?q=example+docs"));

  // Lookalikes, extra parameters, fragments and paths all fail closed.
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://historyx/"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/index"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/?p=1"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/?q=a&p=1"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/?q=a#top"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("https://history/"));
}

MTK_TEST(HistoryQueryIsDecodedBoundedAndFreeOfControlCharacters) {
  MTK_EXPECT_EQ_STR(mtk::history::QueryOf("mtk://history/?q=example+docs"),
                    "example docs");
  MTK_EXPECT_EQ_STR(mtk::history::QueryOf("mtk://history/?q=a%2Bb"), "a+b");
  MTK_EXPECT_EQ_STR(mtk::history::QueryOf("mtk://history/"), "");

  // A newline would let a value break out of the context it is placed in.
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/?q=a%0Ab"));
  MTK_EXPECT_EQ_STR(mtk::history::QueryOf("mtk://history/?q=a%0Ab"), "");
  // Truncated and malformed escapes are refused rather than repaired.
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/?q=a%2"));
  MTK_EXPECT_FALSE(mtk::history::IsHistoryUrl("mtk://history/?q=a%zz"));

  const std::string long_query(mtk::history::kMaxQueryLength + 1, 'a');
  MTK_EXPECT_FALSE(
      mtk::history::IsHistoryUrl("mtk://history/?q=" + long_query));
}

MTK_TEST(HistoryActionsAreAnAllowlistAndCarryValidatedValues) {
  const Action clear_all =
      mtk::history::ParseActionUrl("mtk://history/action/clear-all");
  MTK_EXPECT_TRUE(clear_all.kind == ActionKind::kClearAll);

  const Action day = mtk::history::ParseActionUrl(
      "mtk://history/action/clear-day?d=2026-01-14");
  MTK_EXPECT_TRUE(day.kind == ActionKind::kClearDay);
  MTK_EXPECT_EQ_STR(day.day, "2026-01-14");

  const Action remove =
      mtk::history::ParseActionUrl("mtk://history/action/remove?i=4&t=1750000000");
  MTK_EXPECT_TRUE(remove.kind == ActionKind::kRemoveEntry);
  MTK_EXPECT_EQ_INT(remove.index, 4);
  MTK_EXPECT_EQ_INT(remove.visited, 1750000000);

  // Anything outside the allowlist, and any malformed value, is not an action.
  const char* refused[] = {
      "mtk://history/",
      "mtk://history/action/",
      "mtk://history/action/clear",
      "mtk://history/action/clear-all?d=1",
      "mtk://history/action/clear-day?d=2026-1-4",
      "mtk://history/action/clear-day?d=../../secret",
      "mtk://history/action/remove?i=4",
      "mtk://history/action/remove?i=&t=1",
      "mtk://history/action/remove?i=-1&t=1",
      "mtk://history/action/remove?i=1&t=abc",
      "https://history/action/clear-all",
  };
  for (const char* url : refused) {
    MTK_EXPECT_TRUE(mtk::history::ParseActionUrl(url).kind == ActionKind::kNone);
  }
}

MTK_TEST(HistoryDayKeyFollowsTheLocalCalendarDay) {
  MTK_EXPECT_EQ_STR(mtk::history::DayKeyOf(LocalUnixTime(2026, 1, 14, 20, 5)),
                    "2026-01-14");
  // A visit just before local midnight belongs to that day, not the next one.
  MTK_EXPECT_EQ_STR(mtk::history::DayKeyOf(LocalUnixTime(2026, 3, 9, 23, 59)),
                    "2026-03-09");
  MTK_EXPECT_EQ_STR(mtk::history::DayKeyOf(0), "");
  MTK_EXPECT_EQ_STR(mtk::history::DayKeyOf(-1), "");
}

MTK_TEST(HistoryPageGroupsByDayAndOffersPerDayRemoval) {
  const std::string html =
      mtk::history::BuildHtml(SampleHistory(), "", false);
  MTK_EXPECT_TRUE(Contains(html, "Today"));
  MTK_EXPECT_TRUE(Contains(html, "14 January 2026"));
  MTK_EXPECT_TRUE(Contains(html, "2 pages"));
  MTK_EXPECT_TRUE(Contains(html, "1 page"));
  MTK_EXPECT_TRUE(
      Contains(html, "mtk://history/action/clear-day?d=2026-01-14"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://history/action/clear-all"));
  // Rows link to the visited address and carry the timestamp cross-check.
  MTK_EXPECT_TRUE(Contains(html, "https://www.example.com/docs"));
  MTK_EXPECT_TRUE(Contains(html, "action/remove?i=0&amp;t="));
}

MTK_TEST(HistoryPageSearchFiltersRowsAndReportsNoMatch) {
  const std::string found =
      mtk::history::BuildHtml(SampleHistory(), "story", false);
  MTK_EXPECT_TRUE(Contains(found, "news.example.org"));
  MTK_EXPECT_FALSE(Contains(found, "www.example.com"));

  // Matching is case-insensitive and also looks at the address.
  const std::string by_host =
      mtk::history::BuildHtml(SampleHistory(), "EXAMPLE.NET", false);
  MTK_EXPECT_TRUE(Contains(by_host, "Older Page"));
  MTK_EXPECT_FALSE(Contains(by_host, "A Story"));

  const std::string missing =
      mtk::history::BuildHtml(SampleHistory(), "zzzzz", false);
  MTK_EXPECT_TRUE(Contains(missing, "Nothing matched that search"));
}

MTK_TEST(HistoryPageEscapesStoredTitlesAndAddresses) {
  std::vector<mtk::library::Entry> history{
      {"https://www.example.com/?a=1&b=2",
       L"<script>alert('x')</script>", TodayAt(10, 0)},
  };
  const std::string html = mtk::history::BuildHtml(history, "", false);
  MTK_EXPECT_FALSE(Contains(html, "<script>alert"));
  MTK_EXPECT_TRUE(Contains(html, "&lt;script&gt;"));
  MTK_EXPECT_TRUE(Contains(html, "a=1&amp;b=2"));
}

MTK_TEST(HistoryPageShowsNothingInAPrivateWindow) {
  const std::string html = mtk::history::BuildHtml(SampleHistory(), "", true);
  MTK_EXPECT_TRUE(Contains(html, "Not shown in a private window"));
  MTK_EXPECT_FALSE(Contains(html, "www.example.com"));
  // Nothing to delete, so no destructive action is offered either.
  MTK_EXPECT_FALSE(Contains(html, "action/clear-day"));
}

MTK_TEST(HistoryPageDoesNotLinkAddressesTheSafetyPolicyRefuses) {
  std::vector<mtk::library::Entry> history{
      {"https://www.example.com/fine", L"Fine", TodayAt(11, 0)},
      {"file:///C:/Windows/win.ini", L"Local file", TodayAt(11, 5)},
      {"http://127.0.0.1/admin", L"Loopback", TodayAt(11, 10)},
  };
  const std::string html = mtk::history::BuildHtml(history, "", false);
  MTK_EXPECT_TRUE(Contains(html, "https://www.example.com/fine"));
  MTK_EXPECT_FALSE(Contains(html, "file:///"));
  MTK_EXPECT_FALSE(Contains(html, "127.0.0.1"));
  MTK_EXPECT_TRUE(Contains(html, "safety rules would refuse"));
}

MTK_TEST(HistoryPageCarriesNoScriptAndNoRemoteReference) {
  const std::string html =
      mtk::history::BuildHtml(SampleHistory(), "", false);
  MTK_EXPECT_FALSE(Contains(html, "<script"));
  MTK_EXPECT_FALSE(Contains(html, "cefQuery"));
  MTK_EXPECT_FALSE(Contains(html, "http://fonts"));
  MTK_EXPECT_TRUE(Contains(html, "script-src 'none'"));
  // The search box may only submit back to the document itself.
  MTK_EXPECT_TRUE(Contains(html, "form-action 'self'"));
}
