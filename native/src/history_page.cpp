#include "history_page.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>

#include "favicon_store.h"
#include "home_page.h"
#include "policy.h"

namespace mtk::history {
namespace {

std::mutex g_snapshot_mutex;
std::vector<library::Entry> g_snapshot;

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

// Only http(s) addresses the safety policy still accepts become links, so a rule
// added after a visit was recorded also applies to this page.
std::string SafeUrl(const std::string& raw) {
  const auto decision = policy::EvaluateNavigation(raw);
  if (!decision.allow || (!decision.url.starts_with("https://") &&
                          !decision.url.starts_with("http://"))) return {};
  return decision.url;
}

std::string Lowered(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return result;
}

int HexValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

// Form-encoded value from the document's own URL. Control characters are refused
// rather than stripped, so a malformed value produces no search at all.
bool DecodeFormValue(std::string_view raw, std::string* decoded) {
  if (raw.size() > kMaxQueryLength * 3) return false;
  std::string result;
  result.reserve(raw.size());
  for (size_t index = 0; index < raw.size(); ++index) {
    const char character = raw[index];
    if (character == '+') {
      result.push_back(' ');
      continue;
    }
    if (character == '%') {
      if (index + 2 >= raw.size()) return false;
      const int high = HexValue(raw[index + 1]);
      const int low = HexValue(raw[index + 2]);
      if (high < 0 || low < 0) return false;
      result.push_back(static_cast<char>(high * 16 + low));
      index += 2;
      continue;
    }
    result.push_back(character);
  }
  if (result.size() > kMaxQueryLength) return false;
  for (const unsigned char character : result) {
    if (character < 0x20 || character == 0x7f) return false;
  }
  *decoded = std::move(result);
  return true;
}

bool AllDigits(std::string_view value) {
  return !value.empty() && value.size() <= 19 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isdigit(character) != 0;
         });
}

bool ParseUnsigned(std::string_view value, unsigned long long* out) {
  if (!AllDigits(value)) return false;
  unsigned long long result = 0;
  for (const char character : value) {
    result = result * 10 + static_cast<unsigned long long>(character - '0');
  }
  *out = result;
  return true;
}

// `YYYY-MM-DD` exactly. The value reaches a delete path, so its shape is checked
// before it is compared with anything.
bool IsDayKey(std::string_view value) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
  for (const size_t position : {0, 1, 2, 3, 5, 6, 8, 9}) {
    if (std::isdigit(static_cast<unsigned char>(value[position])) == 0) {
      return false;
    }
  }
  return true;
}

bool LocalTimeOf(long long visited, SYSTEMTIME* local) {
  if (visited <= 0) return false;
  // Unix epoch in 100-nanosecond units since 1601-01-01.
  constexpr long long kEpochDelta = 116444736000000000LL;
  const long long ticks = visited * 10000000LL + kEpochDelta;
  if (ticks < 0) return false;
  FILETIME utc{};
  utc.dwLowDateTime = static_cast<DWORD>(ticks & 0xffffffffLL);
  utc.dwHighDateTime = static_cast<DWORD>((ticks >> 32) & 0xffffffffLL);
  FILETIME local_file{};
  if (!FileTimeToLocalFileTime(&utc, &local_file)) return false;
  return FileTimeToSystemTime(&local_file, local) != FALSE;
}

std::string TodayKey() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char value[16]{};
  std::snprintf(value, sizeof(value), "%04u-%02u-%02u", now.wYear, now.wMonth,
                now.wDay);
  return value;
}

std::string YesterdayKey() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  FILETIME file{};
  if (!SystemTimeToFileTime(&now, &file)) return {};
  ULARGE_INTEGER value{};
  value.LowPart = file.dwLowDateTime;
  value.HighPart = file.dwHighDateTime;
  constexpr unsigned long long kDay = 24ULL * 60 * 60 * 10000000ULL;
  if (value.QuadPart < kDay) return {};
  value.QuadPart -= kDay;
  file.dwLowDateTime = value.LowPart;
  file.dwHighDateTime = value.HighPart;
  SYSTEMTIME yesterday{};
  if (!FileTimeToSystemTime(&file, &yesterday)) return {};
  char text[16]{};
  std::snprintf(text, sizeof(text), "%04u-%02u-%02u", yesterday.wYear,
                yesterday.wMonth, yesterday.wDay);
  return text;
}

