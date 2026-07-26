#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mtk::downloads {

enum class State {
  kStarting,
  kInProgress,
  kPaused,
  kComplete,
  kCanceled,
  kFailed,
};

struct Item {
  // Live CEF downloads use their uint32_t download ID. Persisted history rows
  // are assigned IDs above UINT32_MAX so they can never collide after restart.
  uint64_t id = 0;
  std::wstring file_name;
  std::wstring full_path;
  std::string url;
  State state = State::kStarting;
  int64_t received_bytes = 0;
  int64_t total_bytes = 0;
  int percent = -1;
  std::wstring detail;
  // Saved through standard mode and able to execute code, so opening it always
  // asks first.
  bool risky = false;
  // The large-file warning is shown at most once per download.
  bool size_warned = false;
};

struct Decision {
  bool allow = false;
  std::wstring reason;
  // Sanitized, Windows-safe file name. Only valid when `allow` is true.
  std::wstring file_name;
  // Standard mode only: the file type can run code, so the user is warned
  // before it is saved and before it is ever opened.
  bool risky = false;
};

// Downloads bigger than this are warned about in safe mode, not blocked. No
// mode enforces a size cap.
constexpr int64_t kLargeDownloadBytes = 2LL * 1024 * 1024 * 1024;

// Download behaviour selected in Settings.
enum class Mode {
  // Allowlisted file types only, with a confirmation for every download.
  kSafe,
  // Mainstream browser behaviour: any file type saves straight to the download
  // folder, with a warning only for types that can execute code.
  kStandard,
};

// Reads the mode saved in preferences. Safe mode is the default.
Mode CurrentMode();

// Decides whether a download may proceed. In both modes the URL must be an
// allowed http(s) address under the active safety policy and the file name is
// sanitized. Safe mode additionally restricts the file type; standard mode only
// flags risky types.
Decision Evaluate(const std::string& url, const std::wstring& suggested_name,
                  const std::string& mime_type, Mode mode);

// True when the file name carries an extension that can execute code. Used for
// the standard-mode save warning and the "Open file" confirmation.
bool IsRiskyFileName(const std::wstring& file_name);

// "%USERPROFILE%\Downloads\MTK Browser", created on demand. Falls back to the
// application data directory when the known folder cannot be resolved.
std::wstring DownloadDirectory();

// Full target path inside DownloadDirectory() that does not overwrite an
// existing file. Returns an empty string when no free name is available.
std::wstring UniqueTargetPath(const std::wstring& file_name);

// Session list of downloads. All methods are called on the CEF browser-process
// UI thread, which is also the Win32 UI thread, so no locking is required.
class Manager final {
 public:
  static Manager& Get();

  void Load();
  bool Flush();
  // Inserts a new record, or updates the existing record with the same id.
  void Upsert(const Item& item);
  bool Remove(uint64_t id);
  size_t ClearFinished();
  size_t ActiveCount() const;

  const std::vector<Item>& items() const;

 private:
  std::vector<Item> items_;
  bool dirty_ = false;
};

// Human readable status line for the downloads UI.
std::wstring DescribeItem(const Item& item);

}  // namespace mtk::downloads
