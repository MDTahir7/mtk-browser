#pragma once

#include <string>

// Shipped WebM loops are inlined into the pages that show them, so those pages
// stay self-contained data: URLs that need no local file access and reach no
// network. This is the single validated path for turning such an asset into a
// data URL; both the block page and the Shields panel use it.
namespace mtk::assets {

// Prefix carried by every URL this module produces. Request filtering keys off
// it, so it lives here rather than being spelled out at each use site.
inline constexpr char kWebmDataUrlPrefix[] = "data:video/webm;base64,";

// Largest asset that may be inlined. A truncated or swapped file must never be
// able to break a page, and the finished URL has to stay well inside
// Chromium's 2 MB URL limit.
inline constexpr size_t kMaxInlineWebmBytes = 700 * 1024;

// Returns "data:video/webm;base64,..." for `file_name`, which must sit beside
// the executable. Returns an empty string when the file is missing, is larger
// than kMaxInlineWebmBytes, or does not start with the WebM magic number; every
// caller is expected to fall back to a static glyph in that case.
//
// The result is loaded once per file name and cached for the process lifetime.
const std::string& WebmDataUrl(const std::wstring& file_name);

}  // namespace mtk::assets