std::string GroupLabel(const std::string& day_key, const SYSTEMTIME& local,
                       const std::string& today, const std::string& yesterday) {
  if (!today.empty() && day_key == today) return "Today";
  if (!yesterday.empty() && day_key == yesterday) return "Yesterday";
  static constexpr const char* kMonths[] = {"January", "February", "March",
      "April", "May", "June", "July", "August", "September", "October",
      "November", "December"};
  char value[64]{};
  std::snprintf(value, sizeof(value), "%u %s %u", local.wDay,
                kMonths[std::clamp<int>(local.wMonth, 1, 12) - 1],
                local.wYear);
  return value;
}

std::string ClockText(const SYSTEMTIME& local) {
  const int hour = local.wHour % 12 == 0 ? 12 : local.wHour % 12;
  char value[16]{};
  std::snprintf(value, sizeof(value), "%02d:%02u %s", hour, local.wMinute,
                local.wHour < 12 ? "am" : "pm");
  return value;
}

std::string Initial(std::string_view title) {
  for (const unsigned char character : title) {
    if (std::isalnum(character)) {
      return std::string(1, static_cast<char>(std::toupper(character)));
    }
  }
  return "M";
}

std::string Empty(std::string_view title, std::string_view detail) {
  return "<div class=empty><b>" + home::EscapeHtml(title) + "</b>" +
         home::EscapeHtml(detail) + "</div>";
}

struct Row {
  size_t index = 0;
  long long visited = 0;
  std::string url;
  std::string title;
  std::string host;
  std::string day_key;
  SYSTEMTIME local{};
};

}  // namespace

bool IsHistoryUrl(std::string_view url) {
  if (url == kUrl) return true;
  const std::string_view prefix(kUrl);
  if (!url.starts_with(prefix)) return false;
  std::string_view rest = url.substr(prefix.size());
  if (!rest.starts_with("?q=")) return false;
  rest.remove_prefix(3);
  // One parameter only: no second field, no fragment, no path traversal.
  if (rest.find_first_of("&#/?") != std::string_view::npos) return false;
  std::string decoded;
  return DecodeFormValue(rest, &decoded);
}

std::string QueryOf(std::string_view url) {
  if (!IsHistoryUrl(url) || url == kUrl) return {};
  std::string_view rest = url.substr(std::string_view(kUrl).size() + 3);
  std::string decoded;
  if (!DecodeFormValue(rest, &decoded)) return {};
  return decoded;
}

Action ParseActionUrl(std::string_view url) {
  Action action;
  constexpr std::string_view kClearAll = "mtk://history/action/clear-all";
  constexpr std::string_view kClearDay = "mtk://history/action/clear-day?d=";
  constexpr std::string_view kRemove = "mtk://history/action/remove?i=";

  if (url == kClearAll) {
    action.kind = ActionKind::kClearAll;
    return action;
  }
  if (url.starts_with(kClearDay)) {
    const std::string_view day = url.substr(kClearDay.size());
    if (!IsDayKey(day)) return action;
    action.kind = ActionKind::kClearDay;
    action.day = std::string(day);
    return action;
  }
  if (url.starts_with(kRemove)) {
    std::string_view rest = url.substr(kRemove.size());
    const size_t separator = rest.find("&t=");
    if (separator == std::string_view::npos) return action;
    unsigned long long index = 0;
    unsigned long long visited = 0;
    if (!ParseUnsigned(rest.substr(0, separator), &index)) return action;
    if (!ParseUnsigned(rest.substr(separator + 3), &visited)) return action;
    action.kind = ActionKind::kRemoveEntry;
    action.index = static_cast<size_t>(index);
    action.visited = static_cast<long long>(visited);
    return action;
  }
  return action;
}

std::string DayKeyOf(long long visited) {
  SYSTEMTIME local{};
  if (!LocalTimeOf(visited, &local)) return {};
  char value[16]{};
  std::snprintf(value, sizeof(value), "%04u-%02u-%02u", local.wYear,
                local.wMonth, local.wDay);
  return value;
}

