// Tests for the Chromium bookmarks importer. This parser reads a file written
// by another program, so the cases below cover both ordinary content and
// deliberately malformed or hostile input.

#include <windows.h>

#include <string>

#include "bookmark_import.h"
#include "test_support.h"

namespace {

std::wstring TempJsonPath(const wchar_t* name) {
  wchar_t directory[MAX_PATH]{};
  const DWORD length = GetTempPathW(MAX_PATH, directory);
  if (length == 0 || length > MAX_PATH) return {};
  return std::wstring(directory) + L"mtk_import_test_" + name + L".json";
}

bool WriteText(const std::wstring& path, const std::string& content) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, content.data(),
                            static_cast<DWORD>(content.size()), &written,
                            nullptr) != FALSE &&
                  written == content.size();
  CloseHandle(file);
  return ok;
}

// Writes `content` to a temporary file, parses it, and removes the file.
std::vector<mtk::library::Entry> ParseJson(const wchar_t* name,
                                           const std::string& content,
                                           size_t* refused) {
  const std::wstring path = TempJsonPath(name);
  if (path.empty() || !WriteText(path, content)) return {};
  auto entries = mtk::bookmark_import::Read(path, refused);
  DeleteFileW(path.c_str());
  return entries;
}

}  // namespace

MTK_TEST(ImportReadsChromeBookmarkTree) {
  const std::string json = R"({
    "checksum": "abc",
    "roots": {
      "bookmark_bar": {
        "children": [
          {"date_added": "13300000000000000", "name": "Example",
           "type": "url", "url": "https://example.com/start"},
          {"name": "Folder", "type": "folder", "children": [
            {"name": "Nested", "type": "url", "url": "https://example.org/deep"}
          ]}
        ],
        "name": "Bookmarks bar",
        "type": "folder"
      }
    },
    "version": 1
  })";
  size_t refused = 0;
  const auto entries = ParseJson(L"tree", json, &refused);
  MTK_EXPECT_EQ_INT(entries.size(), 2);
  MTK_EXPECT_EQ_INT(refused, 0);
  if (entries.size() == 2) {
    // Each bookmark is emitted where its object ends, so the order matches the
    // order the entries appear in the file, folders included.
    MTK_EXPECT_EQ_STR(entries[0].url, "https://example.com/start");
    MTK_EXPECT_EQ_WSTR(entries[0].title, L"Example");
    MTK_EXPECT_EQ_STR(entries[1].url, "https://example.org/deep");
    MTK_EXPECT_EQ_WSTR(entries[1].title, L"Nested");
  }
}

MTK_TEST(ImportRefusesAddressesThePolicyBlocks) {
  const std::string json = R"({"roots": {"bookmark_bar": {"children": [
    {"name": "Adult", "type": "url", "url": "https://www.pornhub.com/"},
    {"name": "Router", "type": "url", "url": "http://192.168.1.1/"},
    {"name": "Local file", "type": "url", "url": "file:///C:/Windows/win.ini"},
    {"name": "Fine", "type": "url", "url": "https://example.com/ok"}
  ], "type": "folder"}}})";
  size_t refused = 0;
  const auto entries = ParseJson(L"refused", json, &refused);
  MTK_EXPECT_EQ_INT(entries.size(), 1);
  MTK_EXPECT_EQ_INT(refused, 3);
  if (entries.size() == 1) {
    MTK_EXPECT_EQ_STR(entries[0].url, "https://example.com/ok");
  }
}

MTK_TEST(ImportDecodesEscapesAndUnicodeNames) {
  // "Tab\tand \"quote\"" plus a Devanagari name written as \u escapes.
  const std::string json =
      "{\"roots\":{\"bar\":{\"children\":[{\"name\":\"Quote \\\"x\\\" end\","
      "\"type\":\"url\",\"url\":\"https://example.com/a\"},"
      "{\"name\":\"\\u0939\\u093f\\u0928\\u094d\\u0926\\u0940\","
      "\"type\":\"url\",\"url\":\"https://example.com/b\"}],"
      "\"type\":\"folder\"}}}";
  size_t refused = 0;
  const auto entries = ParseJson(L"escapes", json, &refused);
  MTK_EXPECT_EQ_INT(entries.size(), 2);
  if (entries.size() == 2) {
    MTK_EXPECT_EQ_WSTR(entries[0].title, L"Quote \"x\" end");
    MTK_EXPECT_EQ_WSTR(entries[1].title, L"\u0939\u093f\u0928\u094d\u0926\u0940");
  }
}

MTK_TEST(ImportDecodesUtf8Names) {
  // Raw UTF-8 bytes, which is how Chrome actually stores non-ASCII names.
  const std::string json =
      "{\"roots\":{\"bar\":{\"children\":[{\"name\":\"\xE0\xA4\xB9\xE0\xA4\xBF"
      "\",\"type\":\"url\",\"url\":\"https://example.com/u\"}],"
      "\"type\":\"folder\"}}}";
  const auto entries = ParseJson(L"utf8", json, nullptr);
  MTK_EXPECT_EQ_INT(entries.size(), 1);
  if (entries.size() == 1) {
    MTK_EXPECT_EQ_WSTR(entries[0].title, L"\u0939\u093F");
  }
}

MTK_TEST(ImportIgnoresNonUrlNodesAndEmptyUrls) {
  const std::string json = R"({"roots": {"bar": {"children": [
    {"name": "Just a folder", "type": "folder", "children": []},
    {"name": "No url", "type": "url", "url": ""},
    {"name": "No type", "url": "https://example.com/x"}
  ], "type": "folder"}}})";
  size_t refused = 0;
  const auto entries = ParseJson(L"ignored", json, &refused);
  MTK_EXPECT_EQ_INT(entries.size(), 0);
  MTK_EXPECT_EQ_INT(refused, 0);
}

MTK_TEST(ImportSurvivesTruncatedJson) {
  const std::string json =
      "{\"roots\":{\"bar\":{\"children\":[{\"name\":\"Half\",\"type\":\"url\","
      "\"url\":\"https://example.com/half\"";
  size_t refused = 0;
  const auto entries = ParseJson(L"truncated", json, &refused);
  // The unterminated object never closes, so nothing is emitted and the parser
  // returns instead of reading past the end.
  MTK_EXPECT_EQ_INT(entries.size(), 0);
}

MTK_TEST(ImportStopsAtExcessiveNesting) {
  std::string json;
  for (int depth = 0; depth < 200; ++depth) json += "{\"children\":[";
  json += "{\"name\":\"Deep\",\"type\":\"url\",\"url\":\"https://example.com/d\"}";
  const auto entries = ParseJson(L"deep", json, nullptr);
  MTK_EXPECT_EQ_INT(entries.size(), 0);
}

MTK_TEST(ImportRejectsMissingFile) {
  size_t refused = 7;
  const auto entries =
      mtk::bookmark_import::Read(L"Z:\\mtk-does-not-exist\\Bookmarks", &refused);
  MTK_EXPECT_EQ_INT(entries.size(), 0);
  MTK_EXPECT_EQ_INT(refused, 0);
}
