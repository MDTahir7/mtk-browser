#include "updater.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <array>
#include <cstdio>
#include <string_view>
#include <vector>

#include "preferences.h"
#include "update_public_key.h"

namespace mtk::update {
namespace {

// The manifest and signature are tiny; anything larger is a sign of trouble.
constexpr DWORD kMaxManifestBytes = 8 * 1024;
constexpr DWORD kMaxSignatureBytes = 512;
// Installer size ceiling. Generous, but bounded so a hostile server cannot fill
// the disk even before the hash is checked.
constexpr uint64_t kMaxInstallerBytes = 400ULL * 1024 * 1024;
constexpr size_t kSha256Bytes = 32;

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

// Minimal HTTPS GET. Plain http is impossible to request: the scheme is fixed
// and only https redirects are followed.
bool HttpsGet(const std::string& url, DWORD max_bytes, std::string* body,
              std::wstring* error) {
  body->clear();
  const std::wstring wide_url = Utf8ToWide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256]{};
  wchar_t path[2048]{};
  parts.lpszHostName = host;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts) ||
      parts.nScheme != INTERNET_SCHEME_HTTPS) {
    *error = L"The update address is not a valid https URL.";
    return false;
  }

  HINTERNET session = WinHttpOpen(L"MTKBrowser-Updater/0.3",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                  0);
  if (!session) {
    *error = L"Could not start the update request.";
    return false;
  }
  // Modern TLS only.
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                    WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                   sizeof(protocols));
  DWORD timeout = 20000;
  WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

  HINTERNET connection =
      WinHttpConnect(session, host, parts.nPort, 0);
  if (!connection) {
    WinHttpCloseHandle(session);
    *error = L"Could not reach the update server.";
    return false;
  }
  HINTERNET request = WinHttpOpenRequest(
      connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) {
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    *error = L"Could not create the update request.";
    return false;
  }
  // Certificate errors must never be ignored, so no security flags are relaxed
  // here. Redirects stay inside https because of this policy.
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  bool ok = false;
  if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
      *error = L"The update server returned status " +
               std::to_wstring(status) + L".";
    } else {
      ok = true;
      std::vector<char> chunk(16 * 1024);
      DWORD read = 0;
      while (WinHttpReadData(request, chunk.data(),
                             static_cast<DWORD>(chunk.size()), &read) &&
             read > 0) {
        if (body->size() + read > max_bytes) {
          *error = L"The update response was larger than expected.";
          ok = false;
          break;
        }
        body->append(chunk.data(), read);
      }
    }
  } else {
    *error = L"The update check could not connect.";
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok;
}

// Downloads to `target`, enforcing a byte ceiling. The file is not executed or
// marked as trusted anywhere in this function.
bool HttpsDownload(const std::string& url, const std::wstring& target,
                   uint64_t max_bytes, uint64_t* written, std::wstring* error) {
  *written = 0;
  const std::wstring wide_url = Utf8ToWide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256]{};
  wchar_t path[4096]{};
  parts.lpszHostName = host;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts) ||
      parts.nScheme != INTERNET_SCHEME_HTTPS) {
    *error = L"The download address is not a valid https URL.";
    return false;
  }

  HINTERNET session = WinHttpOpen(L"MTKBrowser-Updater/0.3",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                  0);
  if (!session) {
    *error = L"Could not start the download.";
    return false;
  }
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                    WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                   sizeof(protocols));
  DWORD timeout = 60000;
  WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

  HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
  HINTERNET request = connection
                          ? WinHttpOpenRequest(connection, L"GET", path, nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE)
                          : nullptr;
  if (!request) {
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    *error = L"Could not reach the download server.";
    return false;
  }
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  bool ok = false;
  HANDLE file = INVALID_HANDLE_VALUE;
  if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
      *error = L"The download server returned status " +
               std::to_wstring(status) + L".";
    } else {
      file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE) {
        *error = L"Could not create the download file.";
      } else {
        ok = true;
        std::vector<char> chunk(64 * 1024);
        DWORD read = 0;
        while (WinHttpReadData(request, chunk.data(),
                               static_cast<DWORD>(chunk.size()), &read) &&
               read > 0) {
          if (*written + read > max_bytes) {
            *error = L"The download was larger than the manifest declared.";
            ok = false;
            break;
          }
          DWORD put = 0;
          if (!WriteFile(file, chunk.data(), read, &put, nullptr) ||
              put != read) {
            *error = L"Writing the download failed.";
            ok = false;
            break;
          }
          *written += read;
        }
      }
    }
  } else {
    *error = L"The download could not connect.";
  }

  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  if (!ok) DeleteFileW(target.c_str());
  return ok;
}

