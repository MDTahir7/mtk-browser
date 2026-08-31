#include "dialog_text.h"

namespace mtk::dialog_text {

void ApplyCaption(HWND dialog, strings::Id caption) {
  if (!dialog) return;
  SetWindowTextW(dialog, strings::Get(caption));
}

void Apply(HWND dialog, const Item* items, size_t count) {
  if (!dialog || !items) return;
  for (size_t index = 0; index < count; ++index) {
    if (HWND control = GetDlgItem(dialog, items[index].control)) {
      SetWindowTextW(control, strings::Get(items[index].id));
    }
  }
}

}  // namespace mtk::dialog_text
