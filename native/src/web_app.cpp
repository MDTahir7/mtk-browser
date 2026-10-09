#include "web_app.h"
#include "profile.h"
#include "site_settings.h"
#include "strings.h"
#include "resource.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <cstdio>
#include <algorithm>

namespace mtk::web_app {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring SanitizeAppName(std::wstring_view name) {
  std::wstring clean;
  clean.reserve(name.size());
  for (wchar_t c : name) {
    if (c == L'\\' || c == L'/' || c == L':' || c == L'*' ||
        c == L'?' || c == L'\"' || c == L'<' || c == L'>' || c == L'|') {
      clean.push_back(L' ');
    } else if (c >= 32) {
      clean.push_back(c);
    }
  }
  while (!clean.empty() && clean.front() == L' ') clean.erase(clean.begin());
  while (!clean.empty() && clean.back() == L' ') clean.pop_back();
  return clean;
}

struct InstallPromptContext {
  std::string url;
  std::wstring default_title;
  InstallOptions* options = nullptr;
};

INT_PTR CALLBACK InstallAppDlgProc(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* ctx = reinterpret_cast<InstallPromptContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

      SetWindowTextW(dialog, mtk::strings::Get(mtk::strings::Id::kInstallWebAppTitle));
      SetDlgItemTextW(dialog, IDC_INSTALL_APP_LABEL, mtk::strings::Get(mtk::strings::Id::kInstallWebApp));
      SetDlgItemTextW(dialog, IDC_INSTALL_APP_NAME_LABEL, mtk::strings::Get(mtk::strings::Id::kInstallPromptName));
      SetDlgItemTextW(dialog, IDC_INSTALL_DESKTOP_SHORTCUT, mtk::strings::Get(mtk::strings::Id::kCreateDesktopShortcut));
      SetDlgItemTextW(dialog, IDC_INSTALL_START_MENU, mtk::strings::Get(mtk::strings::Id::kCreateStartMenuShortcut));
      SetDlgItemTextW(dialog, IDOK, mtk::strings::Get(mtk::strings::Id::kInstallButton));
      SetDlgItemTextW(dialog, IDCANCEL, mtk::strings::Get(mtk::strings::Id::kCancel));

      std::wstring initial_name = SanitizeAppName(ctx ? ctx->default_title : L"");
      if (initial_name.empty() || initial_name == L"New Tab" || initial_name == L"MTK Browser") {
        const std::string origin = ctx ? mtk::sites::OriginOf(ctx->url) : "";
        std::string host = origin;
        if (host.starts_with("https://")) host = host.substr(8);
        const size_t colon = host.find(':');
        if (colon != std::string::npos) host = host.substr(0, colon);
        initial_name = Utf8ToWide(host);
      }
      SetDlgItemTextW(dialog, IDC_INSTALL_APP_NAME, initial_name.c_str());

      const std::string origin = ctx ? mtk::sites::OriginOf(ctx->url) : "";
      SetDlgItemTextW(dialog, IDC_INSTALL_APP_URL, Utf8ToWide(origin.empty() ? (ctx ? ctx->url : "") : origin).c_str());

      CheckDlgButton(dialog, IDC_INSTALL_DESKTOP_SHORTCUT,
                     (ctx && ctx->options && ctx->options->desktop_shortcut) ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_INSTALL_START_MENU,
                     (ctx && ctx->options && ctx->options->start_menu_shortcut) ? BST_CHECKED : BST_UNCHECKED);

      HWND name_edit = GetDlgItem(dialog, IDC_INSTALL_APP_NAME);
      SetFocus(name_edit);
      SendMessageW(name_edit, EM_SETSEL, 0, -1);
      return FALSE;
    }
    case WM_COMMAND: {
      const int control_id = LOWORD(w_param);
      auto* ctx = reinterpret_cast<InstallPromptContext*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
      if (!ctx || !ctx->options) return FALSE;

      if (control_id == IDOK) {
        wchar_t buf[256]{};
        GetDlgItemTextW(dialog, IDC_INSTALL_APP_NAME, buf, 256);
        std::wstring name = SanitizeAppName(buf);
        if (name.empty()) {
          name = ctx->default_title;
        }
        ctx->options->display_name = name;
        ctx->options->desktop_shortcut =
            (IsDlgButtonChecked(dialog, IDC_INSTALL_DESKTOP_SHORTCUT) == BST_CHECKED);
        ctx->options->start_menu_shortcut =
            (IsDlgButtonChecked(dialog, IDC_INSTALL_START_MENU) == BST_CHECKED);
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (control_id == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    }
    case WM_DESTROY: {
      SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
      return TRUE;
    }
  }
  return FALSE;
}

struct ManagerContext {
  std::vector<AppInfo> apps;
};

INT_PTR CALLBACK AppManagerDlgProc(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* ctx = new ManagerContext();
      SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
      SetWindowTextW(dialog, mtk::strings::Get(mtk::strings::Id::kManageWebApps));
      SetDlgItemTextW(dialog, IDC_WEB_APPS_HINT, mtk::strings::Get(mtk::strings::Id::kWebAppsHint));
      SetDlgItemTextW(dialog, IDC_WEB_APPS_OPEN, mtk::strings::Get(mtk::strings::Id::kOpenFile));
      SetDlgItemTextW(dialog, IDC_WEB_APPS_DESKTOP, mtk::strings::Get(mtk::strings::Id::kAddToDesktop));
      SetDlgItemTextW(dialog, IDC_WEB_APPS_REMOVE, mtk::strings::Get(mtk::strings::Id::kUninstallApp));
      SetDlgItemTextW(dialog, IDCANCEL, mtk::strings::Get(mtk::strings::Id::kClose));

      const auto reload_list = [dialog, ctx]() {
        ctx->apps = ListInstalledApps();
        HWND list = GetDlgItem(dialog, IDC_WEB_APPS_LIST);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        if (ctx->apps.empty()) {
          SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(No installed web apps)"));
          EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_OPEN), FALSE);
          EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_DESKTOP), FALSE);
          EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_REMOVE), FALSE);
          EnableWindow(list, FALSE);
        } else {
          EnableWindow(list, TRUE);
          for (const auto& app : ctx->apps) {
            std::wstring line = app.display_name;
            if (!app.origin.empty()) line += L" — " + Utf8ToWide(app.origin);
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
          }
          SendMessageW(list, LB_SETCURSEL, 0, 0);
          EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_OPEN), TRUE);
          EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_DESKTOP), TRUE);
          EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_REMOVE), TRUE);
        }
      };
      reload_list();
      return TRUE;
    }
    case WM_COMMAND: {
      const int control_id = LOWORD(w_param);
      const int code = HIWORD(w_param);
      auto* ctx = reinterpret_cast<ManagerContext*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
      if (!ctx) return FALSE;

      if (control_id == IDC_WEB_APPS_OPEN || (control_id == IDC_WEB_APPS_LIST && code == LBN_DBLCLK)) {
        HWND list = GetDlgItem(dialog, IDC_WEB_APPS_LIST);
        const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && static_cast<size_t>(sel) < ctx->apps.size()) {
          ShellExecuteW(dialog, L"open", ctx->apps[sel].shortcut_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          EndDialog(dialog, IDOK);
        }
        return TRUE;
      }
      if (control_id == IDC_WEB_APPS_DESKTOP) {
        HWND list = GetDlgItem(dialog, IDC_WEB_APPS_LIST);
        const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && static_cast<size_t>(sel) < ctx->apps.size()) {
          const auto& app = ctx->apps[sel];
          wchar_t executable[MAX_PATH]{};
          if (GetModuleFileNameW(nullptr, executable, MAX_PATH)) {
            const std::string url = app.origin.empty() ? ("https://" + WideToUtf8(app.display_name)) : app.origin;
            if (CreateDesktopShortcutForInstalledApp(executable, url, app.display_name)) {
              MessageBoxW(dialog, mtk::strings::Get(mtk::strings::Id::kShortcutCreated),
                          mtk::strings::Get(mtk::strings::Id::kManageWebApps),
                          MB_OK | MB_ICONINFORMATION);
            }
          }
        }
        return TRUE;
      }
      if (control_id == IDC_WEB_APPS_REMOVE) {
        HWND list = GetDlgItem(dialog, IDC_WEB_APPS_LIST);
        const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && static_cast<size_t>(sel) < ctx->apps.size()) {
          const auto& app = ctx->apps[sel];
          std::wstring prompt = std::wstring(mtk::strings::Get(mtk::strings::Id::kUninstallAppConfirm)) +
                                L"\n\n" + app.display_name;
          if (MessageBoxW(dialog, prompt.c_str(), mtk::strings::Get(mtk::strings::Id::kUninstallApp),
                          MB_YESNO | MB_ICONQUESTION) == IDYES) {
            RemoveInstalledApp(app.id);
            ctx->apps = ListInstalledApps();
            SendMessageW(list, LB_RESETCONTENT, 0, 0);
            if (ctx->apps.empty()) {
              SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(No installed web apps)"));
              EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_OPEN), FALSE);
              EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_DESKTOP), FALSE);
              EnableWindow(GetDlgItem(dialog, IDC_WEB_APPS_REMOVE), FALSE);
              EnableWindow(list, FALSE);
            } else {
              for (const auto& a : ctx->apps) {
                std::wstring line = a.display_name;
                if (!a.origin.empty()) line += L" — " + Utf8ToWide(a.origin);
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
              }
              const int new_sel = std::min(sel, static_cast<int>(ctx->apps.size() - 1));
              SendMessageW(list, LB_SETCURSEL, new_sel, 0);
            }
          }
        }
        return TRUE;
      }
      if (control_id == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    }
    case WM_DESTROY: {
      auto* ctx = reinterpret_cast<ManagerContext*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
      delete ctx;
      SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
      return TRUE;
    }
  }
  return FALSE;
}

}  // namespace

