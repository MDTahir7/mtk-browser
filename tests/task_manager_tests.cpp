#include "task_manager_dialog.h"
#include "test_support.h"

namespace {

using namespace mtk::task_manager;

MTK_TEST(TaskManagerMemoryFormatting) {
  MTK_EXPECT_TRUE(FormatMemory(-1) == L"\u2014");
  MTK_EXPECT_TRUE(FormatMemory(-100) == L"\u2014");
  MTK_EXPECT_TRUE(FormatMemory(0) == L"0.0 MB");

  // 10 MB
  const int64_t ten_mb = 10LL * 1024LL * 1024LL;
  MTK_EXPECT_TRUE(FormatMemory(ten_mb) == L"10.0 MB");

  // 512.5 MB
  const int64_t five_twelve_mb = static_cast<int64_t>(512.5 * 1024.0 * 1024.0);
  MTK_EXPECT_TRUE(FormatMemory(five_twelve_mb) == L"512.5 MB");

  // 2 GB
  const int64_t two_gb = 2LL * 1024LL * 1024LL * 1024LL;
  MTK_EXPECT_TRUE(FormatMemory(two_gb) == L"2.00 GB");

  // 1.5 GB
  const int64_t one_half_gb = static_cast<int64_t>(1.5 * 1024.0 * 1024.0 * 1024.0);
  MTK_EXPECT_TRUE(FormatMemory(one_half_gb) == L"1.50 GB");
}

MTK_TEST(TaskManagerCpuFormatting) {
  MTK_EXPECT_TRUE(FormatCpu(0.0) == L"0.0%");
  MTK_EXPECT_TRUE(FormatCpu(-5.0) == L"0.0%");
  MTK_EXPECT_TRUE(FormatCpu(12.34) == L"12.3%");
  MTK_EXPECT_TRUE(FormatCpu(99.98) == L"100.0%");
}

MTK_TEST(TaskManagerTypeLabelsEnglishAndHindi) {
  // English
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(1, false)) == L"Browser");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(2, false)) == L"GPU");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(4, false)) == L"Utility");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(5, false)) == L"Tab renderer");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(6, false)) == L"Extension");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(12, false)) == L"Service worker");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(99, false)) == L"Other");

  // Hindi
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(1, true)) == L"ब्राउज़र");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(2, true)) == L"GPU");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(4, true)) == L"यूटिलिटी");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(5, true)) == L"टैब रेंडरर");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(6, true)) == L"एक्सटेंशन");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(12, true)) == L"सर्विस वर्कर");
  MTK_EXPECT_TRUE(std::wstring(TaskTypeLabel(99, true)) == L"अन्य");
}

MTK_TEST(TaskManagerSortingComparators) {
  TaskInfo t1;
  t1.id = 1;
  t1.type = 1;
  t1.title = L"Browser";
  t1.cpu_usage = 1.5;
  t1.memory = 200LL * 1024LL * 1024LL;

  TaskInfo t2;
  t2.id = 2;
  t2.type = 5;
  t2.title = L"Tab: Heavy Page";
  t2.cpu_usage = 35.0;
  t2.memory = 600LL * 1024LL * 1024LL;

  // CPU descending: t2 (35.0%) should come before t1 (1.5%)
  MTK_EXPECT_TRUE(CompareTasks(t2, t1, SortColumn::kCpu, false));
  MTK_EXPECT_FALSE(CompareTasks(t1, t2, SortColumn::kCpu, false));

  // CPU ascending: t1 should come before t2
  MTK_EXPECT_TRUE(CompareTasks(t1, t2, SortColumn::kCpu, true));

  // Memory descending: t2 (600 MB) should come before t1 (200 MB)
  MTK_EXPECT_TRUE(CompareTasks(t2, t1, SortColumn::kMemory, false));

  // Title ascending: "Browser" should come before "Tab: Heavy Page"
  MTK_EXPECT_TRUE(CompareTasks(t1, t2, SortColumn::kTask, true));

  // Type ascending: 1 (Browser) comes before 5 (Tab renderer)
  MTK_EXPECT_TRUE(CompareTasks(t1, t2, SortColumn::kType, true));
}

MTK_TEST(TaskManagerCleanTaskTitle) {
  MTK_EXPECT_TRUE(CleanTaskTitle(L"") == L"Untitled task");
  MTK_EXPECT_TRUE(CleanTaskTitle(L"Tab: MTK Browser Home") == L"Tab: MTK Browser Home");
  MTK_EXPECT_TRUE(CleanTaskTitle(L"GPU Process") == L"GPU Process");
}

}  // namespace
