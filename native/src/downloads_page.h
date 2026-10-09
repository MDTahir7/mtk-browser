#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "download_manager.h"

namespace mtk::downloads_page {

inline constexpr char kHost[] = "downloads";
inline constexpr char kUrl[] = "mtk://downloads/";
inline constexpr wchar_t kUrlWide[] = L"mtk://downloads/";
inline constexpr size_t kMaxQueryLength = 200;

enum class ActionKind {
  kNone,
  kOpen,
  kShow,
  kRemove,
  kClearFinished,
  kRetry,
  kOpenFolder,
  kCopyUrl,
};

struct Action {
  ActionKind kind = ActionKind::kNone;
  uint64_t download_id = 0;
};

// Returns true if the URL matches mtk://downloads/ or mtk://downloads/?q=...
bool IsDownloadsUrl(std::string_view url);

// Extracts search query from URL, if present.
std::string QueryOf(std::string_view url);

// Extracts category filter from URL (e.g. ?cat=docs, ?cat=images), if present.
std::string CategoryOf(std::string_view url);

// Parses action URLs like mtk://downloads/action/open?id=123
Action ParseActionUrl(std::string_view url);

// Builds the self-contained HTML for the downloads page.
std::string BuildHtml(const std::vector<downloads::Item>& items,
                      std::string_view query, std::string_view category,
                      bool light_appearance, bool private_session);

}  // namespace mtk::downloads_page
