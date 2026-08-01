#include "icon_strip.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

namespace mtk::icons {
namespace {

using Microsoft::WRL::ComPtr;

// A toolbar icon strip is small by design. This ceiling stops a swapped file
// from turning into a large allocation before it is rejected.
constexpr UINT kMaxStripWidth = 4096;
constexpr UINT kMaxFrameSize = 256;

std::wstring ApplicationDirectory() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  std::wstring result(path, length);
  const size_t separator = result.find_last_of(L'\\');
  if (separator == std::wstring::npos) return {};
  result.resize(separator);
  return result;
}

// WIC is reached through COM, and the calling thread is not guaranteed to have
// initialised it: the browser's UI thread has, a plain test host has not. This
// keeps the loader self-sufficient either way. Initialisation is intentionally
// never undone, because other callers on this thread may still hold COM objects.
void EnsureComForThread() {
  static thread_local bool attempted = false;
  if (attempted) return;
  attempted = true;
  const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  // S_FALSE means it was already initialised, and RPC_E_CHANGED_MODE means
  // another apartment model is in force; both are usable for WIC.
  (void)result;
}

// WIC is part of Windows, so decoding adds no third-party dependency. The
// factory is created per load, which happens once per strip.
ComPtr<IWICImagingFactory> CreateFactory() {
  EnsureComForThread();
  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory)))) {
    return nullptr;
  }
  return factory;
}

}  // namespace

Strip::~Strip() { Release(); }

void Strip::Release() {
  for (const auto& entry : cache_) {
    if (entry.bitmap) DeleteObject(entry.bitmap);
  }
  cache_.clear();
}

bool Strip::Load(const std::wstring& file_name) {
  Release();
  frame_count_ = 0;
  source_size_ = 0;
  pixels_.clear();

  const std::wstring directory = ApplicationDirectory();
  if (directory.empty() || file_name.empty()) return false;
  const std::wstring path = directory + L"\\" + file_name;

  ComPtr<IWICImagingFactory> factory = CreateFactory();
  if (!factory) return false;

  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr,
                                                GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand,
                                                &decoder))) {
    return false;
  }
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, &frame))) return false;

  UINT width = 0;
  UINT height = 0;
  if (FAILED(frame->GetSize(&width, &height))) return false;
  // Square frames laid out in a row: the height is the frame size, and the width
  // must be a whole number of frames.
  if (height == 0 || height > kMaxFrameSize || width == 0 ||
      width > kMaxStripWidth || width % height != 0) {
    return false;
  }

  ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory->CreateFormatConverter(&converter))) return false;
  if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return false;
  }

  const UINT stride = width * 4;
  pixels_.assign(static_cast<size_t>(stride) * height, 0);
  if (FAILED(converter->CopyPixels(nullptr, stride,
                                   static_cast<UINT>(pixels_.size()),
                                   pixels_.data()))) {
    pixels_.clear();
    return false;
  }

  source_size_ = static_cast<int>(height);
  frame_count_ = static_cast<int>(width / height);
  return true;
}

HBITMAP Strip::ScaledStrip(int size) {
  for (const auto& entry : cache_) {
    if (entry.size == size) return entry.bitmap;
  }
  if (!valid() || size <= 0) return nullptr;

  ComPtr<IWICImagingFactory> factory = CreateFactory();
  if (!factory) return nullptr;

  const UINT source_width = static_cast<UINT>(source_size_ * frame_count_);
  ComPtr<IWICBitmap> source;
  if (FAILED(factory->CreateBitmapFromMemory(
          source_width, static_cast<UINT>(source_size_),
          GUID_WICPixelFormat32bppPBGRA, source_width * 4,
          static_cast<UINT>(pixels_.size()), pixels_.data(), &source))) {
    return nullptr;
  }

  const UINT target_width = static_cast<UINT>(size * frame_count_);
  ComPtr<IWICBitmapScaler> scaler;
  if (FAILED(factory->CreateBitmapScaler(&scaler))) return nullptr;
  // Fant resampling is what keeps a downscaled 3D render from looking crunchy at
  // toolbar size.
  if (FAILED(scaler->Initialize(source.Get(), target_width,
                                static_cast<UINT>(size),
                                WICBitmapInterpolationModeFant))) {
    return nullptr;
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = static_cast<LONG>(target_width);
  // Negative height keeps the DIB top-down, matching WIC's row order.
  info.bmiHeader.biHeight = -static_cast<LONG>(size);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits,
                                    nullptr, 0);
  if (!bitmap || !bits) {
    if (bitmap) DeleteObject(bitmap);
    return nullptr;
  }
  const UINT stride = target_width * 4;
  if (FAILED(scaler->CopyPixels(nullptr, stride, stride * size,
                                static_cast<BYTE*>(bits)))) {
    DeleteObject(bitmap);
    return nullptr;
  }

  cache_.push_back({size, bitmap});
  return bitmap;
}

bool Strip::Draw(HDC dc, const RECT& target, int frame) {
  if (!valid() || !dc) return false;
  const int box = std::min<int>(target.right - target.left,
                                target.bottom - target.top);
  if (box <= 0) return false;

  HBITMAP bitmap = ScaledStrip(box);
  if (!bitmap) return false;

  const int index = frame_count_ > 0 ? ((frame % frame_count_) + frame_count_) %
                                           frame_count_
                                     : 0;
  HDC source = CreateCompatibleDC(dc);
  if (!source) return false;
  HBITMAP previous = static_cast<HBITMAP>(SelectObject(source, bitmap));

  const int x = target.left + (target.right - target.left - box) / 2;
  const int y = target.top + (target.bottom - target.top - box) / 2;
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  const bool drawn = AlphaBlend(dc, x, y, box, box, source, index * box, 0, box,
                                box, blend) != FALSE;

  SelectObject(source, previous);
  DeleteDC(source);
  return drawn;
}

}  // namespace mtk::icons