// Verifies a raw ECDSA P-256 (r||s) signature over `data` using the compiled-in
// public key.
bool VerifySignature(const std::string& data,
                     const std::vector<unsigned char>& signature) {
  if (signature.size() != 64) return false;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) {
    return false;
  }
  BCRYPT_KEY_HANDLE key = nullptr;
  bool ok = BCRYPT_SUCCESS(BCryptImportKeyPair(
      algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
      const_cast<PUCHAR>(kUpdatePublicKey.data()),
      static_cast<ULONG>(kUpdatePublicKey.size()), 0));

  std::array<unsigned char, kSha256Bytes> digest{};
  if (ok) {
    BCRYPT_ALG_HANDLE hash_algorithm = nullptr;
    ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
        &hash_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    if (ok) {
      ok = BCRYPT_SUCCESS(BCryptHash(
          hash_algorithm, nullptr, 0,
          reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
          static_cast<ULONG>(data.size()), digest.data(),
          static_cast<ULONG>(digest.size())));
      BCryptCloseAlgorithmProvider(hash_algorithm, 0);
    }
  }
  if (ok) {
    ok = BCRYPT_SUCCESS(BCryptVerifySignature(
        key, nullptr, digest.data(), static_cast<ULONG>(digest.size()),
        const_cast<PUCHAR>(signature.data()),
        static_cast<ULONG>(signature.size()), 0));
  }
  if (key) BCryptDestroyKey(key);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return ok;
}

std::vector<unsigned char> DecodeBase64(const std::string& text) {
  DWORD size = 0;
  if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                            CRYPT_STRING_BASE64, nullptr, &size, nullptr,
                            nullptr)) {
    return {};
  }
  std::vector<unsigned char> bytes(size);
  if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                            CRYPT_STRING_BASE64, bytes.data(), &size, nullptr,
                            nullptr)) {
    return {};
  }
  bytes.resize(size);
  return bytes;
}

// Reads one string or number field out of the manifest. Deliberately small and
// strict: the manifest is our own fixed-shape document, not arbitrary JSON.
std::string Field(const std::string& json, const std::string& name) {
  const std::string key = "\"" + name + "\":";
  size_t at = json.find(key);
  if (at == std::string::npos) return {};
  at += key.size();
  while (at < json.size() && (json[at] == ' ' || json[at] == '\t')) ++at;
  if (at >= json.size()) return {};
  if (json[at] == '"') {
    const size_t end = json.find('"', at + 1);
    if (end == std::string::npos) return {};
    return json.substr(at + 1, end - at - 1);
  }
  const size_t end = json.find_first_of(",}", at);
  if (end == std::string::npos) return {};
  return json.substr(at, end - at);
}

bool ParseVersion(const std::string& text, int parts[3]) {
  return sscanf_s(text.c_str(), "%d.%d.%d", &parts[0], &parts[1], &parts[2]) == 3;
}

bool IsNewer(const int candidate[3]) {
  for (int index = 0; index < 3; ++index) {
    if (candidate[index] > kCurrentVersion[index]) return true;
    if (candidate[index] < kCurrentVersion[index]) return false;
  }
  return false;
}

bool IsLowerHex(const std::string& value, size_t length) {
  if (value.size() != length) return false;
  for (const char character : value) {
    const bool hex = (character >= '0' && character <= '9') ||
                     (character >= 'a' && character <= 'f');
    if (!hex) return false;
  }
  return true;
}

std::wstring DownloadDirectory() {
  const std::wstring directory =
      mtk::preferences::InstallationDataDirectory() + L"\\Updates";
  CreateDirectoryW(directory.c_str(), nullptr);
  return directory;
}

