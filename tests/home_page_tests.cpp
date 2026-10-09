#include "test_support.h"

#include <fstream>
#include <string>
#include <vector>

#include "home_page.h"

namespace {

bool Contains(const std::string& text, const char* fragment) {
  return text.find(fragment) != std::string::npos;
}

}  // namespace

MTK_TEST(HomePageAppearanceUsesTheResolvedThemeWithoutChangingItsCsp) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string dark = mtk::home::BuildHtml(history, bookmarks, downloads);
  const std::string light =
      mtk::home::BuildHtml(history, bookmarks, downloads, true);
  MTK_EXPECT_TRUE(Contains(dark, "color-scheme:dark"));
  MTK_EXPECT_FALSE(Contains(dark, "color-scheme:light"));
  MTK_EXPECT_TRUE(Contains(light, "color-scheme:light"));
  MTK_EXPECT_TRUE(Contains(light, "--bg:#f5f7fc"));
  // Theme styling must not weaken the internal page's network boundary.
  MTK_EXPECT_TRUE(Contains(light, "default-src 'none'"));
  MTK_EXPECT_TRUE(Contains(light, "script-src 'unsafe-inline'"));
}

MTK_TEST(HomePageIncludesRealTimeClockScriptAndElements) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  MTK_EXPECT_TRUE(Contains(html, "id=home-clock"));
  MTK_EXPECT_TRUE(Contains(html, "id=home-date"));
  MTK_EXPECT_TRUE(Contains(html, "id=home-greeting"));
  MTK_EXPECT_TRUE(Contains(html, "updateTime"));
  MTK_EXPECT_TRUE(Contains(html, "setInterval(updateTime,1000)"));
}

MTK_TEST(HomePageIncludesWeatherWidgetAndScript) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  MTK_EXPECT_TRUE(Contains(html, "id=weather-card"));
  MTK_EXPECT_TRUE(Contains(html, "id=weather-display"));
  MTK_EXPECT_TRUE(Contains(html, "id=weather-temp"));
  MTK_EXPECT_TRUE(Contains(html, "id=weather-loc"));
  MTK_EXPECT_TRUE(Contains(html, "fetchWeather"));
  MTK_EXPECT_TRUE(Contains(html, "https://wttr.in"));
}

MTK_TEST(HomePageIncludesDailyQuotesAndShuffle) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  MTK_EXPECT_TRUE(Contains(html, "id=quote-card"));
  MTK_EXPECT_TRUE(Contains(html, "id=quote-text"));
  MTK_EXPECT_TRUE(Contains(html, "id=quote-author"));
  MTK_EXPECT_TRUE(Contains(html, "id=quote-shuffle"));
  MTK_EXPECT_TRUE(Contains(html, "quotes"));
  MTK_EXPECT_TRUE(Contains(html, "advanceQuote"));
  MTK_EXPECT_TRUE(Contains(html, "fetchLiveQuote"));
  MTK_EXPECT_TRUE(Contains(html, "https://dummyjson.com"));
}

MTK_TEST(HomePageIncludesQuickNoteInteractiveCardAndStorage) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  MTK_EXPECT_TRUE(Contains(html, "id=quick-note-card"));
  MTK_EXPECT_TRUE(Contains(html, "id=quick-note-input"));
  MTK_EXPECT_TRUE(Contains(html, "id=quick-note-copy"));
  MTK_EXPECT_TRUE(Contains(html, "id=quick-note-clear"));
  MTK_EXPECT_TRUE(Contains(html, "mtk_quick_note"));
  MTK_EXPECT_TRUE(Contains(html, "id=nav-notes"));
}

