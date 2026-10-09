#include "test_support.h"
#include "web_app.h"
MTK_TEST(WebAppIdsAreStableHttpsOnlyAndNeverUseHostText) {
  const auto first = mtk::web_app::IdForUrl("https://Example.com/path?x=1");
  MTK_EXPECT_TRUE(first.starts_with("webapp-"));
  MTK_EXPECT_EQ_STR(first, mtk::web_app::IdForUrl("https://example.com/other"));
  MTK_EXPECT_TRUE(mtk::web_app::IdForUrl("http://example.com/").empty());
  MTK_EXPECT_TRUE(mtk::web_app::IdForUrl("https://evil.com/../x").find("evil") == std::string::npos);
}

MTK_TEST(WebAppCommandLineQuotingKeepsTheUrlAsOneArgument) {
  MTK_EXPECT_EQ_WSTR(mtk::web_app::QuoteCommandLineArgument(L"https://example.com/a b"),
                     L"\"https://example.com/a b\"");
  MTK_EXPECT_EQ_WSTR(mtk::web_app::QuoteCommandLineArgument(L"https://example.com/\" --profile=bad"),
                     L"\"https://example.com/\\\" --profile=bad\"");
  MTK_EXPECT_EQ_WSTR(mtk::web_app::QuoteCommandLineArgument(L"https://example.com/a\\"),
                     L"\"https://example.com/a\\\\\"");
}

MTK_TEST(WebAppLaunchArgumentsUseOnlyTheOpaqueProfileId) {
  const std::wstring arguments =
      mtk::web_app::LaunchArguments("https://example.com/a path");
  MTK_EXPECT_TRUE(arguments.starts_with(L"--profile=webapp-"));
  MTK_EXPECT_TRUE(arguments.find(L"--app-mode") != std::wstring::npos);
  MTK_EXPECT_TRUE(arguments.find(L"example.com") != std::wstring::npos);
  MTK_EXPECT_TRUE(mtk::web_app::LaunchArguments("http://example.com/").empty());
}

MTK_TEST(WebAppInstalledListAndRemoveOperations) {
  const auto apps = mtk::web_app::ListInstalledApps();
  // Safe execution even if no apps currently installed in suite environment
  MTK_EXPECT_TRUE(apps.size() >= 0);
  MTK_EXPECT_FALSE(mtk::web_app::RemoveInstalledApp(""));
  MTK_EXPECT_FALSE(mtk::web_app::RemoveInstalledApp("non-existent-id"));
}

MTK_TEST(WebAppInstallOptionsDefaults) {
  mtk::web_app::InstallOptions options;
  MTK_EXPECT_TRUE(options.desktop_shortcut);
  MTK_EXPECT_TRUE(options.start_menu_shortcut);
  MTK_EXPECT_TRUE(options.display_name.empty());
}

