#include "test_support.h"

#include "policy.h"

using mtk::policy::EvaluateNavigation;
using mtk::policy::EvaluateRequest;
using mtk::policy::HostOf;
using mtk::policy::ResolveInput;
using mtk::policy::ResourceType;

MTK_TEST(NavigationAllowsOrdinaryHttpsSite) {
  const auto decision = EvaluateNavigation("https://example.com/");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_STR(decision.url, "https://example.com/");
}

MTK_TEST(NavigationRejectsUnsupportedScheme) {
  MTK_EXPECT_FALSE(EvaluateNavigation("ftp://example.com/file.txt").allow);
  MTK_EXPECT_FALSE(EvaluateNavigation("file:///C:/Windows/win.ini").allow);
  MTK_EXPECT_FALSE(EvaluateNavigation("javascript:alert(1)").allow);
}

MTK_TEST(NavigationBlocksPrivateNetworkHosts) {
  MTK_EXPECT_FALSE(EvaluateNavigation("http://localhost:8080/").allow);
  MTK_EXPECT_FALSE(EvaluateNavigation("http://127.0.0.1/").allow);
  MTK_EXPECT_FALSE(EvaluateNavigation("http://192.168.1.1/").allow);
  MTK_EXPECT_FALSE(EvaluateNavigation("http://10.0.0.5/").allow);
}

MTK_TEST(NavigationBlocksKnownAdultHost) {
  MTK_EXPECT_FALSE(EvaluateNavigation("https://www.pornhub.com/").allow);
}

MTK_TEST(NavigationKeepsLegitimateLookalikeHost) {
  // Conservative matching must not block ordinary words that contain a blocked
  // substring, such as "essex".
  MTK_EXPECT_TRUE(EvaluateNavigation("https://www.essex.ac.uk/").allow);
}

MTK_TEST(NavigationUpgradesHttpToHttps) {
  const auto decision = EvaluateNavigation("http://example.com/page");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_STR(decision.url, "https://example.com/page");
}

MTK_TEST(SafeSearchIsEnforcedForMajorEngines) {
  MTK_EXPECT_EQ_STR(EvaluateNavigation("https://www.google.com/search?q=cats").url,
                    "https://www.google.com/search?q=cats&safe=active");
  MTK_EXPECT_EQ_STR(EvaluateNavigation("https://www.bing.com/search?q=cats").url,
                    "https://www.bing.com/search?q=cats&adlt=strict");
  MTK_EXPECT_EQ_STR(EvaluateNavigation("https://duckduckgo.com/?q=cats").url,
                    "https://duckduckgo.com/?q=cats&kp=1");
}

MTK_TEST(SafeSearchIsNotDuplicatedWhenPresent) {
  MTK_EXPECT_EQ_STR(
      EvaluateNavigation("https://www.google.com/search?q=cats&safe=active").url,
      "https://www.google.com/search?q=cats&safe=active");
}

MTK_TEST(ResolveInputTreatsPlainTextAsSafeSearch) {
  const auto decision = ResolveInput("kids science project");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_TRUE(decision.url.starts_with(
      "https://www.google.com/search?safe=active&q="));
}

MTK_TEST(ResolveInputTreatsHostLikeTextAsUrl) {
  const auto decision = ResolveInput("example.com");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_STR(decision.url, "https://example.com");
}

MTK_TEST(ResolveInputFallsBackForEmptyText) {
  const auto decision = ResolveInput("   ");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_TRUE(decision.url.starts_with("https://www.google.com/"));
}

MTK_TEST(HostOfExtractsLowercaseHost) {
  MTK_EXPECT_EQ_STR(HostOf("https://Example.COM/path?a=b"), "example.com");
  MTK_EXPECT_EQ_STR(HostOf("not a url"), "");
}

MTK_TEST(RequestsFromBlockedHostsAreRefused) {
  MTK_EXPECT_FALSE(
      EvaluateRequest("https://www.pornhub.com/x.jpg", ResourceType::kImage).allow);
  MTK_EXPECT_FALSE(
      EvaluateRequest("http://127.0.0.1/probe", ResourceType::kXhr).allow);
}

MTK_TEST(OrdinarySubresourceIsAllowed) {
  MTK_EXPECT_TRUE(
      EvaluateRequest("https://example.com/style.css", ResourceType::kStylesheet)
          .allow);
}

MTK_TEST(AboutBlankRequestIsAllowed) {
  MTK_EXPECT_TRUE(EvaluateRequest("about:blank", ResourceType::kMainFrame).allow);
}

MTK_TEST(PrivacyHeadersAreSent) {
  const auto headers = mtk::policy::PrivacyRequestHeaders("https://example.com/");
  bool has_dnt = false;
  bool has_gpc = false;
  for (const auto& [name, value] : headers) {
    if (name == "DNT" && value == "1") has_dnt = true;
    if (name == "Sec-GPC" && value == "1") has_gpc = true;
  }
  MTK_EXPECT_TRUE(has_dnt);
  MTK_EXPECT_TRUE(has_gpc);
}
