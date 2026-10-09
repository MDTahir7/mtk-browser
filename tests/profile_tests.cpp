// Tests for profile selection. A profile id arrives on the command line and is
// used to build a directory path, so the cases below concentrate on rejecting
// anything that could escape the profiles directory or name a device.

#include <string>

#include "profile.h"
#include "test_support.h"

MTK_TEST(ProfileAcceptsPlainNames) {
  MTK_EXPECT_TRUE(mtk::profile::IsValidId(L"work"));
  MTK_EXPECT_TRUE(mtk::profile::IsValidId(L"Kids_2"));
  MTK_EXPECT_TRUE(mtk::profile::IsValidId(L"a"));
  MTK_EXPECT_TRUE(mtk::profile::IsValidId(L"school-laptop"));
  MTK_EXPECT_TRUE(mtk::profile::IsValidId(std::wstring(32, L'x')));
}

MTK_TEST(ProfileRejectsPathEscapes) {
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L".."));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"../other"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"..\\other"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"sub\\dir"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"sub/dir"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"C:"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"\\\\server\\share"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"name."));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"a b"));
}

MTK_TEST(ProfileRejectsReservedAndOversizedNames) {
  // Reserved device names contain no '.' or separator, so they are only ruled
  // out because a name may not contain them either way; these confirm the
  // characters that would form them are refused.
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"COM1:"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"NUL."));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L""));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(std::wstring(33, L'x')));
  // A leading dash would be read as another switch on a command line.
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"-kids"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"--profile=x"));
}

MTK_TEST(ProfileRejectsNonAsciiAndControlCharacters) {
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"\u0915\u093e\u092e"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"na\tme"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"na\"me"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidId(L"na*me"));
}

MTK_TEST(ProfileReadsSwitchFromCommandLine) {
  MTK_EXPECT_EQ_WSTR(
      mtk::profile::IdFromCommandLine(L"\"C:\\app\\MTKBrowser.exe\" --profile=work"),
      L"work");
  MTK_EXPECT_EQ_WSTR(
      mtk::profile::IdFromCommandLine(L"MTKBrowser.exe --profile=work --more"),
      L"work");
  MTK_EXPECT_EQ_WSTR(
      mtk::profile::IdFromCommandLine(L"MTKBrowser.exe --profile=\"kids\""),
      L"kids");
}

MTK_TEST(ProfileIgnoresLookalikeSwitches) {
  // The switch has to start a token, so a longer switch that merely ends with
  // the same text must not match.
  MTK_EXPECT_EQ_WSTR(
      mtk::profile::IdFromCommandLine(L"MTKBrowser.exe --not-a-profile=work"),
      L"");
  MTK_EXPECT_EQ_WSTR(mtk::profile::IdFromCommandLine(L"MTKBrowser.exe"), L"");
  MTK_EXPECT_EQ_WSTR(mtk::profile::IdFromCommandLine(L"--profile="), L"");
}

MTK_TEST(ProfileTruncatedSwitchValueIsRefused) {
  // An over-long value is reported as absent rather than silently cut down to a
  // different profile name.
  const std::wstring line =
      L"MTKBrowser.exe --profile=" + std::wstring(64, L'x');
  MTK_EXPECT_EQ_WSTR(mtk::profile::IdFromCommandLine(line), L"");
}

MTK_TEST(ProfileDefaultsBeforeInitialization) {
  // Nothing in this test binary selects a profile, so the default one is active
  // and the data directory is the original one, which is what keeps an existing
  // installation working without migration.
  MTK_EXPECT_TRUE(mtk::profile::ActiveIsDefault());
  MTK_EXPECT_EQ_WSTR(mtk::profile::ActiveId(), L"Default");
  const std::wstring directory = mtk::profile::ActiveDirectory();
  MTK_EXPECT_TRUE(directory.find(L"\\MTK Browser") != std::wstring::npos);
  MTK_EXPECT_TRUE(directory.find(L"\\Profiles") == std::wstring::npos);
}

MTK_TEST(ProfileDisplayNamesAllowReadableUnicodeButRejectUnsafeText) {
  MTK_EXPECT_TRUE(mtk::profile::IsValidDisplayName(L"Work laptop"));
  MTK_EXPECT_TRUE(mtk::profile::IsValidDisplayName(L"काम"));
  MTK_EXPECT_TRUE(mtk::profile::IsValidDisplayName(std::wstring(64, L'x')));
  MTK_EXPECT_FALSE(mtk::profile::IsValidDisplayName(L""));
  MTK_EXPECT_FALSE(mtk::profile::IsValidDisplayName(std::wstring(65, L'x')));
  MTK_EXPECT_FALSE(mtk::profile::IsValidDisplayName(L"Work\nLaptop"));
  MTK_EXPECT_FALSE(mtk::profile::IsValidDisplayName(L"Work\tLaptop"));
}

MTK_TEST(ProfileDisplayNamePersistsSeparatelyFromItsSafeId) {
  if (!mtk::test::WritingToSuiteProfile()) { MTK_EXPECT_TRUE(false); return; }
  MTK_EXPECT_TRUE(mtk::profile::Create(L"display_test"));
  MTK_EXPECT_EQ_WSTR(mtk::profile::DisplayName(L"display_test"), L"display_test");
  MTK_EXPECT_TRUE(mtk::profile::SetDisplayName(L"display_test", L"काम Laptop"));
  MTK_EXPECT_EQ_WSTR(mtk::profile::DisplayName(L"display_test"), L"काम Laptop");
  MTK_EXPECT_FALSE(mtk::profile::SetDisplayName(L"display_test", L"bad\nname"));
  MTK_EXPECT_FALSE(mtk::profile::SetDisplayName(L"../escape", L"Name"));
}

MTK_TEST(ProfileDeletionRefusesDefaultAndActiveProfile) {
  MTK_EXPECT_FALSE(mtk::profile::Delete(mtk::profile::kDefaultId));
  MTK_EXPECT_FALSE(mtk::profile::Delete(mtk::profile::ActiveId()));
  MTK_EXPECT_FALSE(mtk::profile::Delete(L"../escape"));
}
