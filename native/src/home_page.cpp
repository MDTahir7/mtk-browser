#include "home_page.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <unordered_set>

#include "policy.h"
#include "inline_asset.h"
#include "favicon_store.h"
#include "builtin_favicons.h"
#include "library_store.h"
#include "download_manager.h"

namespace mtk::home {
namespace {

std::mutex g_snapshot_mutex;
std::string g_snapshot;
bool g_snapshot_is_light = false;

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
  char value[384]{};
  std::snprintf(value, sizeof(value),
      "<div id=home-clock-card class=card><div id=home-clock class=clock>%02d:%02d <small>%s</small></div>"
      "<p id=home-date class=date>%s, %u %s %u</p></div><!--clock-end-->", hour, now.wMinute,
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

std::string BuildKnownFaviconsJson(const std::vector<Frequent>& frequent,
                                   const std::vector<library::Entry>& bookmarks) {
  std::string json = mtk::favicon::GetBuiltinFaviconsJson();
  if (json.empty() || json.back() != '}') return "{}";
  json.pop_back();

  std::unordered_set<std::string> seen;
  for (const auto& item : frequent) {
    if (item.host.empty() || seen.count(item.host)) continue;
    seen.insert(item.host);
    std::string icon_url = mtk::favicon::GetFaviconDataUrl(item.host);
    if (!icon_url.empty()) {
      json += ",\"" + item.host + "\":\"" + icon_url + "\"";
    }
  }
  for (const auto& entry : bookmarks) {
    const std::string url = SafeUrl(entry.url);
    const std::string host = policy::HostOf(url);
    if (host.empty() || seen.count(host)) continue;
    seen.insert(host);
    std::string icon_url = mtk::favicon::GetFaviconDataUrl(host);
    if (!icon_url.empty()) {
      json += ",\"" + host + "\":\"" + icon_url + "\"";
    }
  }
  json += "}";
  return json;
}

}  // namespace

std::string CleanDisplayFileName(std::string_view raw) {
  if (raw.empty()) return "Unnamed download";
  std::string s(raw);
  static const char* const kPrefixes[] = {
      "objecttype-raw-",
      "objecttype-",
      "response-content-disposition=",
      "filename*=",
      "filename=",
      "attachment; filename=",
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto* prefix : kPrefixes) {
      const size_t len = strlen(prefix);
      if (s.size() > len) {
        bool match = true;
        for (size_t i = 0; i < len; ++i) {
          if (std::tolower(static_cast<unsigned char>(s[i])) !=
              std::tolower(static_cast<unsigned char>(prefix[i]))) {
            match = false;
            break;
          }
        }
        if (match) {
          s.erase(0, len);
          changed = true;
        }
      }
    }
    while (!s.empty() && (s.front() == '"' || s.front() == '\'')) {
      s.erase(s.begin());
      changed = true;
    }
    while (!s.empty() && (s.back() == '"' || s.back() == '\'')) {
      s.pop_back();
      changed = true;
    }
  }
  if (!s.empty() && (s.front() == '_' || s.front() == '-') && s.size() > 1 &&
      std::isalnum(static_cast<unsigned char>(s[1]))) {
    s.erase(s.begin());
  }
  return s.empty() ? "Unnamed download" : s;
}

std::string ExtensionOfUtf8(std::string_view name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string_view::npos || dot + 1 >= name.size()) return {};
  std::string ext;
  ext.reserve(name.size() - dot - 1);
  for (size_t i = dot + 1; i < name.size(); ++i) {
    ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
  }
  return ext;
}

std::string FileIconBadge(std::string_view file_name) {
  const std::string ext = ExtensionOfUtf8(file_name);

  // Images
  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" ||
      ext == "gif" || ext == "svg" || ext == "bmp" || ext == "ico" ||
      ext == "tiff" || ext == "avif") {
    return "<span class=\"file-badge file-img\" title=\"Image\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\" ry=\"2\"/>"
           "<circle cx=\"8.5\" cy=\"8.5\" r=\"1.5\"/>"
           "<polyline points=\"21 15 16 10 5 21\"/>"
           "</svg></span>";
  }

  // PDF
  if (ext == "pdf") {
    return "<span class=\"file-badge file-pdf\" title=\"PDF Document\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/>"
           "<polyline points=\"14 2 14 8 20 8\"/>"
           "<path d=\"M9 13h2a1.5 1.5 0 0 0 0-3H9v6\"/>"
           "<path d=\"M15 10v6\"/>"
           "</svg></span>";
  }

  // Documents
  if (ext == "doc" || ext == "docx" || ext == "txt" || ext == "rtf" ||
      ext == "odt" || ext == "pages" || ext == "md" || ext == "log") {
    return "<span class=\"file-badge file-doc\" title=\"Document\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/>"
           "<polyline points=\"14 2 14 8 20 8\"/>"
           "<line x1=\"16\" y1=\"13\" x2=\"8\" y2=\"13\"/>"
           "<line x1=\"16\" y1=\"17\" x2=\"8\" y2=\"17\"/>"
           "<line x1=\"10\" y1=\"9\" x2=\"8\" y2=\"9\"/>"
           "</svg></span>";
  }

  // Spreadsheets
  if (ext == "xls" || ext == "xlsx" || ext == "csv" || ext == "tsv" ||
      ext == "numbers" || ext == "ods") {
    return "<span class=\"file-badge file-sheet\" title=\"Spreadsheet\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/>"
           "<line x1=\"3\" y1=\"9\" x2=\"21\" y2=\"9\"/>"
           "<line x1=\"3\" y1=\"15\" x2=\"21\" y2=\"15\"/>"
           "<line x1=\"9\" y1=\"3\" x2=\"9\" y2=\"21\"/>"
           "<line x1=\"15\" y1=\"3\" x2=\"15\" y2=\"21\"/>"
           "</svg></span>";
  }

  // Presentations
  if (ext == "ppt" || ext == "pptx" || ext == "key" || ext == "odp") {
    return "<span class=\"file-badge file-pres\" title=\"Presentation\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<rect x=\"2\" y=\"3\" width=\"20\" height=\"14\" rx=\"2\"/>"
           "<line x1=\"8\" y1=\"21\" x2=\"12\" y2=\"17\"/>"
           "<line x1=\"16\" y1=\"21\" x2=\"12\" y2=\"17\"/>"
           "<line x1=\"12\" y1=\"17\" x2=\"12\" y2=\"21\"/>"
           "</svg></span>";
  }

  // Archives
  if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" ||
      ext == "gz" || ext == "bz2" || ext == "xz" || ext == "cab") {
    return "<span class=\"file-badge file-zip\" title=\"Archive\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<polyline points=\"21 8 21 21 3 21 3 8\"/>"
           "<rect x=\"1\" y=\"3\" width=\"22\" height=\"5\"/>"
           "<line x1=\"10\" y1=\"12\" x2=\"14\" y2=\"12\"/>"
           "</svg></span>";
  }

  // Audio
  if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "aac" ||
      ext == "m4a" || ext == "ogg" || ext == "wma") {
    return "<span class=\"file-badge file-audio\" title=\"Audio\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<path d=\"M9 18V5l12-2v13\"/>"
           "<circle cx=\"6\" cy=\"18\" r=\"3\"/>"
           "<circle cx=\"18\" cy=\"16\" r=\"3\"/>"
           "</svg></span>";
  }

  // Video
  if (ext == "mp4" || ext == "mkv" || ext == "webm" || ext == "avi" ||
      ext == "mov" || ext == "flv" || ext == "wmv") {
    return "<span class=\"file-badge file-video\" title=\"Video\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<polygon points=\"23 7 16 12 23 17 23 7\"/>"
           "<rect x=\"1\" y=\"5\" width=\"15\" height=\"14\" rx=\"2\" ry=\"2\"/>"
           "</svg></span>";
  }

  // Code
  if (ext == "cpp" || ext == "h" || ext == "c" || ext == "cs" ||
      ext == "py" || ext == "js" || ext == "ts" || ext == "html" ||
      ext == "css" || ext == "json" || ext == "xml" || ext == "yaml" ||
      ext == "yml" || ext == "sh" || ext == "bat" || ext == "ps1") {
    return "<span class=\"file-badge file-code\" title=\"Code / Script\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<polyline points=\"16 18 22 12 16 6\"/>"
           "<polyline points=\"8 6 2 12 8 18\"/>"
           "</svg></span>";
  }

  // Executable / App
  if (ext == "exe" || ext == "msi" || ext == "apk" || ext == "bin") {
    return "<span class=\"file-badge file-exe\" title=\"Application\">"
           "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<rect x=\"2\" y=\"3\" width=\"20\" height=\"14\" rx=\"2\"/>"
           "<line x1=\"8\" y1=\"21\" x2=\"16\" y2=\"21\"/>"
           "<line x1=\"12\" y1=\"17\" x2=\"12\" y2=\"21\"/>"
           "</svg></span>";
  }

  // Generic / Default
  return "<span class=\"file-badge file-other\" title=\"File\">"
         "<svg width=\"15\" height=\"15\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/>"
         "<polyline points=\"14 2 14 8 20 8\"/>"
         "</svg></span>";
}

bool IsHomeUrl(std::string_view url) { return url == kUrl; }

Action ParseActionUrl(std::string_view url) {
  if (url == "mtk://home/action/downloads") return {ActionKind::kDownloads};
  if (url == "mtk://home/action/history") return {ActionKind::kHistory};
  if (url == "mtk://home/action/bookmarks") return {ActionKind::kBookmarks};
  if (url == "mtk://home/action/settings") return {ActionKind::kSettings};
  static constexpr std::string_view kOpenPrefix = "mtk://home/action/download-open?id=";
  if (url.starts_with(kOpenPrefix)) {
    const auto id_str = url.substr(kOpenPrefix.size());
    uint64_t id = 0;
    try {
      id = std::stoull(std::string(id_str));
    } catch (...) {
      id = 0;
    }
    if (id > 0) return {ActionKind::kDownloadOpen, id};
  }
  static constexpr std::string_view kShowPrefix = "mtk://home/action/download-show?id=";
  if (url.starts_with(kShowPrefix)) {
    const auto id_str = url.substr(kShowPrefix.size());
    uint64_t id = 0;
    try {
      id = std::stoull(std::string(id_str));
    } catch (...) {
      id = 0;
    }
    if (id > 0) return {ActionKind::kDownloadShow, id};
  }
  return {};
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

struct QuoteItem {
  const char* text;
  const char* author;
};

static constexpr QuoteItem kQuotes[] = {
    {"The best way to predict the future is to create it.", "Peter Drucker"},
    {"In the middle of difficulty lies opportunity.", "Albert Einstein"},
    {"The only way to do great work is to love what you do.", "Steve Jobs"},
    {"Live as if you were to die tomorrow. Learn as if you were to live forever.", "Mahatma Gandhi"},
    {"You have to dream before your dreams can come true.", "A. P. J. Abdul Kalam"},
    {"What you seek is seeking you.", "Rumi"},
    {"It always seems impossible until it is done.", "Nelson Mandela"},
    {"Knowing yourself is the beginning of all wisdom.", "Aristotle"},
    {"The journey of a thousand miles begins with a single step.", "Lao Tzu"},
    {"Stay hungry, stay foolish.", "Steve Jobs"},
    {"Simplicity is the ultimate sophistication.", "Leonardo da Vinci"},
    {"Everything you've ever wanted is on the other side of fear.", "George Addair"},
    {"Success is not final, failure is not fatal: it is the courage to continue that counts.", "Winston Churchill"},
    {"Believe you can and you're halfway there.", "Theodore Roosevelt"},
    {"Act as if what you do makes a difference. It does.", "William James"},
    {"Turn your wounds into wisdom.", "Oprah Winfrey"},
    {"The mind is everything. What you think you become.", "Buddha"},
    {"Do what you can, with what you have, where you are.", "Theodore Roosevelt"},
    {"Be the change that you wish to see in the world.", "Mahatma Gandhi"},
    {"If you cannot do great things, do small things in a great way.", "Napoleon Hill"},
    {"Do not go where the path may lead, go instead where there is no path and leave a trail.", "Ralph Waldo Emerson"},
    {"The secret of getting ahead is getting started.", "Mark Twain"},
    {"Quality is not an act, it is a habit.", "Aristotle"},
    {"Wisdom begins in wonder.", "Socrates"},
    {"Not all those who wander are lost.", "J.R.R. Tolkien"},
    {"Peace comes from within. Do not seek it without.", "Buddha"},
    {"It is never too late to be what you might have been.", "George Eliot"},
    {"Happiness depends upon ourselves.", "Aristotle"},
    {"Every moment is a fresh beginning.", "T.S. Eliot"},
    {"To know what you know and what you do not know, that is true knowledge.", "Confucius"},
    {"Patience is a key element of success.", "Bill Gates"},
    {"Strive not to be a success, but rather to be of value.", "Albert Einstein"},
    {"The roots of education are bitter, but the fruit is sweet.", "Aristotle"},
    {"Your time is limited, so don't waste it living someone else's life.", "Steve Jobs"},
    {"Imagination is more important than knowledge.", "Albert Einstein"},
    {"The power of imagination makes us infinite.", "John Muir"}
};

std::string CurrentQuoteCard() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const size_t count = sizeof(kQuotes) / sizeof(kQuotes[0]);
  const size_t day_index = static_cast<size_t>(
      (now.wYear * 366 + now.wMonth * 31 + now.wDay)) % count;
  const auto& q = kQuotes[day_index];

  std::string card;
  card.reserve(512);
  card += "<section class=\"card quote\" id=quote-card title=\"Click or press ↻ for a new quote\">";
  card += "<div style=\"display:flex;align-items:center;justify-content:space-between;\">";
  card += "<div class=marks>“</div>";
  card += "<button id=quote-shuffle type=button aria-label=\"New Quote\" title=\"Shuffle quote\" style=\"background:none;border:none;color:inherit;opacity:0.6;font-size:13px;cursor:pointer;padding:2px 5px;\">↻</button>";
  card += "</div>";
  card += "<p id=quote-text>";
  card += EscapeHtml(q.text);
  card += "</p><small id=quote-author>— ";
  card += EscapeHtml(q.author);
  card += "</small></section><!--quote-end-->";
  return card;
}

