#include "task_manager_dialog.h"

#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>

#include "dialog_text.h"
#include "resource.h"
#include "strings.h"

#if __has_include("include/cef_task_manager.h")
#include "include/cef_task_manager.h"
#include "include/internal/cef_types.h"
#define MTK_HAS_CEF 1
#else
#define MTK_HAS_CEF 0
#endif

namespace mtk::task_manager {

namespace {

HWND g_dialog = nullptr;
SwitchTabCallback g_on_switch_tab = nullptr;
SortColumn g_sort_column = SortColumn::kCpu;
bool g_sort_ascending = false;
int64_t g_selected_task_id = -1;

// Explicit Wide Win32 ListView helper functions:
inline int LvInsertItemW(HWND list, const LVITEMW* item) {
  return static_cast<int>(SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(item)));
}

inline void LvSetItemTextW(HWND list, int item_index, int sub_item, const wchar_t* text) {
  LVITEMW lvi{};
  lvi.iSubItem = sub_item;
  lvi.pszText = const_cast<wchar_t*>(text);
  SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(item_index), reinterpret_cast<LPARAM>(&lvi));
}

inline void LvGetItemTextW(HWND list, int item_index, int sub_item, wchar_t* buffer, int max_chars) {
  LVITEMW lvi{};
  lvi.iSubItem = sub_item;
  lvi.cchTextMax = max_chars;
  lvi.pszText = buffer;
  SendMessageW(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(item_index), reinterpret_cast<LPARAM>(&lvi));
}

inline int LvInsertColumnW(HWND list, int col_index, const LVCOLUMNW* col) {
  return static_cast<int>(SendMessageW(list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(col_index), reinterpret_cast<LPARAM>(col)));
}

inline int LvGetSelectedItem(HWND list) {
  return static_cast<int>(SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), MAKELPARAM(LVNI_SELECTED, 0)));
}

inline bool LvGetItemW(HWND list, LVITEMW* item) {
  return SendMessageW(list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(item)) != 0;
}

inline void LvSetItemState(HWND list, int item_index, UINT state, UINT mask) {
  LVITEMW lvi{};
  lvi.stateMask = mask;
  lvi.state = state;
  SendMessageW(list, LVM_SETITEMSTATE, static_cast<WPARAM>(item_index), reinterpret_cast<LPARAM>(&lvi));
}

inline void LvEnsureVisible(HWND list, int item_index) {
  SendMessageW(list, LVM_ENSUREVISIBLE, static_cast<WPARAM>(item_index), FALSE);
}

inline void LvDeleteAllItems(HWND list) {
  SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
}

std::vector<TaskInfo> FetchLiveTasks() {
  std::vector<TaskInfo> tasks;
#if MTK_HAS_CEF
  auto manager = CefTaskManager::GetTaskManager();
  if (!manager) return tasks;

  CefTaskManager::TaskIdList ids;
  if (!manager->GetTaskIdsList(ids)) return tasks;

  for (const int64_t id : ids) {
    CefTaskInfo info;
    if (!manager->GetTaskInfo(id, info)) continue;

    TaskInfo item;
    item.id = id;
    item.type = static_cast<int>(info.type);
    item.is_killable = (info.is_killable != 0);
    item.title = CefString(&info.title).ToWString();
    if (item.title.empty()) item.title = L"Untitled task";
    item.cpu_usage = info.cpu_usage;
    item.memory = info.memory;
    item.gpu_memory = info.gpu_memory;
    tasks.push_back(item);
  }
#endif
  return tasks;
}

void UpdateEndProcessButtonState(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_TASK_MANAGER_LIST);
  const int selected_row = LvGetSelectedItem(list);
  bool can_kill = false;

  if (selected_row >= 0) {
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = selected_row;
    if (LvGetItemW(list, &item)) {
      g_selected_task_id = static_cast<int64_t>(item.lParam);
#if MTK_HAS_CEF
      auto manager = CefTaskManager::GetTaskManager();
      if (manager) {
        CefTaskInfo info;
        if (manager->GetTaskInfo(g_selected_task_id, info)) {
          can_kill = (info.is_killable != 0);
        }
      }
#endif
    }
  } else {
    g_selected_task_id = -1;
  }

  EnableWindow(GetDlgItem(dialog, IDC_TASK_MANAGER_END_PROCESS), can_kill ? TRUE : FALSE);
}

