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
#include <functional>
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

constexpr int64_t kLargeDownloadBytes = 2LL * 1024 * 1024 * 1024;

enum class Mode {
  kSafe,
  kStandard,
};

Mode CurrentMode();

Decision Evaluate(const std::string& url, const std::wstring& suggested_name,
                  const std::string& mime_type, Mode mode);

bool IsRiskyFileName(const std::wstring& file_name);

bool MatchesSearch(const Item& item, const std::wstring& query);

bool CanRetry(const Item& item);

// Returns true when an interrupted download has recorded partial progress
// and a valid web address, allowing HTTP Range resumption across restarts.
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

// Resumes an interrupted download across browser restarts using WinHTTP with
// `Range: bytes=<offset>-` and `If-Range`. Appends on HTTP 206 Partial Content
// or safely restarts from byte 0 on HTTP 200 OK.
bool ResumeDownloadWithRange(uint64_t id,
                             std::function<void()> on_progress = {});

std::wstring DownloadDirectory();

std::wstring UniqueTargetPath(const std::wstring& file_name);

bool PromptDownloadLocation(HWND owner, const std::wstring& suggested_name,
                            std::wstring* out_full_path,
                            std::wstring* out_file_name);

std::wstring SuggestPdfFileName(const std::wstring& page_title);

std::wstring SuggestScreenshotFileName(const std::wstring& page_title);

bool SaveBitmapToPng(HBITMAP hbitmap, const std::wstring& file_path);

bool CopyBitmapToClipboard(HWND owner, HBITMAP hbitmap);

class Manager final {
 public:
  static Manager& Get();

  void Load();
  bool Flush();
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

std::wstring DescribeItem(const Item& item);

bool ApplyMarkOfTheWeb(const std::wstring& full_path, const std::string& url);

}  // namespace mtk::downloads
