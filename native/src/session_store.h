#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mtk::session {

constexpr size_t kMaxRestoredTabs = 20;

struct Snapshot {
  std::vector<std::string> urls;
  size_t active_index = 0;
};

// Loads only policy-approved http(s) URLs from the bounded session file.
Snapshot Load();
// Atomically replaces the last-session snapshot after validating every URL.
bool Save(const std::vector<std::string>& urls, size_t active_index);
// Removes the snapshot when "Open home page" is selected.
bool Clear();

}  // namespace mtk::session