void PopulateListView(HWND dialog, bool is_refresh = false) {
  HWND list = GetDlgItem(dialog, IDC_TASK_MANAGER_LIST);
  if (!list) return;

  auto tasks = FetchLiveTasks();

  // Sort tasks
  std::sort(tasks.begin(), tasks.end(),
            [](const TaskInfo& a, const TaskInfo& b) {
              return CompareTasks(a, b, g_sort_column, g_sort_ascending);
            });

  // Freeze updates while modifying to eliminate all flicker
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  LvDeleteAllItems(list);

  int restore_select_row = -1;
  const bool hindi = mtk::strings::IsHindi();

  for (size_t i = 0; i < tasks.size(); ++i) {
    const auto& task = tasks[i];
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = static_cast<int>(i);
    item.iSubItem = 0;
    std::wstring clean_title = CleanTaskTitle(task.title);
    item.pszText = const_cast<wchar_t*>(clean_title.c_str());
    item.lParam = static_cast<LPARAM>(task.id);

    const int row = LvInsertItemW(list, &item);
    if (row >= 0) {
      LvSetItemTextW(list, row, 1, TaskTypeLabel(task.type, hindi));
      std::wstring cpu_str = FormatCpu(task.cpu_usage);
      LvSetItemTextW(list, row, 2, cpu_str.c_str());
      std::wstring mem_str = FormatMemory(task.memory);
      LvSetItemTextW(list, row, 3, mem_str.c_str());
      std::wstring gpu_str = FormatMemory(task.gpu_memory);
      LvSetItemTextW(list, row, 4, gpu_str.c_str());
      std::wstring id_str = std::to_wstring(task.id);
      LvSetItemTextW(list, row, 5, id_str.c_str());

      if (task.id == g_selected_task_id) {
        restore_select_row = row;
      }
    }
  }

  if (restore_select_row >= 0) {
    LvSetItemState(list, restore_select_row,
                   LVIS_SELECTED | LVIS_FOCUSED,
                   LVIS_SELECTED | LVIS_FOCUSED);
    LvEnsureVisible(list, restore_select_row);
  }

  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);

  UpdateEndProcessButtonState(dialog);
}

void InitColumns(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_TASK_MANAGER_LIST);
  if (!list) return;

  SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
               LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

  using Text = mtk::strings::Id;
  const struct {
    int width;
    int fmt;
    Text text_id;
  } kCols[] = {
      {230, LVCFMT_LEFT, Text::kTaskManagerColTask},
      {100, LVCFMT_LEFT, Text::kTaskManagerColType},
      {75, LVCFMT_RIGHT, Text::kTaskManagerColCpu},
      {85, LVCFMT_RIGHT, Text::kTaskManagerColMemory},
      {85, LVCFMT_RIGHT, Text::kTaskManagerColGpuMemory},
      {70, LVCFMT_RIGHT, Text::kTaskManagerColProcessId},
  };

  for (int i = 0; i < 6; ++i) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    col.fmt = kCols[i].fmt;
    col.cx = kCols[i].width;
    col.iSubItem = i;
    std::wstring label = mtk::strings::Get(kCols[i].text_id);
    col.pszText = const_cast<wchar_t*>(label.c_str());
    LvInsertColumnW(list, i, &col);
  }
}

void OnEndProcess(HWND dialog) {
  if (g_selected_task_id <= 0) return;

#if MTK_HAS_CEF
  auto manager = CefTaskManager::GetTaskManager();
  if (!manager) return;

  CefTaskInfo info;
  if (manager->GetTaskInfo(g_selected_task_id, info)) {
    if (info.type == CEF_TASK_TYPE_BROWSER || !info.is_killable) {
      MessageBoxW(dialog,
                  mtk::strings::Get(mtk::strings::Id::kTaskManagerCannotEndBrowser),
                  mtk::strings::Get(mtk::strings::Id::kTaskManager),
                  MB_OK | MB_ICONWARNING);
      return;
    }
    manager->KillTask(g_selected_task_id);
  }
#endif

  // Refresh list immediately
  PopulateListView(dialog);
}

