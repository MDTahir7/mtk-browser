#pragma once

#include <string>
#include <vector>

#include "library_store.h"

namespace mtk::bookmark_import {

struct Source {
  // Display name of the browser the file belongs to, e.g. "Google Chrome".
  std::wstring browser;
  std::wstring path;
};

struct Result {
  // Browser the bookmarks were read from, empty when nothing was found.
  std::wstring browser;
  // Bookmarks in the file that pointed at a usable web address.
  size_t found = 0;
  // Bookmarks refused by the navigation policy, e.g. adult or private-network
  // addresses. Reported so the import is never silently lossy.
  size_t refused = 0;
};

// Chromium-family bookmark files this importer knows about, in the order they
// are tried. Only files that exist are returned.
std::vector<Source> AvailableSources();

// Parses `path` and returns the bookmarks it contains. Only entries the
// navigation policy allows are returned, so an import can never introduce a
// bookmark the browser would refuse to open. Bounded in file size, nesting
// depth and entry count, because this file comes from outside the product.
std::vector<mtk::library::Entry> Read(const std::wstring& path,
                                     size_t* refused);

}  // namespace mtk::bookmark_import
