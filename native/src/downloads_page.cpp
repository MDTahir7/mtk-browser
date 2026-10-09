#include "downloads_page.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "home_page.h"
#include "policy.h"

namespace mtk::downloads_page {
namespace {

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

std::string Lowered(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string FormatBytes(int64_t bytes) {
  if (bytes <= 0) return "0 B";
  constexpr int64_t kKb = 1024;
  constexpr int64_t kMb = 1024 * 1024;
  constexpr int64_t kGb = 1024 * 1024 * 1024;
  char buf[32]{};
  if (bytes >= kGb) {
    std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / kGb);
  } else if (bytes >= kMb) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / kMb);
  } else if (bytes >= kKb) {
    std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / kKb);
  } else {
    std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
  }
  return buf;
}

std::string FileCategory(std::string_view filename) {
  const size_t dot = filename.find_last_of('.');
  if (dot == std::string_view::npos) return "other";
  const std::string ext = Lowered(filename.substr(dot + 1));
  if (ext == "pdf" || ext == "doc" || ext == "docx" || ext == "txt" ||
      ext == "xls" || ext == "xlsx" || ext == "ppt" || ext == "pptx" || ext == "rtf") {
    return "docs";
  }
  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" ||
      ext == "gif" || ext == "svg" || ext == "bmp" || ext == "ico") {
    return "images";
  }
  if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" ||
      ext == "gz" || ext == "bz2" || ext == "xz") {
    return "archives";
  }
  if (ext == "exe" || ext == "msi" || ext == "apk" || ext == "bat" ||
      ext == "cmd" || ext == "ps1") {
    return "apps";
  }
  if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "mp4" ||
      ext == "mkv" || ext == "webm" || ext == "avi" || ext == "mov") {
    return "media";
  }
  return "other";
}

bool MatchesCategory(std::string_view filename, std::string_view cat) {
  if (cat.empty() || cat == "all") return true;
  return FileCategory(filename) == cat;
}

std::string StateBadge(downloads::State state) {
  switch (state) {
    case downloads::State::kComplete:
      return "";
    case downloads::State::kInProgress:
      return "<span class=\"badge badge-active\">Downloading</span>";
    case downloads::State::kStarting:
      return "<span class=\"badge badge-active\">Starting</span>";
    case downloads::State::kPaused:
      return "<span class=\"badge badge-warn\">Paused</span>";
    case downloads::State::kCanceled:
      return "<span class=\"badge badge-muted\">Canceled</span>";
    case downloads::State::kFailed:
      return "<span class=\"badge badge-danger\">Failed</span>";
  }
  return "";
}