std::string BuildHtml(const std::vector<library::Entry>& history,
                      const std::vector<library::Entry>& bookmarks,
                      const std::vector<downloads::Item>& downloads,
                      bool light_appearance) {
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
    const std::string icon_url = mtk::favicon::GetFaviconDataUrl(item.host);
    std::string icon_html;
    if (!icon_url.empty()) {
      icon_html = "<span class=\"tile tile-has-icon\"><img src=\"" + EscapeHtml(icon_url) +
                  "\" class=tile-icon alt=\"\"></span>";
    } else {
      icon_html = "<span class=\"tile tile-fallback\">" + EscapeHtml(Initial(title)) + "</span>";
    }
    frequent_html += "<a class=visit href=\"" + EscapeHtml(item.url) +
        "\">" + icon_html +
        "<span><b>" + EscapeHtml(title) + "</b><small>" +
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
    const std::string icon_url = mtk::favicon::GetFaviconDataUrl(host);
    std::string icon_html;
    if (!icon_url.empty()) {
      icon_html = "<span class=\"file file-has-icon\"><img src=\"" + EscapeHtml(icon_url) +
                  "\" class=tile-icon alt=\"\"></span>";
    } else {
      icon_html = "<span class=\"file file-fallback\">" + EscapeHtml(Initial(title)) + "</span>";
    }
    bookmark_html += "<a class=item href=\"" + EscapeHtml(url) +
        "\">" + icon_html +
        "<span><b>" + EscapeHtml(title) + "</b><small>" +
        EscapeHtml(host) + "</small></span></a>";
    ++bookmark_count;
  }
  if (bookmark_html.empty()) bookmark_html = Empty("No bookmarks yet",
      "Save a page with Alt+B and it will appear here.");

  std::string download_html;
  size_t download_count = 0;
  for (auto it = downloads.rbegin(); it != downloads.rend() &&
       download_count < 4; ++it, ++download_count) {
    const std::string raw_name = it->file_name.empty()
                                     ? "Unnamed download"
                                     : WideToUtf8(it->file_name);
    const std::string name = CleanDisplayFileName(raw_name);
    const std::string badge = FileIconBadge(name);
    const std::string size_str = SizeText(*it);
    const std::string state_str = StateText(it->state);
    const uint64_t id = it->id;

    if (it->state == downloads::State::kComplete) {
      download_html += "<div class=\"item dl-item dl-complete\">"
                       "<a class=\"dl-link\" href=\"mtk://home/action/download-open?id=" +
                       std::to_string(id) + "\" title=\"Open " + EscapeHtml(name) + "\">" +
                       badge + "<span class=\"dl-info\"><b>" + EscapeHtml(name) + "</b><small>" +
                       EscapeHtml(size_str) + " · Complete</small></span></a>"
                       "<a class=\"dl-folder-btn\" href=\"mtk://home/action/download-show?id=" +
                       std::to_string(id) + "\" title=\"Show in folder\" aria-label=\"Show in folder\">"
                       "<svg width=\"13\" height=\"13\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                       "<path d=\"M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z\"/>"
                       "</svg></a></div>";
    } else if (it->state == downloads::State::kInProgress || it->state == downloads::State::kStarting) {
      int percent = it->percent;
      if (percent < 0) percent = 0;
      download_html += "<div class=\"item dl-item dl-active\">"
                       "<a class=\"dl-link\" href=\"mtk://home/action/downloads\" title=\"View downloads\">" +
                       badge + "<span class=\"dl-info\"><b>" + EscapeHtml(name) + "</b><small>" +
                       EscapeHtml(size_str) + " · Downloading</small>"
                       "<div class=\"dl-mini-prog\"><span style=\"width:" +
                       std::to_string(std::clamp(percent, 0, 100)) + "%\"></span></div>"
                       "</span></a></div>";
    } else {
      download_html += "<div class=\"item dl-item dl-inactive\">"
                       "<a class=\"dl-link\" href=\"mtk://home/action/downloads\" title=\"View downloads\">" +
                       badge + "<span class=\"dl-info\"><b>" + EscapeHtml(name) + "</b><small>" +
                       EscapeHtml(size_str) + " · " + EscapeHtml(state_str) + "</small></span></a></div>";
    }
  }
  if (download_html.empty()) download_html = Empty("No downloads yet",
      "Files downloaded in this profile will appear here.");

  static constexpr char kHead[] = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><meta name=referrer content=no-referrer>
