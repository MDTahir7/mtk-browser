#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mtk::task_manager {

// Normalized task information for UI and diagnostics.
struct TaskInfo {
  int64_t id = 0;
  int type = 0;          // Maps to cef_task_type_t
  bool is_killable = false;
  std::wstring title;
  double cpu_usage = 0.0;
  int64_t memory = -1;
  int64_t gpu_memory = -1;
  int browser_id = -1;
};

enum class SortColumn {
  kTask = 0,
  kType = 1,
  kCpu = 2,
  kMemory = 3,
  kGpuMemory = 4,
  kProcessId = 5,
};

// Pure, testable helper functions:
std::wstring FormatMemory(int64_t bytes);
std::wstring FormatCpu(double cpu_usage);
const wchar_t* TaskTypeLabel(int type, bool hindi = false);
std::wstring CleanTaskTitle(const std::wstring& title);
bool CompareTasks(const TaskInfo& a, const TaskInfo& b, SortColumn col, bool ascending);

// UI Callback when user double-clicks a tab renderer to switch to it.
using SwitchTabCallback = std::function<void(const std::wstring& title, int browser_id)>;

// Shows or activates the modeless Task Manager window.
void Show(HINSTANCE instance, HWND parent, SwitchTabCallback on_switch_tab = nullptr);

// Closes the Task Manager window if open.
void Close();

// Returns the active modeless window handle, or nullptr if closed.
HWND GetWindow();

}  // namespace mtk::task_manager
