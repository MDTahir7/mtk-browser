// Tests for HTTPS-only mode and the encrypted-DNS provider table.
//
// The subtle rule being pinned down here: HTTPS-only refuses plain HTTP for the
// resources a page pulls in, but must NOT refuse an http main frame, because a
// typed or linked address is upgraded to https on the allowed path. Refusing it
// here would turn that upgrade into a block page.

#include <string>

#include "policy.h"
#include "preferences.h"
#include "site_settings.h"
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

MTK_TEST(HttpsOnlyExemptsOnlyTheOptedInInitiatorOrigin) {
  auto& store = mtk::sites::Store::Get();
  store.Clear();
  mtk::sites::Rule rule;
  rule.origin = "https://plaintext-only.example";
  rule.allow_plain_http = true;
  MTK_EXPECT_TRUE(store.Set(rule));

  // A subresource requested by a page that opted in is allowed even though it
  // is plain http. (logo.png is neither an ad-host label nor a tracking path,
  // so only the HTTPS-only rule is in play here.)
  MTK_EXPECT_FALSE(!mtk::policy::EvaluateRequest(
      "http://example.com/logo.png", mtk::policy::ResourceType::kImage,
      "https://plaintext-only.example").allow);
  // The same request from a page that did not opt in is still refused.
  MTK_EXPECT_TRUE(!mtk::policy::EvaluateRequest(
      "http://example.com/logo.png", mtk::policy::ResourceType::kImage,
      "https://other.example").allow);
  // No initiator at all (the browser's own requests) is also still refused.
  MTK_EXPECT_TRUE(Refused("http://example.com/logo.png",
                          mtk::policy::ResourceType::kImage));
  store.Clear();
}

MTK_TEST(FingerprintProtectionIsOnByDefault) {
  MTK_EXPECT_TRUE(mtk::preferences::Store::Get().values().fingerprint_protection);
}

MTK_TEST(HighEntropyClientHintHeaderNamesCoversTheKnownHighEntropyHints) {
  const auto names = mtk::policy::HighEntropyClientHintHeaderNames();
  // Every name is one of the documented high-entropy hints and none is one of
  // the low-entropy hints Chromium sends unconditionally to every site (those
  // carry far less identifying detail and are left alone).
  for (const auto& name : names) {
    MTK_EXPECT_TRUE(name.starts_with("Sec-CH-UA"));
    MTK_EXPECT_TRUE(name != "Sec-CH-UA");
    MTK_EXPECT_TRUE(name != "Sec-CH-UA-Mobile");
    MTK_EXPECT_TRUE(name != "Sec-CH-UA-Platform");
  }
  MTK_EXPECT_TRUE(!names.empty());
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
