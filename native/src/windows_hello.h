#pragma once

#include <string>

namespace mtk::hello {

// The browser only receives the result of Windows' secure consent UI. It never
// sees a Windows password, face scan or fingerprint template.
struct Result {
  bool available = false;
  bool verified = false;
  std::wstring detail;
};

// Checks whether Windows Hello can be used for the current user without
// displaying a prompt.
Result CheckAvailability();

// Shows Windows' secure verification UI. `purpose` is displayed by Windows to
// explain why the gesture is being requested.
Result Verify(const std::wstring& purpose);

}  // namespace mtk::hello
