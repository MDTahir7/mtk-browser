#include "tab_search.h"
#include <algorithm>
#include <cwctype>
#include "dialog_text.h"
#include "resource.h"
#include "strings.h"

namespace mtk::tabs {
namespace {
std::wstring Fold(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(),
                  [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return text;
}
struct Context {
  const std::vector<SearchEntry>& entries;
  std::vector<size_t> matches;
  size_t active;
  ptrdiff_t selected = -1;
};
void Refresh(HWND dialog, Context& context, bool initial) {
  wchar_t query[257]{};
  GetDlgItemTextW(dialog, IDC_TAB_SEARCH_QUERY, query, 257);
  context.matches = Search(context.entries, query);
  HWND list = GetDlgItem(dialog, IDC_TAB_SEARCH_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  size_t selected = 0;
  for (size_t row = 0; row < context.matches.size(); ++row) {
    const size_t index = context.matches[row];
    const auto& tab = context.entries[index];
    const auto label = std::to_wstring(index + 1) + L". " + tab.title + L" — " + tab.url;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    if (initial && index == context.active) selected = row;
  }
  if (!context.matches.empty()) SendMessageW(list, LB_SETCURSEL, selected, 0);
  EnableWindow(GetDlgItem(dialog, IDOK), !context.matches.empty());
  SetDlgItemTextW(dialog, IDC_TAB_SEARCH_EMPTY, context.matches.empty()
      ? strings::Get(strings::Id::kNoMatchingTabs) : L"");
}
INT_PTR CALLBACK Procedure(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
  auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
  switch (message) {
    case WM_INITDIALOG: {
      context = reinterpret_cast<Context*>(lp);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, lp);
      dialog_text::ApplyCaption(dialog, strings::Id::kSearchTabs);
      static constexpr dialog_text::Item labels[] = {
          {IDC_TAB_SEARCH_LABEL, strings::Id::kTabSearchHint},
          {IDOK, strings::Id::kSwitchTab}, {IDCANCEL, strings::Id::kCancel}};
      dialog_text::Apply(dialog, labels);
      SendDlgItemMessageW(dialog, IDC_TAB_SEARCH_QUERY, EM_SETLIMITTEXT, 256, 0);
      if (context) Refresh(dialog, *context, true);
      return TRUE;
    }
    case WM_COMMAND:
      if (!context) break;
      if (LOWORD(wp) == IDC_TAB_SEARCH_QUERY && HIWORD(wp) == EN_CHANGE) {
        Refresh(dialog, *context, false); return TRUE;
      }
      if (LOWORD(wp) == IDOK ||
          (LOWORD(wp) == IDC_TAB_SEARCH_LIST && HIWORD(wp) == LBN_DBLCLK)) {
        const LRESULT row = SendDlgItemMessageW(dialog, IDC_TAB_SEARCH_LIST, LB_GETCURSEL, 0, 0);
        if (row >= 0 && static_cast<size_t>(row) < context->matches.size()) {
          context->selected = static_cast<ptrdiff_t>(context->matches[row]);
          EndDialog(dialog, IDOK);
        }
        return TRUE;
      }
      if (LOWORD(wp) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      break;
    case WM_CLOSE: EndDialog(dialog, IDCANCEL); return TRUE;
  }
  return FALSE;
}
}

std::vector<size_t> Search(const std::vector<SearchEntry>& entries,
                           const std::wstring& query) {
  const auto folded = Fold(query);
  std::vector<size_t> result;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (folded.empty() || Fold(entries[i].title).find(folded) != std::wstring::npos ||
        Fold(entries[i].url).find(folded) != std::wstring::npos) result.push_back(i);
  }
  return result;
}

ptrdiff_t ShowSearch(HINSTANCE instance, HWND owner,
                     const std::vector<SearchEntry>& entries, size_t active) {
  Context context{entries, {}, active};
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_TAB_SEARCH), owner,
      Procedure, reinterpret_cast<LPARAM>(&context)) != IDOK) return -1;
  return context.selected;
}
}
