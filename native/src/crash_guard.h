#pragma once

#include <string>

namespace mtk::crash {

// Detects an unclean shutdown so tabs can be offered back, and keeps a small
// local record of when it happened.
//
// The mechanism is a marker file in the profile directory. It is written when
// the browser starts and deleted when it exits normally, so finding one at
// startup means the previous run did not get to finish. Nothing is sent
// anywhere: the record is a few lines on this machine.

// Called once at startup, after the profile is selected and before any window
// exists. Returns true when the previous run ended uncleanly. Writes the marker
// for this run either way.
bool BeginSession();

// Called on a normal exit. Removes the marker, so the next start does not think
// this run crashed.
void EndSessionCleanly();

// True when BeginSession() found a marker from a previous run.
bool LastExitWasUnclean();

// Local record of unclean exits, newest first, for support questions like "does
// this keep happening?". Bounded.
std::wstring RecentCrashReport();

// Keeps `history` bounded when `entry` is prepended. Exposed for tests: it is
// the only part of this file with logic worth pinning down.
std::string TrimHistory(const std::string& history, const std::string& entry,
                        size_t max_entries);

}  // namespace mtk::crash
