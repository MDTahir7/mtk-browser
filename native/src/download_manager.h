#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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
  // Resumption metadata across process restarts.
  std::string etag;
  std::string last_modified;
  std::wstring partial_path;
  bool can_resume = false;
  // True when the download originated in a Private Window. Private downloads
  // are never written to the on-disk downloads history and are evicted when
  // the private session ends.
  bool is_private = false;
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

// Case-insensitive local search for the full Downloads view. It never makes a
// network request and matches the visible file name, disk path, source address
// and current status detail.
bool MatchesSearch(const Item& item, const std::wstring& query);

// A retry always starts a fresh request. It is available only for an attempt
// that ended unsuccessfully; a completed or currently active item cannot be
// accidentally downloaded again.
bool CanRetry(const Item& item);

// Returns true when an interrupted download has recorded partial progress
// and a valid web address, allowing HTTP Range resumption.
bool CanResume(const Item& item);

// Computes the partial file path (e.g. "<full_path>.mtkpart") used during transfer.
std::wstring PartialFilePath(const std::wstring& full_path);

// Generates the HTTP Range header value string for resuming, e.g. "bytes=12345-".
std::string BuildRangeHeader(int64_t received_bytes);

// Generates the HTTP If-Range header value from ETag or Last-Modified timestamp.
std::string BuildIfRangeHeader(const std::string& etag,
                               const std::string& last_modified);

// Reads the byte length of any existing partial download file on disk.
int64_t GetExistingPartialBytes(const std::wstring& full_path);

// "%USERPROFILE%\Downloads\MTK Browser", created on demand. Falls back to the
// application data directory when the known folder cannot be resolved.
std::wstring DownloadDirectory();

// Full target path inside DownloadDirectory() that does not overwrite an
// existing file. Returns an empty string when no free name is available.
std::wstring UniqueTargetPath(const std::wstring& file_name);

// Prompts the user with a standard Windows "Save As" file dialog to select the
// target folder and file name. Returns true if the user clicked Save, or false
// if the user canceled.
bool PromptDownloadLocation(HWND owner, const std::wstring& suggested_name,
                            std::wstring* out_full_path,
                            std::wstring* out_file_name);

// Generates a sanitized, Windows-safe filename for offline PDF export from a page title.
std::wstring SuggestPdfFileName(const std::wstring& page_title);

// Generates a sanitized, timestamped filename for webpage screenshot PNG export.
std::wstring SuggestScreenshotFileName(const std::wstring& page_title);

// Saves an HBITMAP to a lossless PNG file using Windows Imaging Component (WIC).
bool SaveBitmapToPng(HBITMAP hbitmap, const std::wstring& file_path);

// Copies an HBITMAP to the Windows clipboard as CF_BITMAP.
bool CopyBitmapToClipboard(HWND owner, HBITMAP hbitmap);

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
  size_t ClearPrivateDownloads();
  size_t ActiveCount() const;

  const std::vector<Item>& items() const;
  Item* FindItem(uint64_t id);

 private:
  std::vector<Item> items_;
  bool dirty_ = false;
};

// Human readable status line for the downloads UI.
std::wstring DescribeItem(const Item& item);

// Writes the Windows "mark of the web" onto a finished download, recording that
// the file came from the internet zone. Windows, Defender/SmartScreen and
// Office then treat it as untrusted: execution warns, and Office documents open
// in Protected View. Returns false when the marker could not be written, which
// happens on file systems without alternate data stream support.
//
// This marks the file; it does not itself run an antivirus scan.
bool ApplyMarkOfTheWeb(const std::wstring& full_path, const std::string& url);

}  // namespace mtk::downloads
