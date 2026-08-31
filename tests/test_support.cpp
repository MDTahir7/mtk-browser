#include "test_support.h"

#include <windows.h>

#include <cstdio>

#include "preferences.h"
#include "profile.h"

namespace mtk::test {
namespace {

int g_current_failures = 0;
std::wstring g_suite_root;
std::wstring g_suite_parent;

// Marks every path the suite is allowed to write to, so a case can assert it is
// not about to touch a real profile.
constexpr wchar_t kSuiteMarker[] = L"mtk-suite-";

void DeleteTree(const std::wstring& directory) {
  WIN32_FIND_DATAW entry{};
  HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &entry);
  if (search != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = entry.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring path = directory + L"\\" + name;
      if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        DeleteTree(path);
      } else {
        DeleteFileW(path.c_str());
      }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
  }
  RemoveDirectoryW(directory.c_str());
}

}  // namespace

const std::wstring& SuiteProfileRoot() { return g_suite_root; }

bool WritingToSuiteProfile() {
  const std::wstring active = mtk::preferences::ApplicationDataDirectory();
  return !g_suite_root.empty() &&
         active.find(kSuiteMarker) != std::wstring::npos;
}

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
  // Several of these cases write, clear and delete profile files. The real
  // profile root comes from a Windows known folder, not from an environment
  // variable, so it can only be redirected explicitly. Doing it here means no
  // case can reach real browsing data, whether or not it remembered to.
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  g_suite_parent = std::wstring(temporary) + kSuiteMarker +
                   std::to_wstring(GetCurrentProcessId());
  // The last element keeps the real directory's name, because that shape is part
  // of what the profile cases check.
  g_suite_root = g_suite_parent + L"\\MTK Browser";
  DeleteTree(g_suite_parent);
  if (!CreateDirectoryW(g_suite_parent.c_str(), nullptr) ||
      !CreateDirectoryW(g_suite_root.c_str(), nullptr)) {
    printf("could not create the throwaway profile directory; refusing to run "
           "against the real profile\n");
    return 1;
  }
  mtk::profile::SetRootForTesting(g_suite_root);

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
  mtk::profile::SetRootForTesting({});
  DeleteTree(g_suite_parent);
  g_suite_root.clear();
  g_suite_parent.clear();

  printf("\n%s  cases=%zu failed=%d\n", failed_cases == 0 ? "PASS" : "FAILED",
         Registry().size(), failed_cases);
  return failed_cases == 0 ? 0 : 1;
}

}  // namespace mtk::test

int main() {
  printf("MTK Browser logic tests\n\n");
  return mtk::test::RunAll();
}