std::string IdForUrl(const std::string& url) {
  const std::string origin = mtk::sites::OriginOf(url);
  if (!origin.starts_with("https://")) return {};
  unsigned long long hash = 1469598103934665603ULL;
  for (unsigned char c : origin) { hash ^= c; hash *= 1099511628211ULL; }
  char id[24]{};
  std::snprintf(id, sizeof(id), "webapp-%016llx", hash);
  return id;
}

std::wstring QuoteCommandLineArgument(std::wstring_view argument) {
  // CommandLineToArgvW-compatible quoting: a quote is escaped by an odd run of
  // backslashes, and the backslashes immediately before the final quote are
  // doubled. This lets an ordinary URL remain one argument even if it contains
  // spaces, quotes, or a trailing backslash.
  std::wstring quoted;
  quoted.reserve(argument.size() + 2);
  quoted.push_back(L'\"');
  size_t backslashes = 0;
  for (const wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'\"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(L'\"');
    } else {
      quoted.append(backslashes, L'\\');
      quoted.push_back(character);
    }
    backslashes = 0;
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

std::wstring LaunchArguments(const std::string& url) {
  const std::string id = IdForUrl(url);
  const std::wstring wide_url = Utf8ToWide(url);
  if (id.empty() || wide_url.empty()) return {};
  return L"--profile=" + Utf8ToWide(id) + L" --app-mode " +
      QuoteCommandLineArgument(wide_url);
}

namespace {

bool CreateShortcutAt(const std::wstring& folder,
                      const std::wstring& file_name,
                      const std::wstring& executable,
                      const std::wstring& arguments,
                      const std::wstring& description) {
  const int dir_res = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
  if (dir_res != ERROR_SUCCESS && dir_res != ERROR_ALREADY_EXISTS) {
    return false;
  }
  const std::wstring path = folder + L"\\" + file_name + L".lnk";
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IShellLinkW* shortcut = nullptr;
  const HRESULT created = CoCreateInstance(CLSID_ShellLink, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shortcut));
  if (FAILED(created) || !shortcut) {
    if (SUCCEEDED(initialized)) CoUninitialize();
    return false;
  }
  HRESULT result = shortcut->SetPath(executable.c_str());
  if (SUCCEEDED(result)) result = shortcut->SetArguments(arguments.c_str());
  if (SUCCEEDED(result)) result = shortcut->SetDescription(description.c_str());
  if (SUCCEEDED(result)) result = shortcut->SetIconLocation(executable.c_str(), 0);
  IPersistFile* persisted = nullptr;
  if (SUCCEEDED(result)) result = shortcut->QueryInterface(IID_PPV_ARGS(&persisted));
  if (SUCCEEDED(result) && persisted) {
    result = persisted->Save(path.c_str(), TRUE);
    persisted->Release();
  }
  shortcut->Release();
  if (SUCCEEDED(initialized)) CoUninitialize();
  return SUCCEEDED(result);
}

}  // namespace

