#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace mtk::tabs {
struct SearchEntry { std::wstring title; std::wstring url; };
// Returns source indexes, in original tab order, for case-insensitive matches.
std::vector<size_t> Search(const std::vector<SearchEntry>& entries,
                           const std::wstring& query);
// Returns the selected snapshot index or -1 on cancel/failure.
ptrdiff_t ShowSearch(HINSTANCE instance, HWND owner,
                     const std::vector<SearchEntry>& entries, size_t active);
}
