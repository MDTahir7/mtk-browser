#include "test_support.h"

#include "translation.h"

MTK_TEST(TranslationUrlsAllowOnlySupportedTargetsAndSafelyEncodeSource) {
  using mtk::translation::GoogleTranslateUrl;
  MTK_EXPECT_TRUE(mtk::translation::IsSupportedTarget("hi"));
  MTK_EXPECT_TRUE(mtk::translation::IsSupportedTarget("en"));
  MTK_EXPECT_FALSE(mtk::translation::IsSupportedTarget("fr"));
  MTK_EXPECT_EQ_STR(
      GoogleTranslateUrl("https://example.com/a?x=1&note=hello world#part", "hi"),
      "https://translate.google.com/translate?sl=auto&tl=hi&u="
      "https%3A%2F%2Fexample.com%2Fa%3Fx%3D1%26note%3Dhello%20world%23part");
  MTK_EXPECT_EQ_STR(GoogleTranslateUrl("http://example.com/\xC3\xA9", "en"),
                    "https://translate.google.com/translate?sl=auto&tl=en&u="
                    "http%3A%2F%2Fexample.com%2F%C3%A9");
  MTK_EXPECT_TRUE(GoogleTranslateUrl("mtk://home/", "hi").empty());
  MTK_EXPECT_TRUE(GoogleTranslateUrl("file:///private.txt", "hi").empty());
  MTK_EXPECT_TRUE(GoogleTranslateUrl("https://example.com/", "fr").empty());
}
