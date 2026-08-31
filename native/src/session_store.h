#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::session {

constexpr size_t kMaxRestoredTabs = 20;

struct Snapshot {
  std::vector<std::string> urls;
  // Parallel to `urls`: which of the restored tabs was pinned. Sized to match
  // `urls` exactly, so callers never need to bounds-check separately.
  std::vector<bool> pinned;
  size_t active_index = 0;
};

// Loads only policy-approved http(s) URLs from the bounded session file. Also
// reads the older MTK_SESSION_V1 format (no pinned state) for files written
// before pinning was persisted; every tab from such a file restores unpinned.
Snapshot Load();
// Atomically replaces the last-session snapshot after validating every URL.
// `pinned` must be the same size as `urls`, or be empty to mean "none pinned".
bool Save(const std::vector<std::string>& urls, size_t active_index,
          const std::vector<bool>& pinned = {});
// Removes the snapshot when "Open home page" is selected.
bool Clear();

}  // namespace mtk::session
