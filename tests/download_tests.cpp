#include "test_support.h"

#include "download_manager.h"

using mtk::downloads::Evaluate;
using mtk::downloads::IsRiskyFileName;
using mtk::downloads::Mode;

namespace {

mtk::downloads::Decision Safe(const std::wstring& name,
                              const std::string& mime = "application/octet-stream",
                              const std::string& url = "https://example.com/f") {
  return Evaluate(url, name, mime, Mode::kSafe);
}

mtk::downloads::Decision Standard(
    const std::wstring& name,
    const std::string& mime = "application/octet-stream",
    const std::string& url = "https://example.com/f") {
  return Evaluate(url, name, mime, Mode::kStandard);
}

}  // namespace

MTK_TEST(SafeModeAllowsKnownDocumentTypes) {
  const auto decision = Safe(L"report.pdf", "application/pdf");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_WSTR(decision.file_name, L"report.pdf");
  MTK_EXPECT_FALSE(decision.risky);
}

MTK_TEST(SafeModeNormalisesExtensionCase) {
  MTK_EXPECT_EQ_WSTR(Safe(L"photo.JPG", "image/jpeg").file_name, L"photo.jpg");
}

MTK_TEST(SafeModeBlocksExecutablesAndScripts) {
  MTK_EXPECT_FALSE(Safe(L"setup.exe").allow);
  MTK_EXPECT_FALSE(Safe(L"tool.msi").allow);
  MTK_EXPECT_FALSE(Safe(L"run.ps1", "text/plain").allow);
  MTK_EXPECT_FALSE(Safe(L"payload.js", "text/javascript").allow);
  MTK_EXPECT_FALSE(Safe(L"app.apk").allow);
  MTK_EXPECT_FALSE(Safe(L"disk.iso").allow);
}

MTK_TEST(SafeModeBlocksDoubleExtensionDisguises) {
  MTK_EXPECT_FALSE(Safe(L"invoice.pdf.exe", "application/pdf").allow);
  MTK_EXPECT_FALSE(Safe(L"invoice.exe.pdf", "application/pdf").allow);
}

MTK_TEST(SafeModeBlocksMacroEnabledOfficeFiles) {
  MTK_EXPECT_FALSE(Safe(L"book.xlsm", "application/vnd.ms-excel").allow);
  MTK_EXPECT_FALSE(Safe(L"notes.docm").allow);
}

MTK_TEST(SafeModeBlocksUnknownAndMissingExtensions) {
  MTK_EXPECT_FALSE(Safe(L"payload.qqq").allow);
  MTK_EXPECT_FALSE(Safe(L"payload").allow);
  MTK_EXPECT_FALSE(Safe(L"   ...   ").allow);
}

MTK_TEST(SafeModeBlocksExecutableMimeEvenWithSafeName) {
  MTK_EXPECT_FALSE(Safe(L"tool.zip", "application/x-msdownload").allow);
}

MTK_TEST(StandardModeAllowsAnyTypeButFlagsRisky) {
  const auto executable = Standard(L"setup.exe");
  MTK_EXPECT_TRUE(executable.allow);
  MTK_EXPECT_TRUE(executable.risky);
  MTK_EXPECT_EQ_WSTR(executable.file_name, L"setup.exe");

  const auto document = Standard(L"report.pdf", "application/pdf");
  MTK_EXPECT_TRUE(document.allow);
  MTK_EXPECT_FALSE(document.risky);

  const auto unknown = Standard(L"payload.qqq");
  MTK_EXPECT_TRUE(unknown.allow);
  MTK_EXPECT_FALSE(unknown.risky);
}

MTK_TEST(StandardModeFallsBackToGenericName) {
  MTK_EXPECT_EQ_WSTR(Standard(L"   ...   ").file_name, L"download");
}

MTK_TEST(StandardModeFlagsExecutableMime) {
  MTK_EXPECT_TRUE(Standard(L"tool.bin", "application/x-msdownload").risky);
}

MTK_TEST(FileNamesCannotEscapeTheDownloadFolder) {
  const auto decision = Standard(L"..\\..\\Windows\\System32\\evil.exe");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_WSTR(decision.file_name, L"_.._Windows_System32_evil.exe");
  MTK_EXPECT_TRUE(decision.file_name.find(L'\\') == std::wstring::npos);
  MTK_EXPECT_TRUE(decision.file_name.find(L'/') == std::wstring::npos);
}

MTK_TEST(ReservedDeviceNamesArePrefixed) {
  MTK_EXPECT_EQ_WSTR(Safe(L"con.txt", "text/plain").file_name, L"_con.txt");
  MTK_EXPECT_EQ_WSTR(Safe(L"COM1.txt", "text/plain").file_name, L"_COM1.txt");
}

MTK_TEST(InvalidCharactersAreReplaced) {
  MTK_EXPECT_EQ_WSTR(Safe(L"in|valid:name?.txt", "text/plain").file_name,
                     L"in_valid_name_.txt");
}

MTK_TEST(BlockedSiteCannotDownloadInEitherMode) {
  const std::string blocked = "https://www.pornhub.com/file.pdf";
  MTK_EXPECT_FALSE(Evaluate(blocked, L"file.pdf", "application/pdf", Mode::kSafe).allow);
  MTK_EXPECT_FALSE(
      Evaluate(blocked, L"file.pdf", "application/pdf", Mode::kStandard).allow);
}

MTK_TEST(NonWebSchemesCannotDownloadInEitherMode) {
  MTK_EXPECT_FALSE(
      Evaluate("ftp://example.com/f.pdf", L"f.pdf", "application/pdf", Mode::kSafe)
          .allow);
  MTK_EXPECT_FALSE(Evaluate("data:text/plain,hello", L"f.txt", "text/plain",
                            Mode::kStandard)
                       .allow);
}

MTK_TEST(RiskyFileNameHelperMatchesExecutableTypes) {
  MTK_EXPECT_TRUE(IsRiskyFileName(L"a.exe"));
  MTK_EXPECT_TRUE(IsRiskyFileName(L"a.bat"));
  MTK_EXPECT_FALSE(IsRiskyFileName(L"a.pdf"));
  MTK_EXPECT_FALSE(IsRiskyFileName(L"a.png"));
}

MTK_TEST(LargeDownloadThresholdIsTwoGigabytes) {
  MTK_EXPECT_EQ_INT(mtk::downloads::kLargeDownloadBytes,
                    2LL * 1024 * 1024 * 1024);
}
