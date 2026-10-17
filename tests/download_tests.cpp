#include "ole_drag_drop.h"
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

MTK_TEST(PartialDownloadRangeHeaderFormatting) {
  MTK_EXPECT_EQ_STR(mtk::downloads::BuildRangeHeader(1024), "bytes=1024-");
  MTK_EXPECT_EQ_STR(mtk::downloads::BuildRangeHeader(50000000), "bytes=50000000-");
  MTK_EXPECT_EQ_STR(mtk::downloads::BuildRangeHeader(0), "");
  MTK_EXPECT_EQ_STR(mtk::downloads::BuildRangeHeader(-1), "");
}

MTK_TEST(IfRangeHeaderPrefersEtagOverLastModified) {
  MTK_EXPECT_EQ_STR(
      mtk::downloads::BuildIfRangeHeader("\"abc123etag\"", "Wed, 21 Oct 2015 07:28:00 GMT"),
      "\"abc123etag\"");
  MTK_EXPECT_EQ_STR(
      mtk::downloads::BuildIfRangeHeader("", "Wed, 21 Oct 2015 07:28:00 GMT"),
      "Wed, 21 Oct 2015 07:28:00 GMT");
  MTK_EXPECT_EQ_STR(mtk::downloads::BuildIfRangeHeader("", ""), "");
}

MTK_TEST(CanResumeRequiresPausedOrFailedWithReceivedBytesAndValidUrl) {
  mtk::downloads::Item item;
  item.url = "https://example.com/archive.zip";
  item.received_bytes = 1048576;
  item.state = mtk::downloads::State::kPaused;
  MTK_EXPECT_TRUE(mtk::downloads::CanResume(item));

  item.state = mtk::downloads::State::kFailed;
  MTK_EXPECT_TRUE(mtk::downloads::CanResume(item));

  item.state = mtk::downloads::State::kComplete;
  MTK_EXPECT_FALSE(mtk::downloads::CanResume(item));

  item.state = mtk::downloads::State::kInProgress;
  MTK_EXPECT_FALSE(mtk::downloads::CanResume(item));

  item.state = mtk::downloads::State::kPaused;
  item.received_bytes = 0;
  MTK_EXPECT_FALSE(mtk::downloads::CanResume(item));

  item.received_bytes = 1048576;
  item.url = "ftp://example.com/archive.zip";
  MTK_EXPECT_FALSE(mtk::downloads::CanResume(item));
}

MTK_TEST(PartialFilePathAppendsMtkPart) {
  MTK_EXPECT_EQ_WSTR(
      mtk::downloads::PartialFilePath(L"C:\\Downloads\\archive.zip"),
      L"C:\\Downloads\\archive.zip.mtkpart");
  MTK_EXPECT_EQ_WSTR(mtk::downloads::PartialFilePath(L""), L"");
}

MTK_TEST(SuggestPdfFileNameProducesWindowsSafeNames) {
  MTK_EXPECT_EQ_WSTR(
      mtk::downloads::SuggestPdfFileName(L"Quantum Computing - Wikipedia"),
      L"Quantum Computing - Wikipedia.pdf");
  MTK_EXPECT_EQ_WSTR(
      mtk::downloads::SuggestPdfFileName(L"Research_Article.html"),
      L"Research_Article.pdf");
  MTK_EXPECT_EQ_WSTR(
      mtk::downloads::SuggestPdfFileName(L"Invoice_2026.pdf"),
      L"Invoice_2026.pdf");
  MTK_EXPECT_EQ_WSTR(
      mtk::downloads::SuggestPdfFileName(L""),
      L"Saved page.pdf");
  MTK_EXPECT_EQ_WSTR(
      mtk::downloads::SuggestPdfFileName(L"   ...   "),
      L"Saved page.pdf");
  const std::wstring complex_title =
      mtk::downloads::SuggestPdfFileName(L"Report: 2026/Q1 *Final* <Draft>? | Test");
  MTK_EXPECT_TRUE(complex_title.find(L':') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.find(L'/') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.find(L'*') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.find(L'<') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.find(L'>') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.find(L'?') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.find(L'|') == std::wstring::npos);
  MTK_EXPECT_TRUE(complex_title.ends_with(L".pdf"));
}



