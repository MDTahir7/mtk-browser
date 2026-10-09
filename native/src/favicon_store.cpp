#include "favicon_store.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "builtin_favicons.h"
#include "preferences.h"

namespace mtk::favicon {
namespace {

using Microsoft::WRL::ComPtr;

std::mutex g_store_mutex;
std::unordered_map<std::string, std::string> g_cache;
bool g_initialized = false;

std::string CleanHost(std::string_view raw) {
  std::string host(raw);
  for (char& c : host) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (host.starts_with("www.")) host = host.substr(4);
  if (host.starts_with("m.")) host = host.substr(2);
  return host;
}

std::wstring FaviconDir() {
  return mtk::preferences::ApplicationDataDirectory() + L"\\favicons";
}

std::wstring HostToPath(const std::string& host) {
  std::string safe = host;
  for (char& c : safe) {
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') c = '_';
  }
  std::wstring wsafe(safe.begin(), safe.end());
  return FaviconDir() + L"\\" + wsafe + L".png";
}

std::string Base64Encode(const BYTE* data, DWORD size) {
  if (!data || size == 0) return {};
  DWORD encoded_len = 0;
  if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            nullptr, &encoded_len) || encoded_len == 0) {
    return {};
  }
  std::string b64(encoded_len, '\0');
  if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            b64.data(), &encoded_len)) {
    return {};
  }
  b64.resize(strnlen(b64.c_str(), b64.size()));
  return b64;
}

std::string BgraToPngDataUrl(const uint32_t* pixels, int w, int h) {
  if (!pixels || w <= 0 || h <= 0) return {};

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  ComPtr<IStream> stream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return {};

  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory)))) {
    return {};
  }

  ComPtr<IWICBitmapEncoder> encoder;
  if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return {};
  if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return {};

  ComPtr<IWICBitmapFrameEncode> frame;
  if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) return {};
  if (FAILED(frame->Initialize(nullptr))) return {};
  if (FAILED(frame->SetSize(w, h))) return {};

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (FAILED(frame->SetPixelFormat(&format))) return {};

  const UINT stride = static_cast<UINT>(w * 4);
  const UINT buffer_size = static_cast<UINT>(w * h * 4);
  if (FAILED(frame->WritePixels(h, stride, buffer_size,
                                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pixels))))) {
    return {};
  }

  if (FAILED(frame->Commit())) return {};
  if (FAILED(encoder->Commit())) return {};

  STATSTG stat{};
  if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return {};
  const ULONG bytes_len = static_cast<ULONG>(stat.cbSize.QuadPart);
  if (bytes_len == 0) return {};

  LARGE_INTEGER zero{};
  stream->Seek(zero, STREAM_SEEK_SET, nullptr);

  std::vector<BYTE> png_bytes(bytes_len);
  ULONG bytes_read = 0;
  if (FAILED(stream->Read(png_bytes.data(), bytes_len, &bytes_read)) || bytes_read != bytes_len) {
    return {};
  }

  const std::string b64 = Base64Encode(png_bytes.data(), bytes_len);
  if (b64.empty()) return {};
  return "data:image/png;base64," + b64;
}

std::string GenerateMonogramSvg(std::string_view host) {
  uint32_t hash = 5381;
  for (char c : host) hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
  const int hue1 = hash % 360;
  const int hue2 = (hue1 + 45) % 360;

  char initial = 'A';
  for (char c : host) {
    if (isalnum(static_cast<unsigned char>(c))) {
      initial = static_cast<char>(toupper(static_cast<unsigned char>(c)));
      break;
    }
  }

  char svg[512];
  snprintf(svg, sizeof(svg),
           "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
           "<defs><linearGradient id=\"g\" x1=\"0%%\" y1=\"0%%\" x2=\"100%%\" y2=\"100%%\">"
           "<stop offset=\"0%%\" stop-color=\"hsl(%d,75%%,52%%)\"/>"
           "<stop offset=\"100%%\" stop-color=\"hsl(%d,80%%,38%%)\"/>"
           "</linearGradient></defs>"
           "<rect width=\"32\" height=\"32\" rx=\"8\" fill=\"url(#g)\"/>"
           "<text x=\"16\" y=\"22\" font-family=\"'Segoe UI Variable','Segoe UI',sans-serif\" "
           "font-size=\"16\" font-weight=\"700\" fill=\"#fff\" text-anchor=\"middle\">%c</text>"
           "</svg>",
           hue1, hue2, initial);

  const std::string b64 = Base64Encode(reinterpret_cast<const BYTE*>(svg),
                                       static_cast<DWORD>(strlen(svg)));
  return "data:image/svg+xml;base64," + b64;
}

}  // namespace