<meta http-equiv=Content-Security-Policy content="default-src 'none'; style-src 'unsafe-inline'; form-action https://www.google.com; base-uri 'none'; object-src 'none'; frame-src 'none'; connect-src https://wttr.in https://dummyjson.com; img-src data:; font-src 'none'; script-src 'unsafe-inline'">
<title>MTK Browser Home</title><style>
:root{color-scheme:dark;--bg:#050817;--panel:#10172d;--panel2:#0b1022;--line:#222b48;--text:#f5f7ff;--muted:#9da8c3;--dim:#687596;--accent:#8b5cf6;--violet:#a855f7;--cyan:#48c9ff;--ok:#49d98a}*{box-sizing:border-box}html,body{margin:0;min-height:100%}body{background:radial-gradient(circle at 65% 0,#10194a 0,transparent 34%),var(--bg);color:var(--text);font:14px/1.45 'Segoe UI Variable','Segoe UI',system-ui,sans-serif;display:grid;grid-template-columns:166px minmax(0,1fr)}a{color:inherit;text-decoration:none}aside{position:sticky;top:0;height:100vh;z-index:3;background:linear-gradient(180deg,#0d1227,#080c1b);border-right:1px solid rgba(130,110,255,.15);padding:15px 10px;display:flex;flex-direction:column;gap:3px}.brand{height:52px;display:flex;align-items:center;gap:8px;padding:0 11px 12px;font-weight:650}.brand .mark{width:24px;height:24px;border-radius:7px;display:grid;place-items:center;background:linear-gradient(145deg,#6c4cff,#bd4cff);box-shadow:0 0 22px rgba(139,92,246,.45)}.brand small{color:var(--muted);font-size:11px}.nav{display:flex;align-items:center;gap:11px;padding:10px 11px;border-radius:9px;color:var(--muted);font-size:12px;border:1px solid transparent}.nav .ico{width:18px;text-align:center;font-size:15px}.nav:hover:not(.disabled){background:#151c35;color:#fff}.nav.on{color:#fff;background:linear-gradient(100deg,rgba(132,89,255,.28),rgba(74,44,155,.12));border-color:rgba(154,115,255,.3);box-shadow:inset 3px 0 #9a6cff}.nav.disabled{color:#66718e;cursor:default}.soon{margin-left:auto;font-size:8px;letter-spacing:.7px;color:#7a86a5}.spacer{flex:1}main{min-width:0;display:flex;flex-direction:column;min-height:100vh;padding-bottom:0}.hero{position:relative;isolation:isolate;overflow:hidden;text-align:center;padding:14px 20px 10px;background:linear-gradient(180deg,rgba(10,17,52,.2),#071027 82%);border-bottom:1px solid rgba(111,91,205,.18)}.hero:before{content:'';position:absolute;inset:0;z-index:-3;background:radial-gradient(ellipse at 51% 80%,rgba(255,105,179,.92) 0,rgba(101,64,216,.48) 13%,transparent 31%),radial-gradient(ellipse at 50% 110%,rgba(56,168,255,.42),transparent 38%),linear-gradient(155deg,#0e1640 0,#15245a 42%,#160d39 72%,#070b1b 100%)}.hero:after{content:'';position:absolute;z-index:-2;left:-5%;right:-5%;bottom:-30px;height:122px;background:linear-gradient(153deg,transparent 0 9%,#0a1029 9.5% 23%,transparent 23.5%),linear-gradient(207deg,transparent 0 12%,#0c1430 12.5% 29%,transparent 29.5%),linear-gradient(174deg,transparent 0 34%,#070d20 34.5% 58%,transparent 58.5%);opacity:.9}.hero h1{margin:0;font-size:24px;font-weight:620;letter-spacing:-.3px;text-shadow:0 2px 22px #080b22}.hero h1 span{font-size:22px}.hero>p{margin:2px 0 8px;color:#c3c9dc;font-size:12px}.search{position:relative;max-width:570px;margin:auto}.search input{width:100%;height:42px;border:1px solid rgba(183,153,255,.46);border-radius:25px;background:rgba(13,18,48,.78);color:#fff;padding:0 57px 0 20px;font-size:12px;outline:none;box-shadow:0 12px 35px rgba(2,5,20,.5),inset 0 0 18px rgba(117,87,255,.1)}.search input:focus{border-color:#aa7cff;box-shadow:0 0 0 3px rgba(139,92,246,.2),0 12px 35px rgba(2,5,20,.5)}.search button{position:absolute;right:4px;top:4px;width:34px;height:34px;border:0;border-radius:50%;color:white;background:linear-gradient(145deg,#9a5cff,#6453e8);box-shadow:0 0 16px rgba(153,87,255,.75);cursor:pointer;display:grid;place-items:center;padding:0;overflow:hidden;transition:transform .25s cubic-bezier(.34,1.56,.64,1),box-shadow .25s ease}.search button:hover{transform:scale(1.12);box-shadow:0 0 24px rgba(168,85,247,.9),0 0 10px rgba(72,201,255,.6)}.search button:active{transform:scale(.94)}.search-btn-3d{position:relative;background:transparent;box-shadow:none;overflow:visible}.search-btn-3d:hover{background:transparent;box-shadow:none}.search-btn-3d img{position:absolute;inset:0;width:100%;height:100%;object-fit:contain;pointer-events:none}.search-btn-3d .search-3d-still{opacity:1;transition:opacity .2s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.search-btn-3d .search-3d-anim{opacity:0;transition:opacity .2s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.search-btn-3d:hover .search-3d-still,.search-btn-3d:focus-visible .search-3d-still{opacity:0}.search-btn-3d:hover .search-3d-anim,.search-btn-3d:focus-visible .search-3d-anim{opacity:1}.home-nav-3d{cursor:pointer;position:relative}.home-nav-3d:hover,.home-nav-3d:focus-visible{z-index:20}.home-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.home-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.home-3d-still{opacity:1}.home-3d-anim{opacity:0}.home-nav-3d:hover .home-3d-still,.home-nav-3d:focus-visible .home-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.6)}.home-nav-3d:hover .home-3d-anim,.home-nav-3d:focus-visible .home-3d-anim{opacity:1;transform:translate(-38%,-55%) scale(2.7);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(139,92,246,.55))}.home-nav-3d:active .home-3d-wrap img{transform:translate(-38%,-55%) scale(2.4)}.store-nav-3d{cursor:pointer;position:relative}.store-nav-3d:hover,.store-nav-3d:focus-visible{z-index:20}.store-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.store-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.store-3d-still{opacity:1}.store-3d-anim{opacity:0}.store-nav-3d:hover .store-3d-still,.store-nav-3d:focus-visible .store-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.6)}.store-nav-3d:hover .store-3d-anim,.store-nav-3d:focus-visible .store-3d-anim{opacity:1;transform:translate(-35%,-52%) scale(2.7);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(72,201,255,.55))}.store-nav-3d:active .store-3d-wrap img{transform:translate(-35%,-52%) scale(2.4)}.q-store-3d{cursor:pointer;position:relative}.q-store-3d:hover{z-index:20}.q-store-3d .store-3d-wrap{width:36px;height:36px;flex:0 0 36px;position:relative;display:inline-flex;align-items:center;justify-content:center}.q-store-3d .store-3d-wrap img{width:42px;height:42px;position:absolute;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 3px 6px rgba(0,0,0,.45))}.q-store-3d:hover .store-3d-still,.q-store-3d:focus-visible .store-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.2)}.q-store-3d:hover .store-3d-anim,.q-store-3d:focus-visible .store-3d-anim{opacity:1;transform:translate(-50%,-55%) scale(2.3);filter:drop-shadow(0 12px 28px rgba(0,0,0,.9)) drop-shadow(0 0 16px rgba(72,201,255,.5))}.q-store-3d:active .store-3d-wrap img{transform:translate(-50%,-55%) scale(2.0)}.q-search-3d{cursor:pointer;position:relative}.q-search-3d:hover{z-index:20}.q-search-3d .search-web-3d-wrap{width:36px;height:36px;flex:0 0 36px;position:relative;display:inline-flex;align-items:center;justify-content:center}.q-search-3d .search-web-3d-wrap img{width:42px;height:42px;position:absolute;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 3px 6px rgba(0,0,0,.45))}.q-search-3d .search-web-3d-still{opacity:1}.q-search-3d .search-web-3d-anim{opacity:0}.q-search-3d:hover .search-web-3d-still,.q-search-3d:focus-visible .search-web-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.2)}.q-search-3d:hover .search-web-3d-anim,.q-search-3d:focus-visible .search-web-3d-anim{opacity:1;transform:translate(-50%,-55%) scale(2.3);filter:drop-shadow(0 12px 28px rgba(0,0,0,.9)) drop-shadow(0 0 16px rgba(139,92,246,.55))}.q-search-3d:active .search-web-3d-wrap img{transform:translate(-50%,-55%) scale(2.0)}.q-ai-3d{cursor:pointer;position:relative}.q-ai-3d:hover{z-index:20}.q-ai-3d .ask-ai-3d-wrap{width:36px;height:36px;flex:0 0 36px;position:relative;display:inline-flex;align-items:center;justify-content:center}.q-ai-3d .ask-ai-3d-wrap img{width:42px;height:42px;position:absolute;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 3px 6px rgba(0,0,0,.45))}.q-ai-3d .ask-ai-3d-still{opacity:1}.q-ai-3d .ask-ai-3d-anim{opacity:0}.q-ai-3d:hover .ask-ai-3d-still,.q-ai-3d:focus-visible .ask-ai-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.2)}.q-ai-3d:hover .ask-ai-3d-anim,.q-ai-3d:focus-visible .ask-ai-3d-anim{opacity:1;transform:translate(-50%,-55%) scale(2.3);filter:drop-shadow(0 12px 28px rgba(0,0,0,.9)) drop-shadow(0 0 16px rgba(168,85,247,.6))}.q-ai-3d:active .ask-ai-3d-wrap img{transform:translate(-50%,-55%) scale(2.0)}.q-publishing-3d{cursor:pointer;position:relative}.q-publishing-3d:hover{z-index:20}.q-publishing-3d .publishing-3d-wrap{width:36px;height:36px;flex:0 0 36px;position:relative;display:inline-flex;align-items:center;justify-content:center}.q-publishing-3d .publishing-3d-wrap img{width:42px;height:42px;position:absolute;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 3px 6px rgba(0,0,0,.45))}.q-publishing-3d .publishing-3d-still{opacity:1}.q-publishing-3d .publishing-3d-anim{opacity:0}.q-publishing-3d:hover .publishing-3d-still,.q-publishing-3d:focus-visible .publishing-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.2)}.q-publishing-3d:hover .publishing-3d-anim,.q-publishing-3d:focus-visible .publishing-3d-anim{opacity:1;transform:translate(-50%,-55%) scale(2.3);filter:drop-shadow(0 12px 28px rgba(0,0,0,.9)) drop-shadow(0 0 16px rgba(212,168,67,.65))}.q-publishing-3d:active .publishing-3d-wrap img{transform:translate(-50%,-55%) scale(2.0)}.notes-nav-3d{cursor:pointer;position:relative}.notes-nav-3d:hover,.notes-nav-3d:focus-visible{z-index:20}.notes-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.notes-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.notes-3d-still{opacity:1}.notes-3d-anim{opacity:0}.notes-nav-3d:hover .notes-3d-still,.notes-nav-3d:focus-visible .notes-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.6)}.notes-nav-3d:hover .notes-3d-anim,.notes-nav-3d:focus-visible .notes-3d-anim{opacity:1;transform:translate(-35%,-50%) scale(2.7);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(168,85,247,.55))}.notes-nav-3d:active .notes-3d-wrap img{transform:translate(-35%,-50%) scale(2.4)}.download-nav-3d{cursor:pointer;position:relative}.download-nav-3d:hover,.download-nav-3d:focus-visible{z-index:20}.download-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.download-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.download-3d-still{opacity:1}.download-3d-anim{opacity:0}.download-nav-3d:hover .download-3d-still,.download-nav-3d:focus-visible .download-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.6)}.download-nav-3d:hover .download-3d-anim,.download-nav-3d:focus-visible .download-3d-anim{opacity:1;transform:translate(-50%,-50%) scale(2.6);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(72,201,255,.55))}.download-nav-3d:active .download-3d-wrap img{transform:translate(-50%,-50%) scale(2.3)}.history-nav-3d{cursor:pointer;position:relative}.history-nav-3d:hover,.history-nav-3d:focus-visible{z-index:20}.history-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.history-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.history-3d-still{opacity:1}.history-3d-anim{opacity:0}.history-nav-3d:hover .history-3d-still,.history-nav-3d:focus-visible .history-3d-still{opacity:0;transform:translate(-68%,-50%) scale(2.6)}.history-nav-3d:hover .history-3d-anim,.history-nav-3d:focus-visible .history-3d-anim{opacity:1;transform:translate(-68%,-50%) scale(2.6);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(72,201,255,.55))}.history-nav-3d:active .history-3d-wrap img{transform:translate(-68%,-50%) scale(2.3)}.settings-nav-3d{cursor:pointer;position:relative}.settings-nav-3d:hover,.settings-nav-3d:focus-visible{z-index:20}.settings-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.settings-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.settings-3d-still{opacity:1}.settings-3d-anim{opacity:0}.settings-nav-3d:hover .settings-3d-still,.settings-nav-3d:focus-visible .settings-3d-still{opacity:0;transform:translate(-72%,-50%) scale(2.6)}.settings-nav-3d:hover .settings-3d-anim,.settings-nav-3d:focus-visible .settings-3d-anim{opacity:1;transform:translate(-72%,-50%) scale(2.6);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(72,201,255,.55))}.settings-nav-3d:active .settings-3d-wrap img{transform:translate(-72%,-50%) scale(2.3)}.bookmarks-nav-3d{cursor:pointer;position:relative}.bookmarks-nav-3d:hover,.bookmarks-nav-3d:focus-visible{z-index:20}.bookmarks-3d-wrap{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 24px}.bookmarks-3d-wrap img{position:absolute;width:30px;height:30px;object-fit:contain;pointer-events:none;left:50%;top:50%;transform:translate(-50%,-50%);transition:transform .32s cubic-bezier(.34,1.56,.64,1),opacity .2s ease,filter .3s ease;filter:drop-shadow(0 2px 5px rgba(0,0,0,.45))}.bookmarks-3d-still{opacity:1}.bookmarks-3d-anim{opacity:0}.bookmarks-nav-3d:hover .bookmarks-3d-still,.bookmarks-nav-3d:focus-visible .bookmarks-3d-still{opacity:0;transform:translate(-50%,-50%) scale(2.4)}.bookmarks-nav-3d:hover .bookmarks-3d-anim,.bookmarks-nav-3d:focus-visible .bookmarks-3d-anim{opacity:1;transform:translate(-58%,-50%) scale(2.4);filter:drop-shadow(0 10px 22px rgba(0,0,0,.85)) drop-shadow(0 0 14px rgba(72,201,255,.55))}.bookmarks-nav-3d:active .bookmarks-3d-wrap img{transform:translate(-58%,-50%) scale(2.1)}.quick{max-width:650px;margin:8px auto 0;display:grid;grid-template-columns:repeat(4,1fr);gap:9px}.quick .q{min-width:0;padding:6px 10px;border:1px solid rgba(111,91,205,.22);background:linear-gradient(145deg,rgba(21,28,58,.94),rgba(14,19,42,.9));border-radius:8px;text-align:left;display:flex;align-items:center;gap:9px;cursor:pointer;box-shadow:0 8px 20px rgba(3,6,20,.25)}.quick .q:hover{border-color:#825eff;transform:translateY(-1px)}.quick .q.muted{cursor:default;opacity:.6}.q.q-search{cursor:pointer;transition:all .25s ease}.q.q-search:hover{border-color:#825eff;box-shadow:0 8px 24px rgba(130,94,255,.2);transform:translateY(-1px)}.q.q-search.active{border-color:#8b5cf6;background:linear-gradient(145deg,rgba(34,26,72,.95),rgba(22,17,50,.92));box-shadow:0 0 18px rgba(139,92,246,.32)}.q.q-search .qicon{transition:transform .25s ease}.q.q-search:hover .qicon{transform:scale(1.15)}.q.q-search:focus-visible,.q.q-ai:focus-visible{outline:2px solid var(--accent);outline-offset:1px}.q.q-ai{cursor:pointer;background:linear-gradient(145deg,rgba(30,22,66,.94),rgba(18,14,46,.9));border-color:rgba(168,85,247,.35);transition:all .25s ease}.q.q-ai:hover{border-color:#bd4cff;box-shadow:0 8px 24px rgba(168,85,247,.25);transform:translateY(-1px)}.q.q-ai.active{background:linear-gradient(145deg,rgba(56,31,105,.95),rgba(32,18,68,.92));border-color:#c084fc;box-shadow:0 0 20px rgba(192,132,252,.35)}.q.q-ai .ai-sparkle{color:#c084fc;text-shadow:0 0 10px rgba(192,132,252,.7);transition:transform .3s ease}.q.q-ai:hover .ai-sparkle{transform:scale(1.25) rotate(15deg)}.search.ai-active input{border-color:#bd4cff!important;box-shadow:0 0 0 3px rgba(189,76,255,.25),0 12px 35px rgba(2,5,20,.5)!important}.q.q-publishing-3d{cursor:pointer;transition:all .25s ease}.q.q-publishing-3d:hover{border-color:#d4a843;box-shadow:0 8px 24px rgba(212,168,67,.28);transform:translateY(-1px)}.q.q-publishing-3d:focus-visible{outline:2px solid #d4a843;outline-offset:1px}.qicon{width:28px;height:28px;flex:0 0 28px;display:grid;place-items:center;color:#9e75ff;font-size:20px}.q b,.q small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.q b{font-size:11px;font-weight:600}.q small{font-size:8.5px;color:#7785a6}.content{padding:6px 10px 8px;display:grid;grid-template-columns:minmax(0,1fr) 215px;grid-template-rows:minmax(0,1fr);gap:6px;flex:1;min-height:0}.primary{display:flex;flex-direction:column;gap:6px;min-height:0;flex:1}.row{display:grid;grid-template-columns:minmax(210px,.95fr) minmax(260px,1.25fr) minmax(220px,1fr);gap:6px;flex:1.2;min-height:0}.row.three,.row.four{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;flex:1;min-height:0}.side{display:flex;flex-direction:column;gap:6px;min-height:0}.card{min-width:0;background:linear-gradient(145deg,rgba(17,24,48,.96),rgba(11,17,36,.97));border:1px solid rgba(86,99,151,.23);border-radius:10px;padding:11px;box-shadow:0 8px 24px rgba(1,4,17,.2);display:flex;flex-direction:column}.card h2{font-size:10.5px;margin:0 0 9px;display:flex;align-items:center;gap:6px;font-weight:600}.card h2 .sym{color:#a277ff}.card h2 a{margin-left:auto;color:#a989ff;font-size:8.5px;font-weight:500}.card h2 a:hover{color:#d1c1ff}.empty{border:1px dashed #283252;border-radius:8px;padding:13px 9px;text-align:center;color:#687695;font-size:9px}.empty b{display:block;color:#a8b2c9;font-size:10px;margin-bottom:3px}.visit,.item{display:grid;grid-template-columns:26px minmax(0,1fr) auto;align-items:center;gap:7px;padding:4px;border-radius:7px}.visit:hover,a.item:hover{background:#18213d}.visit b,.visit small,.item b,.item small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.visit b,.item b{font-size:9.5px;font-weight:550}.visit small,.item small{font-size:7.5px;color:#6d7a99}.visit em{font-style:normal;font-size:7.5px;color:#6d7a99}.tile,.file{width:26px;height:26px;border-radius:7px;display:grid;place-items:center;background:rgba(255,255,255,.06);font-size:9.5px;font-weight:700;overflow:hidden;flex-shrink:0;transition:transform .2s cubic-bezier(.34,1.56,.64,1)}.visit:hover .tile,a.item:hover .file{transform:scale(1.12)}.tile.tile-fallback{background:linear-gradient(145deg,#7658ed,#4331a7);color:#fff}.file.file-fallback{background:#1a2543;color:#9ba7c1}.dl-item{display:grid;grid-template-columns:1fr auto;align-items:center;gap:6px;padding:3px 5px;border-radius:7px;transition:background .15s ease}.dl-item:hover{background:#18213d}.dl-link{display:grid;grid-template-columns:26px minmax(0,1fr);align-items:center;gap:7px;min-width:0;cursor:pointer}.dl-info b,.dl-info small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.dl-info b{font-size:9.5px;font-weight:550;color:var(--text)}.dl-info small{font-size:7.5px;color:#6d7a99;margin-top:1px}.file-badge{width:26px;height:26px;border-radius:7px;display:grid;place-items:center;flex-shrink:0;transition:transform .2s cubic-bezier(.34,1.56,.64,1)}.dl-item:hover .file-badge{transform:scale(1.12)}.file-img{background:rgba(56,189,248,.14);color:#38bdf8;border:1px solid rgba(56,189,248,.25)}.file-pdf{background:rgba(239,68,68,.14);color:#f87171;border:1px solid rgba(239,68,68,.25)}.file-doc{background:rgba(96,165,250,.14);color:#60a5fa;border:1px solid rgba(96,165,250,.25)}.file-sheet{background:rgba(52,211,153,.14);color:#34d399;border:1px solid rgba(52,211,153,.25)}.file-pres{background:rgba(251,146,60,.14);color:#fb923c;border:1px solid rgba(251,146,60,.25)}.file-zip{background:rgba(250,204,21,.14);color:#facc15;border:1px solid rgba(250,204,21,.25)}.file-audio{background:rgba(192,132,252,.14);color:#c084fc;border:1px solid rgba(192,132,252,.25)}.file-video{background:rgba(244,114,182,.14);color:#f472b6;border:1px solid rgba(244,114,182,.25)}.file-code{background:rgba(45,212,191,.14);color:#2dd4bf;border:1px solid rgba(45,212,191,.25)}.file-exe{background:rgba(168,85,247,.14);color:#a855f7;border:1px solid rgba(168,85,247,.25)}.file-other{background:rgba(148,163,184,.14);color:#94a3b8;border:1px solid rgba(148,163,184,.25)}.dl-folder-btn{width:22px;height:22px;border-radius:5px;display:grid;place-items:center;color:var(--dim);opacity:0;transition:opacity .15s ease,background .15s ease,color .15s ease,transform .15s ease}.dl-item:hover .dl-folder-btn,.dl-folder-btn:focus-visible{opacity:1}.dl-folder-btn:hover{background:rgba(255,255,255,.1);color:var(--text);transform:scale(1.15)}.dl-mini-prog{height:3px;background:rgba(255,255,255,.1);border-radius:2px;overflow:hidden;margin-top:3px;width:100%}.dl-mini-prog span{display:block;height:100%;background:linear-gradient(90deg,#a855f7,#48c9ff);border-radius:2px}.tile-has-icon,.file-has-icon,.pinned-tile.tile-has-icon{background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.1)}.tile-icon,.pinned-tile .tile-icon{width:18px;height:18px;object-fit:contain;display:block;border-radius:3px}.feature{display:grid;grid-template-columns:82px 1fr;gap:12px;align-items:center}.cover{height:126px;border-radius:3px;padding:12px 6px;display:flex;align-items:center;text-align:center;background:linear-gradient(150deg,#22477d,#061a32 55%,#a87524);box-shadow:7px 7px 0 #081020,0 8px 20px #02050d;color:#f6cf78;font:10px Georgia,serif;letter-spacing:.5px}.feature h3{margin:0 0 7px;font-size:15px;font-weight:540;line-height:1.25}.feature p{color:#8895b3;font-size:9px;margin:0 0 12px}.actions{display:flex;gap:6px;flex-wrap:wrap}.pill{padding:6px 8px;border-radius:5px;background:linear-gradient(135deg,#8e5af4,#6353dd);font-size:8px}.pill.ghost{background:#10172d;border:1px solid #293354}.progress{height:3px;background:#242c48;border-radius:4px;overflow:hidden;margin-top:9px}.progress span{display:block;width:45%;height:100%;background:linear-gradient(90deg,#a35cff,#6b62ff)}.reading{display:grid;gap:7px}.read-item{display:grid;grid-template-columns:38px 1fr;gap:8px;align-items:center}.mini-cover{height:53px;border-radius:3px;background:linear-gradient(145deg,#b37a2a,#18142c 70%);box-shadow:3px 3px 0 #080d1e}.read-item b{font-size:9px;display:block}.read-item small{color:#73809e;font-size:7.5px}.clock{font-size:27px;line-height:1;font-weight:500}.clock small{font-size:11px;color:#b7a4ef}.date{color:#7784a2;font-size:8.5px;margin:6px 0 0}.weather{display:flex;align-items:center;gap:8px;margin-top:6px;padding-top:6px;border-top:1px solid #202945}.sun{font-size:24px;color:#f7c84b}.weather b{font-size:11px}.weather small{display:block;color:#7784a2;font-size:8px}.goal{display:grid;grid-template-columns:1fr 58px;gap:10px;align-items:center}.goal strong{font-size:20px}.goal small{display:block;color:#7784a2;font-size:8.5px}.ring{width:46px;height:46px;border-radius:50%;display:grid;place-items:center;background:#202945;position:relative}.ring:after{content:'';position:absolute;inset:6px;border-radius:50%;background:#10172d}.ring span{z-index:1;font-size:10px}.quote{padding:8px 10px;cursor:pointer;user-select:none;transition:border-color .2s ease}.quote:hover{border-color:rgba(154,115,255,.45)}.quote .marks{color:#9d65ff;font:26px Georgia,serif;line-height:.5}.quote p{font-size:11px;line-height:1.4;margin:4px 0;transition:opacity .15s ease}.quote small{color:#8692ad;font-size:8.5px;transition:opacity .15s ease}#quote-shuffle:hover{opacity:1!important;color:var(--accent)}#quick-note-card{flex:1;display:flex;flex-direction:column}#quick-note-card>div{flex:1;display:flex;flex-direction:column}.quick-note-textarea{width:100%;flex:1;min-height:50px;resize:none;background:rgba(0,0,0,.22);border:1px solid rgba(86,99,151,.25);border-radius:6px;padding:7px 9px;color:var(--text);font:11px/1.55 'Segoe UI Variable','Segoe UI',system-ui,sans-serif;box-sizing:border-box;outline:none;transition:border-color .2s ease,box-shadow .2s ease}.quick-note-textarea:focus{border-color:var(--accent);box-shadow:0 0 0 2px rgba(139,92,246,.2)}.quick-note-textarea::placeholder{color:var(--dim);opacity:.75}.pinned-list{display:flex;flex-direction:column;gap:2px}.pinned-item{display:grid;grid-template-columns:1fr auto;align-items:center;padding:3px 5px;border-radius:7px;transition:background .15s ease}.pinned-item:hover{background:#18213d}.pinned-link{display:grid;grid-template-columns:26px minmax(0,1fr);align-items:center;gap:7px;min-width:0;cursor:pointer;text-decoration:none}.pinned-tile{width:26px;height:26px;border-radius:7px;display:grid;place-items:center;color:#fff;font-size:11px;font-weight:700;box-shadow:0 2px 6px rgba(0,0,0,.25);transition:transform .2s cubic-bezier(.34,1.56,.64,1);flex-shrink:0}.pinned-item:hover .pinned-tile{transform:scale(1.12)}.pinned-info b,.pinned-info small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.pinned-info b{font-size:9.5px;font-weight:550;color:var(--text)}.pinned-info small{font-size:7.5px;color:#6d7a99;margin-top:1px}.pinned-del-btn{width:20px;height:20px;border:0;background:transparent;color:var(--dim);border-radius:4px;display:grid;place-items:center;cursor:pointer;opacity:0;padding:0;transition:opacity .15s ease,background .15s ease,color .15s ease,transform .15s ease}.pinned-item:hover .pinned-del-btn,.pinned-del-btn:focus-visible{opacity:1}.pinned-del-btn:hover{background:rgba(239,68,68,.2);color:#ef4444;transform:scale(1.15)}.pinned-form-box{margin-top:6px;padding:8px;border-radius:7px;background:rgba(0,0,0,.28);border:1px solid var(--line)}.pinned-form-input{width:100%;height:24px;border:1px solid var(--line);border-radius:5px;background:rgba(0,0,0,.25);color:var(--text);font-size:10px;padding:0 6px;outline:none;box-sizing:border-box;transition:border-color .2s ease}.pinned-form-input:focus{border-color:var(--accent)}@media(max-width:1180px){body{grid-template-columns:140px 1fr}.content{grid-template-columns:1fr}.side{grid-template-columns:repeat(4,1fr)}.row,.row.three{grid-template-columns:1fr 1fr}.row .card:last-child,.row.three .card:last-child{grid-column:1/-1}.row.four{grid-template-columns:1fr 1fr}}@media(max-width:820px){body{grid-template-columns:1fr}aside{display:none}.hero h1{font-size:23px}.quick,.row,.row.three,.row.four,.side{grid-template-columns:1fr 1fr}.row .card:last-child,.row.three .card:last-child{grid-column:auto}}@media(max-width:560px){.hero{padding:24px 12px 18px}.content{padding:8px}.quick,.row,.row.three,.row.four,.side{grid-template-columns:1fr}.feature{grid-template-columns:68px 1fr}.cover{height:105px}}
</style></head><body>)HTML";

  std::string html = kHead;
  if (light_appearance) {
    // Keep the dashboard visually consistent with the resolved native theme.
    // This override leaves the strict no-network CSP and document structure
    // unchanged; it only replaces the design tokens and matching surfaces.
    html += R"HTML(<style>:root{color-scheme:light;--bg:#f5f7fc;--panel:#fff;--panel2:#f8f9fd;--line:#dbe1ef;--text:#192033;--muted:#5c687f;--dim:#788399;--accent:#6941c6;--violet:#7c3aed;--cyan:#1877c9;--ok:#18794e}body{background:radial-gradient(circle at 65% 0,#e0e7ff 0,transparent 34%),var(--bg)}aside{background:linear-gradient(180deg,#fff,#f4f6fb);border-right-color:#dbe1ef}.nav:hover:not(.disabled){background:#e9ecf6;color:#182033}.nav.on{color:#28165d;background:linear-gradient(100deg,#e7ddff,#f4f0ff);border-color:#d5c5ff}.nav.disabled{color:#98a1b3}.hero{background:linear-gradient(180deg,#edf2ff,#f8f9ff 82%);border-bottom-color:#d8def0}.hero:before{background:radial-gradient(ellipse at 51% 80%,rgba(221,175,255,.75) 0,rgba(146,122,238,.32) 13%,transparent 31%),radial-gradient(ellipse at 50% 110%,rgba(109,190,255,.3),transparent 38%),linear-gradient(155deg,#dfe8ff 0,#eaf0ff 42%,#f0e8ff 72%,#f8f9ff 100%)}.hero:after{opacity:.35}.hero h1{color:#1d2440;text-shadow:none}.hero>p{color:#4e5b76}.search input{background:#fff;color:#1d2440;border-color:#bca8ff;box-shadow:0 8px 23px rgba(67,54,122,.12)}.search-btn-3d{background:transparent!important;box-shadow:none!important}.search-btn-3d:hover{background:transparent!important;box-shadow:none!important}.search-btn-3d img{filter:drop-shadow(0 2px 5px rgba(67,54,122,.35))}.home-3d-wrap img,.store-3d-wrap img,.search-web-3d-wrap img,.ask-ai-3d-wrap img,.notes-3d-wrap img,.download-3d-wrap img,.history-3d-wrap img,.settings-3d-wrap img,.bookmarks-3d-wrap img,.publishing-3d-wrap img{filter:drop-shadow(0 2px 5px rgba(67,54,122,.25))}.quick .q{background:linear-gradient(145deg,#fff,#f5f6fb);border-color:#d9ddeb;box-shadow:0 5px 14px rgba(42,52,86,.08)}.quick .q:hover{border-color:#8d6bea}.q small{color:#768199}.card{background:linear-gradient(145deg,#fff,#f7f8fc);border-color:#dce1ed;box-shadow:0 5px 16px rgba(40,52,84,.07)}.visit:hover,a.item:hover{background:#edf0f8}.visit small,.item small,.visit em,.empty,.date,.read-item small,.feature p,.goal small,.quote small{color:#69758d}.empty{border-color:#ccd4e4}.empty b{color:#424e66}.file{background:#e9edf6;color:#53617b}.dl-item:hover{background:#edf0f8}.dl-info small{color:#69758d}.file-img{background:#e0f2fe;color:#0284c7;border-color:#bae6fd}.file-pdf{background:#fee2e2;color:#dc2626;border-color:#fecaca}.file-doc{background:#dbeafe;color:#2563eb;border-color:#bfdbfe}.file-sheet{background:#d1fae5;color:#059669;border-color:#a7f3d0}.file-pres{background:#ffedd5;color:#ea580c;border-color:#fed7aa}.file-zip{background:#fef3c7;color:#d97706;border-color:#fde68a}.file-audio{background:#f3e8ff;color:#9333ea;border-color:#e9d5ff}.file-video{background:#fce7f3;color:#db2777;border-color:#fbcfe8}.file-code{background:#ccfbf1;color:#0d9488;border-color:#99f6e4}.file-exe{background:#ede9fe;color:#7c3aed;border-color:#ddd6fe}.file-other{background:#f1f5f9;color:#64748b;border-color:#e2e8f0}.dl-folder-btn:hover{background:rgba(0,0,0,.08);color:#192033}.dl-mini-prog{background:rgba(0,0,0,.08)}.tile-has-icon,.file-has-icon,.pinned-tile.tile-has-icon{background:#f1f4fa;border-color:rgba(0,0,0,.08)}.progress,.ring{background:#dde3f0}.ring:after{background:#fff}.weather{border-top-color:#dde2ee}.quick-note-textarea{background:#edf2fa;border-color:#d0d7e6;color:#192033}.quick-note-textarea:focus{background:#fff;border-color:var(--accent)}.q.q-search{cursor:pointer;transition:all .25s ease}.q.q-search:hover{border-color:#8d6bea;box-shadow:0 5px 16px rgba(105,65,198,.15)}.q.q-search.active{border-color:#7c3aed;background:linear-gradient(145deg,#fff,#f3e8ff);box-shadow:0 0 16px rgba(124,58,237,.22)}.q.q-ai{background:linear-gradient(145deg,#faf5ff,#f5f3ff);border-color:#d8b4fe;box-shadow:0 5px 14px rgba(147,51,234,.08)}.q.q-ai:hover{border-color:#a855f7;box-shadow:0 5px 18px rgba(147,51,234,.16)}.q.q-ai.active{background:linear-gradient(145deg,#f3e8ff,#ede9fe);border-color:#9333ea;box-shadow:0 0 16px rgba(147,51,234,.22)}.search.ai-active input{border-color:#a855f7!important;box-shadow:0 0 0 3px rgba(168,85,247,.2),0 8px 23px rgba(67,54,122,.12)!important}.q.q-publishing-3d{cursor:pointer;transition:all .25s ease}.q.q-publishing-3d:hover{border-color:#c99732;box-shadow:0 5px 16px rgba(201,151,50,.2)}.pinned-item:hover{background:#edf0f8}.pinned-info small{color:#69758d}.pinned-del-btn:hover{background:rgba(220,38,38,.15);color:#dc2626}.pinned-form-box{background:#edf2fa;border-color:#d0d7e6}.pinned-form-input{background:#fff;border-color:#d0d7e6;color:#192033}</style>)HTML";
  }
  const std::string& home_3d_anim = mtk::assets::WebpDataUrl(L"mtk_home_3d.webp");
  const std::string& home_3d_still = mtk::assets::WebpDataUrl(L"mtk_home_3d_still.webp");
  const std::string& store_3d_anim = mtk::assets::WebpDataUrl(L"mtk_store_3d.webp");
  const std::string& store_3d_still = mtk::assets::WebpDataUrl(L"mtk_store_3d_still.webp");
  const std::string& notes_3d_anim = mtk::assets::WebpDataUrl(L"mtk_notes_3d.webp");
  const std::string& notes_3d_still = mtk::assets::WebpDataUrl(L"mtk_notes_3d_still.webp");
  const std::string& download_3d_anim = mtk::assets::WebpDataUrl(L"mtk_download_3d.webp");
  const std::string& download_3d_still = mtk::assets::WebpDataUrl(L"mtk_download_3d_still.webp");
  const std::string& history_3d_anim = mtk::assets::WebpDataUrl(L"mtk_history_3d.webp");
  const std::string& history_3d_still = mtk::assets::WebpDataUrl(L"mtk_history_3d_still.webp");
  const std::string& bookmarks_3d_anim = mtk::assets::WebpDataUrl(L"mtk_bookmarks_3d.webp");
  const std::string& bookmarks_3d_still = mtk::assets::WebpDataUrl(L"mtk_bookmarks_3d_still.webp");
  const std::string& settings_3d_anim = mtk::assets::WebpDataUrl(L"mtk_settings_3d.webp");
  const std::string& settings_3d_still = mtk::assets::WebpDataUrl(L"mtk_settings_3d_still.webp");
  const std::string& search_web_3d_anim = mtk::assets::WebpDataUrl(L"mtk_search_web_3d.webp");
  const std::string& search_web_3d_still = mtk::assets::WebpDataUrl(L"mtk_search_web_3d_still.webp");
  const std::string& ask_ai_3d_anim = mtk::assets::WebpDataUrl(L"mtk_ask_ai_3d.webp");
  const std::string& ask_ai_3d_still = mtk::assets::WebpDataUrl(L"mtk_ask_ai_3d_still.webp");
  const std::string& publishing_3d_anim = mtk::assets::WebpDataUrl(L"mtk_publishing_3d.webp");
  const std::string& publishing_3d_still = mtk::assets::WebpDataUrl(L"mtk_publishing_3d_still.webp");
  std::string home_nav_item;
  if (!home_3d_anim.empty()) {
    const std::string& still_src = home_3d_still.empty() ? home_3d_anim : home_3d_still;
    home_nav_item = "<div class=\"nav on home-nav-3d\" tabindex=0 role=link aria-label=\"Home\">"
                    "<span class=home-3d-wrap>"
                    "<img src=\"" + still_src + "\" class=home-3d-still alt=\"\">"
                    "<img src=\"" + home_3d_anim + "\" class=home-3d-anim alt=\"\">"
                    "</span>Home</div>";
  } else {
    home_nav_item = "<div class=\"nav on\"><span class=ico>⌂</span>Home</div>";
  }
  std::string store_nav_item;
  if (!store_3d_anim.empty()) {
    const std::string& still_src = store_3d_still.empty() ? store_3d_anim : store_3d_still;
    store_nav_item = "<div class=\"nav store-nav-3d\" tabindex=0 role=link aria-label=\"Store\">"
                     "<span class=store-3d-wrap>"
                     "<img src=\"" + still_src + "\" class=store-3d-still alt=\"\">"
                     "<img src=\"" + store_3d_anim + "\" class=store-3d-anim alt=\"\">"
                     "</span>Store<span class=soon>SOON</span></div>";
  } else {
    store_nav_item = "<span class=\"nav disabled\"><span class=ico>🏬</span>Store<span class=soon>SOON</span></span>";
  }
  std::string notes_nav_item;
  if (!notes_3d_anim.empty()) {
    const std::string& still_src = notes_3d_still.empty() ? notes_3d_anim : notes_3d_still;
    notes_nav_item = "<a class=\"nav notes-nav-3d\" href=\"javascript:void(0)\" id=nav-notes role=link aria-label=\"Notes\">"
                     "<span class=notes-3d-wrap>"
                     "<img src=\"" + still_src + "\" class=notes-3d-still alt=\"\">"
                     "<img src=\"" + notes_3d_anim + "\" class=notes-3d-anim alt=\"\">"
                     "</span>Notes</a>";
  } else {
    notes_nav_item = "<a class=nav href=\"javascript:void(0)\" id=nav-notes><span class=ico>▧</span>Notes</a>";
  }
  std::string download_nav_item;
  if (!download_3d_anim.empty()) {
    const std::string& still_src = download_3d_still.empty() ? download_3d_anim : download_3d_still;
    download_nav_item = "<a class=\"nav download-nav-3d\" href=\"mtk://home/action/downloads\" role=link aria-label=\"Downloads\">"
                        "<span class=download-3d-wrap>"
                        "<img src=\"" + still_src + "\" class=download-3d-still alt=\"\">"
                        "<img src=\"" + download_3d_anim + "\" class=download-3d-anim alt=\"\">"
                        "</span>Downloads</a>";
  } else {
    download_nav_item = "<a class=nav href=\"mtk://home/action/downloads\"><span class=ico>⇩</span>Downloads</a>";
  }
  std::string history_nav_item;
  if (!history_3d_anim.empty()) {
    const std::string& still_src = history_3d_still.empty() ? history_3d_anim : history_3d_still;
    history_nav_item = "<a class=\"nav history-nav-3d\" href=\"mtk://home/action/history\" role=link aria-label=\"History\">"
                       "<span class=history-3d-wrap>"
                       "<img src=\"" + still_src + "\" class=history-3d-still alt=\"\">"
                       "<img src=\"" + history_3d_anim + "\" class=history-3d-anim alt=\"\">"
                       "</span>History</a>";
  } else {
    history_nav_item = "<a class=nav href=\"mtk://home/action/history\"><span class=ico>◷</span>History</a>";
  }
  std::string bookmarks_nav_item;
  if (!bookmarks_3d_anim.empty()) {
    const std::string& still_src = bookmarks_3d_still.empty() ? bookmarks_3d_anim : bookmarks_3d_still;
    bookmarks_nav_item = "<a class=\"nav bookmarks-nav-3d\" href=\"mtk://home/action/bookmarks\" role=link aria-label=\"Bookmarks\">"
                         "<span class=bookmarks-3d-wrap>"
                         "<img src=\"" + still_src + "\" class=bookmarks-3d-still alt=\"\">"
                         "<img src=\"" + bookmarks_3d_anim + "\" class=bookmarks-3d-anim alt=\"\">"
                         "</span>Bookmarks</a>";
  } else {
    bookmarks_nav_item = "<a class=nav href=\"mtk://home/action/bookmarks\"><span class=ico>☆</span>Bookmarks</a>";
  }
  std::string settings_nav_item;
  if (!settings_3d_anim.empty()) {
    const std::string& still_src = settings_3d_still.empty() ? settings_3d_anim : settings_3d_still;
    settings_nav_item = "<a class=\"nav settings-nav-3d\" href=\"mtk://home/action/settings\" role=link aria-label=\"Settings\">"
                        "<span class=settings-3d-wrap>"
                        "<img src=\"" + still_src + "\" class=settings-3d-still alt=\"\">"
                        "<img src=\"" + settings_3d_anim + "\" class=settings-3d-anim alt=\"\">"
                        "</span>Settings</a>";
  } else {
    settings_nav_item = "<a class=nav href=\"mtk://home/action/settings\"><span class=ico>⚙</span>Settings</a>";
  }
  html += R"HTML(<aside>)HTML";
  html += home_nav_item;

  html += store_nav_item;
  html += notes_nav_item;
  html += download_nav_item;
  html += history_nav_item;
  html += bookmarks_nav_item;
  html += settings_nav_item;
  html += R"HTML(</aside><main><section class=hero><h1><span id=home-greeting>)HTML";
  html += Greeting();
  html += R"HTML(</span>! <span>👋</span></h1><p>What would you like to explore today?</p>)HTML";
  const std::string& search_3d_anim = mtk::assets::WebpDataUrl(L"mtk_search_3d.webp");
  const std::string& search_3d_still = mtk::assets::WebpDataUrl(L"mtk_search_3d_still.webp");
  if (!search_3d_anim.empty()) {
    const std::string& still_src = search_3d_still.empty() ? search_3d_anim : search_3d_still;
    html += "<form class=search id=search-form method=get action=\"https://www.google.com/search\">"
            "<input id=search-box name=q type=search placeholder=\"Search the web or type a URL...\" autofocus autocomplete=off spellcheck=false aria-label=\"Search the web\">"
            "<input type=hidden name=safe value=active>"
            "<input type=hidden name=udm id=search-udm value=\"\" disabled>"
            "<button type=submit class=search-btn-3d aria-label=Search title=Search>"
            "<img src=\"" + still_src + "\" class=search-3d-still alt=\"\">"
            "<img src=\"" + search_3d_anim + "\" class=search-3d-anim alt=\"\">"
            "</button>"
            "</form>";
  } else {
    html += "<form class=search id=search-form method=get action=\"https://www.google.com/search\"><input id=search-box name=q type=search placeholder=\"Search the web or type a URL...\" autofocus autocomplete=off spellcheck=false aria-label=\"Search the web\"><input type=hidden name=safe value=active><input type=hidden name=udm id=search-udm value=\"\" disabled><button type=submit aria-label=Search>◆</button></form>";
  }
  std::string store_quick_item;
  if (!store_3d_anim.empty()) {
    const std::string& still_src = store_3d_still.empty() ? store_3d_anim : store_3d_still;
    store_quick_item = "<span class=\"q q-store-3d\" role=link tabindex=0 title=\"Store\">"
                       "<span class=store-3d-wrap>"
                       "<img src=\"" + still_src + "\" class=store-3d-still alt=\"\">"
                       "<img src=\"" + store_3d_anim + "\" class=store-3d-anim alt=\"\">"
                       "</span><span><b>Store</b><small>Extensions & Themes</small></span></span>";
  } else {
    store_quick_item = "<span class=\"q muted\"><span class=qicon>🏬</span><span><b>Store</b><small>Extensions & Themes</small></span></span>";
  }
  std::string search_web_quick_item;
  if (!search_web_3d_anim.empty()) {
    const std::string& still_src = search_web_3d_still.empty() ? search_web_3d_anim : search_web_3d_still;
    search_web_quick_item = "<div class=\"q q-search q-search-3d active\" id=quick-search-web tabindex=0 role=button title=\"Search the Web\">"
                            "<span class=search-web-3d-wrap>"
                            "<img src=\"" + still_src + "\" class=search-web-3d-still alt=\"\">"
                            "<img src=\"" + search_web_3d_anim + "\" class=search-web-3d-anim alt=\"\">"
                            "</span><span><b>Search Web</b><small>Explore the internet</small></span></div>";
  } else {
    search_web_quick_item = "<div class=\"q q-search active\" id=quick-search-web tabindex=0 role=button title=\"Search the Web\"><span class=qicon>◎</span><span><b>Search Web</b><small>Explore the internet</small></span></div>";
  }
  std::string ask_ai_quick_item;
  if (!ask_ai_3d_anim.empty()) {
    const std::string& still_src = ask_ai_3d_still.empty() ? ask_ai_3d_anim : ask_ai_3d_still;
    ask_ai_quick_item = "<div class=\"q q-ai q-ai-3d\" id=quick-ask-ai tabindex=0 role=button title=\"Ask Google AI (AI Overview)\">"
                        "<span class=ask-ai-3d-wrap>"
                        "<img src=\"" + still_src + "\" class=ask-ai-3d-still alt=\"\">"
                        "<img src=\"" + ask_ai_3d_anim + "\" class=ask-ai-3d-anim alt=\"\">"
                        "</span><span><b>Ask AI</b><small>Google AI Answers</small></span></div>";
  } else {
    ask_ai_quick_item = "<div class=\"q q-ai\" id=quick-ask-ai tabindex=0 role=button title=\"Ask Google AI (AI Overview)\"><span class=\"qicon ai-sparkle\">✦</span><span><b>Ask AI</b><small>Google AI Answers</small></span></div>";
  }
  std::string publishing_quick_item;
  if (!publishing_3d_anim.empty()) {
    const std::string& still_src = publishing_3d_still.empty() ? publishing_3d_anim : publishing_3d_still;
    publishing_quick_item = "<div class=\"q q-publishing-3d\" id=quick-featured-publishing tabindex=0 role=button title=\"Featured: MTK Publishing\">"
                            "<span class=publishing-3d-wrap>"
                            "<img src=\"" + still_src + "\" class=publishing-3d-still alt=\"\">"
                            "<img src=\"" + publishing_3d_anim + "\" class=publishing-3d-anim alt=\"\">"
                            "</span><span><b>Featured</b><small>MTK Publishing</small></span></div>";
  } else {
    publishing_quick_item = "<span class=\"q muted\"><span class=qicon>♧</span><span><b>Featured</b><small>MTK Publishing</small></span></span>";
  }
  html += "<div class=quick>" + search_web_quick_item + ask_ai_quick_item + store_quick_item + publishing_quick_item + "</div></section><div class=content><div class=primary><div class=row><section class=card><h2><span class=sym>◉</span>Frequently Visited <a href=\"mtk://home/action/history\">View All</a></h2>";
  html += frequent_html;
  html += R"HTML(</section><section class=card id=featured-collection-card><h2><span class=sym>◇</span>Featured Collection</h2><div class=feature><div class=cover>THE ORIGINS AND BELIEF OF SHIA ISLAM</div><div><h3>The Origins and Belief of Shia Islam</h3><p>A deep dive into history, beliefs and teachings from the MTK Publishing catalogue.</p><div class=actions><span class=pill>Read Preview</span><span class="pill ghost">Coming Soon</span></div><div class=progress><span></span></div></div></div></section><section class=card><h2><span class=sym>▤</span>Continue Reading</h2><div class=reading><div class=read-item><div class=mini-cover></div><div><b>Your reading queue is ready</b><small>Open a supported book to begin</small><div class=progress><span></span></div></div></div>)HTML";
  html += Empty("Nothing in progress",
      "Reading activity will appear after the MTK reader is added.");
  html += R"HTML(</div></section></div><div class="row three"><section class=card><h2><span class=sym>⇩</span>Recent Downloads <a href="mtk://home/action/downloads">View All</a></h2>)HTML";
  html += download_html;
  html += R"HTML(</section><section class=card><h2><span class=sym>☆</span>Bookmarks <a href="mtk://home/action/bookmarks">View All</a></h2>)HTML";
  html += bookmark_html;
  html += R"HTML(</section><section class=card id=pinned-sites-card><h2><span class=sym>⌘</span>Pinned Sites <a href="javascript:void(0)" id=add-pinned-btn title="Add a pinned site">+ Add</a></h2><div id=pinned-sites-list class=pinned-list></div><div id=pinned-form-box class=pinned-form-box style="display:none;"><div style="font-size:9.5px;font-weight:600;margin-bottom:6px;color:var(--text);display:flex;align-items:center;justify-content:space-between;"><span>Add Pinned Site</span><a href="javascript:void(0)" id=pinned-form-close style="color:var(--muted);font-size:11px;text-decoration:none;">✕</a></div><div style="display:flex;flex-direction:column;gap:5px;"><input id=pinned-name-input class=pinned-form-input type=text placeholder="Name (e.g. YouTube)" autocomplete=off><input id=pinned-url-input class=pinned-form-input type=text placeholder="URL (e.g. youtube.com)" autocomplete=off><div style="display:flex;justify-content:flex-end;gap:5px;margin-top:2px;"><button id=pinned-cancel-btn style="border:1px solid var(--line);border-radius:5px;background:transparent;color:var(--muted);font-size:9.5px;padding:2px 8px;cursor:pointer;">Cancel</button><button id=pinned-save-btn style="border:0;border-radius:5px;background:var(--accent);color:#fff;font-size:9.5px;padding:2px 10px;cursor:pointer;font-weight:600;">Pin</button></div></div></div></section></div></div><div class=side>)HTML";
  html += CurrentTimeCard();
  html += R"HTML(<section class=card id=weather-card><h2><span class=sym>☀</span>Weather<a href="javascript:void(0)" id=weather-refresh title="Refresh weather" style="margin-left:auto;color:inherit;text-decoration:none;font-size:12px;opacity:.7;padding:0 3px;">↻</a><a href="javascript:void(0)" id=weather-city-btn title="Change city" style="margin-left:4px;color:inherit;text-decoration:none;font-size:11px;opacity:.7;padding:0 3px;">✎</a></h2><div class=weather id=weather-display><span class=sun id=weather-icon>☼</span><span style="flex:1;min-width:0;"><b id=weather-temp style="font-size:13px;display:flex;align-items:center;gap:6px;"><span id=weather-temp-val>--°C</span><span id=weather-desc style="font-size:10px;font-weight:normal;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">Checking weather…</span></b><small id=weather-loc style="color:var(--dim);margin-top:2px;">Locating…</small></span></div><div id=weather-input-box style="display:none;margin-top:8px;padding-top:8px;border-top:1px solid rgba(86,99,151,.2);"><div style="display:flex;gap:4px;"><input id=weather-city-input type=text placeholder="Enter city name..." style="flex:1;min-width:0;height:24px;border:1px solid var(--line);border-radius:5px;background:rgba(0,0,0,.25);color:var(--text);font-size:10px;padding:0 6px;outline:none;"><button id=weather-save-btn style="border:0;border-radius:5px;background:var(--accent);color:#fff;font-size:9.5px;padding:0 8px;cursor:pointer;">Set</button><button id=weather-auto-btn title="Auto-detect via IP" style="border:1px solid var(--line);border-radius:5px;background:transparent;color:var(--muted);font-size:9.5px;padding:0 6px;cursor:pointer;">Auto</button></div></div></section><section class=card><h2><span class=sym>◷</span>Daily Goal <a href="mtk://home/action/settings">Edit</a></h2><div class=goal><div><small>Reading time</small><strong>0</strong><small>No goal configured</small></div><div class=ring><span>0%</span></div></div></section>)HTML";
  html += CurrentQuoteCard();
  html += R"HTML(<section class=card id=quick-note-card><h2><span class=sym>✎</span>Quick Note<span id=quick-note-status style="margin-left:auto;font-size:8.5px;font-weight:500;color:var(--ok);opacity:0;transition:opacity .25s ease;">Saved</span><a href="javascript:void(0)" id=quick-note-copy title="Copy note text" style="margin-left:6px;color:var(--muted);font-size:8.5px;text-decoration:none;padding:0 3px;">Copy</a><a href="javascript:void(0)" id=quick-note-clear title="Clear note" style="margin-left:4px;color:var(--muted);font-size:8.5px;text-decoration:none;padding:0 3px;">Clear</a></h2><div style="display:flex;flex-direction:column;gap:5px;margin-top:2px;flex:1;"><textarea id=quick-note-input class=quick-note-textarea placeholder="Write your thoughts, reminders or links here..." spellcheck=false aria-label="Quick Note"></textarea><div style="display:flex;align-items:center;justify-content:space-between;font-size:8px;color:var(--dim);padding:0 2px;"><span id=quick-note-count>0 chars</span><span id=quick-note-time>Auto-saved locally</span></div></div></section></div></div></main><script>
