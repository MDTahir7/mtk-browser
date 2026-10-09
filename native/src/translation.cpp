#include "translation.h"

#include <cctype>

namespace mtk::translation {
namespace {

bool IsWebUrl(const std::string& url) {
  return url.starts_with("https://") || url.starts_with("http://");
}

std::string PercentEncode(const std::string& input) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(input.size() * 3);
  for (const unsigned char byte : input) {
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~') {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[byte >> 4]);
      encoded.push_back(kHex[byte & 0x0F]);
    }
  }
  return encoded;
}

}  // namespace

bool IsSupportedTarget(const std::string_view language) {
  return language == "hi" || language == "en";
}

std::string GoogleTranslateUrl(const std::string& source_url,
                               const std::string_view target_language) {
  if (!IsWebUrl(source_url) || !IsSupportedTarget(target_language)) return {};
  return "https://translate.google.com/translate?sl=auto&tl=" +
         std::string(target_language) + "&u=" + PercentEncode(source_url);
}

}  // namespace mtk::translation
