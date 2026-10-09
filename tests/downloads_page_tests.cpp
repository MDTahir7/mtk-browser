#include "test_support.h"

#include <string>
#include <vector>

#include "downloads_page.h"

namespace {

using mtk::downloads_page::Action;
using mtk::downloads_page::ActionKind;

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

mtk::downloads::Item MakeTestItem(uint64_t id, const std::wstring& filename,
                                  const std::string& url,
                                  mtk::downloads::State state,
                                  int64_t received, int64_t total) {
  mtk::downloads::Item item;
  item.id = id;
  item.file_name = filename;
  item.full_path = L"C:\\Users\\User\\Downloads\\" + filename;
  item.url = url;
  item.state = state;
  item.received_bytes = received;
  item.total_bytes = total;
  item.percent = total > 0 ? static_cast<int>((received * 100) / total) : 0;
  return item;
}

}  // namespace

MTK_TEST(DownloadsUrlRecognition) {
  MTK_EXPECT_TRUE(mtk::downloads_page::IsDownloadsUrl("mtk://downloads"));
  MTK_EXPECT_TRUE(mtk::downloads_page::IsDownloadsUrl("mtk://downloads/"));
  MTK_EXPECT_TRUE(mtk::downloads_page::IsDownloadsUrl("mtk://downloads/?q=manual"));
  MTK_EXPECT_TRUE(mtk::downloads_page::IsDownloadsUrl("mtk://downloads/?cat=docs"));
  MTK_EXPECT_TRUE(mtk::downloads_page::IsDownloadsUrl("mtk://downloads/?cat=docs&q=test"));
  MTK_EXPECT_FALSE(mtk::downloads_page::IsDownloadsUrl("mtk://home/"));
  MTK_EXPECT_FALSE(mtk::downloads_page::IsDownloadsUrl("https://downloads/"));
  MTK_EXPECT_FALSE(mtk::downloads_page::IsDownloadsUrl("mtk://downloads/invalid"));
}

MTK_TEST(DownloadsQueryAndCategoryParsing) {
  MTK_EXPECT_EQ_STR(mtk::downloads_page::QueryOf("mtk://downloads/"), "");
  MTK_EXPECT_EQ_STR(mtk::downloads_page::QueryOf("mtk://downloads/?q=setup"), "setup");
  MTK_EXPECT_EQ_STR(mtk::downloads_page::QueryOf("mtk://downloads/?q=my+document"), "my document");
  MTK_EXPECT_EQ_STR(mtk::downloads_page::QueryOf("mtk://downloads/?cat=docs&q=guide"), "guide");

  MTK_EXPECT_EQ_STR(mtk::downloads_page::CategoryOf("mtk://downloads/"), "");
  MTK_EXPECT_EQ_STR(mtk::downloads_page::CategoryOf("mtk://downloads/?cat=docs"), "docs");
  MTK_EXPECT_EQ_STR(mtk::downloads_page::CategoryOf("mtk://downloads/?cat=images&q=wallpaper"), "images");
}

MTK_TEST(DownloadsActionParsing) {
  const auto open_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/open?id=42");
  MTK_EXPECT_EQ_INT(static_cast<int>(open_action.kind), static_cast<int>(ActionKind::kOpen));
  MTK_EXPECT_EQ_INT(open_action.download_id, 42ULL);

  const auto show_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/show?id=101");
  MTK_EXPECT_EQ_INT(static_cast<int>(show_action.kind), static_cast<int>(ActionKind::kShow));
  MTK_EXPECT_EQ_INT(show_action.download_id, 101ULL);

  const auto remove_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/remove?id=7");
  MTK_EXPECT_EQ_INT(static_cast<int>(remove_action.kind), static_cast<int>(ActionKind::kRemove));
  MTK_EXPECT_EQ_INT(remove_action.download_id, 7ULL);

  const auto retry_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/retry?id=88");
  MTK_EXPECT_EQ_INT(static_cast<int>(retry_action.kind), static_cast<int>(ActionKind::kRetry));
  MTK_EXPECT_EQ_INT(retry_action.download_id, 88ULL);

  const auto clear_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/clear-all");
  MTK_EXPECT_EQ_INT(static_cast<int>(clear_action.kind), static_cast<int>(ActionKind::kClearFinished));

  const auto folder_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/open-folder");
  MTK_EXPECT_EQ_INT(static_cast<int>(folder_action.kind), static_cast<int>(ActionKind::kOpenFolder));

  const auto unknown_action = mtk::downloads_page::ParseActionUrl("mtk://downloads/action/invalid");
  MTK_EXPECT_EQ_INT(static_cast<int>(unknown_action.kind), static_cast<int>(ActionKind::kNone));
}

MTK_TEST(DownloadsPageRendersItemsAndFilterTabs) {
  std::vector<mtk::downloads::Item> items;
  items.push_back(MakeTestItem(1, L"guide.pdf", "https://example.com/guide.pdf",
                               mtk::downloads::State::kComplete, 1024 * 1024, 1024 * 1024));
  items.push_back(MakeTestItem(2, L"photo.png", "https://example.com/photo.png",
                               mtk::downloads::State::kInProgress, 500 * 1024, 1000 * 1024));

  const std::string html = mtk::downloads_page::BuildHtml(items, "", "", false, false);
  MTK_EXPECT_TRUE(Contains(html, "Downloads"));
  MTK_EXPECT_TRUE(Contains(html, "guide.pdf"));
  MTK_EXPECT_TRUE(Contains(html, "Documents"));
  MTK_EXPECT_TRUE(Contains(html, "categories"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://downloads/action/open?id=1"));
  MTK_EXPECT_TRUE(Contains(html, "mtk://downloads/action/show?id=1"));
}

MTK_TEST(DownloadsPageCategoryFiltering) {
  std::vector<mtk::downloads::Item> items;
  items.push_back(MakeTestItem(1, L"document.pdf", "https://example.com/doc.pdf",
                               mtk::downloads::State::kComplete, 1024, 1024));
  items.push_back(MakeTestItem(2, L"image.jpg", "https://example.com/img.jpg",
                               mtk::downloads::State::kComplete, 2048, 2048));

  // Filter only images
  const std::string html_images = mtk::downloads_page::BuildHtml(items, "", "images", false, false);
  MTK_EXPECT_TRUE(Contains(html_images, "image.jpg"));
  MTK_EXPECT_FALSE(Contains(html_images, "document.pdf"));

  // Filter only docs
  const std::string html_docs = mtk::downloads_page::BuildHtml(items, "", "docs", false, false);
  MTK_EXPECT_TRUE(Contains(html_docs, "document.pdf"));
  MTK_EXPECT_FALSE(Contains(html_docs, "image.jpg"));
}
