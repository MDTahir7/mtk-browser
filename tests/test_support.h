#pragma once

// Minimal self-contained test harness for MTK Browser logic tests. No external
// dependency is added to the project: tests are compiled by
// scripts/run_tests.ps1 with the same MSVC toolchain as the browser.

#include <string>
#include <vector>

namespace mtk::test {

struct Case {
  const char* name;
  void (*function)();
};

std::vector<Case>& Registry();

struct Registrar {
  Registrar(const char* name, void (*function)());
};

// Records a failure for the currently running case.
void Fail(const char* file, int line, const std::string& message);

// Narrow rendering of a wide string for failure messages only.
std::string Describe(const std::wstring& value);

// Throwaway profile directory the whole suite is redirected to while it runs.
// Cases that touch profile files build their paths from this.
const std::wstring& SuiteProfileRoot();

// False when the redirection is not in effect. A case that writes profile files
// must check this and do nothing rather than reach the real profile.
bool WritingToSuiteProfile();

int RunAll();

}  // namespace mtk::test

#define MTK_TEST(test_name)                                              \
  static void test_name();                                               \
  static ::mtk::test::Registrar mtk_registrar_##test_name(#test_name,    \
                                                          test_name);    \
  static void test_name()

#define MTK_EXPECT_TRUE(expression)                                      \
  do {                                                                   \
    if (!(expression)) {                                                 \
      ::mtk::test::Fail(__FILE__, __LINE__,                              \
                        "expected true: " #expression);                  \
    }                                                                    \
  } while (false)

#define MTK_EXPECT_FALSE(expression)                                     \
  do {                                                                   \
    if ((expression)) {                                                  \
      ::mtk::test::Fail(__FILE__, __LINE__,                              \
                        "expected false: " #expression);                 \
    }                                                                    \
  } while (false)

#define MTK_EXPECT_EQ_STR(actual, expected)                              \
  do {                                                                   \
    const std::string mtk_actual = (actual);                             \
    const std::string mtk_expected = (expected);                         \
    if (mtk_actual != mtk_expected) {                                    \
      ::mtk::test::Fail(__FILE__, __LINE__,                              \
                        "expected \"" + mtk_expected + "\", got \"" +    \
                            mtk_actual + "\"");                          \
    }                                                                    \
  } while (false)

#define MTK_EXPECT_EQ_WSTR(actual, expected)                             \
  do {                                                                   \
    const std::wstring mtk_actual = (actual);                            \
    const std::wstring mtk_expected = (expected);                        \
    if (mtk_actual != mtk_expected) {                                    \
      ::mtk::test::Fail(__FILE__, __LINE__,                              \
                        "expected \"" +                                  \
                            ::mtk::test::Describe(mtk_expected) +        \
                            "\", got \"" +                              \
                            ::mtk::test::Describe(mtk_actual) + "\"");   \
    }                                                                    \
  } while (false)

#define MTK_EXPECT_EQ_INT(actual, expected)                              \
  do {                                                                   \
    const long long mtk_actual = static_cast<long long>(actual);         \
    const long long mtk_expected = static_cast<long long>(expected);     \
    if (mtk_actual != mtk_expected) {                                    \
      ::mtk::test::Fail(__FILE__, __LINE__,                              \
                        "expected " + std::to_string(mtk_expected) +     \
                            ", got " + std::to_string(mtk_actual));      \
    }                                                                    \
  } while (false)
