#pragma once

#include <windows.h>

#include <cstddef>

#include "strings.h"

namespace mtk::dialog_text {

// The dialog templates in the resource script carry English text, which is what
// a resource editor shows and what appears if this pass is ever skipped. The
// text is then replaced at run time from the one string table that holds both
// languages, rather than keeping a second copy of every dialog per language.
//
// A static label needs a real control id for this to reach it; a label left at
// -1 in the template cannot be addressed.

struct Item {
  int control;
  strings::Id id;
};

// Replaces the window caption.
void ApplyCaption(HWND dialog, strings::Id caption);

// Replaces the text of each listed control. Controls that are absent from the
// template are skipped rather than treated as an error, so a dialog can share a
// table with a longer one.
void Apply(HWND dialog, const Item* items, size_t count);

template <size_t N>
void Apply(HWND dialog, const Item (&items)[N]) {
  Apply(dialog, items, N);
}

}  // namespace mtk::dialog_text