std::string FileSha256(const std::wstring& path, std::wstring* error) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *error = L"The downloaded file could not be read.";
    return {};
  }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
      BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0,
                                      0))) {
    std::vector<unsigned char> chunk(64 * 1024);
    DWORD read = 0;
    bool ok = true;
    while (ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                    nullptr) &&
           read > 0) {
      if (!BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(), read, 0))) {
        ok = false;
        break;
      }
    }
    std::array<unsigned char, kSha256Bytes> digest{};
    if (ok && BCRYPT_SUCCESS(BCryptFinishHash(
                  hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
      static constexpr char kDigits[] = "0123456789abcdef";
      for (const unsigned char byte : digest) {
        result.push_back(kDigits[byte >> 4]);
        result.push_back(kDigits[byte & 0x0F]);
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (result.empty()) *error = L"The downloaded file could not be hashed.";
  return result;
}

}  // namespace

Result Check() {
  Result result;
  if (!mtk::preferences::Store::Get().values().auto_update) {
    result.status = Status::kDisabled;
    result.detail = L"Update checks are switched off in Settings.";
    return result;
  }

  std::string manifest;
  std::wstring error;
  if (!HttpsGet(kManifestUrl, kMaxManifestBytes, &manifest, &error)) {
    result.status = Status::kCheckFailed;
    result.detail = error;
    return result;
  }
  std::string signature_text;
  if (!HttpsGet(kSignatureUrl, kMaxSignatureBytes, &signature_text, &error)) {
    result.status = Status::kCheckFailed;
    result.detail = error;
    return result;
  }

  // Verification happens before a single manifest field is trusted.
  if (!VerifySignature(manifest, DecodeBase64(signature_text))) {
    result.status = Status::kRejected;
    result.detail =
        L"The update information was not signed by MTK Browser, so it was "
        L"refused. Nothing was downloaded.";
    return result;
  }

  const std::string version_text = Field(manifest, "version");
  const std::string url = Field(manifest, "url");
  const std::string sha256 = Field(manifest, "sha256");
  const std::string size_text = Field(manifest, "size");
  int version[3]{};
  if (!ParseVersion(version_text, version) || !IsLowerHex(sha256, 64) ||
      !url.starts_with("https://")) {
    result.status = Status::kRejected;
    result.detail = L"The signed update information was not in the expected "
                    L"form, so it was refused.";
    return result;
  }
  // A version that is not strictly newer is ignored, which also blocks a
  // downgrade to an older build.
  if (!IsNewer(version)) {
    result.status = Status::kUpToDate;
    result.detail = L"MTK Browser is up to date.";
    return result;
  }

  uint64_t declared_size = 0;
  if (!size_text.empty()) {
    declared_size = _strtoui64(size_text.c_str(), nullptr, 10);
  }
  const uint64_t ceiling =
      (declared_size > 0 && declared_size <= kMaxInstallerBytes)
          ? declared_size
          : kMaxInstallerBytes;

  const std::wstring wide_version = Utf8ToWide(version_text);
  const std::wstring target =
      DownloadDirectory() + L"\\MTK-Browser-Setup-" + wide_version + L".exe";
  uint64_t written = 0;
  if (!HttpsDownload(url, target, ceiling, &written, &error)) {
    result.status = Status::kCheckFailed;
    result.detail = error;
    return result;
  }
  if (declared_size > 0 && written != declared_size) {
    DeleteFileW(target.c_str());
    result.status = Status::kRejected;
    result.detail = L"The downloaded update did not match its declared size, "
                    L"so it was deleted.";
    return result;
  }

  const std::string actual = FileSha256(target, &error);
  if (actual.empty()) {
    DeleteFileW(target.c_str());
    result.status = Status::kCheckFailed;
    result.detail = error;
    return result;
  }
  if (actual != sha256) {
    DeleteFileW(target.c_str());
    result.status = Status::kRejected;
    result.detail = L"The downloaded update did not match its signed checksum, "
                    L"so it was deleted.";
    return result;
  }

  result.status = Status::kUpdateReady;
  result.version = wide_version;
  result.installer_path = target;
  result.detail = L"MTK Browser " + wide_version + L" is ready to install.";
  return result;
}

bool LaunchInstaller(const std::wstring& installer_path) {
  if (installer_path.empty()) return false;
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOASYNC;
  info.lpVerb = L"open";
  info.lpFile = installer_path.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

void CleanupDownloads() {
  const std::wstring directory = DownloadDirectory();
  WIN32_FIND_DATAW found{};
  HANDLE search = FindFirstFileW((directory + L"\\*.exe").c_str(), &found);
  if (search == INVALID_HANDLE_VALUE) return;
  do {
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      DeleteFileW((directory + L"\\" + found.cFileName).c_str());
    }
  } while (FindNextFileW(search, &found));
  FindClose(search);
}

}  // namespace mtk::update
