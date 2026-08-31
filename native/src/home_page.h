#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "download_manager.h"
#include "library_store.h"

namespace mtk::home {

inline constexpr char kScheme[] = "mtk";
inline constexpr char kHost[] = "home";
inline constexpr char kUrl[] = "mtk://home/";
// Compatibility names used by persistence code; all three identify the same
// exact browser-owned document.
inline constexpr char kHomeUrl[] = "mtk://home/";
inline constexpr wchar_t kHomeUrlWide[] = L"mtk://home/";

enum class Action {
  kNone,
  kDownloads,
  kHistory,
  kBookmarks,
  kSettings,
};

// Only the exact document URL is browser-owned. Lookalike hosts, paths,
// queries, fragments and credentials are deliberately not accepted.
bool IsHomeUrl(std::string_view url);
// Internal actions are an explicit allowlist and are intercepted before CEF
// asks the scheme handler for a resource. Unknown mtk: URLs fail closed.
Action ParseActionUrl(std::string_view url);

// Escapes untrusted UTF-8 or UTF-16 text for a quoted HTML text/attribute
// context. Dynamic titles and URLs always pass through one of these overloads.
std::string EscapeHtml(std::string_view value);
std::string EscapeHtml(std::wstring_view value);

// Produces one self-contained document from profile-local browser data. It has
// no remote scripts, images, fonts, stylesheets or network-backed widgets.
std::string BuildHtml(const std::vector<library::Entry>& history,
                      const std::vector<library::Entry>& bookmarks,
                      const std::vector<downloads::Item>& downloads);

// Store access stays on the browser UI thread. The CEF IO-thread scheme
// factory reads only this immutable, mutex-protected HTML snapshot.
void RefreshSnapshot(const std::vector<library::Entry>& history,
                     const std::vector<library::Entry>& bookmarks,
                     const std::vector<downloads::Item>& downloads);
std::string SnapshotHtml();

}  // namespace mtk::home
