#include "test_support.h"

#include <windows.h>

#include <cstdio>

namespace mtk::test {
namespace {

int g_current_failures = 0;

}  // namespace

std::vector<Case>& Registry() {
  static std::vector<Case> registry;
  return registry;
}

Registrar::Registrar(const char* name, void (*function)()) {
  Registry().push_back({name, function});
}

void Fail(const char* file, int line, const std::string& message) {
  ++g_current_failures;
  printf("      %s:%d %s\n", file, line, message.c_str());
}

std::string Describe(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return "<unprintable>";
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

int RunAll() {
  int failed_cases = 0;
  for (const auto& test_case : Registry()) {
    g_current_failures = 0;
    test_case.function();
    if (g_current_failures == 0) {
      printf("  ok    %s\n", test_case.name);
    } else {
      ++failed_cases;
      printf("  FAIL  %s (%d assertion failure(s))\n", test_case.name,
             g_current_failures);
    }
  }
  printf("\n%s  cases=%zu failed=%d\n", failed_cases == 0 ? "PASS" : "FAILED",
         Registry().size(), failed_cases);
  return failed_cases == 0 ? 0 : 1;
}

}  // namespace mtk::test

int main() {
  printf("MTK Browser logic tests\n\n");
  return mtk::test::RunAll();
}