std::string BraveFileBadge(std::string_view filename) {
  const size_t dot = filename.find_last_of('.');
  const std::string ext = (dot != std::string_view::npos)
                              ? Lowered(filename.substr(dot + 1))
                              : "";

  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" ||
      ext == "gif" || ext == "svg" || ext == "bmp" || ext == "ico") {
    return "<div class=\"file-badge file-img\" title=\"Image\">"
           "<svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#3b82f6\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/>"
           "<circle cx=\"8.5\" cy=\"8.5\" r=\"1.5\"/>"
           "<polyline points=\"21 15 16 10 5 21\"/>"
           "</svg></div>";
  }

  if (ext == "mkv" || ext == "mp4" || ext == "avi" || ext == "webm" ||
      ext == "mov" || ext == "flv") {
    // VLC traffic cone style icon
    return "<div class=\"file-badge file-video\" title=\"Video\">"
           "<svg width=\"22\" height=\"22\" viewBox=\"0 0 24 24\" fill=\"none\">"
           "<path d=\"M3 21h18M6.5 21l3.5-17h4l3.5 17\" stroke=\"#f97316\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
           "<path d=\"M8 15.5h8M9.5 9h5\" stroke=\"#ffffff\" stroke-width=\"2.2\" stroke-linecap=\"round\"/>"
           "</svg></div>";
  }

  if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" ||
      ext == "gz" || ext == "bz2" || ext == "xz") {
    // Zipper folder icon
    return "<div class=\"file-badge file-zip\" title=\"Archive\">"
           "<svg width=\"22\" height=\"22\" viewBox=\"0 0 24 24\" fill=\"none\">"
           "<path d=\"M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z\" fill=\"#fef9c3\" stroke=\"#eab308\" stroke-width=\"1.8\"/>"
           "<line x1=\"12\" y1=\"10\" x2=\"12\" y2=\"17\" stroke=\"#64748b\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
           "<rect x=\"10.5\" y=\"12.5\" width=\"3\" height=\"4\" fill=\"#94a3b8\" rx=\"1\"/>"
           "</svg></div>";
  }

  if (ext == "pdf" || ext == "doc" || ext == "docx" || ext == "txt" ||
      ext == "xls" || ext == "xlsx" || ext == "ppt" || ext == "pptx") {
    return "<div class=\"file-badge file-doc\" title=\"Document\">"
           "<svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#64748b\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
           "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/>"
           "<polyline points=\"14 2 14 8 20 8\"/>"
           "<line x1=\"16\" y1=\"13\" x2=\"8\" y2=\"13\"/>"
           "<line x1=\"16\" y1=\"17\" x2=\"8\" y2=\"17\"/>"
           "</svg></div>";
  }

  return "<div class=\"file-badge file-other\" title=\"File\">"
         "<svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#64748b\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/>"
         "<polyline points=\"14 2 14 8 20 8\"/>"
         "</svg></div>";
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool DecodeFormValue(std::string_view raw, std::string* decoded) {
  if (raw.size() > kMaxQueryLength * 3) return false;
  std::string result;
  result.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '+') {
      result.push_back(' ');
      continue;
    }
    if (c == '%') {
      if (i + 2 >= raw.size()) return false;
      const int high = HexValue(raw[i + 1]);
      const int low = HexValue(raw[i + 2]);
      if (high < 0 || low < 0) return false;
      result.push_back(static_cast<char>(high * 16 + low));
      i += 2;
      continue;
    }
    result.push_back(c);
  }
  if (result.size() > kMaxQueryLength) return false;
  for (const unsigned char c : result) {
    if (c < 0x20 || c == 0x7f) return false;
  }
  *decoded = std::move(result);
  return true;
}

}  // namespace

bool IsDownloadsUrl(std::string_view url) {
  if (url == kUrl || url == "mtk://downloads") return true;
  const std::string_view prefix(kUrl);
  if (!url.starts_with(prefix)) return false;
  std::string_view rest = url.substr(prefix.size());
  return rest.starts_with("?q=") || rest.starts_with("?cat=");
}

std::string QueryOf(std::string_view url) {
  if (!IsDownloadsUrl(url)) return {};
  const size_t pos = url.find("q=");
  if (pos == std::string_view::npos) return {};
  std::string_view rest = url.substr(pos + 2);
  const size_t amp = rest.find('&');
  if (amp != std::string_view::npos) rest = rest.substr(0, amp);
  std::string decoded;
  if (!DecodeFormValue(rest, &decoded)) return {};
  return decoded;
}

std::string CategoryOf(std::string_view url) {
  if (!IsDownloadsUrl(url)) return {};
  const size_t pos = url.find("cat=");
  if (pos == std::string_view::npos) return {};
  std::string_view rest = url.substr(pos + 4);
  const size_t amp = rest.find('&');
  if (amp != std::string_view::npos) rest = rest.substr(0, amp);
  return std::string(rest);
}

Action ParseActionUrl(std::string_view url) {
  Action action;
  constexpr std::string_view kOpenPrefix = "mtk://downloads/action/open?id=";
  constexpr std::string_view kShowPrefix = "mtk://downloads/action/show?id=";
  constexpr std::string_view kRemovePrefix = "mtk://downloads/action/remove?id=";
  constexpr std::string_view kRetryPrefix = "mtk://downloads/action/retry?id=";
  constexpr std::string_view kCopyUrlPrefix = "mtk://downloads/action/copy-url?id=";
  constexpr std::string_view kClearAll = "mtk://downloads/action/clear-all";
  constexpr std::string_view kOpenFolder = "mtk://downloads/action/open-folder";

  if (url == kClearAll) {
    action.kind = ActionKind::kClearFinished;
    return action;
  }
  if (url == kOpenFolder) {
    action.kind = ActionKind::kOpenFolder;
    return action;
  }
  auto parse_id = [](std::string_view prefix, std::string_view full, ActionKind kind, Action* out) {
    if (full.starts_with(prefix)) {
      const auto id_str = full.substr(prefix.size());
      uint64_t id = 0;
      try {
        id = std::stoull(std::string(id_str));
      } catch (...) {
        id = 0;
      }
      if (id > 0) {
        out->kind = kind;
        out->download_id = id;
        return true;
      }
    }
    return false;
  };

  if (parse_id(kOpenPrefix, url, ActionKind::kOpen, &action)) return action;
  if (parse_id(kShowPrefix, url, ActionKind::kShow, &action)) return action;
  if (parse_id(kRemovePrefix, url, ActionKind::kRemove, &action)) return action;
  if (parse_id(kRetryPrefix, url, ActionKind::kRetry, &action)) return action;
  if (parse_id(kCopyUrlPrefix, url, ActionKind::kCopyUrl, &action)) return action;

  return action;
}

