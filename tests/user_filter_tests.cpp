// Tests for the user's own filter rules. Two properties matter most: an allow
// rule must win over a block rule, and a custom rule must never be able to open
// something the safety policy refuses. Nothing here writes to disk.

#include <string>
#include <vector>

#include "policy.h"
#include "user_filters.h"

#include "test_support.h"

namespace {

void Apply(std::vector<std::wstring> lines) {
  mtk::user_filters::ApplyWithoutSaving(lines);
}

bool Blocks(const char* url) {
  return mtk::user_filters::Evaluate(url) == mtk::user_filters::Decision::kBlock;
}

bool Allows(const char* url) {
  return mtk::user_filters::Evaluate(url) == mtk::user_filters::Decision::kAllow;
}

}  // namespace

MTK_TEST(UserFiltersBlockOnSubstring) {
  Apply({L"ads.example.com", L"/banner-"});
  MTK_EXPECT_TRUE(Blocks("https://ads.example.com/unit.js"));
  MTK_EXPECT_TRUE(Blocks("https://cdn.test/banner-300x250.png"));
  MTK_EXPECT_FALSE(Blocks("https://example.com/news"));
  Apply({});
}

MTK_TEST(UserFiltersAllowRuleWinsOverBlock) {
  Apply({L"example.com", L"@@example.com/needed.js"});
  // The block rule matches both, so the allow rule is what makes the difference.
  MTK_EXPECT_TRUE(Blocks("https://example.com/tracker.js"));
  MTK_EXPECT_TRUE(Allows("https://example.com/needed.js"));
  Apply({});
}

MTK_TEST(UserFiltersIgnoreBlankLinesAndComments) {
  Apply({L"", L"   ", L"# ads.example.com is a note, not a rule", L"\t"});
  MTK_EXPECT_EQ_INT(mtk::user_filters::ActiveRuleCount(), 0);
  MTK_EXPECT_FALSE(Blocks("https://ads.example.com/unit.js"));
  Apply({});
}

MTK_TEST(UserFiltersAreCaseInsensitive) {
  Apply({L"ADS.Example.COM"});
  MTK_EXPECT_TRUE(Blocks("https://ads.example.com/unit.js"));
  Apply({});
}

MTK_TEST(UserFiltersSkipRulesThatCouldNeverMatch) {
  // A non-ASCII rule cannot match an address that reaches the request path, so
  // it is not compiled: keeping it would look like it worked.
  Apply({L"\u0935\u093f\u091c\u094d\u091e\u093e\u092a\u0928", L"ads.test"});
  MTK_EXPECT_EQ_INT(mtk::user_filters::ActiveRuleCount(), 1);
  MTK_EXPECT_TRUE(Blocks("https://ads.test/x"));
  Apply({});
}

MTK_TEST(UserFiltersKeepAuthoredLinesForTheEditor) {
  const std::vector<std::wstring> authored{L"# my notes", L"ads.test", L"",
                                           L"@@ads.test/ok.js"};
  Apply(authored);
  const auto lines = mtk::user_filters::Lines();
  MTK_EXPECT_EQ_INT(lines.size(), authored.size());
  if (lines.size() == authored.size()) {
    for (size_t index = 0; index < lines.size(); ++index) {
      MTK_EXPECT_EQ_WSTR(lines[index], authored[index]);
    }
  }
  // Blanks and comments are kept as written but are not rules.
  MTK_EXPECT_EQ_INT(mtk::user_filters::ActiveRuleCount(), 2);
  Apply({});
}

MTK_TEST(UserFiltersCannotOpenWhatTheSafetyPolicyRefuses) {
  // An allow rule for an adult host is compiled, because the syntax permits it,
  // but the request policy is consulted first and independently, so the address
  // is still refused. This is the property that keeps custom rules safe.
  Apply({L"@@pornhub.com", L"@@192.168.1.1"});
  MTK_EXPECT_TRUE(Allows("https://www.pornhub.com/"));
  MTK_EXPECT_FALSE(
      mtk::policy::EvaluateRequest("https://www.pornhub.com/",
                                   mtk::policy::ResourceType::kMainFrame)
          .allow);
  MTK_EXPECT_FALSE(
      mtk::policy::EvaluateRequest("http://192.168.1.1/",
                                   mtk::policy::ResourceType::kMainFrame)
          .allow);
  Apply({});
}

MTK_TEST(UserFiltersWithNoRulesDecideNothing) {
  Apply({});
  MTK_EXPECT_TRUE(mtk::user_filters::Evaluate("https://example.com/") ==
                  mtk::user_filters::Decision::kNone);
  MTK_EXPECT_TRUE(mtk::user_filters::Evaluate("") ==
                  mtk::user_filters::Decision::kNone);
}
