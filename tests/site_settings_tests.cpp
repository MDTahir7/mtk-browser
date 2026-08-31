// Tests for per-site settings. The store keys on an origin derived from a URL,
// so origin parsing gets the most attention: two URLs that name the same site
// must produce the same key, and anything that is not an ordinary web address
// must produce none at all.
//
// Nothing here calls Flush(), so no file is written and the developer's own
// profile is left untouched.

#include <limits>
#include <string>

#include "policy.h"
#include "site_settings.h"
#include "test_support.h"

MTK_TEST(SiteOriginKeepsSchemeAndHost) {
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("https://example.com/a/b?c=d#e"),
                    "https://example.com");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("http://example.com"),
                    "http://example.com");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("https://sub.example.co.uk/page"),
                    "https://sub.example.co.uk");
}

MTK_TEST(SiteOriginIsCaseInsensitiveAndPortIndependent) {
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("HTTPS://Example.COM/Path"),
                    "https://example.com");
  // The port is folded away on purpose, so one site cannot end up with two
  // different rule sets.
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("https://example.com:8443/x"),
                    "https://example.com");
}

MTK_TEST(SiteOriginRefusesNonWebAddresses) {
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("about:blank"), "");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("data:text/html,<b>x</b>"), "");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("file:///C:/Windows/win.ini"), "");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("chrome://settings"), "");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("https://"), "");
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf(""), "");
  // Credentials would let two spellings name one site, so such a URL has no key.
  MTK_EXPECT_EQ_STR(mtk::sites::OriginOf("https://user:pass@example.com/"), "");
}

MTK_TEST(SiteZoomRoundTripsThroughChromiumLevels) {
  MTK_EXPECT_EQ_INT(mtk::sites::ZoomPercentForLevel(0.0), 100);
  for (const int percent : {25, 50, 75, 90, 100, 110, 150, 200, 300, 500}) {
    const double level = mtk::sites::ZoomLevelForPercent(percent);
    MTK_EXPECT_EQ_INT(mtk::sites::ZoomPercentForLevel(level), percent);
  }
}

MTK_TEST(SiteZoomClampsOutOfRangeValues) {
  MTK_EXPECT_EQ_INT(mtk::sites::ZoomPercentForLevel(-40.0),
                    mtk::sites::kMinZoomPercent);
  MTK_EXPECT_EQ_INT(mtk::sites::ZoomPercentForLevel(40.0),
                    mtk::sites::kMaxZoomPercent);
  // A level that is not a number must not produce a nonsense zoom.
  MTK_EXPECT_EQ_INT(mtk::sites::ZoomPercentForLevel(
                        std::numeric_limits<double>::quiet_NaN()),
                    100);
}

MTK_TEST(SiteStoreKeepsOnlyRealDecisions) {
  auto& store = mtk::sites::Store::Get();
  store.Clear();

  mtk::sites::Rule rule;
  rule.origin = "https://example.com";
  // A rule that says nothing is not worth a row in the file.
  MTK_EXPECT_TRUE(store.Set(rule));
  MTK_EXPECT_FALSE(store.HasRule("https://example.com"));

  rule.camera = mtk::sites::Permission::kAllow;
  MTK_EXPECT_TRUE(store.Set(rule));
  MTK_EXPECT_TRUE(store.HasRule("https://example.com"));
  MTK_EXPECT_TRUE(store.For("https://example.com").camera ==
                  mtk::sites::Permission::kAllow);

  // Returning it to the defaults removes it again.
  rule.camera = mtk::sites::Permission::kAsk;
  MTK_EXPECT_TRUE(store.Set(rule));
  MTK_EXPECT_FALSE(store.HasRule("https://example.com"));
  store.Clear();
}

MTK_TEST(SiteStoreRefusesKeysThatAreNotOrigins) {
  auto& store = mtk::sites::Store::Get();
  store.Clear();
  mtk::sites::Rule rule;
  rule.cookies = false;
  // A full URL, a bare host and an empty string are all unusable as keys, so
  // they cannot slip into the file and silently never match anything.
  for (const char* key : {"https://example.com/page", "example.com", "",
                          "https://example.com:443"}) {
    rule.origin = key;
    MTK_EXPECT_FALSE(store.Set(rule));
  }
  MTK_EXPECT_EQ_INT(store.rules().size(), 0);
}

MTK_TEST(SiteStoreDefaultsAreSafe) {
  auto& store = mtk::sites::Store::Get();
  store.Clear();
  const auto rule = store.For("https://unknown.example");
  // No permission is granted by default, filtering is on, cookies behave
  // normally, zoom is untouched and HTTPS-only is not exempted.
  MTK_EXPECT_TRUE(rule.camera == mtk::sites::Permission::kAsk);
  MTK_EXPECT_TRUE(rule.microphone == mtk::sites::Permission::kAsk);
  MTK_EXPECT_TRUE(rule.location == mtk::sites::Permission::kAsk);
  MTK_EXPECT_TRUE(rule.notifications == mtk::sites::Permission::kAsk);
  MTK_EXPECT_TRUE(rule.ad_filtering);
  MTK_EXPECT_TRUE(rule.cookies);
  MTK_EXPECT_TRUE(!rule.allow_plain_http);
  MTK_EXPECT_EQ_INT(rule.zoom_percent, 100);
}

MTK_TEST(SiteStoreAllowPlainHttpRoundTrips) {
  auto& store = mtk::sites::Store::Get();
  store.Clear();

  mtk::sites::Rule rule;
  rule.origin = "https://example.com";
  rule.allow_plain_http = true;
  MTK_EXPECT_TRUE(store.Set(rule));
  MTK_EXPECT_TRUE(store.HasRule("https://example.com"));
  MTK_EXPECT_TRUE(store.For("https://example.com").allow_plain_http);

  // Turning it back off removes the rule again, since nothing else is set.
  rule.allow_plain_http = false;
  MTK_EXPECT_TRUE(store.Set(rule));
  MTK_EXPECT_FALSE(store.HasRule("https://example.com"));
  store.Clear();
}

MTK_TEST(SiteShieldsOffNeverWeakensSafetyRules) {
  // The per-site switch may only drop ad and tracker filtering. Everything the
  // request policy refuses for safety has to stay refused with filtering off.
  const auto blocked_with_filtering_off = [](const char* url,
                                             mtk::policy::ResourceType type) {
    return !mtk::policy::EvaluateRequest(url, type, {}, false).allow;
  };
  MTK_EXPECT_TRUE(blocked_with_filtering_off("https://www.pornhub.com/",
                                             mtk::policy::ResourceType::kMainFrame));
  MTK_EXPECT_TRUE(blocked_with_filtering_off("http://192.168.0.1/",
                                             mtk::policy::ResourceType::kMainFrame));
  MTK_EXPECT_TRUE(blocked_with_filtering_off("file:///C:/Windows/win.ini",
                                             mtk::policy::ResourceType::kMainFrame));
  MTK_EXPECT_TRUE(blocked_with_filtering_off(
      "https://www.google.com/search?q=porn",
      mtk::policy::ResourceType::kMainFrame));
  // An ordinary page still loads.
  MTK_EXPECT_FALSE(blocked_with_filtering_off(
      "https://example.com/", mtk::policy::ResourceType::kMainFrame));
}