std::string BuildHtml(const std::vector<downloads::Item>& items,
                      std::string_view query, std::string_view category,
                      bool light_appearance, bool private_session) {
  const std::string lowered_query = Lowered(query);
  const std::string active_cat = category.empty() ? "all" : std::string(category);

  std::string cards;
  size_t shown_count = 0;

  if (!private_session) {
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
      const std::string raw_name = it->file_name.empty()
                                       ? "Unnamed download"
                                       : WideToUtf8(it->file_name);
      const std::string filename = home::CleanDisplayFileName(raw_name);
      if (!MatchesCategory(raw_name, active_cat) && !MatchesCategory(filename, active_cat)) continue;
      if (!lowered_query.empty() &&
          Lowered(filename).find(lowered_query) == std::string::npos &&
          Lowered(raw_name).find(lowered_query) == std::string::npos &&
          Lowered(it->url).find(lowered_query) == std::string::npos) {
        continue;
      }

      ++shown_count;
      const std::string badge = BraveFileBadge(filename);
      const std::string state_badge = StateBadge(it->state);
      const std::string size_info = (it->state == downloads::State::kInProgress && it->total_bytes > 0)
          ? FormatBytes(it->received_bytes) + " of " + FormatBytes(it->total_bytes)
          : FormatBytes(it->received_bytes > 0 ? it->received_bytes : it->total_bytes);

      cards += "<div class=\"dl-card\">";
      cards += "<div class=\"dl-main\">";
      cards += badge;
      cards += "<div class=\"dl-text\">";
      cards += "<div class=\"dl-header-line\">";
      cards += "<a class=\"dl-name\" href=\"mtk://downloads/action/open?id=" +
               std::to_string(it->id) + "\" title=\"Open file\">" +
               home::EscapeHtml(filename) + "</a>";
      if (!state_badge.empty()) {
        cards += state_badge;
      }
      cards += "</div>";

      cards += "<div class=\"dl-meta\">";
      if (!it->url.empty()) {
        const std::string source_target = (it->url.starts_with("http://") || it->url.starts_with("https://"))
                                              ? it->url
                                              : ("https://" + it->url);
        cards += "<span class=\"dl-source\">From " + home::EscapeHtml(source_target) + "</span>";
      } else {
        cards += "<span class=\"dl-source\">Local file</span>";
      }
      cards += "<span class=\"sep\">•</span><span>" + home::EscapeHtml(size_info) + "</span>";
      cards += "</div>";

      if (it->state == downloads::State::kInProgress) {
        int percent = it->percent;
        if (percent < 0) percent = 0;
        cards += "<div class=\"dl-prog-bar\"><span style=\"width:" +
                 std::to_string(std::clamp(percent, 0, 100)) + "%\"></span></div>";
      }
      cards += "</div></div>";

      // 3 action buttons on right: Copy link, Show in folder, Remove from list
      cards += "<div class=\"dl-actions\">";
      const std::string raw_url_escaped = home::EscapeHtml(it->url);
      cards += "<a class=\"btn-icon-action\" href=\"mtk://downloads/action/copy-url?id=" +
               std::to_string(it->id) + "\" onclick=\"copyDownloadUrl(event, '" + raw_url_escaped + "')\" title=\"Copy download link\">"
               "<svg width=\"18\" height=\"18\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71\"/><path d=\"M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71\"/></svg></a>";
      cards += "<a class=\"btn-icon-action\" href=\"mtk://downloads/action/show?id=" +
               std::to_string(it->id) + "\" title=\"Show in folder\">"
               "<svg width=\"18\" height=\"18\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z\"/></svg></a>";
      cards += "<a class=\"btn-icon-action btn-drop\" href=\"mtk://downloads/action/remove?id=" +
               std::to_string(it->id) + "\" title=\"Remove from list\">"
               "<svg width=\"18\" height=\"18\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/><line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/></svg></a>";
      cards += "</div></div>";
    }
  }

  if (private_session) {
    cards = "<div class=\"empty-box\"><svg width=40 height=40 viewBox=\"0 0 24 24\" fill=none stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round><path d=\"M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24\"/><line x1=\"1\" y1=\"1\" x2=\"23\" y2=\"23\"/></svg><b>Private Window</b><p>Downloads in private windows are not saved to your profile download history.</p></div>";
  } else if (shown_count == 0) {
    cards = "<div class=\"empty-box\"><svg width=42 height=42 viewBox=\"0 0 24 24\" fill=none stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round><path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><polyline points=\"7 10 12 15 17 10\"/><line x1=\"12\" y1=\"15\" x2=\"12\" y2=\"3\"/></svg><b>No downloads found</b><p>Files you download in MTK Browser will appear here.</p></div>";
  }

  static constexpr char kHead[] = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><meta name=referrer content=no-referrer>
