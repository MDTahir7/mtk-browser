#pragma once

#include <cstdint>
#include <string>

namespace mtk::update {

// Current shipped version, compared against the manifest.
inline constexpr int kCurrentVersion[3] = {0, 3, 0};

// Address of the signed update manifest and its detached signature.
inline constexpr char kManifestUrl[] =
    "https://mdtahir7.github.io/mtk-browser-releases/update.json";
inline constexpr char kSignatureUrl[] =
    "https://mdtahir7.github.io/mtk-browser-releases/update.json.sig";

// Address of the signed security & ad/tracker filter feed manifest.
inline constexpr char kFilterManifestUrl[] =
    "https://mdtahir7.github.io/mtk-browser-releases/filters.json";
inline constexpr char kFilterSignatureUrl[] =
    "https://mdtahir7.github.io/mtk-browser-releases/filters.json.sig";

enum class Status {
  kUpToDate,
  kUpdateReady,
  kCheckFailed,
  kRejected,
  kDisabled,
};

struct Result {
  Status status = Status::kCheckFailed;
  std::wstring version;
  std::wstring detail;
  std::wstring installer_path;
};

struct FilterUpdateResult {
  Status status = Status::kCheckFailed;
  size_t rules_loaded = 0;
  std::wstring version;
  std::wstring detail;
};

// Verifies a detached base64-encoded ECDSA P-256 signature over `data` against
// either the primary or backup pinned public key.
bool VerifySignedPayload(const std::string& data,
                         const std::string& base64_signature);

// Validates a signed filter manifest + filter list payload in memory (offline
// testable) and returns true when signature, SHA-256 checksum, and version are
// valid.
bool ValidateSignedFilterBundle(const std::string& manifest_json,
                                const std::string& base64_signature,
                                const std::string& filter_payload);

// Performs a full browser update check.
Result Check();

// Fetches, verifies, atomically saves, and hot-reloads the signed security
// filter list (`mtk_filters.txt`). Falls back safely to the existing bundled
// rules if offline or if verification fails.
FilterUpdateResult CheckAndUpdateFilters();

// Launches a verified installer and returns true when the process started.
bool LaunchInstaller(const std::wstring& installer_path);

// Removes installers left behind by earlier runs.
void CleanupDownloads();

}  // namespace mtk::update
