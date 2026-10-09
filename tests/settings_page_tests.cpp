#include "test_support.h"

#include <string>

#include "preferences.h"
#include "settings_page.h"

namespace {

using mtk::settings_page::Action;
using mtk::settings_page::ActionKind;

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

MTK_TEST(SettingsUrlRecognition) {
  MTK_EXPECT_TRUE(mtk::settings_page::IsSettingsUrl("mtk://settings"));
  MTK_EXPECT_TRUE(mtk::settings_page::IsSettingsUrl("mtk://settings/"));
  MTK_EXPECT_TRUE(mtk::settings_page::IsSettingsUrl("mtk://settings/?q=theme"));
  MTK_EXPECT_FALSE(mtk::settings_page::IsSettingsUrl("mtk://home/"));
  MTK_EXPECT_FALSE(mtk::settings_page::IsSettingsUrl("https://settings/"));
  MTK_EXPECT_FALSE(mtk::settings_page::IsSettingsUrl("mtk://settings/unknown"));
}

MTK_TEST(SettingsQueryParsing) {
  MTK_EXPECT_EQ_STR(mtk::settings_page::QueryOf("mtk://settings/"), "");
  MTK_EXPECT_EQ_STR(mtk::settings_page::QueryOf("mtk://settings/?q=https"), "https");
  MTK_EXPECT_EQ_STR(mtk::settings_page::QueryOf("mtk://settings/?q=dark+mode"), "dark mode");
}

MTK_TEST(SettingsActionParsing) {
  const auto a_https = mtk::settings_page::ParseActionUrl("mtk://settings/action/toggle?key=https_only");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_https.kind), static_cast<int>(ActionKind::kToggleHttpsOnly));

  const auto a_fp = mtk::settings_page::ParseActionUrl("mtk://settings/action/toggle?key=fingerprint");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_fp.kind), static_cast<int>(ActionKind::kToggleFingerprint));

  const auto a_focus = mtk::settings_page::ParseActionUrl("mtk://settings/action/toggle?key=focus_mode");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_focus.kind), static_cast<int>(ActionKind::kToggleFocusMode));

  const auto a_mem = mtk::settings_page::ParseActionUrl("mtk://settings/action/toggle?key=memory_saver");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_mem.kind), static_cast<int>(ActionKind::kToggleMemorySaver));

  const auto a_vtabs = mtk::settings_page::ParseActionUrl("mtk://settings/action/toggle?key=vertical_tabs");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_vtabs.kind), static_cast<int>(ActionKind::kToggleVerticalTabs));

  const auto a_ask = mtk::settings_page::ParseActionUrl("mtk://settings/action/toggle?key=ask_download");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_ask.kind), static_cast<int>(ActionKind::kToggleDownloadAskLocation));

  const auto a_theme = mtk::settings_page::ParseActionUrl("mtk://settings/action/set-theme?val=dark");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_theme.kind), static_cast<int>(ActionKind::kSetTheme));
  MTK_EXPECT_EQ_STR(a_theme.value, "dark");

  const auto a_search = mtk::settings_page::ParseActionUrl("mtk://settings/action/set-search?val=duckduckgo");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_search.kind), static_cast<int>(ActionKind::kSetSearchEngine));
  MTK_EXPECT_EQ_STR(a_search.value, "duckduckgo");

  const auto a_startup = mtk::settings_page::ParseActionUrl("mtk://settings/action/set-startup?val=restore");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_startup.kind), static_cast<int>(ActionKind::kSetStartupMode));
  MTK_EXPECT_EQ_STR(a_startup.value, "restore");

  const auto a_doh = mtk::settings_page::ParseActionUrl("mtk://settings/action/set-doh?val=quad9");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_doh.kind), static_cast<int>(ActionKind::kSetDohProvider));
  MTK_EXPECT_EQ_STR(a_doh.value, "quad9");

  const auto a_folder = mtk::settings_page::ParseActionUrl("mtk://settings/action/open-folder");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_folder.kind), static_cast<int>(ActionKind::kOpenDownloadFolder));

  const auto a_clear = mtk::settings_page::ParseActionUrl("mtk://settings/action/clear-data");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_clear.kind), static_cast<int>(ActionKind::kClearBrowsingData));

  const auto a_reset = mtk::settings_page::ParseActionUrl("mtk://settings/action/reset-settings");
  MTK_EXPECT_EQ_INT(static_cast<int>(a_reset.kind), static_cast<int>(ActionKind::kResetSettings));
}

MTK_TEST(SettingsPageRendersSectionsAndPreferences) {
  mtk::preferences::Values prefs;
  prefs.https_only = true;
  prefs.download_ask_location = true;
  prefs.search_engine = "google";
  prefs.theme_mode = mtk::preferences::ThemeMode::kDark;

  const std::string html = mtk::settings_page::BuildHtml(prefs, "", false, false);
  MTK_EXPECT_TRUE(Contains(html, "Settings"));
  MTK_EXPECT_TRUE(Contains(html, "HTTPS-Only Mode"));
  MTK_EXPECT_TRUE(Contains(html, "Ask Where to Save Each File Before Downloading"));
  MTK_EXPECT_TRUE(Contains(html, "Privacy &amp; Security"));
  MTK_EXPECT_TRUE(Contains(html, "Theme Mode"));
  MTK_EXPECT_TRUE(Contains(html, "Reset Settings"));
}
