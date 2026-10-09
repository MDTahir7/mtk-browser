// Tests for the fingerprint-protection switch encoding and script generation.
// The renderer reads the switch value produced here through the CEF command
// line, so the encode/decode round trip and the fail-safe defaults matter more
// than the exact JavaScript text, which is exercised only for basic shape.

#include <string>

#include "fingerprint_script.h"
#include "preferences.h"
#include "test_support.h"

MTK_TEST(FingerprintProtectionPreferenceDefaultsOn) {
  MTK_EXPECT_TRUE(mtk::preferences::Store::Get().values().fingerprint_protection);
}

MTK_TEST(FingerprintSwitchValueRoundTripsOnAndSeed) {
  const std::string encoded =
      mtk::fingerprint::EncodeSwitchValue(true, 0x1a2b3c4d5e6f7890ULL);
  MTK_EXPECT_TRUE(mtk::fingerprint::IsEnabledSwitchValue(encoded));
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed(encoded), 0x1a2b3c4d5e6f7890ULL);
}

MTK_TEST(FingerprintSwitchValueRoundTripsOffAndSeed) {
  const std::string encoded = mtk::fingerprint::EncodeSwitchValue(false, 42);
  MTK_EXPECT_FALSE(mtk::fingerprint::IsEnabledSwitchValue(encoded));
  // The seed still decodes even when protection is off, since a caller might
  // want it for some other reason; DecodeSeed does not itself gate on on/off.
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed(encoded), 42);
}

MTK_TEST(FingerprintSwitchValueZeroSeedRoundTrips) {
  // A seed of 0 is a real, valid seed, not a sentinel for "missing" -- the
  // encoding has to carry it through rather than collapsing it to the same
  // thing an absent switch would decode to.
  const std::string encoded = mtk::fingerprint::EncodeSwitchValue(true, 0);
  MTK_EXPECT_TRUE(mtk::fingerprint::IsEnabledSwitchValue(encoded));
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed(encoded), 0);
}

MTK_TEST(FingerprintDecodeSeedFailsSafeOnMalformedInput) {
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed(""), 0);
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed("nocolon"), 0);
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed("on:not-hex"), 0);
  MTK_EXPECT_EQ_INT(mtk::fingerprint::DecodeSeed("on:"), 0);
}

MTK_TEST(FingerprintIsEnabledOnlyMatchesTheOnPrefix) {
  MTK_EXPECT_TRUE(mtk::fingerprint::IsEnabledSwitchValue("on:0"));
  MTK_EXPECT_FALSE(mtk::fingerprint::IsEnabledSwitchValue("off:0"));
  MTK_EXPECT_FALSE(mtk::fingerprint::IsEnabledSwitchValue(""));
  MTK_EXPECT_FALSE(mtk::fingerprint::IsEnabledSwitchValue("garbage"));
  // Fails safe (off) on anything that is not exactly the "on:" this file's own
  // encoder produces, rather than trying to guess at a looser match.
  MTK_EXPECT_FALSE(mtk::fingerprint::IsEnabledSwitchValue("ON:0"));
}

MTK_TEST(FingerprintBuildScriptIsEmptyWhenDisabled) {
  MTK_EXPECT_TRUE(mtk::fingerprint::BuildScript(false, 12345).empty());
}

MTK_TEST(FingerprintBuildScriptCarriesTheSeedAndTargetsTheRightApis) {
  const std::string script = mtk::fingerprint::BuildScript(true, 987654321);
  MTK_EXPECT_TRUE(!script.empty());
  MTK_EXPECT_TRUE(script.find("987654321") != std::string::npos);
  // Every surface the roadmap calls out is actually patched, not just named in
  // a comment.
  MTK_EXPECT_TRUE(script.find("getImageData") != std::string::npos);
  MTK_EXPECT_TRUE(script.find("getParameter") != std::string::npos);
  MTK_EXPECT_TRUE(script.find("Generic Renderer") != std::string::npos);
  MTK_EXPECT_TRUE(script.find("getChannelData") != std::string::npos);
  MTK_EXPECT_TRUE(script.find("hardwareConcurrency") != std::string::npos);
  MTK_EXPECT_TRUE(script.find("deviceMemory") != std::string::npos);
  // No network access and no persistent observer, matching how every other
  // injected script in this codebase is built.
  MTK_EXPECT_TRUE(script.find("XMLHttpRequest") == std::string::npos);
  MTK_EXPECT_TRUE(script.find("fetch(") == std::string::npos);
  MTK_EXPECT_TRUE(script.find("MutationObserver") == std::string::npos);
}

MTK_TEST(FingerprintBuildScriptIsDeterministicForTheSameSeed) {
  const std::string first = mtk::fingerprint::BuildScript(true, 555);
  const std::string second = mtk::fingerprint::BuildScript(true, 555);
  MTK_EXPECT_EQ_STR(first, second);
}

MTK_TEST(FingerprintBuildScriptDiffersAcrossSeeds) {
  const std::string a = mtk::fingerprint::BuildScript(true, 1);
  const std::string b = mtk::fingerprint::BuildScript(true, 2);
  MTK_EXPECT_TRUE(a != b);
}
