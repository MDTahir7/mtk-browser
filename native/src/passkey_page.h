#pragma once

#include <string>
#include <string_view>
#include "windows_hello.h"

namespace mtk::passkeys {

inline constexpr char kScheme[] = "mtk";
inline constexpr char kHost[] = "passkeys";
inline constexpr char kUrl[] = "mtk://passkeys/";
inline constexpr wchar_t kUrlWide[] = L"mtk://passkeys/";

// Internal Content-Security-Policy for mtk://passkeys/
// Allows self-contained inline script/styles and data: images.
// Strictly prevents external network calls (connect-src 'none').
inline constexpr char kPasskeyCsp[] =
    "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
    "img-src data:; form-action 'self'; base-uri 'none'; object-src 'none'; "
    "frame-src 'none'; connect-src 'none'; font-src 'none';";

enum class Action {
  kNone,
  kTestHello,
};

// True for mtk://passkeys/ with optional trailing slash or query parameters.
bool IsPasskeyUrl(std::string_view url);

// Intercepts allowed actions like mtk://passkeys/action/test-hello.
Action ParseActionUrl(std::string_view url);

// Generates the self-contained diagnostic HTML document.
std::string BuildHtml(const hello::Result& hello_status,
                      bool light_appearance = false,
                      bool hindi = false);

}  // namespace mtk::passkeys
