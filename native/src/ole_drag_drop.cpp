#include "ole_drag_drop.h"

#include <ole2.h>
#include <shlobj.h>

#include <cstring>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace mtk::dragdrop {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

class SimpleDropSource final : public IDropSource {
 public:
  SimpleDropSource() = default;

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *ppv = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return InterlockedIncrement(&ref_count_);
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) delete this;
    return count;
  }

  STDMETHODIMP QueryContinueDrag(BOOL escape_pressed,
                                 DWORD key_state) override {
    if (escape_pressed) return DRAGDROP_S_CANCEL;
    if ((key_state & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
    return S_OK;
  }

  STDMETHODIMP GiveFeedback(DWORD) override {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }

 private:
  ~SimpleDropSource() = default;
  ULONG ref_count_ = 1;
};

class SimpleEnumFormatEtc final : public IEnumFORMATETC {
 public:
  explicit SimpleEnumFormatEtc(const FORMATETC& fmt) : fmt_(fmt) {}

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
      *ppv = static_cast<IEnumFORMATETC*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return InterlockedIncrement(&ref_count_);
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) delete this;
    return count;
  }

  STDMETHODIMP Next(ULONG celt, FORMATETC* rgelt,
                    ULONG* pcelt_fetched) override {
    if (!rgelt) return E_POINTER;
    if (index_ == 0 && celt >= 1) {
      rgelt[0] = fmt_;
      index_ = 1;
      if (pcelt_fetched) *pcelt_fetched = 1;
      return S_OK;
    }
    if (pcelt_fetched) *pcelt_fetched = 0;
    return S_FALSE;
  }

  STDMETHODIMP Skip(ULONG celt) override {
    index_ += celt;
    return index_ <= 1 ? S_OK : S_FALSE;
  }

  STDMETHODIMP Reset() override {
    index_ = 0;
    return S_OK;
  }

  STDMETHODIMP Clone(IEnumFORMATETC** ppenum) override {
    if (!ppenum) return E_POINTER;
    auto* copy = new SimpleEnumFormatEtc(fmt_);
    copy->index_ = index_;
    *ppenum = copy;
    return S_OK;
  }

 private:
  ~SimpleEnumFormatEtc() = default;
  FORMATETC fmt_{};
  ULONG index_ = 0;
  ULONG ref_count_ = 1;
};

class SimpleDataObject final : public IDataObject {
 public:
  SimpleDataObject(CLIPFORMAT format, std::vector<unsigned char> payload)
      : format_(format), payload_(std::move(payload)) {}

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *ppv = static_cast<IDataObject*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return InterlockedIncrement(&ref_count_);
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) delete this;
    return count;
  }

  STDMETHODIMP GetData(FORMATETC* fmt, STGMEDIUM* medium) override {
    if (!fmt || !medium) return E_POINTER;
    if (fmt->cfFormat != format_ || (fmt->tymed & TYMED_HGLOBAL) == 0) {
      return DV_E_FORMATETC;
    }
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, payload_.size());
    if (!global) return E_OUTOFMEMORY;
    void* locked = GlobalLock(global);
    if (!locked) {
      GlobalFree(global);
      return E_OUTOFMEMORY;
    }
    std::memcpy(locked, payload_.data(), payload_.size());
    GlobalUnlock(global);

    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    medium->pUnkForRelease = nullptr;
    return S_OK;
  }

  STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP QueryGetData(FORMATETC* fmt) override {
    if (!fmt) return E_POINTER;
    if (fmt->cfFormat == format_ && (fmt->tymed & TYMED_HGLOBAL) != 0) {
      return S_OK;
    }
    return DV_E_FORMATETC;
  }

  STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
    if (!out) return E_POINTER;
    out->ptd = nullptr;
    return E_NOTIMPL;
  }

  STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumFormatEtc(DWORD direction,
                             IEnumFORMATETC** ppenum) override {
    if (!ppenum) return E_POINTER;
    if (direction != DATADIR_GET) return E_NOTIMPL;
    FORMATETC fmt{};
    fmt.cfFormat = format_;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;
    *ppenum = new SimpleEnumFormatEtc(fmt);
    return S_OK;
  }

  STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

  STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }

  STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

 private:
  ~SimpleDataObject() = default;
  CLIPFORMAT format_ = 0;
  std::vector<unsigned char> payload_;
  ULONG ref_count_ = 1;
};

}  // namespace

bool BuildHdropPayload(const std::wstring& file_path,
                       std::vector<unsigned char>* out_bytes) {
  if (!out_bytes || file_path.empty() || file_path.size() > 32767) return false;
  // DROPFILES header + wide path + two terminating null wchar_ts.
  const size_t path_bytes = (file_path.size() + 2) * sizeof(wchar_t);
  const size_t total_bytes = sizeof(DROPFILES) + path_bytes;
  out_bytes->assign(total_bytes, 0);

  auto* drop = reinterpret_cast<DROPFILES*>(out_bytes->data());
  drop->pFiles = sizeof(DROPFILES);
  drop->fWide = TRUE;

  auto* dest = reinterpret_cast<wchar_t*>(out_bytes->data() + sizeof(DROPFILES));
  std::memcpy(dest, file_path.data(), file_path.size() * sizeof(wchar_t));
  dest[file_path.size()] = L'\0';
  dest[file_path.size() + 1] = L'\0';
  return true;
}

bool StartFileDrag(const std::wstring& file_path) {
  if (file_path.empty() ||
      GetFileAttributesW(file_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  std::vector<unsigned char> payload;
  if (!BuildHdropPayload(file_path, &payload)) return false;

  const HRESULT ole_hr = OleInitialize(nullptr);
  const bool should_uninit = SUCCEEDED(ole_hr);

  auto* data_object = new SimpleDataObject(CF_HDROP, std::move(payload));
  auto* drop_source = new SimpleDropSource();
  DWORD effect = DROPEFFECT_NONE;
  const HRESULT hr = DoDragDrop(data_object, drop_source,
                                DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
  drop_source->Release();
  data_object->Release();
  if (should_uninit) OleUninitialize();
  return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}

bool StartBookmarkDrag(const std::string& url, const std::wstring& title) {
  if (url.empty()) return false;
  const std::wstring wide = Utf8ToWide(url);
  if (wide.empty()) return false;
  (void)title;

  std::vector<unsigned char> payload((wide.size() + 1) * sizeof(wchar_t), 0);
  std::memcpy(payload.data(), wide.data(), wide.size() * sizeof(wchar_t));

  const HRESULT ole_hr = OleInitialize(nullptr);
  const bool should_uninit = SUCCEEDED(ole_hr);

  auto* data_object = new SimpleDataObject(CF_UNICODETEXT, std::move(payload));
  auto* drop_source = new SimpleDropSource();
  DWORD effect = DROPEFFECT_NONE;
  const HRESULT hr = DoDragDrop(data_object, drop_source,
                                DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
  drop_source->Release();
  data_object->Release();
  if (should_uninit) OleUninitialize();
  return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}

}  // namespace mtk::dragdrop