MTK_TEST(EvaluateStripsCdnAndObjectTypePrefixes) {
  const auto decision = Safe(L"objecttype-raw-_photo.jpg", "image/jpeg");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_WSTR(decision.file_name, L"photo.jpg");
}

MTK_TEST(EvaluateInfersExtensionFromMimeType) {
  const auto decision = Safe(L"photo_without_ext", "image/jpeg");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_WSTR(decision.file_name, L"photo_without_ext.jpg");
}

MTK_TEST(EvaluateInfersExtensionFromMimeWithParameters) {
  const auto decision = Safe(L"document_without_ext", "application/pdf; charset=binary");
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_WSTR(decision.file_name, L"document_without_ext.pdf");
}

MTK_TEST(EvaluateInfersExtensionFromUrlPathFallback) {
  const auto decision = Evaluate("https://cdn.example.com/assets/graphic.png?v=2",
                                 L"graphic", "application/octet-stream", Mode::kSafe);
  MTK_EXPECT_TRUE(decision.allow);
  MTK_EXPECT_EQ_WSTR(decision.file_name, L"graphic.png");
}

MTK_TEST(SafeModeBlocksActiveSvgButAllowsAvifImages) {
  const auto svg_dec = Safe(L"icon.svg", "image/svg+xml");
  MTK_EXPECT_FALSE(svg_dec.allow);
  MTK_EXPECT_FALSE(svg_dec.risky);

  const auto avif_dec = Safe(L"photo.avif", "image/avif");
  MTK_EXPECT_TRUE(avif_dec.allow);
  MTK_EXPECT_FALSE(avif_dec.risky);
}

MTK_TEST(SuggestScreenshotFileNameGeneratesCleanTimestampedPng) {
  const std::wstring empty_title = mtk::downloads::SuggestScreenshotFileName(L"");
  MTK_EXPECT_TRUE(empty_title.starts_with(L"Screenshot_Webpage_"));
  MTK_EXPECT_TRUE(empty_title.ends_with(L".png"));

  const std::wstring html_title = mtk::downloads::SuggestScreenshotFileName(L"Google Search.html");
  MTK_EXPECT_TRUE(html_title.starts_with(L"Screenshot_Google_Search_"));
  MTK_EXPECT_TRUE(html_title.ends_with(L".png"));

  const std::wstring dirty_title = mtk::downloads::SuggestScreenshotFileName(L"My Page: What / When * Now? <Demo> | Test");
  MTK_EXPECT_TRUE(dirty_title.find(L':') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.find(L'/') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.find(L'*') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.find(L'<') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.find(L'>') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.find(L'?') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.find(L'|') == std::wstring::npos);
  MTK_EXPECT_TRUE(dirty_title.ends_with(L".png"));
}