std::string BuildHtml(const std::vector<library::Entry>& history,
                      std::string_view query, bool private_session) {
  const std::string lowered_query = Lowered(query);

  std::vector<Row> rows;
  size_t skipped = 0;
  if (!private_session) {
    for (size_t index = 0; index < history.size(); ++index) {
      const library::Entry& entry = history[index];
      const std::string url = SafeUrl(entry.url);
      if (url.empty()) {
        ++skipped;
        continue;
      }
      Row row;
      row.index = index;
      row.visited = entry.visited;
      row.url = url;
      row.title = WideToUtf8(entry.title);
      row.host = policy::HostOf(url);
      if (row.title.empty()) row.title = row.host;
      if (!lowered_query.empty() &&
          Lowered(row.title).find(lowered_query) == std::string::npos &&
          Lowered(row.url).find(lowered_query) == std::string::npos) {
        continue;
      }
      if (!LocalTimeOf(row.visited, &row.local)) continue;
      row.day_key = DayKeyOf(row.visited);
      if (row.day_key.empty()) continue;
      rows.push_back(std::move(row));
    }
  }

  const std::string today = TodayKey();
  const std::string yesterday = YesterdayKey();

  std::string groups;
  size_t position = 0;
  while (position < rows.size()) {
    const std::string& day_key = rows[position].day_key;
    size_t end = position;
    while (end < rows.size() && rows[end].day_key == day_key) ++end;
    const size_t count = end - position;
    groups += "<section class=day><h2>" +
        home::EscapeHtml(GroupLabel(day_key, rows[position].local, today,
                                    yesterday)) +
        "<span class=count>" + std::to_string(count) +
        (count == 1 ? " page" : " pages") +
        "</span><a class=wipe href=\"mtk://history/action/clear-day?d=" +
        home::EscapeHtml(day_key) + "\">Remove this day</a></h2>";
    for (size_t index = position; index < end; ++index) {
      const Row& row = rows[index];
      const std::string icon_url = mtk::favicon::GetFaviconDataUrl(row.host);
      std::string icon_html;
      if (!icon_url.empty()) {
        icon_html = "<span class=\"tile tile-has-icon\"><img src=\"" +
                    home::EscapeHtml(icon_url) + "\" class=tile-icon alt=\"\"></span>";
      } else {
        icon_html = "<span class=\"tile tile-fallback\">" +
                    home::EscapeHtml(Initial(row.title)) + "</span>";
      }
      groups += "<div class=row><a class=open href=\"" +
          home::EscapeHtml(row.url) + "\">" + icon_html +
          "<span class=text><b>" +
          home::EscapeHtml(row.title) + "</b><small>" +
          home::EscapeHtml(row.host) + "</small></span></a><span class=when>" +
          home::EscapeHtml(ClockText(row.local)) +
          "</span><a class=drop href=\"mtk://history/action/remove?i=" +
          std::to_string(row.index) + "&amp;t=" +
          std::to_string(row.visited) + "\" title=\"Remove this entry\">✕</a></div>";
    }
    groups += "</section>";
    position = end;
  }

  if (private_session) {
    groups = Empty("Not shown in a private window",
        "This window records nothing, and it does not display the browsing "
        "history saved in your ordinary profile.");
  } else if (rows.empty() && !query.empty()) {
    groups = Empty("Nothing matched that search",
        "Try a shorter word, or clear the box to see everything again.");
  } else if (rows.empty()) {
    groups = Empty("No history yet",
        "Pages you open will be listed here. Private windows are never "
        "included.");
  }

  static constexpr char kHead[] = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><meta name=referrer content=no-referrer>
<meta http-equiv=Content-Security-Policy content="default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; object-src 'none'; frame-src 'none'; connect-src 'none'; img-src data:; font-src 'none'; script-src 'none'">
<title>History — MTK Browser</title><style>
:root{color-scheme:dark;--bg:#050817;--panel:#10172d;--line:#222b48;--text:#f5f7ff;--muted:#9da8c3;--dim:#687596;--accent:#8b5cf6}*{box-sizing:border-box}html,body{margin:0;min-height:100%}body{background:radial-gradient(circle at 70% -10%,#141d4f 0,transparent 38%),var(--bg);color:var(--text);font:14px/1.45 'Segoe UI Variable','Segoe UI',system-ui,sans-serif}a{color:inherit;text-decoration:none}.wrap{max-width:860px;margin:auto;padding:26px 18px 44px}header{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:16px}header h1{margin:0;font-size:21px;font-weight:620}.crumb{color:var(--dim);font-size:11px}.crumb a{color:#a989ff}.tools{display:flex;gap:8px;margin:0 0 18px;flex-wrap:wrap}form.find{position:relative;flex:1;min-width:220px}form.find:before{content:'⌕';position:absolute;left:14px;top:8px;color:var(--dim);font-size:17px}form.find input{width:100%;height:38px;border:1px solid rgba(183,153,255,.34);border-radius:19px;background:rgba(13,18,48,.8);color:#fff;padding:0 14px 0 38px;font-size:12px;outline:none}form.find input:focus{border-color:#aa7cff;box-shadow:0 0 0 3px rgba(139,92,246,.18)}.btn{height:38px;padding:0 14px;display:inline-flex;align-items:center;border:1px solid var(--line);border-radius:19px;background:var(--panel);color:var(--muted);font-size:11px}.btn:hover{border-color:var(--accent);color:#fff}.btn.danger:hover{border-color:#e5606a;color:#ffd9dc}.hint{color:var(--dim);font-size:10.5px;margin:0 0 16px}.day{background:linear-gradient(145deg,rgba(17,24,48,.96),rgba(11,17,36,.97));border:1px solid rgba(86,99,151,.23);border-radius:12px;padding:12px 13px;margin-bottom:11px}.day h2{margin:0 0 9px;font-size:12px;font-weight:600;display:flex;align-items:center;gap:9px}.count{color:var(--dim);font-size:9.5px;font-weight:400}.wipe{margin-left:auto;color:#a989ff;font-size:9.5px}.wipe:hover{color:#e2b3bb}.row{display:grid;grid-template-columns:minmax(0,1fr) auto 22px;align-items:center;gap:8px;border-radius:8px;padding:3px 4px}.row:hover{background:#18213d}.open{display:grid;grid-template-columns:26px minmax(0,1fr);align-items:center;gap:9px;min-width:0}.text{min-width:0}.text b,.text small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.text b{font-size:11.5px;font-weight:520}.text small{font-size:9px;color:var(--dim)}.tile{width:26px;height:26px;border-radius:7px;display:grid;place-items:center;background:rgba(255,255,255,.06);font-size:9.5px;font-weight:700;overflow:hidden;flex-shrink:0;transition:transform .2s cubic-bezier(.34,1.56,.64,1)}.row:hover .tile{transform:scale(1.12)}.tile.tile-fallback{background:linear-gradient(145deg,#7658ed,#4331a7);color:#fff}.tile-has-icon{background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.1)}.tile-icon{width:18px;height:18px;object-fit:contain;display:block}.when{color:var(--dim);font-size:9.5px;white-space:nowrap}.drop{width:20px;height:20px;display:grid;place-items:center;border-radius:6px;color:#6b7899;font-size:10px}.drop:hover{background:#3a1d28;color:#ffc4cb}.empty{border:1px dashed #283252;border-radius:12px;padding:34px 18px;text-align:center;color:var(--dim);font-size:11.5px}.empty b{display:block;color:#a8b2c9;font-size:13px;margin-bottom:5px}@media(prefers-color-scheme:light){:root{color-scheme:light;--bg:#f5f7fc;--panel:#fff;--line:#dbe1ef;--text:#192033;--muted:#5c687f;--dim:#788399;--accent:#6941c6}body{background:radial-gradient(circle at 70% -10%,#e0e7ff 0,transparent 38%),var(--bg)}form.find input{background:#fff;color:#1d2440;border-color:#bca8ff}.btn{background:var(--panel);border-color:var(--line);color:var(--muted)}.day{background:linear-gradient(145deg,#fff,#f7f8fc);border-color:#dce1ed;box-shadow:0 5px 16px rgba(40,52,84,.07)}.row:hover{background:#edf0f8}.tile-has-icon{background:#f1f4fa;border:1px solid rgba(0,0,0,.08)}.drop:hover{background:#fee2e2;color:#dc2626}}@media(max-width:560px){.wrap{padding:18px 12px 34px}.when{display:none}}
</style></head><body><div class=wrap>)HTML";

  std::string html = kHead;
  html += "<header><h1>History</h1><span class=crumb><a href=\"";
  html += home::kUrl;
  html += "\">← Home</a></span></header><div class=tools>";
  html += "<form class=find method=get action=\"";
  html += kUrl;
  html += "\"><input name=q type=search value=\"";
  html += home::EscapeHtml(query);
  html += "\" placeholder=\"Search history\" autocomplete=off spellcheck=false "
          "maxlength=200 aria-label=\"Search history\"></form>";
  if (!query.empty()) {
    html += "<a class=btn href=\"";
    html += kUrl;
    html += "\">Clear search</a>";
  }
  if (!private_session && !history.empty()) {
    html += "<a class=\"btn danger\" href=\"mtk://history/action/clear-all\">"
            "Remove everything</a>";
  }
  html += "</div>";
  if (skipped > 0) {
    html += "<p class=hint>" + std::to_string(skipped) +
        (skipped == 1 ? " saved address is" : " saved addresses are") +
        " not shown because the current safety rules would refuse to open"
        " them.</p>";
  }
  html += groups;
  html += "</div></body></html>";
  return html;
}

void RefreshSnapshot(const std::vector<library::Entry>& history) {
  std::vector<library::Entry> copy = history;
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot = std::move(copy);
}

std::vector<library::Entry> Snapshot() {
  std::lock_guard lock(g_snapshot_mutex);
  return g_snapshot;
}

}  // namespace mtk::history
