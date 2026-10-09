#include "test_support.h"

#include "policy.h"

using mtk::policy::EvaluateNavigation;
using mtk::policy::EvaluateRequest;
using mtk::policy::HostOf;
using mtk::policy::IsQuestionQuery;
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

MTK_TEST(ResolveInputSupportsSearchEngineShortcuts) {
  const auto duck = ResolveInput("@d privacy test");
  MTK_EXPECT_TRUE(duck.allow);
  MTK_EXPECT_EQ_STR(duck.url, "https://duckduckgo.com/?kp=1&q=privacy+test");

  const auto bing = ResolveInput(":b microsoft search");
  MTK_EXPECT_TRUE(bing.allow);
  MTK_EXPECT_EQ_STR(bing.url, "https://www.bing.com/search?adlt=strict&q=microsoft+search");

  const auto wiki = ResolveInput("@w Albert Einstein");
  MTK_EXPECT_TRUE(wiki.allow);
  MTK_EXPECT_EQ_STR(wiki.url, "https://en.wikipedia.org/w/index.php?search=Albert+Einstein");

  const auto google = ResolveInput("@g fast search");
  MTK_EXPECT_TRUE(google.allow);
  MTK_EXPECT_EQ_STR(google.url, "https://www.google.com/search?safe=active&q=fast+search");

  const auto ai_shortcut = ResolveInput("@ai quantum mechanics");
  MTK_EXPECT_TRUE(ai_shortcut.allow);
  MTK_EXPECT_EQ_STR(ai_shortcut.url, "https://www.google.com/search?safe=active&udm=50&q=quantum+mechanics");

  const auto gemini_shortcut = ResolveInput("@gemini deep learning");
  MTK_EXPECT_TRUE(gemini_shortcut.allow);
  MTK_EXPECT_EQ_STR(gemini_shortcut.url, "https://www.google.com/search?safe=active&udm=50&q=deep+learning");

  const auto empty_shortcut = ResolveInput("@d ");
  MTK_EXPECT_TRUE(empty_shortcut.allow);
  MTK_EXPECT_EQ_STR(empty_shortcut.url, "https://duckduckgo.com/?kp=1");
}

MTK_TEST(QuestionsAutomaticallyRouteToGoogleAiWithUdm50) {
  // Direct IsQuestionQuery tests
  MTK_EXPECT_TRUE(IsQuestionQuery("mausam kaisa h lucknow ka"));
  MTK_EXPECT_TRUE(IsQuestionQuery("kya aaj barish hogi?"));
  MTK_EXPECT_TRUE(IsQuestionQuery("what is photosynthesis"));
  MTK_EXPECT_TRUE(IsQuestionQuery("how to bake sourdough bread"));
  MTK_EXPECT_TRUE(IsQuestionQuery("weather in tokyo"));
  MTK_EXPECT_TRUE(IsQuestionQuery("kab khulega school?"));
  MTK_EXPECT_TRUE(IsQuestionQuery("kitna samay lagega"));
  MTK_EXPECT_FALSE(IsQuestionQuery("wikipedia.org"));
  MTK_EXPECT_FALSE(IsQuestionQuery("download mtk browser"));
  MTK_EXPECT_FALSE(IsQuestionQuery("apple quarterly earnings"));

  // Questions routed through ResolveInput
  const auto hindi_q = ResolveInput("mausam kaisa h lucknow ka");
  MTK_EXPECT_TRUE(hindi_q.allow);
  MTK_EXPECT_EQ_STR(hindi_q.url,
                    "https://www.google.com/search?safe=active&udm=50&q=mausam+kaisa+h+lucknow+ka");

  const auto english_q = ResolveInput("what is quantum computing");
  MTK_EXPECT_TRUE(english_q.allow);
  MTK_EXPECT_EQ_STR(english_q.url,
                    "https://www.google.com/search?safe=active&udm=50&q=what+is+quantum+computing");

  const auto question_mark = ResolveInput("is today a holiday?");
  MTK_EXPECT_TRUE(question_mark.allow);
  MTK_EXPECT_EQ_STR(question_mark.url,
                    "https://www.google.com/search?safe=active&udm=50&q=is+today+a+holiday%3F");

  // Non-questions should not have udm=50
  const auto normal_search = ResolveInput("latest tech gadgets 2026");
  MTK_EXPECT_TRUE(normal_search.allow);
  MTK_EXPECT_EQ_STR(normal_search.url,
                    "https://www.google.com/search?safe=active&q=latest+tech+gadgets+2026");
}


MTK_TEST(TwitterSocialEmbedResourcesAreAllowed) {
  const std::string initiator = "https://indianexpress.com/article/world/news/";

  // Widget script
  const auto script_dec = EvaluateRequest(
      "https://platform.twitter.com/widgets.js", ResourceType::kScript, initiator);
  MTK_EXPECT_TRUE(script_dec.allow);

  // Embedded tweet frame
  const auto frame_dec = EvaluateRequest(
      "https://platform.twitter.com/embed/Tweet.html?id=2102409156112142701",
      ResourceType::kSubFrame, initiator);
  MTK_EXPECT_TRUE(frame_dec.allow);

  // Tweet result syndication API
  const auto api_dec = EvaluateRequest(
      "https://cdn.syndication.twimg.com/tweet-result?id=2102409156112142701&lang=en",
      ResourceType::kXhr, initiator);
  MTK_EXPECT_TRUE(api_dec.allow);

  // Profile and media images
  const auto img_dec = EvaluateRequest(
      "https://pbs.twimg.com/profile_images/123/avatar.jpg",
      ResourceType::kImage, initiator);
  MTK_EXPECT_TRUE(img_dec.allow);

  // Video stream
  const auto media_dec = EvaluateRequest(
      "https://video.twimg.com/ext_tw_video/123/pu/vid/720x1280/video.mp4",
      ResourceType::kMedia, initiator);
  MTK_EXPECT_TRUE(media_dec.allow);

  // Actual Twitter ads domain must still be blocked
  const auto ad_dec = EvaluateRequest(
      "https://ads-twitter.com/pixel", ResourceType::kScript, initiator);
  MTK_EXPECT_FALSE(ad_dec.allow);
}

MTK_TEST(SearchTermBlockingCatchesSearchEngineXhrAndSuggestions) {
  // Google autocomplete XHR with blocked adult term
  const auto google_xhr = EvaluateRequest(
      "https://www.google.com/complete/search?client=chrome&q=porn+video",
      ResourceType::kXhr, "https://www.google.com/");
  MTK_EXPECT_FALSE(google_xhr.allow);

  // Bing suggestions XHR with blocked illegal term
  const auto bing_xhr = EvaluateRequest(
      "https://www.bing.com/AS/Suggestions?qry=free+crack+download",
      ResourceType::kXhr, "https://www.bing.com/");
  MTK_EXPECT_FALSE(bing_xhr.allow);

  // DuckDuckGo autocomplete XHR with blocked term
  const auto ddg_xhr = EvaluateRequest(
      "https://duckduckgo.com/ac/?q=nude+pictures",
      ResourceType::kXhr, "https://duckduckgo.com/");
  MTK_EXPECT_FALSE(ddg_xhr.allow);

  // Clean search suggestion XHR remains allowed
  const auto clean_xhr = EvaluateRequest(
      "https://www.google.com/complete/search?client=chrome&q=weather+today",
      ResourceType::kXhr, "https://www.google.com/");
  MTK_EXPECT_TRUE(clean_xhr.allow);
}
