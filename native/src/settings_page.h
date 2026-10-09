#pragma once

#include <string>
#include <string_view>
#include "preferences.h"

namespace mtk::settings_page {

inline constexpr char kHost[] = "settings";
inline constexpr char kUrl[] = "mtk://settings/";
inline constexpr wchar_t kUrlWide[] = L"mtk://settings/";

enum class ActionKind {
  kNone,
  kToggleHttpsOnly,
  kToggleSafeSearch,
  kToggleFingerprint,
  kToggleFocusMode,
  kToggleMemorySaver,
  kToggleVerticalTabs,
  kToggleDownloadAskLocation,
  kToggleClearOnExit,
  kToggleFamiliarShortcuts,
  kToggleWindowsHello,
  kSetTheme,
  kSetSearchEngine,
  kSetStartupMode,
  kSetDohProvider,
  kOpenDownloadFolder,
  kClearBrowsingData,
  kResetSettings,
};

struct Action {
  ActionKind kind = ActionKind::kNone;
  std::string value;
};

// Returns true if URL matches mtk://settings/ or mtk://settings/?q=...
bool IsSettingsUrl(std::string_view url);

// Extracts search query from URL, if present.
std::string QueryOf(std::string_view url);

// Parses action URLs like mtk://settings/action/toggle?key=https_only
Action ParseActionUrl(std::string_view url);

// Builds the self-contained HTML for the settings dashboard.
std::string BuildHtml(const preferences::Values& prefs,
                      std::string_view query, bool light_appearance,
                      bool hindi);

}  // namespace mtk::settings_page
