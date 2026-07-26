#pragma once

#include <string>

namespace mtk::preferences {

// How downloads are handled. Safe mode is the default and only saves
// allowlisted file types; standard mode behaves like a mainstream browser.
enum class DownloadMode {
  kSafe,
  kStandard,
};

enum class StartupMode {
  kHomePage,
  kContinueSession,
};

struct Values {
  std::wstring home_page;
  std::string accept_language;
  DownloadMode download_mode = DownloadMode::kSafe;
  StartupMode startup_mode = StartupMode::kHomePage;
};

// MTK-owned data is kept separate from CEF's User Data directory.
std::wstring ApplicationDataDirectory();
std::wstring UserDataDirectory();

class Store final {
 public:
  static Store& Get();

  void Load();
  const Values& values() const;
  bool Save(const Values& values);

 private:
  Values values_;
};

}  // namespace mtk::preferences