INT_PTR CALLBACK TaskManagerProc(HWND dialog, UINT message, WPARAM w_param,
                                LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      using Text = mtk::strings::Id;
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_TASK_MANAGER_HINT, Text::kTaskManagerHint},
          {IDC_TASK_MANAGER_END_PROCESS, Text::kTaskManagerEndProcess},
          {IDC_TASK_MANAGER_REFRESH, Text::kTaskManagerRefresh},
          {IDCANCEL, Text::kClose},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kTaskManager);
      mtk::dialog_text::Apply(dialog, kText);

      InitColumns(dialog);
      PopulateListView(dialog);
      SetTimer(dialog, 1, 1000, nullptr);
      return TRUE;
    }

    case WM_TIMER:
      if (w_param == 1) {
        PopulateListView(dialog, true);
        return TRUE;
      }
      break;

    case WM_COMMAND: {
      const WORD id = LOWORD(w_param);
      if (id == IDC_TASK_MANAGER_REFRESH) {
        PopulateListView(dialog);
        return TRUE;
      }
      if (id == IDC_TASK_MANAGER_END_PROCESS) {
        OnEndProcess(dialog);
        return TRUE;
      }
      if (id == IDCANCEL) {
        DestroyWindow(dialog);
        return TRUE;
      }
      break;
    }

    case WM_NOTIFY: {
      NMHDR* hdr = reinterpret_cast<NMHDR*>(l_param);
      if (hdr && hdr->idFrom == IDC_TASK_MANAGER_LIST) {
        if (hdr->code == LVN_ITEMCHANGED) {
          UpdateEndProcessButtonState(dialog);
        } else if (hdr->code == LVN_COLUMNCLICK) {
          NMLISTVIEW* nmlv = reinterpret_cast<NMLISTVIEW*>(l_param);
          const SortColumn clicked = static_cast<SortColumn>(nmlv->iSubItem);
          if (clicked == g_sort_column) {
            g_sort_ascending = !g_sort_ascending;
          } else {
            g_sort_column = clicked;
            // CPU, Memory, GPU Memory default to descending (highest first)
            g_sort_ascending = (clicked == SortColumn::kTask ||
                                clicked == SortColumn::kType);
          }
          PopulateListView(dialog);
          return TRUE;
        } else if (hdr->code == NM_DBLCLK) {
          NMITEMACTIVATE* nmia = reinterpret_cast<NMITEMACTIVATE*>(l_param);
          if (nmia->iItem >= 0 && g_on_switch_tab) {
            HWND list = GetDlgItem(dialog, IDC_TASK_MANAGER_LIST);
            wchar_t title_buf[256]{};
            LvGetItemTextW(list, nmia->iItem, 0, title_buf, 256);
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = nmia->iItem;
            int64_t task_id = -1;
            if (LvGetItemW(list, &item)) {
              task_id = static_cast<int64_t>(item.lParam);
            }
            g_on_switch_tab(title_buf, static_cast<int>(task_id));
          }
          return TRUE;
        }
      }
      break;
    }

    case WM_CLOSE:
      DestroyWindow(dialog);
      return TRUE;

    case WM_NCDESTROY:
      KillTimer(dialog, 1);
      g_dialog = nullptr;
      g_on_switch_tab = nullptr;
      return TRUE;
  }
  return FALSE;
}

}  // namespace

std::wstring FormatMemory(int64_t bytes) {
  if (bytes < 0) return L"\u2014";  // Em dash
  if (bytes == 0) return L"0.0 MB";

  if (bytes >= 1024LL * 1024LL * 1024LL) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    wchar_t text[48]{};
    swprintf_s(text, L"%.2f GB", gib);
    return text;
  }

  const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
  wchar_t text[48]{};
  swprintf_s(text, L"%.1f MB", mib);
  return text;
}

std::wstring FormatCpu(double cpu_usage) {
  if (cpu_usage < 0.0) return L"0.0%";
  wchar_t text[32]{};
  swprintf_s(text, L"%.1f%%", cpu_usage);
  return text;
}