<title>Downloads — MTK Browser</title><style>
:root{
  color-scheme: light;
  --bg: #f8fafc;
  --header-bg: #ffffff;
  --card-bg: #ffffff;
  --card-hover: #ffffff;
  --card-shadow: 0 1px 3px rgba(0,0,0,0.04), 0 1px 2px rgba(0,0,0,0.02);
  --card-hover-shadow: 0 4px 6px -1px rgba(0,0,0,0.06), 0 2px 4px -1px rgba(0,0,0,0.03);
  --border: #e2e8f0;
  --border-card: #e5e7eb;
  --border-focus: #4f46e5;
  --text: #0f172a;
  --text-name: #2563eb;
  --text-name-hover: #1d4ed8;
  --text-meta: #64748b;
  --pill-bg: transparent;
  --pill-active-bg: #4f46e5;
  --pill-active-text: #ffffff;
  --btn-icon: #475569;
  --btn-icon-hover: #0f172a;
  --btn-hover-bg: #f1f5f9;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{min-height:100%}
body{background:var(--bg);color:var(--text);font:13.5px/1.4 -apple-system,BlinkMacSystemFont,'Segoe UI Variable Text','Segoe UI',Roboto,Helvetica,Arial,sans-serif;-webkit-font-smoothing:antialiased}
a{color:inherit;text-decoration:none}
.top-nav{background:var(--header-bg);border-bottom:1px solid var(--border);padding:14px 28px;display:flex;align-items:center;justify-content:space-between;gap:20px;position:sticky;top:0;z-index:20}
.header-left{display:flex;align-items:center;gap:12px}
.header-title{font-size:20px;font-weight:700;letter-spacing:-0.015em;color:var(--text)}
.header-center{flex:1;max-width:580px;display:flex;justify-content:center}
form.find{position:relative;width:100%}
form.find svg.search-icon{position:absolute;left:14px;top:11px;color:#94a3b8;pointer-events:none}
form.find input{width:100%;height:38px;border:1.5px solid #cbd5e1;border-radius:24px;background:#ffffff;color:#0f172a;padding:0 16px 0 42px;font-size:13.5px;outline:none;transition:border-color .15s,box-shadow .15s}
form.find input:focus{border-color:var(--border-focus);box-shadow:0 0 0 3px rgba(79,70,229,.15)}
.header-right{display:flex;align-items:center;gap:10px}
.btn-clear-all{height:32px;padding:0 16px;display:inline-flex;align-items:center;border:1.5px solid #cbd5e1;border-radius:20px;background:#ffffff;color:#475569;font-size:12.5px;font-weight:500;cursor:pointer;white-space:nowrap;transition:all .15s}
.btn-clear-all:hover{border-color:var(--border-focus);color:var(--border-focus);background:#f5f3ff}
.wrap{max-width:880px;margin:0 auto;padding:24px 20px 80px}
.categories{display:flex;gap:6px;overflow-x:auto;padding-bottom:12px;margin-bottom:12px}
.pill{padding:5px 14px;border-radius:20px;font-size:12px;font-weight:500;color:#64748b;background:rgba(0,0,0,.03);border:1px solid transparent;transition:all .12s}
.pill:hover{background:rgba(0,0,0,.06);color:var(--text)}
.pill.active{background:var(--pill-active-bg);color:var(--pill-active-text);font-weight:600}
.date-header{font-size:13.5px;font-weight:600;color:#64748b;margin:18px 0 12px 4px}
.dl-card{background:var(--card-bg);border:1px solid var(--border-card);border-radius:12px;padding:16px 20px;margin-bottom:10px;display:flex;align-items:center;justify-content:space-between;gap:16px;box-shadow:var(--card-shadow);transition:box-shadow .15s,border-color .15s}
.dl-card:hover{border-color:#cbd5e1;box-shadow:var(--card-hover-shadow)}
.dl-main{display:flex;align-items:center;gap:16px;min-width:0;flex:1}
.file-badge{width:40px;height:40px;border-radius:8px;display:grid;place-items:center;flex-shrink:0}
.file-img{background:#eff6ff}
.file-video{background:#fff7ed}
.file-zip{background:#fefce8}
.file-doc{background:#f1f5f9}
.file-other{background:#f1f5f9}
.dl-text{min-width:0;flex:1}
.dl-header-line{display:flex;align-items:center;gap:8px}
.dl-name{font-size:14px;font-weight:600;color:var(--text-name);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;display:inline-block;max-width:100%}
.dl-name:hover{color:var(--text-name-hover);text-decoration:underline}
.dl-meta{font-size:12px;color:var(--text-meta);margin-top:4px;display:flex;align-items:center;gap:6px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dl-meta .sep{opacity:.6}
.dl-source{color:var(--text-meta);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dl-actions{display:flex;align-items:center;gap:10px;flex-shrink:0}
.btn-icon-action{width:34px;height:34px;border-radius:6px;display:inline-flex;align-items:center;justify-content:center;color:var(--btn-icon);transition:all .12s}
.btn-icon-action:hover{background:var(--btn-hover-bg);color:var(--btn-icon-hover)}
.btn-icon-action.btn-drop:hover{background:#fee2e2;color:#ef4444}
.dl-prog-bar{height:4px;background:#e2e8f0;border-radius:2px;overflow:hidden;margin-top:6px;width:100%;max-width:280px}
.dl-prog-bar span{display:block;height:100%;background:#4f46e5;border-radius:2px}
.badge{padding:2px 6px;border-radius:4px;font-size:9.5px;font-weight:600}
.badge-active{background:rgba(79,70,229,.14);color:#4f46e5}
.badge-warn{background:rgba(234,179,8,.18);color:#ca8a04}
.badge-danger{background:rgba(239,68,68,.18);color:#dc2626}
.badge-muted{background:rgba(148,163,184,.14);color:#64748b}
.empty-box{border:1px dashed var(--border);border-radius:12px;padding:56px 24px;text-align:center;color:#64748b;background:#ffffff;margin-top:12px}
.empty-box svg{display:block;margin:0 auto 12px;color:#94a3b8}
.empty-box b{display:block;color:var(--text);font-size:15px;margin-bottom:6px}
.empty-box p{margin:0;font-size:12.5px}
@media(max-width:680px){.top-nav{flex-wrap:wrap;padding:12px 16px}.header-center{order:3;max-width:100%;margin-top:8px}.dl-card{flex-direction:column;align-items:flex-start}.dl-actions{width:100%;justify-content:flex-end;margin-top:8px}}
</style>)HTML";

  std::string html = kHead;
  if (!light_appearance) {
    html += R"HTML(<style>
:root{
  color-scheme: dark;
  --bg: #0f172a;
  --header-bg: #1e293b;
  --card-bg: #1e293b;
  --card-hover: #243042;
  --card-shadow: 0 1px 3px rgba(0,0,0,0.2);
  --card-hover-shadow: 0 4px 6px rgba(0,0,0,0.3);
  --border: rgba(255,255,255,.08);
  --border-card: rgba(255,255,255,.08);
  --border-focus: #818cf8;
  --text: #f8fafc;
  --text-name: #60a5fa;
  --text-name-hover: #93c5fd;
  --text-meta: #94a3b8;
  --pill-bg: rgba(255,255,255,.06);
  --pill-active-bg: #4f46e5;
  --pill-active-text: #ffffff;
  --btn-icon: #94a3b8;
  --btn-icon-hover: #f8fafc;
  --btn-hover-bg: rgba(255,255,255,.08);
}
form.find input{background:#0f172a;border-color:rgba(255,255,255,.14);color:#f8fafc}
.btn-clear-all{background:#1e293b;border-color:rgba(255,255,255,.14);color:#cbd5e1}
.btn-clear-all:hover{border-color:#818cf8;color:#818cf8;background:rgba(99,102,241,.1)}
.pill{background:rgba(255,255,255,.06);color:#94a3b8}
.pill:hover{background:rgba(255,255,255,.1);color:#f8fafc}
.empty-box{background:#1e293b;border-color:rgba(255,255,255,.1)}
.file-img{background:rgba(59,130,246,.15)}
.file-video{background:rgba(249,115,22,.15)}
.file-zip{background:rgba(234,179,8,.15)}
.file-doc,.file-other{background:rgba(148,163,184,.15)}
</style>)HTML";
  }

  html += R"HTML(<script>
function copyDownloadUrl(e, url) {
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(url);
  }
}
</script></head><body>)HTML";

  html += "<header class=\"top-nav\">";
  html += "<div class=\"header-left\">";
  html += "<svg class=\"brand-logo\" width=\"28\" height=\"28\" viewBox=\"0 0 24 24\" fill=\"none\">"
          "<path d=\"M12 2L4 5v6.09c0 5.05 3.41 9.76 8 10.91 4.59-1.15 8-5.86 8-10.91V5l-8-3z\" fill=\"#f97316\"/>"
          "<path d=\"M12 4.4L5.6 6.8v4.29c0 4.04 2.73 7.81 6.4 8.73 3.67-.92 6.4-4.69 6.4-8.73V6.8L12 4.4z\" fill=\"#ea580c\"/>"
          "<path d=\"M12 7l3 3-3 3-3-3 3-3z\" fill=\"#ffffff\"/>"
          "</svg>";
  html += "<h1 class=\"header-title\">Downloads</h1></div>";

  html += "<div class=\"header-center\">";
  html += "<form class=\"find\" method=\"get\" action=\"" + std::string(kUrl) + "\">";
  html += "<svg class=\"search-icon\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"11\" cy=\"11\" r=\"8\"/><line x1=\"21\" y1=\"21\" x2=\"16.65\" y2=\"16.65\"/></svg>";
  html += "<input name=\"q\" type=\"search\" value=\"" + home::EscapeHtml(query) +
          "\" placeholder=\"Search download history\" autocomplete=\"off\" spellcheck=\"false\" maxlength=\"200\" aria-label=\"Search download history\">";
  if (!category.empty()) {
    html += "<input type=\"hidden\" name=\"cat\" value=\"" + home::EscapeHtml(category) + "\">";
  }
  html += "</form></div>";

  html += "<div class=\"header-right\">";
  if (!private_session && !items.empty()) {
    html += "<a class=\"btn-clear-all\" href=\"mtk://downloads/action/clear-all\" title=\"Clear all downloads\">Clear all</a>";
  }
  html += "</div></header>";

  html += "<div class=\"wrap\">";

  // Category filter tabs
  html += "<div class=\"categories\">";
  auto cat_link = [&](const char* id, const char* label) {
    std::string href = std::string(kUrl) + "?cat=" + id;
    if (!query.empty()) href += "&q=" + std::string(query);
    const bool is_act = (active_cat == id);
    return "<a class=\"pill" + std::string(is_act ? " active" : "") +
           "\" href=\"" + href + "\">" + label + "</a>";
  };
  html += cat_link("all", "All");
  html += cat_link("docs", "Documents");
  html += cat_link("images", "Images");
  html += cat_link("archives", "Archives");
  html += cat_link("apps", "Apps");
  html += cat_link("media", "Media");
  html += "</div>";

  if (shown_count > 0 && !private_session) {
    html += "<div class=\"date-header\">Today</div>";
  }

  html += cards;
  html += "</div></body></html>";
  return html;
}

}  // namespace mtk::downloads_page