void InitializeStore() {
  std::lock_guard lock(g_store_mutex);
  if (g_initialized) return;
  g_initialized = true;

  CreateDirectoryW(FaviconDir().c_str(), nullptr);
}

std::string GetFaviconDataUrl(std::string_view raw_host) {
  const std::string host = CleanHost(raw_host);
  if (host.empty()) return {};

  {
    std::lock_guard lock(g_store_mutex);
    auto it = g_cache.find(host);
    if (it != g_cache.end() && !it->second.empty()) {
      return it->second;
    }
  }

  // 1. Check built-in crisp brand vector icons
  std::string builtin = GetBuiltinFavicon(host);
  if (!builtin.empty()) {
    std::lock_guard lock(g_store_mutex);
    g_cache[host] = builtin;
    return builtin;
  }

  // 2. Check disk cache
  const std::wstring path = HostToPath(host);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (file.is_open()) {
    const std::streamoff size = file.tellg();
    if (size > 0 && size < 256 * 1024) {
      std::vector<BYTE> buffer(static_cast<size_t>(size));
      file.seekg(0);
      if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        const std::string b64 = Base64Encode(buffer.data(), static_cast<DWORD>(size));
        if (!b64.empty()) {
          const std::string data_url = "data:image/png;base64," + b64;
          std::lock_guard lock(g_store_mutex);
          g_cache[host] = data_url;
          return data_url;
        }
      }
    }
  }

  // 3. Fallback: dynamic stylized monogram badge
  const std::string monogram = GenerateMonogramSvg(host);
  {
    std::lock_guard lock(g_store_mutex);
    g_cache[host] = monogram;
  }
  return monogram;
}

void StoreDownloadedFavicon(const std::string& raw_host,
                            const uint32_t* src_pixels,
                            int width, int height) {
  const std::string host = CleanHost(raw_host);
  if (host.empty() || !src_pixels || width <= 0 || height <= 0) return;

  const std::string data_url = BgraToPngDataUrl(src_pixels, width, height);
  if (data_url.empty()) return;

  {
    std::lock_guard lock(g_store_mutex);
    g_cache[host] = data_url;
  }

  // Save raw PNG bytes to disk for persistent reload across sessions
  CreateDirectoryW(FaviconDir().c_str(), nullptr);
  const std::wstring path = HostToPath(host);

  // Decode the base64 part back to binary to save on disk
  const size_t comma = data_url.find(',');
  if (comma != std::string::npos) {
    const std::string b64 = data_url.substr(comma + 1);
    DWORD bin_len = 0;
    if (CryptStringToBinaryA(b64.data(), static_cast<DWORD>(b64.size()),
                             CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr, nullptr) && bin_len > 0) {
      std::vector<BYTE> bin(bin_len);
      if (CryptStringToBinaryA(b64.data(), static_cast<DWORD>(b64.size()),
                               CRYPT_STRING_BASE64, bin.data(), &bin_len, nullptr, nullptr)) {
        std::ofstream out(path, std::ios::binary);
        if (out.is_open()) {
          out.write(reinterpret_cast<const char*>(bin.data()), bin_len);
        }
      }
    }
  }
}

}  // namespace mtk::favicon
