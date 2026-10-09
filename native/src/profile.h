#pragma once

#include <string>
#include <vector>

namespace mtk::profile {

// A profile is one set of browsing data: preferences, bookmarks and history,
// downloads, saved session and the Chromium cache. Each profile runs in its own
// process, selected with --profile=<id> on the command line, because the stores
// are process-wide singletons and Chromium holds one lock per cache directory.
//
// The default profile deliberately keeps using the original data directory, so
// an existing installation keeps its data without any migration step.

inline constexpr wchar_t kDefaultId[] = L"Default";

// Ids come from the command line and are used to build a path, so they are
// restricted to ASCII letters, digits, '-' and '_', with a bounded length. That
// rules out separators, drive letters, '..' and reserved device names.
constexpr size_t kMaxIdLength = 32;
bool IsValidId(const std::wstring& id);

constexpr size_t kMaxDisplayNameLength = 64;
bool IsValidDisplayName(const std::wstring& name);

// Selects the active profile for this process. An empty, invalid or unknown id
// falls back to the default profile. Returns false when `id` was rejected, in
// which case the default profile is active. Must be called before any store is
// read, and never again afterwards.
bool Initialize(const std::wstring& id);

// Active profile id, `kDefaultId` when none was selected.
const std::wstring& ActiveId();
bool ActiveIsDefault();

// Directory holding the active profile's data. This is what
// preferences::ApplicationDataDirectory() returns.
std::wstring ActiveDirectory();

// Every profile that exists on disk, default first.
std::vector<std::wstring> List();

// Creates the directory for `id`. Returns false when the id is invalid or the
// directory could not be created.
bool Create(const std::wstring& id);

std::wstring DisplayName(const std::wstring& id);
bool SetDisplayName(const std::wstring& id, const std::wstring& name);

// Moves an inactive, non-default profile directory to the Windows Recycle Bin.
// The active profile is never eligible, so an open process cannot lose the
// files it is currently using.
bool Delete(const std::wstring& id);

// Reads --profile=<id> out of a command line. Returns an empty string when the
// switch is absent.
std::wstring IdFromCommandLine(const std::wstring& command_line);

// Points the profile root somewhere else for the current process only, and an
// empty path restores the real one.
//
// This exists because the real root comes from the Windows known folder rather
// than from an environment variable, which is deliberate: an environment
// variable can be set by anything that starts the browser, and profile paths
// decide where credentials and browsing data live. The consequence is that a
// test cannot redirect the stores by setting LOCALAPPDATA, so without this hook
// the logic tests would read and write the real profile. Nothing in the shipping
// browser calls it.
void SetRootForTesting(const std::wstring& root);

}  // namespace mtk::profile
