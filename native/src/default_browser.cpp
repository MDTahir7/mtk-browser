#include "default_browser.h"

#include <shlwapi.h>
#include <shellapi.h>

#include <string>

namespace mtk::default_browser {
namespace {

constexpr wchar_t kProgId[] = L"MTKBrowserHTML";
constexpr wchar_t kApplicationName[] = L"MTK Browser";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\MTK Browser\\Capabilities";

std::wstring ExecutablePath() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  return std::wstring(path, length);
}

bool WriteString(HKEY root, const std::wstring& subkey, const wchar_t* name,
                 const std::wstring& value) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                      nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return false;
  }
  const DWORD bytes =
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  const bool ok =
      RegSetValueExW(key, name, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(value.c_str()), bytes) ==
      ERROR_SUCCESS;
  RegCloseKey(key);
  return ok;
}

}  // namespace

bool Register() {
  const std::wstring executable = ExecutablePath();
  if (executable.empty()) return false;
  const std::wstring quoted = L"\"" + executable + L"\"";
  const std::wstring open_command = quoted + L" \"%1\"";

  bool ok = true;
  // The ProgID: what Windows shows and what it runs.
  ok &= WriteString(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\") + kProgId,
                    nullptr, L"MTK Browser HTML Document");
  ok &= WriteString(HKEY_CURRENT_USER,
                    std::wstring(L"Software\\Classes\\") + kProgId +
                        L"\\DefaultIcon",
                    nullptr, quoted + L",0");
  ok &= WriteString(HKEY_CURRENT_USER,
                    std::wstring(L"Software\\Classes\\") + kProgId +
                        L"\\shell\\open\\command",
                    nullptr, open_command);
  // Chromium-style: declares that this ProgID understands being handed a URL.
  ok &= WriteString(HKEY_CURRENT_USER,
                    std::wstring(L"Software\\Classes\\") + kProgId,
                    L"AppUserModelId", L"MTK.Browser");

  // Capabilities, which is what the Default apps page reads.
  ok &= WriteString(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationName",
                    kApplicationName);
  ok &= WriteString(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationIcon",
                    quoted + L",0");
  ok &= WriteString(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationDescription",
                    L"Privacy-first family-safe browser");
  for (const wchar_t* scheme : {L"http", L"https"}) {
    ok &= WriteString(HKEY_CURRENT_USER,
                      std::wstring(kCapabilitiesKey) + L"\\URLAssociations",
                      scheme, kProgId);
  }
  for (const wchar_t* extension : {L".htm", L".html", L".shtml", L".xht",
                                   L".xhtml", L".svg"}) {
    ok &= WriteString(HKEY_CURRENT_USER,
                      std::wstring(kCapabilitiesKey) + L"\\FileAssociations",
                      extension, kProgId);
  }

  // Listing the capabilities key is what makes the app appear as a candidate.
  ok &= WriteString(HKEY_CURRENT_USER,
                    L"Software\\RegisteredApplications", kApplicationName,
                    kCapabilitiesKey);

  // App Paths, so "MTKBrowser.exe" resolves from the Run dialog as well.
  ok &= WriteString(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\App "
                    L"Paths\\MTKBrowser.exe",
                    nullptr, executable);
  return ok;
}

bool IsDefault() {
  wchar_t handler[256]{};
  DWORD size = static_cast<DWORD>(std::size(handler));
  // The user choice for a scheme is what actually decides, so that is what is
  // read rather than the class registration.
  if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"https", L"open",
                        handler, &size) != S_OK) {
    return false;
  }
  const std::wstring current(handler);
  const std::wstring expected = ExecutablePath();
  if (current.empty() || expected.empty()) return false;
  return _wcsicmp(current.c_str(), expected.c_str()) == 0;
}

bool PromptUserToChoose(HWND owner) {
  // Windows 10 and 11 removed the programmatic path on purpose. The honest thing
  // is to send the user to the page where the choice is actually made.
  const auto open = [owner](const wchar_t* target) {
    SHELLEXECUTEINFOW info{sizeof(SHELLEXECUTEINFOW)};
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = target;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;
  };
  if (open(L"ms-settings:defaultapps?registeredAppMachine=MTK%20Browser")) {
    return true;
  }
  if (open(L"ms-settings:defaultapps")) return true;
  return open(L"control.exe /name Microsoft.DefaultPrograms");
}

}  // namespace mtk::default_browser
