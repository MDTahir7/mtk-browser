#include "home_page.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>

#include "policy.h"

namespace mtk::home {
namespace {

std::mutex g_snapshot_mutex;
std::string g_snapshot;

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

std::string Initial(std::string_view title) {
  for (const unsigned char character : title) {
    if (std::isalnum(character)) {
      return std::string(1, static_cast<char>(std::toupper(character)));
    }
  }
  return "M";
}

std::string SafeUrl(const std::string& raw) {
  const auto decision = policy::EvaluateNavigation(raw);
  if (!decision.allow || (!decision.url.starts_with("https://") &&
                          !decision.url.starts_with("http://"))) return {};
  return decision.url;
}

std::string SizeText(const downloads::Item& item) {
  const int64_t bytes = item.total_bytes > 0 ? item.total_bytes
                                             : item.received_bytes;
  if (bytes <= 0) return "Size unavailable";
  char text[64]{};
  if (bytes >= 1024LL * 1024 * 1024) {
    std::snprintf(text, sizeof(text), "%.1f GB",
                  static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
  } else if (bytes >= 1024LL * 1024) {
    std::snprintf(text, sizeof(text), "%.1f MB",
                  static_cast<double>(bytes) / (1024.0 * 1024));
  } else if (bytes >= 1024) {
    std::snprintf(text, sizeof(text), "%.1f KB",
                  static_cast<double>(bytes) / 1024.0);
  } else {
    std::snprintf(text, sizeof(text), "%lld bytes",
                  static_cast<long long>(bytes));
  }
  return text;
}

const char* StateText(downloads::State state) {
  switch (state) {
    case downloads::State::kStarting: return "Starting";
    case downloads::State::kInProgress: return "Downloading";
    case downloads::State::kPaused: return "Paused";
    case downloads::State::kComplete: return "Complete";
    case downloads::State::kCanceled: return "Canceled";
    case downloads::State::kFailed: return "Failed";
  }
  return "Unknown";
}

std::string CurrentTimeCard() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  static constexpr const char* kDays[] = {"Sunday", "Monday", "Tuesday",
      "Wednesday", "Thursday", "Friday", "Saturday"};
  static constexpr const char* kMonths[] = {"January", "February", "March",
      "April", "May", "June", "July", "August", "September", "October",
      "November", "December"};
  const int hour = now.wHour % 12 == 0 ? 12 : now.wHour % 12;
  char value[256]{};
  std::snprintf(value, sizeof(value),
      "<div class=card><div class=clock>%02d:%02d <small>%s</small></div>"
      "<p class=date>%s, %u %s %u</p></div>", hour, now.wMinute,
      now.wHour < 12 ? "AM" : "PM", kDays[now.wDayOfWeek], now.wDay,
      kMonths[std::clamp<int>(now.wMonth, 1, 12) - 1], now.wYear);
  return value;
}

// The page carries no script, so the greeting is decided here rather than in the
// document. Without this it would read "Good afternoon" at midnight.
const char* Greeting() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  if (now.wHour < 12) return "Good morning";
  if (now.wHour < 17) return "Good afternoon";
  return "Good evening";
}

std::string Empty(std::string_view title, std::string_view detail) {
  return "<div class=empty><b>" + EscapeHtml(title) + "</b>" +
         EscapeHtml(detail) + "</div>";
}

struct Frequent {
  std::string host;
  std::string url;
  std::string title;
  size_t count = 0;
  long long latest = 0;
};

}  // namespace

bool IsHomeUrl(std::string_view url) { return url == kUrl; }

Action ParseActionUrl(std::string_view url) {
  if (url == "mtk://home/action/downloads") return Action::kDownloads;
  if (url == "mtk://home/action/history") return Action::kHistory;
  if (url == "mtk://home/action/bookmarks") return Action::kBookmarks;
  if (url == "mtk://home/action/settings") return Action::kSettings;
  return Action::kNone;
}

std::string EscapeHtml(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped.push_back(character); break;
    }
  }
  return escaped;
}

std::string EscapeHtml(std::wstring_view value) {
  return EscapeHtml(WideToUtf8(value));
}