bool PromptInstallApp(HINSTANCE instance, HWND parent,
                      const std::string& url,
                      const std::wstring& default_title,
                      InstallOptions* out_options) {
  if (!out_options) return false;
  InstallPromptContext ctx;
  ctx.url = url;
  ctx.default_title = default_title;
  ctx.options = out_options;
  INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_INSTALL_WEB_APP),
                                   parent, InstallAppDlgProc,
                                   reinterpret_cast<LPARAM>(&ctx));
  return (result == IDOK);
}

bool CreateAppShortcuts(const std::wstring& executable,
                        const std::string& url,
                        const std::wstring& display_name,
                        bool desktop_shortcut,
                        bool start_menu_shortcut) {
  const std::string id = IdForUrl(url);
  const std::wstring arguments = LaunchArguments(url);
  if (executable.empty() || id.empty() || arguments.empty()) return false;

  std::wstring clean_name = SanitizeAppName(display_name);
  if (clean_name.empty() || clean_name == L"New Tab" || clean_name == L"MTK Browser") {
    const std::string origin = mtk::sites::OriginOf(url);
    std::string host = origin;
    if (host.starts_with("https://")) host = host.substr(8);
    const size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    clean_name = Utf8ToWide(host);
  }
  if (clean_name.empty()) clean_name = Utf8ToWide(id);

  mtk::profile::SetDisplayName(Utf8ToWide(id), clean_name);

  bool success = true;

  if (start_menu_shortcut) {
    PWSTR programs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs)) && programs) {
      const std::wstring folder = std::wstring(programs) + L"\\MTK Browser Apps";
      CoTaskMemFree(programs);
      const std::wstring legacy_path = folder + L"\\MTK Browser app - " + Utf8ToWide(id) + L".lnk";
      const std::wstring path = folder + L"\\" + clean_name + L".lnk";
      if (legacy_path != path) {
        DeleteFileW(legacy_path.c_str());
      }
      if (!CreateShortcutAt(folder, clean_name, executable, arguments, (clean_name + L" — MTK Browser"))) {
        success = false;
      }
    } else {
      success = false;
    }
  }

  if (desktop_shortcut) {
    PWSTR desktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) && desktop) {
      const std::wstring folder = desktop;
      CoTaskMemFree(desktop);
      if (!CreateShortcutAt(folder, clean_name, executable, arguments, (clean_name + L" — MTK Browser"))) {
        success = false;
      }
    } else {
      success = false;
    }
  }

  return success;
}

