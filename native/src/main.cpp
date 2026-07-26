#include <windows.h>

#include <string>

#include "include/cef_app.h"
#if defined(MTK_ENABLE_CEF_SANDBOX)
#include "include/cef_sandbox_win.h"
#endif
#include "browser_window.h"
#include "filter_engine.h"
#include "mtk_app.h"

namespace {

// Directory that contains MTKBrowser.exe, used to locate the filter list.
std::string ApplicationDirectory() {
  wchar_t wide_path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, wide_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  std::wstring path(wide_path, length);
  const size_t separator = path.find_last_of(L'\\');
  if (separator == std::wstring::npos) return {};
  path.resize(separator);

  const int size = WideCharToMultiByte(CP_UTF8, 0, path.data(),
                                       static_cast<int>(path.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring RootCachePath() {
  std::wstring path(32767, L'\0');
  DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) {
    length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
  }
  if (length == 0 || length >= path.size()) return L"C:\\MTKBrowserData";
  path.resize(length);
  if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
  path += L"MTK Browser\\User Data";
  return path;
}

// A second browser process cannot share the profile directory, which used to
// look like "the app does not start". Focus the running window instead.
bool FocusRunningInstance() {
  HWND existing = FindWindowW(L"MTKBrowserWindow", nullptr);
  if (!existing) return false;
  if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
  SetForegroundWindow(existing);
  return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  CefMainArgs main_args(instance);
  CefRefPtr<MtkApp> app = new MtkApp(instance, show_command);
#if defined(MTK_ENABLE_CEF_SANDBOX)
  CefScopedSandboxInfo sandbox_info;
#endif

  const int exit_code = CefExecuteProcess(
      main_args, app,
#if defined(MTK_ENABLE_CEF_SANDBOX)
      sandbox_info
#else
      nullptr
#endif
  );
  if (exit_code >= 0) return exit_code;

  // Only the browser process reaches this point, so the guard never affects
  // CEF's own renderer/utility child processes.
  HANDLE singleton = CreateMutexW(nullptr, FALSE, L"MTKBrowser.SingleInstance");
  if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (!FocusRunningInstance()) {
      MessageBoxW(nullptr,
                  L"MTK Browser is already running.\n\n"
                  L"If no window is visible, close the existing "
                  L"MTKBrowser.exe processes and start the app again.",
                  L"MTK Browser", MB_OK | MB_ICONINFORMATION);
    }
    CloseHandle(singleton);
    return 0;
  }

  // Load MTK Shields rules before any browser exists so every request is
  // filtered from the first navigation onward.
  mtk::filters::Engine::Get().LoadFromDirectory(ApplicationDirectory());

  CefSettings settings;
#if !defined(MTK_ENABLE_CEF_SANDBOX)
  settings.no_sandbox = true;
#endif
  settings.persist_session_cookies = false;
  CefString(&settings.root_cache_path) = RootCachePath();
  CefString(&settings.accept_language_list) = "en-US,en,hi";

  if (!CefInitialize(
      main_args, settings, app,
#if defined(MTK_ENABLE_CEF_SANDBOX)
      sandbox_info
#else
      nullptr
#endif
  )) {
    MessageBoxW(nullptr,
                L"MTK Browser could not start its browser engine.\n\n"
                L"This usually means the CEF runtime files are missing next to "
                L"MTKBrowser.exe, or another copy is still running.",
                L"MTK Browser startup error", MB_OK | MB_ICONERROR);
    if (singleton) CloseHandle(singleton);
    return 1;
  }

  CefRunMessageLoop();
  CefShutdown();
  if (singleton) CloseHandle(singleton);
  return 0;
}