var BUILTIN_FAVICONS = )HTML";
  html += BuildKnownFaviconsJson(frequent, bookmarks);
  html += R"HTML(;
(function(){
  function updateTime(){
    var now=new Date();
    var h=now.getHours();
    var m=now.getMinutes();
    var ampm=h>=12?'PM':'AM';
    var h12=h%12||12;
    var hs=(h12<10?'0':'')+h12;
    var ms=(m<10?'0':'')+m;
    var c=document.getElementById('home-clock');
    if(c)c.innerHTML=hs+':'+ms+' <small>'+ampm+'</small>';
    var days=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
    var months=['January','February','March','April','May','June','July','August','September','October','November','December'];
    var d=document.getElementById('home-date');
    if(d)d.textContent=days[now.getDay()]+', '+now.getDate()+' '+months[now.getMonth()]+' '+now.getFullYear();
    var g=document.getElementById('home-greeting');
    if(g){
      var hr=now.getHours();
      g.textContent=hr<12?'Good morning':(hr<17?'Good afternoon':'Good evening');
    }
  }
  updateTime();
  setInterval(updateTime,1000);

  function getWeatherIcon(desc){
    desc=(desc||'').toLowerCase();
    if(desc.indexOf('sun')!==-1||desc.indexOf('clear')!==-1)return '☀️';
    if(desc.indexOf('partly')!==-1)return '⛅';
    if(desc.indexOf('cloud')!==-1||desc.indexOf('overcast')!==-1)return '☁️';
    if(desc.indexOf('rain')!==-1||desc.indexOf('drizzle')!==-1||desc.indexOf('shower')!==-1)return '🌧️';
    if(desc.indexOf('thunder')!==-1||desc.indexOf('storm')!==-1)return '⛈️';
    if(desc.indexOf('snow')!==-1||desc.indexOf('ice')!==-1||desc.indexOf('sleet')!==-1)return '❄️';
    if(desc.indexOf('fog')!==-1||desc.indexOf('mist')!==-1||desc.indexOf('haze')!==-1)return '🌫️';
    return '☀️';
  }

  function renderWeather(data,isCached){
    try{
      var cond=data.current_condition&&data.current_condition[0];
      var area=data.nearest_area&&data.nearest_area[0];
      if(!cond)return;
      var temp=cond.temp_C+'°C';
      var desc=(cond.weatherDesc&&cond.weatherDesc[0]&&cond.weatherDesc[0].value)||'Clear';
      var city=(area&&area.areaName&&area.areaName[0]&&area.areaName[0].value)||'';
      var region=(area&&area.region&&area.region[0]&&area.region[0].value)||'';
      var locText=city+(region?(', '+region):'');
      if(isCached)locText+=' (cached)';

      var iconEl=document.getElementById('weather-icon');
      var tempValEl=document.getElementById('weather-temp-val');
      var descEl=document.getElementById('weather-desc');
      var locEl=document.getElementById('weather-loc');

      if(iconEl)iconEl.textContent=getWeatherIcon(desc);
      if(tempValEl)tempValEl.textContent=temp;
      if(descEl)descEl.textContent=desc;
      if(locEl)locEl.textContent=locText;
    }catch(e){}
  }

  function fetchWeather(force){
    var savedCity=localStorage.getItem('mtk_weather_city')||'';
    var cachedData=null;
    try{
      var raw=localStorage.getItem('mtk_weather_cache');
      if(raw)cachedData=JSON.parse(raw);
    }catch(e){}

    var now=Date.now();
    if(!force&&cachedData&&cachedData.timestamp&&(now-cachedData.timestamp<1200000)&&cachedData.cityKey===savedCity){
      renderWeather(cachedData.data,false);
      return;
    }

    if(cachedData&&cachedData.data){
      renderWeather(cachedData.data,true);
    }

    var descEl=document.getElementById('weather-desc');
    if(descEl&&!cachedData)descEl.textContent='Checking…';

    var url='https://wttr.in/'+(savedCity?encodeURIComponent(savedCity):'')+'?format=j1';
    var controller=new AbortController();
    var timeoutId=setTimeout(function(){controller.abort();},6000);

    fetch(url,{signal:controller.signal})
      .then(function(res){
        clearTimeout(timeoutId);
        if(!res.ok)throw new Error('HTTP '+res.status);
        return res.json();
      })
      .then(function(json){
        renderWeather(json,false);
        try{
          localStorage.setItem('mtk_weather_cache',JSON.stringify({
            timestamp:Date.now(),
            cityKey:savedCity,
            data:json
          }));
        }catch(e){}
      })
      .catch(function(err){
        clearTimeout(timeoutId);
        if(!cachedData){
          var locEl=document.getElementById('weather-loc');
          var descEl=document.getElementById('weather-desc');
          if(descEl)descEl.textContent='Offline';
          if(locEl)locEl.textContent='Click ↻ to retry';
        }
      });
  }

  var refreshBtn=document.getElementById('weather-refresh');
  if(refreshBtn){
    refreshBtn.addEventListener('click',function(e){
      e.preventDefault();
      fetchWeather(true);
    });
  }

  var cityBtn=document.getElementById('weather-city-btn');
  var inputBox=document.getElementById('weather-input-box');
  var cityInput=document.getElementById('weather-city-input');
  var saveBtn=document.getElementById('weather-save-btn');
  var autoBtn=document.getElementById('weather-auto-btn');

  if(cityBtn&&inputBox){
    cityBtn.addEventListener('click',function(e){
      e.preventDefault();
      var isHidden=inputBox.style.display==='none';
      inputBox.style.display=isHidden?'block':'none';
      if(isHidden&&cityInput){
        cityInput.value=localStorage.getItem('mtk_weather_city')||'';
        cityInput.focus();
      }
    });
  }

  if(saveBtn&&cityInput){
    saveBtn.addEventListener('click',function(){
      var val=cityInput.value.trim();
      if(val){
        localStorage.setItem('mtk_weather_city',val);
        inputBox.style.display='none';
        fetchWeather(true);
      }
    });
    cityInput.addEventListener('keydown',function(e){
      if(e.key==='Enter'){
        saveBtn.click();
      }
    });
  }

  if(autoBtn){
    autoBtn.addEventListener('click',function(){
      localStorage.removeItem('mtk_weather_city');
      if(cityInput)cityInput.value='';
      inputBox.style.display='none';
      fetchWeather(true);
    });
  }

  fetchWeather(false);
  setInterval(function(){fetchWeather(false);},1200000);

  var quotes=[
    {text:"The best way to predict the future is to create it.",author:"Peter Drucker"},
    {text:"In the middle of difficulty lies opportunity.",author:"Albert Einstein"},
    {text:"The only way to do great work is to love what you do.",author:"Steve Jobs"},
    {text:"Live as if you were to die tomorrow. Learn as if you were to live forever.",author:"Mahatma Gandhi"},
    {text:"You have to dream before your dreams can come true.",author:"A. P. J. Abdul Kalam"},
    {text:"What you seek is seeking you.",author:"Rumi"},
    {text:"It always seems impossible until it is done.",author:"Nelson Mandela"},
    {text:"Knowing yourself is the beginning of all wisdom.",author:"Aristotle"},
    {text:"The journey of a thousand miles begins with a single step.",author:"Lao Tzu"},
    {text:"Stay hungry, stay foolish.",author:"Steve Jobs"},
    {text:"Simplicity is the ultimate sophistication.",author:"Leonardo da Vinci"},
    {text:"Everything you've ever wanted is on the other side of fear.",author:"George Addair"},
    {text:"Success is not final, failure is not fatal: it is the courage to continue that counts.",author:"Winston Churchill"},
    {text:"Believe you can and you're halfway there.",author:"Theodore Roosevelt"},
    {text:"Act as if what you do makes a difference. It does.",author:"William James"},
    {text:"Turn your wounds into wisdom.",author:"Oprah Winfrey"},
    {text:"The mind is everything. What you think you become.",author:"Buddha"},
    {text:"Do what you can, with what you have, where you are.",author:"Theodore Roosevelt"},
    {text:"Be the change that you wish to see in the world.",author:"Mahatma Gandhi"},
    {text:"If you cannot do great things, do small things in a great way.",author:"Napoleon Hill"},
    {text:"Do not go where the path may lead, go instead where there is no path and leave a trail.",author:"Ralph Waldo Emerson"},
    {text:"The secret of getting ahead is getting started.",author:"Mark Twain"},
    {text:"Quality is not an act, it is a habit.",author:"Aristotle"},
    {text:"Wisdom begins in wonder.",author:"Socrates"},
    {text:"Not all those who wander are lost.",author:"J.R.R. Tolkien"},
    {text:"Peace comes from within. Do not seek it without.",author:"Buddha"},
    {text:"It is never too late to be what you might have been.",author:"George Eliot"},
    {text:"Happiness depends upon ourselves.",author:"Aristotle"},
    {text:"Every moment is a fresh beginning.",author:"T.S. Eliot"},
    {text:"To know what you know and what you do not know, that is true knowledge.",author:"Confucius"},
    {text:"Patience is a key element of success.",author:"Bill Gates"},
    {text:"Strive not to be a success, but rather to be of value.",author:"Albert Einstein"},
    {text:"The roots of education are bitter, but the fruit is sweet.",author:"Aristotle"},
    {text:"Your time is limited, so don't waste it living someone else's life.",author:"Steve Jobs"},
    {text:"Imagination is more important than knowledge.",author:"Albert Einstein"},
    {text:"The power of imagination makes us infinite.",author:"John Muir"}
  ];
  function getDailyQuoteIdx(d){
    d=d||new Date();
    return Math.abs(d.getFullYear()*366+(d.getMonth()+1)*31+d.getDate())%quotes.length;
  }
  var curQuoteIdx=getDailyQuoteIdx();
  var quoteCard=document.getElementById('quote-card');
  var quoteShuffle=document.getElementById('quote-shuffle');
  var quoteText=document.getElementById('quote-text');
  var quoteAuthor=document.getElementById('quote-author');
  var userShuffled=false;

  function renderQuote(text, author){
    if(!quoteText||!quoteAuthor)return;
    quoteText.style.opacity='0';
    quoteAuthor.style.opacity='0';
    setTimeout(function(){
      quoteText.textContent=text;
      quoteAuthor.textContent='— '+author;
      quoteText.style.opacity='1';
      quoteAuthor.style.opacity='1';
    },150);
  }

  function fetchLiveQuote(isRandom){
    var dayOffset = Math.abs(new Date().getFullYear()*366 + (new Date().getMonth()+1)*31 + new Date().getDate()) % 1450 + 1;
    var url = isRandom ? 'https://dummyjson.com/quotes/random' : ('https://dummyjson.com/quotes/' + dayOffset);
    var timer = setTimeout(function(){
      if(isRandom){
        curQuoteIdx = (curQuoteIdx + 1) % quotes.length;
        renderQuote(quotes[curQuoteIdx].text, quotes[curQuoteIdx].author);
      }
    }, 4000);

    fetch(url)
      .then(function(res){ return res.json(); })
      .then(function(data){
        clearTimeout(timer);
        if(data && data.quote && data.author){
          renderQuote(data.quote, data.author);
          if(!isRandom){
            try {
              var todayStr = new Date().toISOString().slice(0,10);
              localStorage.setItem('mtk_daily_quote_date', todayStr);
              localStorage.setItem('mtk_daily_quote_text', data.quote);
              localStorage.setItem('mtk_daily_quote_author', data.author);
            } catch(e){}
          }
        }
      })
      .catch(function(err){
        clearTimeout(timer);
        curQuoteIdx = (curQuoteIdx + 1) % quotes.length;
        renderQuote(quotes[curQuoteIdx].text, quotes[curQuoteIdx].author);
      });
  }

  function advanceQuote(e){
    if(e)e.stopPropagation();
    userShuffled=true;
    fetchLiveQuote(true);
  }

  if(quoteShuffle){
    quoteShuffle.addEventListener('click',advanceQuote);
  }
  if(quoteCard){
    quoteCard.addEventListener('click',function(e){
      if(e.target===quoteShuffle)return;
      advanceQuote(e);
    });
  }

  try {
    var todayStr = new Date().toISOString().slice(0,10);
    var savedDate = localStorage.getItem('mtk_daily_quote_date');
    var savedText = localStorage.getItem('mtk_daily_quote_text');
    var savedAuthor = localStorage.getItem('mtk_daily_quote_author');
    if(savedDate === todayStr && savedText && savedAuthor){
      renderQuote(savedText, savedAuthor);
    } else {
      fetchLiveQuote(false);
    }
  } catch(e){
    fetchLiveQuote(false);
  }

  setInterval(function(){
    if(!userShuffled){
      try {
        var todayStr = new Date().toISOString().slice(0,10);
        var savedDate = localStorage.getItem('mtk_daily_quote_date');
        if(savedDate !== todayStr){
          fetchLiveQuote(false);
        }
      } catch(e){}
    }
  },60000);

  var noteInput=document.getElementById('quick-note-input');
  var noteStatus=document.getElementById('quick-note-status');
  var noteCount=document.getElementById('quick-note-count');
  var noteTime=document.getElementById('quick-note-time');
  var noteCopy=document.getElementById('quick-note-copy');
  var noteClear=document.getElementById('quick-note-clear');
  var navNotes=document.getElementById('nav-notes');
  var statusTimeout=null;

  function flashNoteStatus(msg,color){
    if(!noteStatus)return;
    noteStatus.textContent=msg||'Saved';
    noteStatus.style.color=color||'var(--ok)';
    noteStatus.style.opacity='1';
    clearTimeout(statusTimeout);
    statusTimeout=setTimeout(function(){
      noteStatus.style.opacity='0';
    },1800);
  }

  function updateNoteMeta(val){
    if(noteCount){
      var len=(val||'').length;
      noteCount.textContent=len===1?'1 char':(len+' chars');
    }
  }

  if(noteInput){
    try{
      var savedNote=localStorage.getItem('mtk_quick_note')||'';
      var savedTime=localStorage.getItem('mtk_quick_note_time')||'';
      noteInput.value=savedNote;
      updateNoteMeta(savedNote);
      if(savedTime&&noteTime){
        noteTime.textContent='Saved at '+savedTime;
      }
    }catch(e){}

    var saveDebounce=null;
    noteInput.addEventListener('input',function(){
      var val=noteInput.value;
      updateNoteMeta(val);
      clearTimeout(saveDebounce);
      saveDebounce=setTimeout(function(){
        try{
          localStorage.setItem('mtk_quick_note',val);
          var now=new Date();
          var h=now.getHours();
          var m=now.getMinutes();
          var ampm=h>=12?'PM':'AM';
          var timeStr=(h%12||12)+':'+(m<10?'0':'')+m+' '+ampm;
          localStorage.setItem('mtk_quick_note_time',timeStr);
          if(noteTime)noteTime.textContent='Saved at '+timeStr;
          flashNoteStatus('Saved');
        }catch(e){}
      },350);
    });
  }

  if(noteCopy&&noteInput){
    noteCopy.addEventListener('click',function(e){
      e.preventDefault();
      var text=noteInput.value;
      if(!text){
        flashNoteStatus('Empty note','var(--dim)');
        return;
      }
      if(navigator.clipboard&&navigator.clipboard.writeText){
        navigator.clipboard.writeText(text).then(function(){
          flashNoteStatus('Copied!');
        }).catch(function(){
          noteInput.select();
          document.execCommand('copy');
          flashNoteStatus('Copied!');
        });
      }else{
        noteInput.select();
        document.execCommand('copy');
        flashNoteStatus('Copied!');
      }
    });
  }

  if(noteClear&&noteInput){
    noteClear.addEventListener('click',function(e){
      e.preventDefault();
      if(!noteInput.value)return;
      noteInput.value='';
      updateNoteMeta('');
      try{
        localStorage.removeItem('mtk_quick_note');
        localStorage.removeItem('mtk_quick_note_time');
      }catch(e){}
      if(noteTime)noteTime.textContent='Cleared';
      flashNoteStatus('Cleared','var(--dim)');
    });
  }

  if(navNotes&&noteInput){
    navNotes.addEventListener('click',function(e){
      e.preventDefault();
      var card=document.getElementById('quick-note-card');
      if(card){
        card.scrollIntoView({behavior:'smooth',block:'center'});
      }
      noteInput.focus();
    });
  }
  var sBtn=document.querySelector('.search-btn-3d');
  if(sBtn){
    var aImg=sBtn.querySelector('.search-3d-anim');
    var aSrc=aImg?aImg.src:'';
    sBtn.addEventListener('mouseenter',function(){
      if(aImg&&aSrc)aImg.src=aSrc;
    });
  }
  var hNav=document.querySelector('.home-nav-3d');
  if(hNav){
    var hAnim=hNav.querySelector('.home-3d-anim');
    var hSrc=hAnim?hAnim.src:'';
    hNav.addEventListener('mouseenter',function(){
      if(hAnim&&hSrc)hAnim.src=hSrc;
    });
    hNav.addEventListener('click',function(){
      window.scrollTo({top:0,behavior:'smooth'});
    });
  }
  var histNav=document.querySelector('.history-nav-3d');
  if(histNav){
    var histAnim=histNav.querySelector('.history-3d-anim');
    var histSrc=histAnim?histAnim.src:'';
    histNav.addEventListener('mouseenter',function(){
      if(histAnim&&histSrc)histAnim.src=histSrc;
    });
  }
  var bmarkNav=document.querySelector('.bookmarks-nav-3d');
  if(bmarkNav){
    var bmarkAnim=bmarkNav.querySelector('.bookmarks-3d-anim');
    var bmarkSrc=bmarkAnim?bmarkAnim.src:'';
    bmarkNav.addEventListener('mouseenter',function(){
      if(bmarkAnim&&bmarkSrc)bmarkAnim.src=bmarkSrc;
    });
  }
  var setNav=document.querySelector('.settings-nav-3d');
  if(setNav){
    var setAnim=setNav.querySelector('.settings-3d-anim');
    var setSrc=setAnim?setAnim.src:'';
    setNav.addEventListener('mouseenter',function(){
      if(setAnim&&setSrc)setAnim.src=setSrc;
    });
  }
  function showStoreModal(){
    var existing=document.getElementById('mtk-store-modal');
    if(existing){existing.style.display='flex';return;}
    var modal=document.createElement('div');
    modal.id='mtk-store-modal';
    modal.style.cssText='position:fixed;inset:0;background:rgba(0,0,0,0.65);backdrop-filter:blur(6px);z-index:9999;display:flex;align-items:center;justify-content:center;padding:20px;';
    modal.innerHTML='<div style="background:#1e1e24;border:1px solid rgba(255,255,255,0.12);border-radius:12px;max-width:440px;width:100%;padding:24px;box-shadow:0 20px 50px rgba(0,0,0,0.6);font-family:system-ui,-apple-system,sans-serif;color:#f4f4f5;">'
      +'<div style="display:flex;align-items:center;gap:12px;margin-bottom:14px;">'
      +'<div style="width:36px;height:36px;border-radius:8px;background:linear-gradient(135deg,#8b5cf6,#06b6d4);display:grid;place-items:center;font-size:18px;">🏬</div>'
      +'<div><h3 style="margin:0;font-size:16px;font-weight:600;">Extensions & Themes Store</h3>'
      +'<small style="color:#a1a1aa;font-size:12px;">Coming Soon in Next Release</small></div>'
      +'</div>'
      +'<p style="font-size:13px;line-height:1.55;color:#d4d4d8;margin:0 0 16px;">'
      +'MTK Browser currently runs on a high-speed, lightweight CEF core. Essential protections—including <b>Ad-Blocking, Anti-Tracking, Family Protection, and HTTPS-Only Mode</b>—are already built directly into the browser core.'
      +'<br><br>'
      +'Support for third-party Chrome Web Store extensions (Manifest V3) is under development and will be available in the upcoming update.'
      +'</p>'
      +'<div style="display:flex;justify-content:flex-end;">'
      +'<button id="mtk-store-close-btn" style="background:#0078d4;color:#fff;border:0;border-radius:6px;padding:7px 18px;font-size:13px;font-weight:550;cursor:pointer;transition:background 0.15s;">Got It</button>'
      +'</div></div>';
    modal.addEventListener('click',function(e){
      if(e.target===modal||e.target.id==='mtk-store-close-btn'){
        modal.style.display='none';
      }
    });
    document.body.appendChild(modal);
  }
  var storeNav=document.querySelector('.store-nav-3d');
  if(storeNav){
    var stAnim=storeNav.querySelector('.store-3d-anim');
    var stSrc=stAnim?stAnim.src:'';
    storeNav.addEventListener('mouseenter',function(){
      if(stAnim&&stSrc)stAnim.src=stSrc;
    });
    storeNav.addEventListener('click',function(){
      showStoreModal();
    });
  }
  var qStore=document.querySelector('.q-store-3d');
  if(qStore){
    var qStAnim=qStore.querySelector('.store-3d-anim');
    var qStSrc=qStAnim?qStAnim.src:'';
    qStore.addEventListener('mouseenter',function(){
      if(qStAnim&&qStSrc)qStAnim.src=qStSrc;
    });
    qStore.addEventListener('click',function(){
      showStoreModal();
    });
  }
  var qPub=document.getElementById('quick-featured-publishing');
  if(qPub){
    var qPubAnim=qPub.querySelector('.publishing-3d-anim');
    var qPubSrc=qPubAnim?qPubAnim.src:'';
    qPub.addEventListener('mouseenter',function(){
      if(qPubAnim&&qPubSrc)qPubAnim.src=qPubSrc;
    });
    function openFeatured(){
      var fCard=document.getElementById('featured-collection-card');
      if(fCard){
        fCard.scrollIntoView({behavior:'smooth',block:'center'});
        fCard.style.transition='box-shadow .4s ease,border-color .4s ease';
        fCard.style.borderColor='rgba(212,168,67,.85)';
        fCard.style.boxShadow='0 0 25px rgba(212,168,67,.35)';
        setTimeout(function(){
          fCard.style.borderColor='';
          fCard.style.boxShadow='';
        },1400);
      }
    }
    qPub.addEventListener('click',openFeatured);
    qPub.addEventListener('keydown',function(e){
      if(e.key==='Enter'||e.key===' '){
        e.preventDefault();
        openFeatured();
      }
    });
  }
  var searchForm=document.getElementById('search-form');
  var searchBox=document.getElementById('search-box');
  var searchUdm=document.getElementById('search-udm');
  var quickSearchWeb=document.getElementById('quick-search-web');
  var quickAskAi=document.getElementById('quick-ask-ai');

  var isAiMode=false;

  function isQuestionQuery(text){
    if(!text)return false;
    text=text.trim();
    if(text.indexOf('?')!==-1)return true;
    var lower=text.toLowerCase();
    var starters=[
      'who','what','when','where','why','which','how',
      'is','are','can','could','will','would','should','does','do','did',
      'kaisa','kaise','kaisi','kya','kab','kahan','kaha',
      'kyun','kyu','kaun','kon','kitna','kitne','kitni'
    ];
    for(var i=0;i<starters.length;i++){
      var s=starters[i];
      if(lower===s||lower.indexOf(s+' ')===0)return true;
    }
    var phrases=[
      'kaisa h','kaisa hai','kaise h','kaise hai','kya h','kya hai',
      'kyun h','kyu h','kab h','kab hai','weather in','mausam'
    ];
    for(var j=0;j<phrases.length;j++){
      if(lower.indexOf(phrases[j])!==-1)return true;
    }
    return false;
  }

  function setAiMode(enable){
    isAiMode=!!enable;
    if(searchForm){
      if(isAiMode){
        searchForm.classList.add('ai-active');
      }else{
        searchForm.classList.remove('ai-active');
      }
    }
    if(quickAskAi){
      if(isAiMode){
        quickAskAi.classList.add('active');
      }else{
        quickAskAi.classList.remove('active');
      }
    }
    if(quickSearchWeb){
      if(isAiMode){
        quickSearchWeb.classList.remove('active');
      }else{
        quickSearchWeb.classList.add('active');
      }
    }
    if(searchUdm){
      if(isAiMode){
        searchUdm.value='50';
        searchUdm.disabled=false;
      }else{
        searchUdm.value='';
        searchUdm.disabled=true;
      }
    }

    if(searchBox){
      if(isAiMode){
        searchBox.placeholder='Ask Google AI anything... (or ask a question)';
      }else{
        searchBox.placeholder='Search the web or type a URL...';
      }
    }
  }

  function toggleAiMode(){
    setAiMode(!isAiMode);
    if(searchBox){
      searchBox.focus();
    }
  }

  if(quickAskAi){
    quickAskAi.addEventListener('click',toggleAiMode);
    quickAskAi.addEventListener('keydown',function(e){
      if(e.key==='Enter'||e.key===' '){
        e.preventDefault();
        toggleAiMode();
      }
    });
  }

  if(quickSearchWeb){
    function activateSearchWeb(e){
      if(e)e.preventDefault();
      setAiMode(false);
      if(searchBox){
        var val=searchBox.value;
        if(val.indexOf('@ai ')===0)searchBox.value=val.substring(4);
        else if(val.indexOf(':ai ')===0)searchBox.value=val.substring(4);
        else if(val.indexOf('@gemini ')===0)searchBox.value=val.substring(8);
        searchBox.focus();
      }
    }
    quickSearchWeb.addEventListener('click',activateSearchWeb);
    quickSearchWeb.addEventListener('keydown',function(e){
      if(e.key==='Enter'||e.key===' '){
        activateSearchWeb(e);
      }
    });
  }



  if(searchBox){
    searchBox.addEventListener('input',function(){
      var val=searchBox.value.trim();
      var looksLikeAi=isAiMode || isQuestionQuery(val) ||
                      val.indexOf('@ai ')===0 || val.indexOf(':ai ')===0 || val.indexOf('@gemini ')===0;
      if(searchForm){
        if(looksLikeAi){
          searchForm.classList.add('ai-active');
        }else if(!isAiMode){
          searchForm.classList.remove('ai-active');
        }
      }
    });
  }

  if(searchForm){
    searchForm.addEventListener('submit',function(e){
      if(!searchBox)return;
      e.preventDefault();
      var rawVal=searchBox.value;
      var trimmed=rawVal.trim();
      var aiPrefix=false;
      if(trimmed.indexOf('@ai ')===0){
        trimmed=trimmed.substring(4).trim();
        aiPrefix=true;
      }else if(trimmed.indexOf(':ai ')===0){
        trimmed=trimmed.substring(4).trim();
        aiPrefix=true;
      }else if(trimmed.indexOf('@gemini ')===0){
        trimmed=trimmed.substring(8).trim();
        aiPrefix=true;
      }
      var query=trimmed;
      if(!query)return;
      var shouldUseAi=isAiMode || aiPrefix || isQuestionQuery(query);
      var encoded=encodeURIComponent(query).replace(/%20/g,'+');
      var url;
      if(shouldUseAi){
        url='https://www.google.com/search?safe=active&udm=50&q='+encoded;
      }else{
        url='https://www.google.com/search?safe=active&q='+encoded;
      }
      window.location.href=url;
    });
  }

  // --- Pinned Sites Manager ---
  var defaultPinned = [
    {name: 'Google', url: 'https://www.google.com'},
    {name: 'YouTube', url: 'https://www.youtube.com'},
    {name: 'Wikipedia', url: 'https://www.wikipedia.org'},
    {name: 'GitHub', url: 'https://github.com'}
  ];

  var tileGradients = [
    'linear-gradient(135deg,#4285f4,#34a853)',
    'linear-gradient(135deg,#ff0000,#cc0000)',
    'linear-gradient(135deg,#6366f1,#a855f7)',
    'linear-gradient(135deg,#06b6d4,#3b82f6)',
    'linear-gradient(135deg,#f59e0b,#ef4444)',
    'linear-gradient(135deg,#10b981,#059669)',
    'linear-gradient(135deg,#8b5cf6,#ec4899)',
    'linear-gradient(135deg,#3b82f6,#1d4ed8)'
  ];

  function getPinnedSites() {
    try {
      var raw = localStorage.getItem('mtk_pinned_sites');
      if (raw) {
        var parsed = JSON.parse(raw);
        if (Array.isArray(parsed)) return parsed;
      }
    } catch(e) {}
    try {
      localStorage.setItem('mtk_pinned_sites', JSON.stringify(defaultPinned));
    } catch(e) {}
    return defaultPinned.slice();
  }

  function savePinnedSites(sites) {
    try {
      localStorage.setItem('mtk_pinned_sites', JSON.stringify(sites));
    } catch(e) {}
  }

  function escapePinnedHtml(str) {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function getPinnedDomain(url) {
    try {
      var clean = (url || '').replace(/^https?:\/\//i, '').split('/')[0];
      return clean.replace(/^www\./i, '');
    } catch(e) {
      return url || '';
    }
  }

  function getPinnedInitial(name, url) {
    var s = (name || getPinnedDomain(url) || 'M').trim();
    for (var i = 0; i < s.length; i++) {
      var ch = s.charAt(i);
      if (/[a-zA-Z0-9]/.test(ch)) return ch.toUpperCase();
    }
    return 'M';
  }

  function getSiteFavicon(url, domain) {
    if (!domain) domain = getPinnedDomain(url);
    domain = (domain || '').toLowerCase().trim();
    if (domain.indexOf('://') !== -1) domain = getPinnedDomain(domain);
    domain = domain.replace(/^www\./, '').replace(/^m\./, '');
    if (typeof BUILTIN_FAVICONS !== 'undefined' && BUILTIN_FAVICONS) {
      if (BUILTIN_FAVICONS[domain]) return BUILTIN_FAVICONS[domain];
      for (var key in BUILTIN_FAVICONS) {
        if (domain === key || domain.endsWith('.' + key)) {
          return BUILTIN_FAVICONS[key];
        }
      }
    }
    return '';
  }

  function generateFallbackIconSvg(domain, initial) {
    var hash = 5381;
    var str = domain || initial || 'M';
    for (var i = 0; i < str.length; i++) {
      hash = ((hash << 5) + hash) + str.charCodeAt(i);
    }
    var hue1 = Math.abs(hash) % 360;
    var hue2 = (hue1 + 45) % 360;
    var ch = (initial || 'M').charAt(0).toUpperCase();
    var svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">' +
      '<defs><linearGradient id="g" x1="0%" y1="0%" x2="100%" y2="100%">' +
      '<stop offset="0%" stop-color="hsl(' + hue1 + ',75%,52%)"/>' +
      '<stop offset="100%" stop-color="hsl(' + hue2 + ',80%,38%)"/>' +
      '</linearGradient></defs>' +
      '<rect width="32" height="32" rx="8" fill="url(#g)"/>' +
      '<text x="16" y="22" font-family="-apple-system,BlinkMacSystemFont,\'Segoe UI\',Roboto,sans-serif" font-size="16" font-weight="700" fill="#fff" text-anchor="middle">' + ch + '</text>' +
      '</svg>';
    return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
  }

  function renderPinnedSites() {
    var container = document.getElementById('pinned-sites-list');
    if (!container) return;
    var sites = getPinnedSites();
    if (sites.length === 0) {
      container.innerHTML = '<div class="empty"><b>No pinned sites</b>Click + Add to pin your favourite sites</div>';
      return;
    }

    var html = '';
    for (var i = 0; i < sites.length; i++) {
      var site = sites[i];
      var name = site.name || getPinnedDomain(site.url) || 'Site';
      var domain = getPinnedDomain(site.url);
      var initial = getPinnedInitial(site.name, site.url);
      var iconUrl = site.icon || getSiteFavicon(site.url, domain) || generateFallbackIconSvg(domain, initial);

      var tileHtml = '';
      if (iconUrl) {
        tileHtml = '<span class="pinned-tile tile-has-icon">' +
                     '<img src="' + escapePinnedHtml(iconUrl) + '" class="tile-icon" alt="" onerror="this.style.display=\'none\';this.parentNode.classList.remove(\'tile-has-icon\');this.parentNode.textContent=\'' + escapePinnedHtml(initial) + '\';">' +
                   '</span>';
      } else {
        var grad = tileGradients[i % tileGradients.length];
        tileHtml = '<span class="pinned-tile" style="background:' + grad + '">' + escapePinnedHtml(initial) + '</span>';
      }

      html += '<div class="pinned-item" data-idx="' + i + '">' +
                '<a class="pinned-link" href="' + escapePinnedHtml(site.url) + '" title="Open ' + escapePinnedHtml(name) + '">' +
                  tileHtml +
                  '<span class="pinned-info">' +
                    '<b>' + escapePinnedHtml(name) + '</b>' +
                    '<small>' + escapePinnedHtml(domain) + '</small>' +
                  '</span>' +
                '</a>' +
                '<button type="button" class="pinned-del-btn" data-del="' + i + '" title="Unpin ' + escapePinnedHtml(name) + '" aria-label="Unpin">' +
                  '<svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>' +
                '</button>' +
              '</div>';
    }
    container.innerHTML = html;

    var delBtns = container.querySelectorAll('.pinned-del-btn');
    for (var d = 0; d < delBtns.length; d++) {
      delBtns[d].addEventListener('click', function(e) {
        e.preventDefault();
        e.stopPropagation();
        var idx = parseInt(this.getAttribute('data-del'), 10);
        var current = getPinnedSites();
        if (idx >= 0 && idx < current.length) {
          current.splice(idx, 1);
          savePinnedSites(current);
          renderPinnedSites();
        }
      });
    }
  }

  var addPinnedBtn = document.getElementById('add-pinned-btn');
  var pinnedFormBox = document.getElementById('pinned-form-box');
  var pinnedFormClose = document.getElementById('pinned-form-close');
  var pinnedNameInput = document.getElementById('pinned-name-input');
  var pinnedUrlInput = document.getElementById('pinned-url-input');
  var pinnedSaveBtn = document.getElementById('pinned-save-btn');
  var pinnedCancelBtn = document.getElementById('pinned-cancel-btn');

  function openPinnedForm() {
    if (!pinnedFormBox) return;
    pinnedFormBox.style.display = 'block';
    if (pinnedNameInput) pinnedNameInput.value = '';
    if (pinnedUrlInput) {
      pinnedUrlInput.value = '';
      pinnedUrlInput.style.borderColor = '';
      pinnedUrlInput.focus();
    }
  }

  function closePinnedForm() {
    if (!pinnedFormBox) return;
    pinnedFormBox.style.display = 'none';
  }

  function handleSavePinned() {
    if (!pinnedUrlInput) return;
    var rawUrl = (pinnedUrlInput.value || '').trim();
    if (!rawUrl) {
      pinnedUrlInput.focus();
      return;
    }

    if (!/^https?:\/\//i.test(rawUrl)) {
      rawUrl = 'https://' + rawUrl;
    }

    if (!/^https?:\/\/[a-z0-9]/i.test(rawUrl)) {
      pinnedUrlInput.style.borderColor = '#ef4444';
      pinnedUrlInput.focus();
      return;
    }

    var name = (pinnedNameInput && pinnedNameInput.value ? pinnedNameInput.value.trim() : '');
    if (!name) {
      var d = getPinnedDomain(rawUrl);
      var parts = d.split('.');
      name = (parts.length > 1 ? parts[0] : d);
      if (name.length > 0) name = name.charAt(0).toUpperCase() + name.slice(1);
    }

    var domain = getPinnedDomain(rawUrl);
    var icon = getSiteFavicon(rawUrl, domain);
    var newSite = {name: name, url: rawUrl};
    if (icon) newSite.icon = icon;

    var current = getPinnedSites();
    current.push(newSite);
    savePinnedSites(current);
    closePinnedForm();
    renderPinnedSites();
  }

  if (addPinnedBtn) {
    addPinnedBtn.addEventListener('click', function(e) {
      e.preventDefault();
      if (pinnedFormBox && pinnedFormBox.style.display === 'block') {
        closePinnedForm();
      } else {
        openPinnedForm();
      }
    });
  }

  if (pinnedFormClose) {
    pinnedFormClose.addEventListener('click', function(e) {
      e.preventDefault();
      closePinnedForm();
    });
  }

  if (pinnedCancelBtn) {
    pinnedCancelBtn.addEventListener('click', function(e) {
      e.preventDefault();
      closePinnedForm();
    });
  }

  if (pinnedSaveBtn) {
    pinnedSaveBtn.addEventListener('click', function(e) {
      e.preventDefault();
      handleSavePinned();
    });
  }

  if (pinnedUrlInput) {
    pinnedUrlInput.addEventListener('keydown', function(e) {
      pinnedUrlInput.style.borderColor = '';
      if (e.key === 'Enter') {
        e.preventDefault();
        handleSavePinned();
      } else if (e.key === 'Escape') {
        closePinnedForm();
      }
    });
  }

  if (pinnedNameInput) {
    pinnedNameInput.addEventListener('keydown', function(e) {
      if (e.key === 'Enter') {
        e.preventDefault();
        if (pinnedUrlInput) pinnedUrlInput.focus();
      } else if (e.key === 'Escape') {
        closePinnedForm();
      }
    });
  }

  renderPinnedSites();
})();
</script></body></html>)HTML";
  return html;
}

