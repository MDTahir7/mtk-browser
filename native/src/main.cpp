#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <string>

#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "browser_window.h"
#include "download_manager.h"
#include "filter_engine.h"
#include "library_store.h"
#include "mtk_app.h"
#include "preferences.h"
#include "password_store.h"
#include "profile.h"
#include "site_settings.h"
#include "strings.h"

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
  return mtk::preferences::UserDataDirectory();
}

// The single-instance guard exists because two browser processes cannot share
// one profile directory. Scope the mutex to that directory (like Chrome does)
// so a different profile can still run independently.
std::wstring SingletonName(const std::wstring& profile_path) {
  std::wstring name = L"MTKBrowser.SingleInstance.";
  uint64_t hash = 1469598103934665603ULL;
  for (const wchar_t character : profile_path) {
    hash ^= static_cast<uint64_t>(towlower(character));
    hash *= 1099511628211ULL;
  }
  wchar_t suffix[17]{};
  swprintf(suffix, 17, L"%016llx", static_cast<unsigned long long>(hash));
  name += suffix;
  return name;
}

// A second browser process cannot share the profile directory, which used to
// look like "the app does not start". Focus the running window instead.
//
// Windows of other profiles run in other processes, so the search is restricted
// to windows this process's own profile could own: FindWindow finds a window of
// the class in any process, and focusing another profile's window would be
// wrong. The mutex already told us a process for *this* profile is running, and
// only that process has a window whose title matches this profile.
bool FocusRunningInstance() {
  struct Search {
    std::wstring title_suffix;
    HWND found = nullptr;
  };
  Search search;
  search.title_suffix = mtk::profile::ActiveIsDefault()
                            ? std::wstring()
                            : L" [" + mtk::profile::ActiveId() + L"]";
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto* state = reinterpret_cast<Search*>(parameter);
        wchar_t class_name[64]{};
        if (GetClassNameW(window, class_name, 64) == 0) return TRUE;
        if (wcscmp(class_name, L"MTKBrowserWindow") != 0) return TRUE;
        wchar_t title[256]{};
        GetWindowTextW(window, title, 256);
        const std::wstring text(title);
        const bool matches =
            state->title_suffix.empty()
                ? text.find(L'[') == std::wstring::npos
                : text.size() >= state->title_suffix.size() &&
                      text.compare(text.size() - state->title_suffix.size(),
                                   state->title_suffix.size(),
                                   state->title_suffix) == 0;
        if (!matches) return TRUE;
        state->found = window;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&search));
  if (!search.found) return false;
  if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);
  SetForegroundWindow(search.found);
  return true;
}

// Per-monitor DPI awareness normally comes from the executable manifest. With
// the sandbox enabled the executable is CEF's bootstrap, so the build also
// stamps the manifest onto it; this call is a defensive fallback for the case
// where that manifest is missing, and is a no-op when it is present.
void EnsurePerMonitorDpiAwareness() {
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto set_context = reinterpret_cast<SetContext>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
      set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
  }
}

// Shared entry point. `sandbox_info` is supplied by CEF's bootstrap when the
// sandbox is enabled and is null for the unsandboxed debugging build.
int RunBrowser(HINSTANCE instance, int show_command, void* sandbox_info) {
  CefMainArgs main_args(instance);
  CefRefPtr<MtkApp> app = new MtkApp(instance, show_command);

  const int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
  if (exit_code >= 0) return exit_code;

  EnsurePerMonitorDpiAwareness();

  // The profile decides where every store reads from, so it is selected before
  // anything is loaded. An unusable id falls back to the default profile rather
  // than starting with a half-configured one.
  const std::wstring requested_profile =
      mtk::profile::IdFromCommandLine(GetCommandLineW());
  if (!mtk::profile::Initialize(requested_profile) &&
      !requested_profile.empty()) {
    MessageBoxW(nullptr,
                L"That profile name could not be used, so MTK Browser opened "
                L"the default profile instead.\n\nProfile names may contain "
                L"letters, digits, '-' and '_' only.",
                L"MTK Browser", MB_OK | MB_ICONWARNING);
  }

  // Only the browser process reaches this point, so the guard never affects
  // CEF's own renderer/utility child processes.
  const std::wstring profile_path = RootCachePath();
  HANDLE singleton =
      CreateMutexW(nullptr, FALSE, SingletonName(profile_path).c_str());
  if (!singleton) {
    MessageBoxW(nullptr,
                L"MTK Browser could not lock its profile. No profile data was "
                L"opened or changed.",
                L"MTK Browser startup error", MB_OK | MB_ICONERROR);
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
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

  // Load mutable profile stores only after this process owns the profile.
  // A second launch must never rewrite a running instance's active downloads.
  mtk::preferences::Store::Get().Load();
  mtk::library::Store::Get().Load();
  mtk::downloads::Manager::Get().Load();
  mtk::sites::Store::Get().Load();
  mtk::passwords::Store::Get().Load();
  // UI language follows the language preference, so it is read once the
  // preferences are loaded and before any window exists.
  mtk::strings::Refresh();

  // Load MTK Shields rules before any browser exists so every request is
  // filtered from the first navigation onward.
  mtk::filters::Engine::Get().LoadFromDirectory(ApplicationDirectory());

  CefSettings settings;
  // The sandbox is driven by the bootstrap-supplied sandbox info. It is only
  // disabled when this is the unsandboxed debugging build.
  settings.no_sandbox = sandbox_info == nullptr;
  settings.persist_session_cookies = false;
  CefString(&settings.root_cache_path) = profile_path;
  CefString(&settings.accept_language_list) =
      mtk::preferences::Store::Get().values().accept_language;

  if (!CefInitialize(main_args, settings, app, sandbox_info)) {
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

}  // namespace

#if defined(CEF_USE_BOOTSTRAP)

// Sandboxed packaging. CEF's bootstrap executable creates the sandbox
// information, loads this DLL and calls this export for every process, so the
// normal CefExecuteProcess/CefInitialize sequence still runs inside
// RunBrowser(). `version_info` describes the bootstrap that loaded us and is
// only needed when libcef.dll is resolved dynamically, which MTK Browser does
// not do: it ships libcef.dll beside the executable.
extern "C" __declspec(dllexport) int RunWinMain(HINSTANCE instance,
                                                LPTSTR /*command_line*/,
                                                int show_command,
                                                void* sandbox_info,
                                                cef_version_info_t*) {
  return RunBrowser(instance, show_command, sandbox_info);
}

#else

// Unsandboxed debugging build: a single executable with no bootstrap.
int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  return RunBrowser(instance, show_command, nullptr);
}

#endif  // defined(CEF_USE_BOOTSTRAP)