MTK_TEST(SaveBitmapToPngEncodesValidPngFile) {
  HDC screen_dc = GetDC(nullptr);
  MTK_EXPECT_TRUE(screen_dc != nullptr);
  HDC mem_dc = CreateCompatibleDC(screen_dc);
  MTK_EXPECT_TRUE(mem_dc != nullptr);
  HBITMAP hbm = CreateCompatibleBitmap(screen_dc, 32, 32);
  MTK_EXPECT_TRUE(hbm != nullptr);

  HGDIOBJ old_bm = SelectObject(mem_dc, hbm);
  RECT rc = {0, 0, 32, 32};
  FillRect(mem_dc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  SelectObject(mem_dc, old_bm);
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);

  wchar_t temp_path[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp_path);
  std::wstring out_file = std::wstring(temp_path) + L"mtk_test_screenshot.png";
  DeleteFileW(out_file.c_str());

  const bool saved = mtk::downloads::SaveBitmapToPng(hbm, out_file);
  DeleteObject(hbm);
  MTK_EXPECT_TRUE(saved);

  HANDLE hfile = CreateFileW(out_file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  MTK_EXPECT_TRUE(hfile != INVALID_HANDLE_VALUE);
  if (hfile != INVALID_HANDLE_VALUE) {
    DWORD bytes_read = 0;
    unsigned char header[8] = {0};
    ReadFile(hfile, header, 8, &bytes_read, nullptr);
    CloseHandle(hfile);
    DeleteFileW(out_file.c_str());

    MTK_EXPECT_EQ_INT(bytes_read, 8);
    // Standard PNG magic bytes: 0x89 'P' 'N' 'G' 0x0D 0x0A 0x1A 0x0A
    MTK_EXPECT_EQ_INT(header[0], 0x89);
    MTK_EXPECT_EQ_INT(header[1], 'P');
    MTK_EXPECT_EQ_INT(header[2], 'N');
    MTK_EXPECT_EQ_INT(header[3], 'G');
    MTK_EXPECT_EQ_INT(header[4], 0x0D);
    MTK_EXPECT_EQ_INT(header[5], 0x0A);
    MTK_EXPECT_EQ_INT(header[6], 0x1A);
    MTK_EXPECT_EQ_INT(header[7], 0x0A);
  }
}


MTK_TEST(InterruptedDownloadPersistsEtagAndResumptionAcrossRestart) {
  auto& mgr = mtk::downloads::Manager::Get();
  mgr.ClearFinished();

  wchar_t temp_dir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp_dir);
  const std::wstring target =
      std::wstring(temp_dir) + L"mtk_resume_test_archive.zip";
  const std::wstring part = mtk::downloads::PartialFilePath(target);

  // Write a 1024-byte partial file on disk.
  HANDLE hpart = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  MTK_EXPECT_TRUE(hpart != INVALID_HANDLE_VALUE);
  if (hpart != INVALID_HANDLE_VALUE) {
    std::vector<char> dummy(1024, 'A');
    DWORD written = 0;
    WriteFile(hpart, dummy.data(), static_cast<DWORD>(dummy.size()), &written,
              nullptr);
    CloseHandle(hpart);
  }

  mtk::downloads::Item item;
  item.id = 777;
  item.file_name = L"mtk_resume_test_archive.zip";
  item.full_path = target;
  item.url = "https://example.com/files/mtk_resume_test_archive.zip";
  item.state = mtk::downloads::State::kInProgress;
  item.received_bytes = 1024;
  item.total_bytes = 4096;
  item.percent = 25;
  item.etag = "etag-v1-xyz";
  item.last_modified = "Wed, 16 Oct 2026 12:00:00 GMT";
  mgr.Upsert(item);
  MTK_EXPECT_TRUE(mgr.Flush());

  mgr.Load();
  MTK_EXPECT_TRUE(!mgr.items().empty());
  if (!mgr.items().empty()) {
    const auto& loaded = mgr.items().back();
    MTK_EXPECT_TRUE(loaded.state == mtk::downloads::State::kFailed);
    MTK_EXPECT_EQ_INT(loaded.received_bytes, 1024);
    MTK_EXPECT_EQ_STR(loaded.etag, "etag-v1-xyz");
    MTK_EXPECT_EQ_STR(loaded.last_modified, "Wed, 16 Oct 2026 12:00:00 GMT");
    MTK_EXPECT_TRUE(loaded.can_resume);
    MTK_EXPECT_TRUE(mtk::downloads::CanResume(loaded));
  }

  mgr.Remove(777);
  mgr.Flush();
  DeleteFileW(part.c_str());
  DeleteFileW(target.c_str());
}

MTK_TEST(OleDragDropBuildsValidHdropPayload) {
  std::vector<unsigned char> payload;
  const std::wstring sample_path = L"C:\\Users\\Test\\Downloads\\report.pdf";
  MTK_EXPECT_TRUE(mtk::dragdrop::BuildHdropPayload(sample_path, &payload));
  MTK_EXPECT_TRUE(payload.size() > sizeof(wchar_t) * sample_path.size());
}
