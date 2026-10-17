#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace mtk::dragdrop {

// Builds a standard Windows CF_HDROP payload (DROPFILES header followed by a
// double-null-terminated UTF-16LE file path). Pure memory helper so it can be
// verified in unit tests without starting a modal OLE drag loop.
bool BuildHdropPayload(const std::wstring& file_path,
                       std::vector<unsigned char>* out_bytes);

// Starts a synchronous Win32 OLE drag-and-drop operation (`DoDragDrop`) for a
// finished download file on disk (`CF_HDROP`). Ensures OLE is initialized on
// the calling UI thread before starting the drag loop. Returns true if the
// user dropped the file onto a target.
bool StartFileDrag(const std::wstring& file_path);

// Starts a synchronous Win32 OLE drag-and-drop operation (`CF_UNICODETEXT`)
// for a bookmark URL/title.
bool StartBookmarkDrag(const std::string& url, const std::wstring& title);

}  // namespace mtk::dragdrop