bool CreateStartMenuShortcut(const std::wstring& executable,
                             const std::string& url,
                             const std::wstring& display_name) {
  return CreateAppShortcuts(executable, url, display_name, false, true);
}

bool CreateDesktopShortcutForInstalledApp(const std::wstring& executable,
                                         const std::string& url,
                                         const std::wstring& display_name) {
  return CreateAppShortcuts(executable, url, display_name, true, false);
}

std::vector<AppInfo> ListInstalledApps() {
  std::vector<AppInfo> apps;
  PWSTR programs = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs)) || !programs) {
    return apps;
  }
  std::wstring folder = std::wstring(programs) + L"\\MTK Browser Apps";
  CoTaskMemFree(programs);
  WIN32_FIND_DATAW data{};
  HANDLE search = FindFirstFileW((folder + L"\\*.lnk").c_str(), &data);
  if (search == INVALID_HANDLE_VALUE) return apps;

  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IShellLinkW* shell_link = nullptr;
  IPersistFile* persist_file = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&shell_link))) && shell_link) {
    shell_link->QueryInterface(IID_PPV_ARGS(&persist_file));
  }

  do {
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      std::wstring filename = data.cFileName;
      if (filename.ends_with(L".lnk")) {
        std::wstring full_path = folder + L"\\" + filename;
        std::wstring name_without_ext = filename.substr(0, filename.size() - 4);
        constexpr std::wstring_view legacy_prefix = L"MTK Browser app - ";
        if (name_without_ext.starts_with(legacy_prefix)) {
          name_without_ext = name_without_ext.substr(legacy_prefix.size());
        }

        std::string found_id;
        std::string found_url;

        if (persist_file && SUCCEEDED(persist_file->Load(full_path.c_str(), STGM_READ))) {
          wchar_t args[1024]{};
          if (SUCCEEDED(shell_link->GetArguments(args, 1024))) {
            std::wstring args_str(args);
            size_t prof_pos = args_str.find(L"--profile=");
            if (prof_pos != std::wstring::npos) {
              size_t start = prof_pos + 10;
              size_t end = args_str.find(L" ", start);
              std::wstring id_w = args_str.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
              if (id_w.starts_with(L"\"") && id_w.ends_with(L"\"")) {
                id_w = id_w.substr(1, id_w.size() - 2);
              }
              const int size = WideCharToMultiByte(CP_UTF8, 0, id_w.data(), static_cast<int>(id_w.size()), nullptr, 0, nullptr, nullptr);
              found_id.assign(static_cast<size_t>(size), '\0');
              WideCharToMultiByte(CP_UTF8, 0, id_w.data(), static_cast<int>(id_w.size()), found_id.data(), size, nullptr, nullptr);
            }
            size_t app_pos = args_str.find(L"--app-mode");
            if (app_pos != std::wstring::npos) {
              std::wstring url_part = args_str.substr(app_pos + 10);
              while (!url_part.empty() && url_part.front() == L' ') url_part.erase(url_part.begin());
              if (url_part.starts_with(L"\"") && url_part.ends_with(L"\"")) {
                url_part = url_part.substr(1, url_part.size() - 2);
              }
              const int size = WideCharToMultiByte(CP_UTF8, 0, url_part.data(), static_cast<int>(url_part.size()), nullptr, 0, nullptr, nullptr);
              found_url.assign(static_cast<size_t>(size), '\0');
              WideCharToMultiByte(CP_UTF8, 0, url_part.data(), static_cast<int>(url_part.size()), found_url.data(), size, nullptr, nullptr);
            }
          }
        }

        if (found_id.empty() && name_without_ext.starts_with(L"webapp-")) {
          const int size = WideCharToMultiByte(CP_UTF8, 0, name_without_ext.data(), static_cast<int>(name_without_ext.size()), nullptr, 0, nullptr, nullptr);
          found_id.assign(static_cast<size_t>(size), '\0');
          WideCharToMultiByte(CP_UTF8, 0, name_without_ext.data(), static_cast<int>(name_without_ext.size()), found_id.data(), size, nullptr, nullptr);
        }

        if (!found_id.empty()) {
          AppInfo app;
          app.id = found_id;
          app.shortcut_path = full_path;
          std::wstring id_w = Utf8ToWide(found_id);
          app.display_name = mtk::profile::DisplayName(id_w);
          if (app.display_name.empty() || app.display_name == id_w) {
            app.display_name = name_without_ext;
          }
          if (!found_url.empty()) {
            app.origin = mtk::sites::OriginOf(found_url);
          }
          apps.push_back(std::move(app));
        }
      }
    }
  } while (FindNextFileW(search, &data));
  FindClose(search);

  if (persist_file) persist_file->Release();
  if (shell_link) shell_link->Release();
  if (SUCCEEDED(initialized)) CoUninitialize();
  return apps;
}

