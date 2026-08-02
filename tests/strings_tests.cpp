// Tests for the UI string table. The most valuable thing checked here is the
// encoding: the sources are UTF-8 without a byte order mark, and if the compiler
// reads them in the system ANSI code page instead, every non-ASCII literal turns
// into mojibake and still compiles. These cases fail loudly in that situation.

#include <string>

#include "strings.h"
#include "test_support.h"

namespace {

bool IsDevanagari(wchar_t character) {
  return character >= 0x0900 && character <= 0x097F;
}

// True when the string contains at least one Devanagari letter and no character
// from the Latin-1 supplement, which is what mangled UTF-8 decays into.
bool LooksLikeHindi(const std::wstring& text) {
  bool devanagari = false;
  for (const wchar_t character : text) {
    if (IsDevanagari(character)) devanagari = true;
    // 0x80-0xFF is where "â€”" style mojibake lands.
    if (character >= 0x0080 && character <= 0x00FF) return false;
  }
  return devanagari;
}

}  // namespace

MTK_TEST(StringsEveryEntryHasBothLanguages) {
  for (size_t index = 0; index < static_cast<size_t>(mtk::strings::Id::kCount);
       ++index) {
    const auto id = static_cast<mtk::strings::Id>(index);
    const std::wstring english = mtk::strings::GetFor(id, false);
    const std::wstring hindi = mtk::strings::GetFor(id, true);
    if (english.empty()) {
      ::mtk::test::Fail(__FILE__, __LINE__,
                        "empty English string at index " +
                            std::to_string(index));
    }
    if (hindi.empty()) {
      ::mtk::test::Fail(__FILE__, __LINE__,
                        "empty Hindi string at index " + std::to_string(index));
    }
  }
}

MTK_TEST(StringsHindiIsActuallyDevanagariNotMojibake) {
  // A representative spread rather than every entry: a few strings keep a Latin
  // product name or a key name on purpose.
  const mtk::strings::Id ids[] = {
      mtk::strings::Id::kAddBookmark,   mtk::strings::Id::kHistory,
      mtk::strings::Id::kDownloads,     mtk::strings::Id::kNewWindow,
      mtk::strings::Id::kNewPrivateWindow, mtk::strings::Id::kCloseTab,
      mtk::strings::Id::kDuplicateTab,  mtk::strings::Id::kSettings,
      mtk::strings::Id::kClearBrowsingData, mtk::strings::Id::kPause,
      mtk::strings::Id::kResume,        mtk::strings::Id::kCancel,
      mtk::strings::Id::kBack,          mtk::strings::Id::kForward,
      mtk::strings::Id::kAddressPlaceholder,
  };
  for (const auto id : ids) {
    const std::wstring hindi = mtk::strings::GetFor(id, true);
    if (!LooksLikeHindi(hindi)) {
      ::mtk::test::Fail(__FILE__, __LINE__,
                        "Hindi string is not Devanagari: " +
                            ::mtk::test::Describe(hindi));
    }
  }
}

MTK_TEST(StringsEnglishStaysAscii) {
  // English entries are plain ASCII, so a mangled build shows up as a
  // difference here too rather than only in Hindi.
  for (size_t index = 0; index < static_cast<size_t>(mtk::strings::Id::kCount);
       ++index) {
    const std::wstring english =
        mtk::strings::GetFor(static_cast<mtk::strings::Id>(index), false);
    for (const wchar_t character : english) {
      if (character > 0x007F) {
        ::mtk::test::Fail(__FILE__, __LINE__,
                          "non-ASCII English string at index " +
                              std::to_string(index) + ": " +
                              ::mtk::test::Describe(english));
        break;
      }
    }
  }
}

MTK_TEST(StringsCountedPatternKeepsItsPlaceholder) {
  // The active-downloads label splices a number in, so both translations have to
  // carry the placeholder or the count would silently disappear.
  const std::wstring english =
      mtk::strings::GetFor(mtk::strings::Id::kDownloadsActive, false);
  const std::wstring hindi =
      mtk::strings::GetFor(mtk::strings::Id::kDownloadsActive, true);
  MTK_EXPECT_TRUE(english.find(L"%d") != std::wstring::npos);
  MTK_EXPECT_TRUE(hindi.find(L"%d") != std::wstring::npos);
}

MTK_TEST(StringsUnknownIdIsHarmless) {
  const std::wstring text = mtk::strings::GetFor(
      static_cast<mtk::strings::Id>(static_cast<int>(mtk::strings::Id::kCount)),
      true);
  MTK_EXPECT_TRUE(text.empty());
}