std::string BuildHtml(const std::vector<library::Entry>& history,
                      const std::vector<library::Entry>& bookmarks,
                      const std::vector<downloads::Item>& downloads) {
  std::map<std::string, Frequent> by_host;
  for (const auto& entry : history) {
    const std::string url = SafeUrl(entry.url);
    const std::string host = policy::HostOf(url);
    if (url.empty() || host.empty()) continue;
    auto& item = by_host[host];
    ++item.count;
    if (item.url.empty() || entry.visited > item.latest) {
      item.host = host;
      item.url = url;
      item.title = WideToUtf8(entry.title);
      item.latest = entry.visited;
    }
  }
  std::vector<Frequent> frequent;
  for (auto& [host, item] : by_host) frequent.push_back(std::move(item));
  std::sort(frequent.begin(), frequent.end(), [](const Frequent& left,
                                                  const Frequent& right) {
    if (left.count != right.count) return left.count > right.count;
    return left.latest > right.latest;
  });
  if (frequent.size() > 5) frequent.resize(5);

  std::string frequent_html;
  for (const auto& item : frequent) {
    const std::string title = item.title.empty() ? item.host : item.title;
    frequent_html += "<a class=visit href=\"" + EscapeHtml(item.url) +
        "\"><span class=tile>" + EscapeHtml(Initial(title)) +
        "</span><span><b>" + EscapeHtml(title) + "</b><small>" +
        EscapeHtml(item.host) + "</small></span><em>" +
        std::to_string(item.count) + (item.count == 1 ? " entry" : " entries") +
        "</em></a>";
  }
  if (frequent_html.empty()) {
    frequent_html = Empty("No history yet",
        "Sites you visit will appear here. Private windows are never included.");
  }

  std::string bookmark_html;
  size_t bookmark_count = 0;
  for (const auto& entry : bookmarks) {
    if (bookmark_count == 4) break;
    const std::string url = SafeUrl(entry.url);
    if (url.empty()) continue;
    const std::string host = policy::HostOf(url);
    const std::string title = entry.title.empty() ? host : WideToUtf8(entry.title);
    bookmark_html += "<a class=item href=\"" + EscapeHtml(url) +
        "\"><span class=file>" + EscapeHtml(Initial(title)) +
        "</span><span><b>" + EscapeHtml(title) + "</b><small>" +
        EscapeHtml(host) + "</small></span></a>";
    ++bookmark_count;
  }
  if (bookmark_html.empty()) bookmark_html = Empty("No bookmarks yet",
      "Save a page with Alt+B and it will appear here.");

  std::string download_html;
  size_t download_count = 0;
  for (auto it = downloads.rbegin(); it != downloads.rend() &&
       download_count < 4; ++it, ++download_count) {
    const std::string name = it->file_name.empty()
                                 ? "Unnamed download"
                                 : WideToUtf8(it->file_name);
    download_html += "<div class=item><span class=file>" +
        EscapeHtml(Initial(name)) + "</span><span><b>" + EscapeHtml(name) +
        "</b><small>" + EscapeHtml(SizeText(*it)) + " · " +
        StateText(it->state) + "</small></span></div>";
  }
  if (download_html.empty()) download_html = Empty("No downloads yet",
      "Files downloaded in this profile will appear here.");

  static constexpr char kHead[] = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><meta name=referrer content=no-referrer>
<meta http-equiv=Content-Security-Policy content="default-src 'none'; style-src 'unsafe-inline'; form-action https://www.google.com; base-uri 'none'; object-src 'none'; frame-src 'none'; connect-src 'none'; img-src 'none'; font-src 'none'; script-src 'none'">
<title>MTK Browser Home</title><style>
:root{color-scheme:dark;--bg:#050817;--panel:#10172d;--panel2:#0b1022;--line:#222b48;--text:#f5f7ff;--muted:#9da8c3;--dim:#687596;--accent:#8b5cf6;--violet:#a855f7;--cyan:#48c9ff;--ok:#49d98a}*{box-sizing:border-box}html,body{margin:0;min-height:100%}body{background:radial-gradient(circle at 65% 0,#10194a 0,transparent 34%),var(--bg);color:var(--text);font:14px/1.45 'Segoe UI Variable','Segoe UI',system-ui,sans-serif;display:grid;grid-template-columns:166px minmax(0,1fr)}a{color:inherit;text-decoration:none}aside{position:sticky;top:0;height:100vh;z-index:3;background:linear-gradient(180deg,#0d1227,#080c1b);border-right:1px solid rgba(130,110,255,.15);padding:15px 10px;display:flex;flex-direction:column;gap:3px}.brand{height:52px;display:flex;align-items:center;gap:8px;padding:0 11px 12px;font-weight:650}.brand .mark{width:24px;height:24px;border-radius:7px;display:grid;place-items:center;background:linear-gradient(145deg,#6c4cff,#bd4cff);box-shadow:0 0 22px rgba(139,92,246,.45)}.brand small{color:var(--muted);font-size:11px}.nav{display:flex;align-items:center;gap:11px;padding:10px 11px;border-radius:9px;color:var(--muted);font-size:12px;border:1px solid transparent}.nav .ico{width:18px;text-align:center;font-size:15px}.nav:hover:not(.disabled){background:#151c35;color:#fff}.nav.on{color:#fff;background:linear-gradient(100deg,rgba(132,89,255,.28),rgba(74,44,155,.12));border-color:rgba(154,115,255,.3);box-shadow:inset 3px 0 #9a6cff}.nav.disabled{color:#66718e;cursor:default}.soon{margin-left:auto;font-size:8px;letter-spacing:.7px;color:#7a86a5}.spacer{flex:1}.publisher{text-align:center;padding:17px 8px 7px;color:#e9e3ff;font-family:Georgia,serif;font-size:22px;line-height:1;letter-spacing:2px}.publisher small{display:block;margin-top:5px;color:#6f7a99;font:7px 'Segoe UI',sans-serif;letter-spacing:2.5px}.theme{width:52px;margin:14px auto 2px;padding:5px;border:1px solid #202945;border-radius:20px;color:#7886a8;text-align:center;font-size:11px}main{min-width:0;padding-bottom:24px}.hero{position:relative;isolation:isolate;overflow:hidden;text-align:center;padding:29px 22px 24px;background:linear-gradient(180deg,rgba(10,17,52,.2),#071027 82%);border-bottom:1px solid rgba(111,91,205,.18)}.hero:before{content:'';position:absolute;inset:0;z-index:-3;background:radial-gradient(ellipse at 51% 80%,rgba(255,105,179,.92) 0,rgba(101,64,216,.48) 13%,transparent 31%),radial-gradient(ellipse at 50% 110%,rgba(56,168,255,.42),transparent 38%),linear-gradient(155deg,#0e1640 0,#15245a 42%,#160d39 72%,#070b1b 100%)}.hero:after{content:'';position:absolute;z-index:-2;left:-5%;right:-5%;bottom:-30px;height:122px;background:linear-gradient(153deg,transparent 0 9%,#0a1029 9.5% 23%,transparent 23.5%),linear-gradient(207deg,transparent 0 12%,#0c1430 12.5% 29%,transparent 29.5%),linear-gradient(174deg,transparent 0 34%,#070d20 34.5% 58%,transparent 58.5%);opacity:.9}.hero h1{margin:0;font-size:27px;font-weight:620;letter-spacing:-.3px;text-shadow:0 2px 22px #080b22}.hero h1 span{font-size:24px}.hero>p{margin:4px 0 17px;color:#c3c9dc;font-size:12px}.search{position:relative;max-width:570px;margin:auto}.search:before{content:'⌕';position:absolute;z-index:1;left:20px;top:12px;color:#aeb9d2;font-size:22px}.search input{width:100%;height:48px;border:1px solid rgba(183,153,255,.46);border-radius:25px;background:rgba(13,18,48,.78);color:#fff;padding:0 57px 0 50px;font-size:12px;outline:none;box-shadow:0 12px 35px rgba(2,5,20,.5),inset 0 0 18px rgba(117,87,255,.1)}.search input:focus{border-color:#aa7cff;box-shadow:0 0 0 3px rgba(139,92,246,.2),0 12px 35px rgba(2,5,20,.5)}.search button{position:absolute;right:7px;top:7px;width:34px;height:34px;border:0;border-radius:50%;color:white;background:linear-gradient(145deg,#9a5cff,#6453e8);box-shadow:0 0 15px rgba(153,87,255,.7);cursor:pointer}.quick{max-width:650px;margin:17px auto 0;display:grid;grid-template-columns:repeat(4,1fr);gap:9px}.quick .q{min-width:0;padding:10px 12px;border:1px solid rgba(111,91,205,.22);background:linear-gradient(145deg,rgba(21,28,58,.94),rgba(14,19,42,.9));border-radius:8px;text-align:left;display:flex;align-items:center;gap:9px;cursor:pointer;box-shadow:0 8px 20px rgba(3,6,20,.25)}.quick .q:hover{border-color:#825eff;transform:translateY(-1px)}.quick .q.muted{cursor:default;opacity:.6}.qicon{width:28px;height:28px;flex:0 0 28px;display:grid;place-items:center;color:#9e75ff;font-size:20px}.q b,.q small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.q b{font-size:11px;font-weight:600}.q small{font-size:8.5px;color:#7785a6}.content{padding:12px;display:grid;grid-template-columns:minmax(0,1fr) 205px;gap:10px}.primary{display:grid;align-content:start;gap:10px}.row{display:grid;grid-template-columns:minmax(210px,.95fr) minmax(260px,1.25fr) minmax(220px,1fr);gap:10px}.row.four{grid-template-columns:repeat(4,minmax(0,1fr))}.side{display:grid;align-content:start;gap:10px}.card{min-width:0;background:linear-gradient(145deg,rgba(17,24,48,.96),rgba(11,17,36,.97));border:1px solid rgba(86,99,151,.23);border-radius:10px;padding:11px;box-shadow:0 8px 24px rgba(1,4,17,.2)}.card h2{font-size:10.5px;margin:0 0 9px;display:flex;align-items:center;gap:6px;font-weight:600}.card h2 .sym{color:#a277ff}.card h2 a{margin-left:auto;color:#a989ff;font-size:8.5px;font-weight:500}.card h2 a:hover{color:#d1c1ff}.empty{border:1px dashed #283252;border-radius:8px;padding:13px 9px;text-align:center;color:#687695;font-size:9px}.empty b{display:block;color:#a8b2c9;font-size:10px;margin-bottom:3px}.visit,.item{display:grid;grid-template-columns:25px minmax(0,1fr) auto;align-items:center;gap:7px;padding:4px;border-radius:7px}.visit:hover,a.item:hover{background:#18213d}.visit b,.visit small,.item b,.item small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.visit b,.item b{font-size:9.5px;font-weight:550}.visit small,.item small{font-size:7.5px;color:#6d7a99}.visit em{font-style:normal;font-size:7.5px;color:#6d7a99}.tile,.file{width:24px;height:24px;border-radius:6px;display:grid;place-items:center;background:linear-gradient(145deg,#7658ed,#4331a7);font-size:9px;font-weight:700}.file{background:#1a2543;color:#9ba7c1}.feature{display:grid;grid-template-columns:82px 1fr;gap:12px;align-items:center}.cover{height:126px;border-radius:3px;padding:12px 6px;display:flex;align-items:center;text-align:center;background:linear-gradient(150deg,#22477d,#061a32 55%,#a87524);box-shadow:7px 7px 0 #081020,0 8px 20px #02050d;color:#f6cf78;font:10px Georgia,serif;letter-spacing:.5px}.feature h3{margin:0 0 7px;font-size:15px;font-weight:540;line-height:1.25}.feature p{color:#8895b3;font-size:9px;margin:0 0 12px}.actions{display:flex;gap:6px;flex-wrap:wrap}.pill{padding:6px 8px;border-radius:5px;background:linear-gradient(135deg,#8e5af4,#6353dd);font-size:8px}.pill.ghost{background:#10172d;border:1px solid #293354}.progress{height:3px;background:#242c48;border-radius:4px;overflow:hidden;margin-top:9px}.progress span{display:block;width:45%;height:100%;background:linear-gradient(90deg,#a35cff,#6b62ff)}.reading{display:grid;gap:7px}.read-item{display:grid;grid-template-columns:38px 1fr;gap:8px;align-items:center}.mini-cover{height:53px;border-radius:3px;background:linear-gradient(145deg,#b37a2a,#18142c 70%);box-shadow:3px 3px 0 #080d1e}.read-item b{font-size:9px;display:block}.read-item small{color:#73809e;font-size:7.5px}.clock{font-size:27px;line-height:1;font-weight:500}.clock small{font-size:11px;color:#b7a4ef}.date{color:#7784a2;font-size:8.5px;margin:6px 0 0}.weather{display:flex;align-items:center;gap:10px;margin-top:11px;padding-top:10px;border-top:1px solid #202945}.sun{font-size:28px;color:#f7c84b}.weather b{font-size:11px}.weather small{display:block;color:#7784a2;font-size:8px}.goal{display:grid;grid-template-columns:1fr 58px;gap:10px;align-items:center}.goal strong{font-size:20px}.goal small{display:block;color:#7784a2;font-size:8.5px}.ring{width:55px;height:55px;border-radius:50%;display:grid;place-items:center;background:#202945;position:relative}.ring:after{content:'';position:absolute;inset:6px;border-radius:50%;background:#10172d}.ring span{z-index:1;font-size:10px}.quote{padding:18px 15px}.quote .marks{color:#9d65ff;font:32px Georgia,serif;line-height:.6}.quote p{font-size:12px;line-height:1.5;margin:8px 0}.quote small{color:#8692ad;font-size:8.5px}.note{min-height:82px;color:#667492;font-size:9px;padding-top:3px}.note:after{content:'✎';float:right;margin-top:35px;color:#8490ad;font-size:14px}.tipbar{height:28px;border:1px solid #1c2542;border-radius:8px;background:#0d1429;display:flex;align-items:center;justify-content:center;color:#8a97b4;font-size:8.5px}.tipbar b{color:#b8a2ff;margin-right:6px}.status-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:7px}.status-chip{padding:8px;border-radius:7px;background:#0c1327;border:1px solid #202a48;color:#7f8ca8;font-size:8px}.status-chip b{display:block;color:#b0b9cd;font-size:9px;margin-bottom:2px}@media(max-width:1180px){body{grid-template-columns:140px 1fr}.content{grid-template-columns:1fr}.side{grid-template-columns:repeat(4,1fr)}.row{grid-template-columns:1fr 1fr}.row .card:last-child{grid-column:1/-1}.row.four{grid-template-columns:1fr 1fr}}@media(max-width:820px){body{grid-template-columns:1fr}aside{display:none}.hero h1{font-size:23px}.quick,.row,.row.four,.side{grid-template-columns:1fr 1fr}.row .card:last-child{grid-column:auto}}@media(max-width:560px){.hero{padding:24px 12px 18px}.content{padding:8px}.quick,.row,.row.four,.side{grid-template-columns:1fr}.feature{grid-template-columns:68px 1fr}.cover{height:105px}}
</style></head><body>)HTML";

  std::string html = kHead;
  html += R"HTML(<aside><div class=brand><span class=mark>◆</span><small>MTK Browser</small></div><div class="nav on"><span class=ico>⌂</span>Home</div><span class="nav disabled"><span class=ico>▤</span>Books<span class=soon>SOON</span></span><span class="nav disabled"><span class=ico>✦</span>AI Assistant<span class=soon>SOON</span></span><span class="nav disabled"><span class=ico>♧</span>Store<span class=soon>SOON</span></span><span class="nav disabled"><span class=ico>▧</span>Notes<span class=soon>SOON</span></span><a class=nav href="mtk://home/action/downloads"><span class=ico>⇩</span>Downloads</a><a class=nav href="mtk://home/action/history"><span class=ico>◷</span>History</a><a class=nav href="mtk://home/action/bookmarks"><span class=ico>☆</span>Bookmarks</a><a class=nav href="mtk://home/action/settings"><span class=ico>⚙</span>Settings</a><div class=spacer></div><div class=publisher>MTK<small>PUBLISHING</small></div><div class=theme>☼　☾</div></aside><main><section class=hero><h1>)HTML";
  html += Greeting();
  html += R"HTML(! <span>👋</span></h1><p>What would you like to explore today?</p><form class=search method=get action="https://www.google.com/search"><input id=search-box name=q type=search placeholder="Search the web or type a URL..." autofocus autocomplete=off spellcheck=false aria-label="Search the web"><input type=hidden name=safe value=active><button type=submit aria-label=Search>◆</button></form><div class=quick><label class=q for=search-box><span class=qicon>◎</span><span><b>Search Web</b><small>Explore the internet</small></span></label><span class="q muted"><span class=qicon>✦</span><span><b>Ask AI</b><small>Coming soon</small></span></span><span class="q muted"><span class=qicon>▤</span><span><b>Search Books</b><small>Local catalogue</small></span></span><span class="q muted"><span class=qicon>♧</span><span><b>Visit Store</b><small>MTK Publishing</small></span></span></div></section><div class=content><div class=primary><div class=row><section class=card><h2><span class=sym>◉</span>Frequently Visited <a href="mtk://home/action/history">View All</a></h2>)HTML";
  html += frequent_html;
  html += R"HTML(</section><section class=card><h2><span class=sym>◇</span>Featured Collection</h2><div class=feature><div class=cover>THE ORIGINS AND BELIEF OF SHIA ISLAM</div><div><h3>The Origins and Belief of Shia Islam</h3><p>A deep dive into history, beliefs and teachings from the MTK Publishing catalogue.</p><div class=actions><span class=pill>Read Preview</span><span class="pill ghost">Coming Soon</span></div><div class=progress><span></span></div></div></div></section><section class=card><h2><span class=sym>▤</span>Continue Reading</h2><div class=reading><div class=read-item><div class=mini-cover></div><div><b>Your reading queue is ready</b><small>Open a supported book to begin</small><div class=progress><span></span></div></div></div>)HTML";
  html += Empty("Nothing in progress",
      "Reading activity will appear after the MTK reader is added.");
  html += R"HTML(</div></section></div><div class="row four"><section class=card><h2><span class=sym>⇩</span>Recent Downloads <a href="mtk://home/action/downloads">View All</a></h2>)HTML";
  html += download_html;
  html += R"HTML(</section><section class=card><h2><span class=sym>☆</span>Bookmarks <a href="mtk://home/action/bookmarks">View All</a></h2>)HTML";
  html += bookmark_html;
  html += R"HTML(</section><section class=card><h2><span class=sym>⌘</span>Pinned Sites</h2><div class=status-grid><div class=status-chip><b>＋ Add site</b>Pin a favourite</div><div class=status-chip><b>Private</b>Profile only</div></div></section><section class=card><h2><span class=sym>▧</span>Browser Status</h2><div class=status-grid><div class=status-chip><b>SafeSearch</b>Active</div><div class=status-chip><b>Shields</b>Active</div><div class=status-chip><b>HTTPS</b>Preferred</div><div class=status-chip><b>Analytics</b>Off</div></div></section></div><div class=tipbar><b>✧ MTK Tip:</b> Read daily, learn constantly, publish fearlessly.</div></div><div class=side>)HTML";
  html += CurrentTimeCard();
  html += R"HTML(<section class=card><h2><span class=sym>☀</span>Weather</h2><div class=weather><span class=sun>☼</span><span><b>Off by default</b><small>No location leaves your device</small></span></div></section><section class=card><h2><span class=sym>◷</span>Daily Goal <a href="mtk://home/action/settings">Edit</a></h2><div class=goal><div><small>Reading time</small><strong>0</strong><small>No goal configured</small></div><div class=ring><span>0%</span></div></div></section><section class="card quote"><div class=marks>“</div><p>The best way to predict the future is to create it.</p><small>— Peter Drucker</small></section><section class=card><h2><span class=sym>✎</span>Quick Note <span class=soon>COMING</span></h2><div class=note>Write your thoughts…<br><br>Notes stay disabled until local encrypted storage is available.</div></section></div></div></main></body></html>)HTML";
  return html;
}

void RefreshSnapshot(const std::vector<library::Entry>& history,
                     const std::vector<library::Entry>& bookmarks,
                     const std::vector<downloads::Item>& downloads) {
  std::string next = BuildHtml(history, bookmarks, downloads);
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot = std::move(next);
}

std::string SnapshotHtml() {
  std::lock_guard lock(g_snapshot_mutex);
  return g_snapshot;
}

}  // namespace mtk::home
