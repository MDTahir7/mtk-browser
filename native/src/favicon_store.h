#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mtk::favicon {

// Returns a valid "data:image/..." URL (PNG or SVG) for `host`.
// Checks:
// 1. Downloaded/cached favicon on disk or in memory.
// 2. Built-in high-resolution brand vector SVG.
// 3. Dynamic brand-colored monogram SVG badge.
std::string GetFaviconDataUrl(std::string_view host);

// Stores downloaded raw BGRA pixels for `host`, encodes to PNG, and persists to disk.
void StoreDownloadedFavicon(const std::string& host,
                            const uint32_t* src_pixels,
                            int width, int height);

// Initializes the persistent store on profile startup.
void InitializeStore();

}  // namespace mtk::favicon
