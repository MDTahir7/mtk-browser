#include "strings.h"

#include <iterator>
#include <string>

#include "preferences.h"

namespace mtk::strings {
namespace {

const wchar_t* const kEnglish[] = {
#define MTK_STRING_ENGLISH(name, english, hindi) english,
    MTK_STRING_TABLE(MTK_STRING_ENGLISH)
#undef MTK_STRING_ENGLISH
};

const wchar_t* const kHindi[] = {
#define MTK_STRING_HINDI(name, english, hindi) hindi,
    MTK_STRING_TABLE(MTK_STRING_HINDI)
#undef MTK_STRING_HINDI
};

// Generated from one table, so this can only fail if the macro list itself is
// malformed, which is exactly what it is here to catch.
static_assert(std::size(kEnglish) == static_cast<size_t>(Id::kCount),
              "English table does not match the string list");
static_assert(std::size(kHindi) == static_cast<size_t>(Id::kCount),
              "Hindi table does not match the string list");

bool g_hindi = false;

}  // namespace

void Refresh() {
  // The Settings language choice writes an Accept-Language list. Hindi first
  // means the person asked for Hindi, so the UI follows.
  const std::string& languages =
      mtk::preferences::Store::Get().values().accept_language;
  g_hindi = languages.starts_with("hi");
}

bool IsHindi() { return g_hindi; }

const wchar_t* GetFor(Id id, bool hindi) {
  const size_t index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(Id::kCount)) return L"";
  const wchar_t* text = hindi ? kHindi[index] : kEnglish[index];
  // An empty translation would show a blank menu entry, which is worse than
  // showing English, so it falls back rather than trusting the table blindly.
  if (text == nullptr || text[0] == L'\0') return kEnglish[index];
  return text;
}

const wchar_t* Get(Id id) { return GetFor(id, g_hindi); }

}  // namespace mtk::strings
