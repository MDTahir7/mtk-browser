#pragma once

#include <string>

namespace mtk::preferences {

struct Values {
  std::wstring home_page;
  std::string accept_language;
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
