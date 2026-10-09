#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::session {

constexpr size_t kMaxRestoredTabs = 20;
constexpr size_t kMaxRestoredWindows = 8;

// A normal (non-private) browser window saved for crash recovery. Bounds are
// workspace coordinates; a zero width/height means that no usable placement
// was captured and the regular Windows cascade should be used instead.
struct Window {
  std::vector<std::string> urls;
  std::vector<bool> pinned;
  size_t active_index = 0;
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
  bool maximized = false;
  // UTF-8 labels parallel to urls; absent entries mean ungrouped.
  std::vector<std::string> groups;
  // UTF-8 workspace labels parallel to urls; empty means default workspace.
  std::vector<std::string> workspaces;
  size_t split_index = static_cast<size_t>(-1);
  int split_ratio = 50;
};

struct Snapshot {
  std::vector<std::string> urls;
  // Parallel to `urls`: which of the restored tabs was pinned. Sized to match
  // `urls` exactly, so callers never need to bounds-check separately.
  std::vector<bool> pinned;
  size_t active_index = 0;
  size_t split_index = static_cast<size_t>(-1);
  int split_ratio = 50;
  // MTK_SESSION_V3 additionally records every normal window. The original
  // fields mirror windows[0] for callers still restoring a single window.
  std::vector<Window> windows;
};

// Loads only policy-approved http(s) URLs from the bounded session file. Also
// reads the older MTK_SESSION_V1 format (no pinned state) for files written
// before pinning was persisted; every tab from such a file restores unpinned.
Snapshot Load();
// Atomically replaces the last-session snapshot after validating every URL.
// `pinned` must be the same size as `urls`, or be empty to mean "none pinned".
bool Save(const std::vector<std::string>& urls, size_t active_index,
          const std::vector<bool>& pinned = {});
// Atomically saves all normal browser windows. Unsafe URLs are discarded and
// the total tab count is bounded across the whole session.
bool SaveWindows(const std::vector<Window>& windows);
// Removes the snapshot when "Open home page" is selected.
bool Clear();

}  // namespace mtk::session
