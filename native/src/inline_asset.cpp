#include "inline_asset.h"

#include <windows.h>
#include <wincrypt.h>

#include <fstream>
#include <map>
#include <mutex>

namespace mtk::assets {
namespace {

std::wstring ModuleDirectory() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  std::wstring result(path, length);
  const size_t separator = result.find_last_of(L'\\');
  if (separator == std::wstring::npos) return {};
  result.resize(separator);
  return result;
}

std::string Base64(const std::string& bytes) {
  DWORD encoded_length = 0;
  if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()),
                            static_cast<DWORD>(bytes.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr,
                            &encoded_length) ||
      encoded_length == 0) {
    return {};
  }
  std::string encoded(encoded_length, '\0');
  if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()),
                            static_cast<DWORD>(bytes.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            encoded.data(), &encoded_length)) {
    return {};
  }
  encoded.resize(strnlen(encoded.c_str(), encoded.size()));
  return encoded;
}

std::string Load(const std::wstring& file_name) {
  const std::wstring directory = ModuleDirectory();
  if (directory.empty()) return {};
  std::ifstream file(directory + L'\\' + file_name,
                     std::ios::binary | std::ios::ate);
  if (!file.is_open()) return {};
  const std::streamoff size = file.tellg();
  if (size <= 0 || static_cast<size_t>(size) > kMaxInlineWebmBytes) return {};
  std::string bytes(static_cast<size_t>(size), '\0');
  file.seekg(0);
  if (!file.read(bytes.data(), size)) return {};
  // WebM files start with the EBML magic number; a mismatch means the asset was
  // replaced with something else, so the caller falls back to its glyph.
  if (bytes.compare(0, 4, "\x1A\x45\xDF\xA3", 4) != 0) return {};

  const std::string encoded = Base64(bytes);
  if (encoded.empty()) return {};

  // The payload is spliced into a URL, so the one base64 character that some
  // URL parsers treat specially is escaped. '/' and '=' are safe in a path.
  std::string url(kWebmDataUrlPrefix);
  url.reserve(url.size() + encoded.size() + encoded.size() / 32);
  for (const char character : encoded) {
    if (character == '+') {
      url += "%2B";
    } else {
      url += character;
    }
  }
  return url;
}

}  // namespace

const std::string& WebmDataUrl(const std::wstring& file_name) {
  // Assets are requested from the CEF UI thread today, but the cache outlives
  // any single call and the lock keeps that assumption from becoming a latent
  // data race if a future caller runs elsewhere.
  static std::mutex mutex;
  static std::map<std::wstring, std::string> cache;
  const std::lock_guard<std::mutex> guard(mutex);
  const auto existing = cache.find(file_name);
  if (existing != cache.end()) return existing->second;
  return cache.emplace(file_name, Load(file_name)).first->second;
}

}  // namespace mtk::assets
