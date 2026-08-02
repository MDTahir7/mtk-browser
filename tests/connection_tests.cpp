// Tests for HTTPS-only mode and the encrypted-DNS provider table.
//
// The subtle rule being pinned down here: HTTPS-only refuses plain HTTP for the
// resources a page pulls in, but must NOT refuse an http main frame, because a
// typed or linked address is upgraded to https on the allowed path. Refusing it
// here would turn that upgrade into a block page.

#include <string>

#include "policy.h"
#include "preferences.h"
#include "test_support.h"

namespace {

bool Refused(const char* url, mtk::policy::ResourceType type) {
  return !mtk::policy::EvaluateRequest(url, type).allow;
}

}  // namespace

MTK_TEST(HttpsOnlyIsOnByDefault) {
  MTK_EXPECT_TRUE(mtk::preferences::Store::Get().values().https_only);
}

MTK_TEST(HttpsOnlyRefusesPlainHttpSubresources) {
  MTK_EXPECT_TRUE(Refused("http://example.com/tracker.js",
                          mtk::policy::ResourceType::kScript));
  MTK_EXPECT_TRUE(Refused("http://example.com/pixel.png",
                          mtk::policy::ResourceType::kImage));
  MTK_EXPECT_TRUE(Refused("http://example.com/frame.html",
                          mtk::policy::ResourceType::kSubFrame));
  MTK_EXPECT_TRUE(Refused("http://example.com/api",
                          mtk::policy::ResourceType::kXhr));
}

MTK_TEST(HttpsOnlyLeavesMainFrameAddressesToTheUpgrade) {
  // Allowed here, and then rewritten to https by the navigation path.
  MTK_EXPECT_FALSE(Refused("http://example.com/page",
                           mtk::policy::ResourceType::kMainFrame));
  const auto decision = mtk::policy::EvaluateNavigation("http://example.com/page");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_TRUE(decision.url.starts_with("https://"));
}

MTK_TEST(HttpsSubresourcesStillAllowedOverHttps) {
  MTK_EXPECT_FALSE(Refused("https://example.com/app.js",
                           mtk::policy::ResourceType::kScript));
}

MTK_TEST(DohProvidersHaveHttpsTemplatesExceptSystem) {
  using mtk::preferences::DohProvider;
  for (const auto provider :
       {DohProvider::kCloudflareFamily, DohProvider::kCloudflare,
        DohProvider::kQuad9, DohProvider::kGoogle}) {
    const std::string uri = mtk::preferences::DohTemplate(provider);
    MTK_EXPECT_TRUE(uri.starts_with("https://"));
    MTK_EXPECT_TRUE(uri.find("/dns-query") != std::string::npos);
    MTK_EXPECT_TRUE(mtk::preferences::DohLabel(provider)[0] != L'\0');
  }
  // The system choice has no template, which is what switches encrypted DNS off.
  MTK_EXPECT_EQ_STR(mtk::preferences::DohTemplate(DohProvider::kSystem), "");
}

MTK_TEST(DohDefaultIsTheFamilyResolver) {
  // The default resolver refuses malware and adult domains at the DNS layer,
  // which is a second line behind the browser's own rules.
  MTK_EXPECT_TRUE(mtk::preferences::Store::Get().values().doh_provider ==
                  mtk::preferences::DohProvider::kCloudflareFamily);
  MTK_EXPECT_EQ_STR(
      mtk::preferences::DohTemplate(
          mtk::preferences::Store::Get().values().doh_provider),
      "https://family.cloudflare-dns.com/dns-query");
}