MTK_TEST(HomePageIncludesSearch3DAnimationAndDataUrlCsp) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify CSP allows data: images for our inlined 3D assets while maintaining strict isolation
  MTK_EXPECT_TRUE(Contains(html, "img-src data:;"));
  // Left side animation was removed per user request: verify no search-3d-lead
  MTK_EXPECT_FALSE(Contains(html, "search-3d-lead"));
  // Right side button has both still and animated 3D elements for hover activation
  MTK_EXPECT_TRUE(Contains(html, "search-btn-3d"));
  MTK_EXPECT_TRUE(Contains(html, "search-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "search-3d-anim"));
  // Verify WebP data URLs are present
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesHomeButton3DAnimationAndSmokeHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify Home button uses 3D container with still and animated smoke elements
  MTK_EXPECT_TRUE(Contains(html, "home-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "home-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "home-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "home-3d-anim"));
  // Verify both WebP assets are inlined via data URLs
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesStoreButton3DAnimationAndHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify Store button and quick link use 3D container with still and animated elements
  MTK_EXPECT_TRUE(Contains(html, "store-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "store-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "store-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "store-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "q-store-3d"));
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
  // Verify Store button appears directly above Notes button
  const size_t store_pos = html.find("store-nav-3d");
  const size_t notes_pos = html.find("notes-nav-3d");
  MTK_EXPECT_TRUE(store_pos != std::string::npos && notes_pos != std::string::npos && store_pos < notes_pos);
  // Verify Books button is completely removed
  MTK_EXPECT_FALSE(Contains(html, "books-nav-3d"));
  MTK_EXPECT_FALSE(Contains(html, "q-books-3d"));
}

MTK_TEST(HomePageIncludesNotesButton3DAnimationAndWritingHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify Notes button uses 3D container with still and animated handwriting elements
  MTK_EXPECT_TRUE(Contains(html, "notes-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "notes-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "notes-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "notes-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "id=nav-notes"));
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesDownloadButton3DAnimationAndHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify Downloads button uses 3D container with still and animated download elements
  MTK_EXPECT_TRUE(Contains(html, "download-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "download-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "download-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "download-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://home/action/downloads"));
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesHistoryButton3DAnimationAndHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  std::ofstream out("E:\\MTK Browser\\scratch\\home_preview.html", std::ios::binary);
  if (out.is_open()) {
    out.write(html.data(), html.size());
    out.close();
  }
  // Verify History button uses 3D container with still and animated history elements
  MTK_EXPECT_TRUE(Contains(html, "history-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "history-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "history-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "history-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://home/action/history"));
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesSettingsButton3DAnimationAndHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify Settings button uses 3D container with still and animated gear elements
  MTK_EXPECT_TRUE(Contains(html, "settings-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "settings-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "settings-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "settings-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://home/action/settings"));
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesBookmarksButton3DAnimationAndHover) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  // Verify Bookmarks button uses 3D container with still and animated ribbon elements
  MTK_EXPECT_TRUE(Contains(html, "bookmarks-nav-3d"));
  MTK_EXPECT_TRUE(Contains(html, "bookmarks-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "bookmarks-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "bookmarks-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://home/action/bookmarks"));
  MTK_EXPECT_TRUE(Contains(html, "data:image/webp;base64,"));
}

MTK_TEST(HomePageIncludesAiAssistantAndInteractiveSearchUdm) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);

  // Interactive Quick Search Web and Ask AI actions
  MTK_EXPECT_TRUE(Contains(html, "id=quick-search-web"));
  MTK_EXPECT_TRUE(Contains(html, "Search Web"));
  MTK_EXPECT_TRUE(Contains(html, "q-search-3d"));
  MTK_EXPECT_TRUE(Contains(html, "search-web-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "search-web-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "search-web-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "id=quick-ask-ai"));
  MTK_EXPECT_TRUE(Contains(html, "Ask AI"));
  MTK_EXPECT_TRUE(Contains(html, "Google AI Answers"));
  MTK_EXPECT_TRUE(Contains(html, "q-ai-3d"));
  MTK_EXPECT_TRUE(Contains(html, "ask-ai-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "ask-ai-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "ask-ai-3d-anim"));

  // Verify "Coming soon" is removed from Ask AI
  MTK_EXPECT_FALSE(Contains(html, "Ask AI</b><small>Coming soon</small>"));

  // Hidden UDM input for Google AI overview routing
  MTK_EXPECT_TRUE(Contains(html, "id=search-udm"));
  MTK_EXPECT_TRUE(Contains(html, "name=udm"));

  // Script question detection and mode handling
  MTK_EXPECT_TRUE(Contains(html, "isQuestionQuery"));
  MTK_EXPECT_TRUE(Contains(html, "setAiMode"));
  MTK_EXPECT_TRUE(Contains(html, "udm=50"));
}

MTK_TEST(HomePageDownloadsRenderWithFileIconBadgesAndCleanNames) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  std::vector<mtk::downloads::Item> downloads;
  mtk::downloads::Item item;
  item.id = 42;
  item.file_name = L"objecttype-raw-_WiE1pPPT9k-unsplash.jpg";
  item.total_bytes = 1900000;
  item.state = mtk::downloads::State::kComplete;
  downloads.push_back(item);

  mtk::downloads::Item item2;
  item2.id = 43;
  item2.file_name = L"Annual_Report_2026.pdf";
  item2.total_bytes = 4500000;
  item2.state = mtk::downloads::State::kComplete;
  downloads.push_back(item2);

  mtk::downloads::Item item3;
  item3.id = 44;
  item3.file_name = L"ubuntu-24.04-desktop.iso";
  item3.received_bytes = 1500000000;
  item3.total_bytes = 4000000000;
  item3.percent = 37;
  item3.state = mtk::downloads::State::kInProgress;
  downloads.push_back(item3);

  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);
  std::ofstream out_dl("E:\\MTK Browser\\scratch\\home_with_downloads.html", std::ios::binary);
  if (out_dl.is_open()) {
    out_dl.write(html.data(), html.size());
    out_dl.close();
  }
  MTK_EXPECT_TRUE(Contains(html, "file-badge file-img"));
  MTK_EXPECT_TRUE(Contains(html, "WiE1pPPT9k-unsplash.jpg"));
  MTK_EXPECT_FALSE(Contains(html, "objecttype-raw-"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://home/action/download-open?id=42"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://home/action/download-show?id=42"));
  MTK_EXPECT_TRUE(Contains(html, "dl-folder-btn"));
}

MTK_TEST(HomePageDownloadActionsParsing) {
  const auto open_act = mtk::home::ParseActionUrl("mtk://home/action/download-open?id=42");
  MTK_EXPECT_TRUE(open_act.kind == mtk::home::ActionKind::kDownloadOpen);
  MTK_EXPECT_EQ_INT(open_act.download_id, 42);

  const auto show_act = mtk::home::ParseActionUrl("mtk://home/action/download-show?id=88");
  MTK_EXPECT_TRUE(show_act.kind == mtk::home::ActionKind::kDownloadShow);
  MTK_EXPECT_EQ_INT(show_act.download_id, 88);
}

MTK_TEST(HomePageIncludesInteractivePinnedSitesAndStorage) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);

  // Card container and interactive elements
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-sites-card"));
  MTK_EXPECT_TRUE(Contains(html, "id=add-pinned-btn"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-sites-list"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-form-box"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-name-input"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-url-input"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-save-btn"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-cancel-btn"));

  // Old dummy chips must be removed
  MTK_EXPECT_FALSE(Contains(html, "Pin a favourite"));
  MTK_EXPECT_FALSE(Contains(html, "Profile only"));

  // Storage and JS functionality
  MTK_EXPECT_TRUE(Contains(html, "mtk_pinned_sites"));
  MTK_EXPECT_TRUE(Contains(html, "renderPinnedSites"));
  MTK_EXPECT_TRUE(Contains(html, "defaultPinned"));
  MTK_EXPECT_TRUE(Contains(html, "pinned-del-btn"));

  // Built-in favicons & vector icons for pinned sites
  MTK_EXPECT_TRUE(Contains(html, "BUILTIN_FAVICONS"));
  MTK_EXPECT_TRUE(Contains(html, "getSiteFavicon"));
  MTK_EXPECT_TRUE(Contains(html, "tile-has-icon"));
  MTK_EXPECT_TRUE(Contains(html, "google.com"));
  MTK_EXPECT_TRUE(Contains(html, "youtube.com"));
  MTK_EXPECT_TRUE(Contains(html, "wikipedia.org"));
  MTK_EXPECT_TRUE(Contains(html, "github.com"));
}

MTK_TEST(HomePageExcludesBrowserStatusCardAndUsesThreeColumnLayout) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);

  // Browser Status card must be completely removed
  MTK_EXPECT_FALSE(Contains(html, "Browser Status"));
  MTK_EXPECT_FALSE(Contains(html, "SafeSearch"));
  MTK_EXPECT_FALSE(Contains(html, "status-grid"));
  MTK_EXPECT_FALSE(Contains(html, "status-chip"));

  // Bottom row uses balanced 3-column layout for Recent Downloads, Bookmarks, and Pinned Sites
  MTK_EXPECT_TRUE(Contains(html, "row three"));
  MTK_EXPECT_TRUE(Contains(html, "Recent Downloads"));
  MTK_EXPECT_TRUE(Contains(html, "Bookmarks"));
  MTK_EXPECT_TRUE(Contains(html, "id=pinned-sites-card"));
}

MTK_TEST(HomePageExcludesPublisherAndThemeToggleInSidebar) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);

  MTK_EXPECT_FALSE(Contains(html, "PUBLISHING"));
  MTK_EXPECT_FALSE(Contains(html, "class=publisher"));
  MTK_EXPECT_FALSE(Contains(html, "class=theme"));
  MTK_EXPECT_FALSE(Contains(html, "class=brand"));
}

MTK_TEST(HomePageIncludesPublishing3DAnimationAndInteractiveButton) {
  const std::vector<mtk::library::Entry> history;
  const std::vector<mtk::library::Entry> bookmarks;
  const std::vector<mtk::downloads::Item> downloads;
  const std::string html = mtk::home::BuildHtml(history, bookmarks, downloads);

  // Quick link has interactive 3D container with still and animated emblem
  MTK_EXPECT_TRUE(Contains(html, "q-publishing-3d"));
  MTK_EXPECT_TRUE(Contains(html, "id=quick-featured-publishing"));
  MTK_EXPECT_TRUE(Contains(html, "publishing-3d-wrap"));
  MTK_EXPECT_TRUE(Contains(html, "publishing-3d-still"));
  MTK_EXPECT_TRUE(Contains(html, "publishing-3d-anim"));
  MTK_EXPECT_TRUE(Contains(html, "Featured"));
  MTK_EXPECT_TRUE(Contains(html, "MTK Publishing"));

  // Featured Collection target card exists for smooth scroll interaction
  MTK_EXPECT_TRUE(Contains(html, "id=featured-collection-card"));

  // Script contains hover activation and smooth scroll interaction
  MTK_EXPECT_TRUE(Contains(html, "quick-featured-publishing"));
  MTK_EXPECT_TRUE(Contains(html, "featured-collection-card"));
  MTK_EXPECT_TRUE(Contains(html, "openFeatured"));
}