bool RemoveInstalledApp(const std::string& id) {
  if (id.empty()) return false;
  std::wstring id_w = Utf8ToWide(id);
  const auto apps = ListInstalledApps();
  bool deleted_any_shortcut = false;
  for (const auto& app : apps) {
    if (app.id == id) {
      if (DeleteFileW(app.shortcut_path.c_str())) {
        deleted_any_shortcut = true;
      }
      PWSTR desktop = nullptr;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) && desktop) {
        std::wstring desktop_link = std::wstring(desktop) + L"\\" + app.display_name + L".lnk";
        CoTaskMemFree(desktop);
        if (DeleteFileW(desktop_link.c_str())) {
          deleted_any_shortcut = true;
        }
      }
    }
  }
  PWSTR programs = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs)) && programs) {
    std::wstring legacy_shortcut = std::wstring(programs) + L"\\MTK Browser Apps\\MTK Browser app - " + id_w + L".lnk";
    CoTaskMemFree(programs);
    if (DeleteFileW(legacy_shortcut.c_str())) {
      deleted_any_shortcut = true;
    }
  }
  const bool profile_deleted = mtk::profile::Delete(id_w);
  return deleted_any_shortcut || profile_deleted;
}

void ShowAppManager(HINSTANCE instance, HWND parent) {
  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_WEB_APPS), parent, AppManagerDlgProc, 0);
}

}  // namespace mtk::web_app
