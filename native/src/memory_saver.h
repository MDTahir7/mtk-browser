#pragma once

#include <cstdint>

namespace mtk::memory {

// Inactive threshold before a background tab can be discarded automatically.
// 30 minutes gives ample time for users switching back and forth.
constexpr uint64_t kInactiveDiscardThresholdMs = 30 * 60 * 1000;

struct TabState {
  bool active = false;
  bool split = false;
  bool pinned = false;
  bool loading = false;
  bool camera_active = false;
  bool microphone_active = false;
  bool discarded = false;
  uint64_t last_active_tick = 0;
};

inline bool IsEligibleForDiscard(const TabState& tab, uint64_t current_tick,
                                 uint64_t threshold_ms = kInactiveDiscardThresholdMs) {
  // Active and split-view tabs are actively visible to the user.
  if (tab.active || tab.split) return false;
  // Pinned tabs are designated as permanent/frequent by user preference.
  if (tab.pinned) return false;
  // Actively loading pages must finish before being considered.
  if (tab.loading) return false;
  // Live media captures (audio/video calls) must never be interrupted.
  if (tab.camera_active || tab.microphone_active) return false;
  // Cannot discard an already discarded tab.
  if (tab.discarded) return false;
  // If a threshold is requested, check inactive duration.
  if (threshold_ms > 0 && tab.last_active_tick > 0) {
    if ((current_tick - tab.last_active_tick) < threshold_ms) {
      return false;
    }
  }
  return true;
}

}  // namespace mtk::memory
