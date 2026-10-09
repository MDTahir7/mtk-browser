#include "passkey_page.h"

#include <string>
#include <string_view>

#include "test_support.h"

namespace {

MTK_TEST(PasskeyUrlValidation) {
  // Accepted diagnostic URLs
  MTK_EXPECT_TRUE(mtk::passkeys::IsPasskeyUrl("mtk://passkeys/"));
  MTK_EXPECT_TRUE(mtk::passkeys::IsPasskeyUrl("mtk://passkeys"));
  MTK_EXPECT_TRUE(mtk::passkeys::IsPasskeyUrl("mtk://passkeys/?lang=hi"));
  MTK_EXPECT_TRUE(mtk::passkeys::IsPasskeyUrl("mtk://passkeys?theme=light"));

  // Rejected URLs
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl(""));
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl("mtk://home/"));
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl("mtk://history/"));
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl("https://passkeys/"));
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl("http://passkeys"));
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl("data:text/html,passkeys"));
  MTK_EXPECT_FALSE(mtk::passkeys::IsPasskeyUrl("mtk://passkeys-fake/"));
}

MTK_TEST(PasskeyActionUrlParsing) {
  // Allowed action URLs
  MTK_EXPECT_TRUE(mtk::passkeys::ParseActionUrl("mtk://passkeys/action/test-hello") ==
                  mtk::passkeys::Action::kTestHello);

  // Rejected / unknown actions
  MTK_EXPECT_TRUE(mtk::passkeys::ParseActionUrl("mtk://passkeys/action/unknown") ==
                  mtk::passkeys::Action::kNone);
  MTK_EXPECT_TRUE(mtk::passkeys::ParseActionUrl("mtk://passkeys/action/") ==
                  mtk::passkeys::Action::kNone);
  MTK_EXPECT_TRUE(mtk::passkeys::ParseActionUrl("https://passkeys/action/test-hello") ==
                  mtk::passkeys::Action::kNone);
  MTK_EXPECT_TRUE(mtk::passkeys::ParseActionUrl("mtk://home/action/test-hello") ==
                  mtk::passkeys::Action::kNone);
}

MTK_TEST(PasskeyHtmlGenerationContainsSecurityAndDiagnostics) {
  mtk::hello::Result status;
  status.available = true;
  status.verified = false;
  status.detail = L"Windows Hello face recognition active";

  const std::string html = mtk::passkeys::BuildHtml(status, /*light_appearance=*/false, /*hindi=*/false);

  // Checks document structure and CSP
  MTK_EXPECT_TRUE(html.find("<!doctype html>") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("Content-Security-Policy") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("default-src 'none'") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("connect-src 'none'") != std::string::npos);

  // Checks diagnostics components
  MTK_EXPECT_TRUE(html.find("Windows Hello") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("Windows Hello face recognition active") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("btn-test-hello") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("mtk://passkeys/action/test-hello") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("PublicKeyCredential") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("isUserVerifyingPlatformAuthenticatorAvailable") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("isConditionalMediationAvailable") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("getClientCapabilities") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("Zero-Knowledge") != std::string::npos);
}

MTK_TEST(PasskeyHtmlGenerationHindiSupport) {
  mtk::hello::Result status;
  status.available = false;
  status.verified = false;
  status.detail = L"बायोमेट्रिक हार्डवेयर नहीं मिला";

  const std::string html = mtk::passkeys::BuildHtml(status, /*light_appearance=*/true, /*hindi=*/true);

  // Checks Hindi strings and data-theme
  MTK_EXPECT_TRUE(html.find("lang=\"hi\"") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("data-theme=\"light\"") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("पासकी एवं विंडोज हैलो जांच") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("बायोमेट्रिक हार्डवेयर नहीं मिला") != std::string::npos);
  MTK_EXPECT_TRUE(html.find("शून्य-ज्ञान") != std::string::npos);
}

}  // namespace
