#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "library_store.h"

namespace mtk::history {

// Second browser-owned document on the existing `mtk` scheme. Only this exact
// path is served; the search box adds one allowlisted query parameter.
inline constexpr char kHost[] = "history";
inline constexpr char kUrl[] = "mtk://history/";
inline constexpr wchar_t kUrlWide[] = L"mtk://history/";

// Longest search text accepted from the document URL. The box is for finding a
// page again, not for carrying arbitrary payloads.
inline constexpr size_t kMaxQueryLength = 200;

enum class ActionKind {
  kNone,
  kRemoveEntry,
  kClearDay,
  kClearAll,
};

struct Action {
  ActionKind kind = ActionKind::kNone;
  // Position in the store's history vector.
  size_t index = 0;
  // The row's stored timestamp, carried so a list that shifted between render
  // and click cannot make the wrong entry be deleted.
  long long visited = 0;
  // Local calendar day, `YYYY-MM-DD`, for kClearDay.
  std::string day;
};

// True only for the exact document, optionally with a single `q` parameter.
// Other hosts, paths, parameters and fragments are refused.
bool IsHistoryUrl(std::string_view url);

// Percent-decoded search text from the document URL, empty when absent or
// unusable. Never longer than kMaxQueryLength.
std::string QueryOf(std::string_view url);

// Allowlisted internal actions, intercepted before CEF asks for a resource.
// Anything unrecognised fails closed as kNone.
Action ParseActionUrl(std::string_view url);

// Local calendar day of a stored Unix timestamp as `YYYY-MM-DD`. The page and
// the "clear this day" action must group by the same rule, so both call this.
std::string DayKeyOf(long long visited);

// One self-contained document: no remote scripts, images, fonts or stylesheets,
// and no script of its own. `private_session` renders the empty state, because a
// private window must not display the on-disk profile's browsing history.
std::string BuildHtml(const std::vector<library::Entry>& history,
                      std::string_view query, bool private_session);

// The store may only be read on the browser UI thread, so a copy is published
// here and the CEF IO-thread scheme factory renders from that copy.
void RefreshSnapshot(const std::vector<library::Entry>& history);
std::vector<library::Entry> Snapshot();

}  // namespace mtk::history