const wchar_t* TaskTypeLabel(int type, bool hindi) {
  switch (type) {
    case 1:  // CEF_TASK_TYPE_BROWSER
      return hindi ? L"ब्राउज़र" : L"Browser";
    case 2:  // CEF_TASK_TYPE_GPU
      return L"GPU";
    case 3:  // CEF_TASK_TYPE_ZYGOTE
      return hindi ? L"ज़ाइगोट" : L"Zygote";
    case 4:  // CEF_TASK_TYPE_UTILITY
      return hindi ? L"यूटिलिटी" : L"Utility";
    case 5:  // CEF_TASK_TYPE_RENDERER
      return hindi ? L"टैब रेंडरर" : L"Tab renderer";
    case 6:  // CEF_TASK_TYPE_EXTENSION
      return hindi ? L"एक्सटेंशन" : L"Extension";
    case 7:  // CEF_TASK_TYPE_GUEST
      return hindi ? L"गेस्ट" : L"Guest";
    case 8:  // CEF_TASK_TYPE_PLUGIN
      return hindi ? L"प्लगइन" : L"Plugin";
    case 9:  // CEF_TASK_TYPE_SANDBOX_HELPER
      return hindi ? L"सैंडबॉक्स हेल्पर" : L"Sandbox helper";
    case 10: // CEF_TASK_TYPE_DEDICATED_WORKER
      return hindi ? L"डेडिकेटेड वर्कर" : L"Dedicated worker";
    case 11: // CEF_TASK_TYPE_SHARED_WORKER
      return hindi ? L"शेयर्ड वर्कर" : L"Shared worker";
    case 12: // CEF_TASK_TYPE_SERVICE_WORKER
      return hindi ? L"सर्विस वर्कर" : L"Service worker";
    default:
      return hindi ? L"अन्य" : L"Other";
  }
}

std::wstring CleanTaskTitle(const std::wstring& title) {
  if (title.empty()) return L"Untitled task";
  return title;
}

bool CompareTasks(const TaskInfo& a, const TaskInfo& b, SortColumn col, bool ascending) {
  auto compare_values = [&](auto val_a, auto val_b) -> bool {
    if (val_a != val_b) {
      return ascending ? (val_a < val_b) : (val_a > val_b);
    }
    // Stable tie-breaker by id
    return a.id < b.id;
  };

  switch (col) {
    case SortColumn::kTask: {
      std::wstring lower_a = a.title;
      std::wstring lower_b = b.title;
      std::transform(lower_a.begin(), lower_a.end(), lower_a.begin(), ::towlower);
      std::transform(lower_b.begin(), lower_b.end(), lower_b.begin(), ::towlower);
      return compare_values(lower_a, lower_b);
    }
    case SortColumn::kType:
      return compare_values(a.type, b.type);
    case SortColumn::kCpu:
      return compare_values(a.cpu_usage, b.cpu_usage);
    case SortColumn::kMemory:
      return compare_values(a.memory, b.memory);
    case SortColumn::kGpuMemory:
      return compare_values(a.gpu_memory, b.gpu_memory);
    case SortColumn::kProcessId:
      return compare_values(a.id, b.id);
  }
  return a.id < b.id;
}

void Show(HINSTANCE instance, HWND parent, SwitchTabCallback on_switch_tab) {
  if (g_dialog && IsWindow(g_dialog)) {
    if (IsIconic(g_dialog)) ShowWindow(g_dialog, SW_RESTORE);
    SetForegroundWindow(g_dialog);
    BringWindowToTop(g_dialog);
    return;
  }

  g_on_switch_tab = on_switch_tab;
  g_dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TASK_MANAGER),
                                parent, TaskManagerProc, 0);
  if (g_dialog) {
    ShowWindow(g_dialog, SW_SHOW);
    UpdateWindow(g_dialog);
  }
}

void Close() {
  if (g_dialog && IsWindow(g_dialog)) {
    DestroyWindow(g_dialog);
    g_dialog = nullptr;
  }
}

HWND GetWindow() {
  return (g_dialog && IsWindow(g_dialog)) ? g_dialog : nullptr;
}

}  // namespace mtk::task_manager