void RefreshSnapshot(const std::vector<library::Entry>& history,
                     const std::vector<library::Entry>& bookmarks,
                     const std::vector<downloads::Item>& downloads,
                     bool light_appearance) {
  std::string next = BuildHtml(history, bookmarks, downloads, light_appearance);
  std::lock_guard lock(g_snapshot_mutex);
  g_snapshot = std::move(next);
  g_snapshot_is_light = light_appearance;
}

std::string SnapshotHtml(bool light_appearance) {
  std::lock_guard lock(g_snapshot_mutex);
  if (g_snapshot.empty() || g_snapshot_is_light != light_appearance) {
    g_snapshot = BuildHtml(mtk::library::Store::Get().history(),
                           mtk::library::Store::Get().bookmarks(),
                           mtk::downloads::Manager::Get().items(),
                           light_appearance);
    g_snapshot_is_light = light_appearance;
  }
  std::string result = g_snapshot;
  const std::string needle_start = "<div id=home-clock-card";
  const std::string needle_end = "<!--clock-end-->";
  const size_t start = result.find(needle_start);
  const size_t end = result.find(needle_end);
  if (start != std::string::npos && end != std::string::npos && end > start) {
    result.replace(start, (end + needle_end.size()) - start, CurrentTimeCard());
  }
  const std::string q_start = "<section class=\"card quote\" id=quote-card";
  const std::string q_end = "<!--quote-end-->";
  const size_t qs = result.find(q_start);
  const size_t qe = result.find(q_end);
  if (qs != std::string::npos && qe != std::string::npos && qe > qs) {
    result.replace(qs, (qe + q_end.size()) - qs, CurrentQuoteCard());
  }
  return result;
}

}  // namespace mtk::home
