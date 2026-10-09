#pragma once

#include <string>
#include <string_view>

namespace mtk::translation {

// The embedded browser has no cloud translation credential. These supported
// targets deliberately use the public Google Translate web flow instead of
// pretending that text is translated locally. The caller must obtain explicit
// user approval before opening the returned URL.
bool IsSupportedTarget(std::string_view language);

// Returns an empty string for a non-web source URL or unsupported target.
// The source address is percent-encoded as one query parameter so a URL with
// '&', Unicode, or a fragment cannot change the translation request.
std::string GoogleTranslateUrl(const std::string& source_url,
                               std::string_view target_language);

}  // namespace mtk::translation
