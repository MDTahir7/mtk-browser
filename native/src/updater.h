#pragma once

#include <cstdint>
#include <string>

namespace mtk::update {

// Current shipped version, compared against the manifest.
inline constexpr int kCurrentVersion[3] = {0, 3, 0};

// Address of the signed update manifest and its detached signature. Both are
// fetched over HTTPS with certificate validation; the signature is what makes
// the update trustworthy, not the host.
inline constexpr char kManifestUrl[] =
    "https://mdtahir7.github.io/mtk-browser-releases/update.json";
inline constexpr char kSignatureUrl[] =
    "https://mdtahir7.github.io/mtk-browser-releases/update.json.sig";

enum class Status {
  // No newer version is published.
  kUpToDate,
  // A newer version is available and has been verified end to end.
  kUpdateReady,
  // The check could not complete (offline, server error, bad manifest).
  kCheckFailed,
  // A manifest was served but failed verification. Treated as hostile.
  kRejected,
  // Updates are switched off in Settings.
  kDisabled,
};

struct Result {
  Status status = Status::kCheckFailed;
  std::wstring version;
  // Human readable detail for the UI. Never contains a URL supplied by the
  // server, so a hostile manifest cannot craft the message the user sees.
  std::wstring detail;
  // Downloaded installer, only set when status is kUpdateReady.
  std::wstring installer_path;
};

// Performs a full check: fetch manifest, verify signature, compare versions,
// download the installer and verify its SHA-256. Blocking, so it must be called
// on a background thread. Nothing is executed by this function.
Result Check();

// Launches a verified installer and returns true when the process started. The
// caller is expected to close the browser afterwards.
bool LaunchInstaller(const std::wstring& installer_path);

// Removes installers left behind by earlier runs.
void CleanupDownloads();

}  // namespace mtk::update
