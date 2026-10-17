#include "browser_window_internal.h"

namespace {

// Reads a password field without leaving the PIN in a long-lived buffer.
std::wstring TakePinField(HWND dialog, int control) {
  wchar_t buffer[65]{};
  const int length = GetDlgItemTextW(dialog, control, buffer,
                                     static_cast<int>(std::size(buffer)));
  std::wstring pin(buffer, static_cast<size_t>(std::max(0, length)));
  SecureZeroMemory(buffer, sizeof(buffer));
  SetDlgItemTextW(dialog, control, L"");
  return pin;
}

void ScrubPin(std::wstring* pin) {
  if (pin && !pin->empty()) {
    SecureZeroMemory(pin->data(), pin->size() * sizeof(wchar_t));
    pin->clear();
  }
}

struct PinPromptContext {
  const wchar_t* message = nullptr;
  // Wrong attempts are counted per prompt and slowed down, so the short PIN
  // cannot be brute-forced by rapid retries.
  int attempts = 0;
};

INT_PTR CALLBACK PinPromptDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_PIN_PROMPT_LABEL, Text::kParentPin},
          {IDOK, Text::kUnlock},
          {IDCANCEL, Text::kCancel},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kParentPinTitle);
      mtk::dialog_text::Apply(dialog, kText);
      auto* context = reinterpret_cast<PinPromptContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA,
                        static_cast<LONG_PTR>(l_param));
      SetDlgItemTextW(dialog, IDC_PIN_PROMPT_TEXT,
                      context && context->message
                          ? context->message
                          : mtk::strings::Get(Text::kEnterParentPin));
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* context = reinterpret_cast<PinPromptContext*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        std::wstring pin = TakePinField(dialog, IDC_PIN_PROMPT_VALUE);
        const bool correct = mtk::preferences::VerifyPin(pin);
        ScrubPin(&pin);
        if (correct) {
          EndDialog(dialog, IDOK);
          return TRUE;
        }
        if (context) ++context->attempts;
        const int attempts = context ? context->attempts : 1;
        if (attempts >= 3) Sleep(1500);
        MessageBoxW(dialog, L"That PIN is not correct.", L"Parent PIN",
                    MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_PIN_PROMPT_VALUE));
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

INT_PTR CALLBACK UserFiltersDialogProc(HWND dialog, UINT message,
                                      WPARAM w_param, LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_FILTERS_LINE1, Text::kFiltersLine1},
          {IDC_FILTERS_LINE2, Text::kFiltersLine2},
          {IDC_FILTERS_NOTE, Text::kFiltersNote},
          {IDOK, Text::kSave},
          {IDCANCEL, Text::kCancel},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kUserFiltersTitle);
      mtk::dialog_text::Apply(dialog, kText);
      std::wstring text;
      for (const std::wstring& line : mtk::user_filters::Lines()) {
        text += line;
        text += L"\r\n";
      }
      SetDlgItemTextW(dialog, IDC_FILTERS_TEXT, text.c_str());
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        HWND field = GetDlgItem(dialog, IDC_FILTERS_TEXT);
        const int length = GetWindowTextLengthW(field);
        // Bounded by the store's own limits; this is only the transfer buffer.
        const int capacity = std::min(length, 256 * 1024);
        std::wstring text(static_cast<size_t>(capacity) + 1, L'\0');
        GetWindowTextW(field, text.data(), capacity + 1);
        text.resize(static_cast<size_t>(capacity));

        std::vector<std::wstring> lines;
        size_t offset = 0;
        while (offset <= text.size()) {
          const size_t end = text.find(L'\n', offset);
          std::wstring line = text.substr(
              offset, (end == std::wstring::npos ? text.size() : end) - offset);
          if (!line.empty() && line.back() == L'\r') line.pop_back();
          // Trailing blank lines are dropped rather than stored as empty rules.
          if (!line.empty() || end != std::wstring::npos) {
            lines.push_back(std::move(line));
          }
          if (end == std::wstring::npos) break;
          offset = end + 1;
        }
        while (!lines.empty() && lines.back().empty()) lines.pop_back();

        std::wstring error;
        if (!mtk::user_filters::Save(lines, &error)) {
          MessageBoxW(dialog,
                      error.empty() ? L"Those rules could not be saved."
                                    : error.c_str(),
                      L"Your filter rules", MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

void PopulatePasswordList(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_PASSWORDS_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  const auto& credentials = mtk::passwords::Store::Get().credentials();
  for (size_t index = 0; index < credentials.size(); ++index) {
    const std::wstring row = Utf8ToWide(credentials[index].origin) + L"  —  " +
                             credentials[index].user_name;
    const LRESULT added =
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    if (added != LB_ERR && added != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(added), index);
    }
  }
  const bool any = !credentials.empty();
  for (const int control : {IDC_PASSWORDS_REVEAL, IDC_PASSWORDS_COPY,
                            IDC_PASSWORDS_REMOVE, IDC_PASSWORDS_CLEAR}) {
    EnableWindow(GetDlgItem(dialog, control), any ? TRUE : FALSE);
  }
  if (any) SendMessageW(list, LB_SETCURSEL, 0, 0);
  SetDlgItemTextW(dialog, IDC_PASSWORDS_NOTE,
                  mtk::preferences::PinIsSet()
                      ? L"The parent PIN was required to open this list."
                      : L"No parent PIN is set, so anyone using this account can "
                        L"open this list. Set one in Settings.");
}

// Index of the selected credential, or -1.
ptrdiff_t SelectedCredentialIndex(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_PASSWORDS_LIST);
  const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR) return -1;
  const LRESULT data =
      SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
  if (data == LB_ERR) return -1;
  const auto& credentials = mtk::passwords::Store::Get().credentials();
  return static_cast<size_t>(data) < credentials.size()
             ? static_cast<ptrdiff_t>(data)
             : -1;
}

// Puts one password on the clipboard. Never the whole list, and never as a file.
bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) return false;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  bool ok = false;
  if (memory) {
    if (void* target = GlobalLock(memory)) {
      memcpy(target, text.c_str(), bytes);
      GlobalUnlock(memory);
      ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    }
    if (!ok) GlobalFree(memory);
  }
  CloseClipboard();
  return ok;
}

INT_PTR CALLBACK PasswordsDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_PASSWORDS_INTRO, Text::kPasswordsIntro},
          {IDC_PASSWORDS_REVEAL, Text::kShowPassword},
          {IDC_PASSWORDS_COPY, Text::kCopyPassword},
          {IDC_PASSWORDS_REMOVE, Text::kRemove},
          {IDC_PASSWORDS_CLEAR, Text::kRemoveAll},
          {IDCANCEL, Text::kClose},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kPasswordsTitle);
      mtk::dialog_text::Apply(dialog, kText);
      PopulatePasswordList(dialog);
      return TRUE;
    }
    case WM_COMMAND: {
      const WORD command = LOWORD(w_param);
      if (command == IDCANCEL) {
        EndDialog(dialog, 0);
        return TRUE;
      }
      if (command == IDC_PASSWORDS_CLEAR) {
        if (MessageBoxW(dialog,
                        L"Remove every saved password?\n\nThey cannot be "
                        L"recovered afterwards.",
                        L"Saved passwords", MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        auto& store = mtk::passwords::Store::Get();
        if (!store.Clear() || !store.Flush()) {
          MessageBoxW(dialog, L"MTK Browser could not remove the saved passwords.",
                      L"Saved passwords", MB_OK | MB_ICONERROR);
        }
        PopulatePasswordList(dialog);
        return TRUE;
      }

      const ptrdiff_t selected = SelectedCredentialIndex(dialog);
      if (selected < 0) return TRUE;
      // A copy is taken because the list rebuilds after any change.
      const mtk::passwords::Credential credential =
          mtk::passwords::Store::Get().credentials()[static_cast<size_t>(selected)];

      if (command == IDC_PASSWORDS_REMOVE) {
        auto& store = mtk::passwords::Store::Get();
        if (!store.Remove(credential.origin, credential.user_name) ||
            !store.Flush()) {
          MessageBoxW(dialog, L"That password could not be removed.",
                      L"Saved passwords", MB_OK | MB_ICONERROR);
        }
        PopulatePasswordList(dialog);
        return TRUE;
      }
      if (command == IDC_PASSWORDS_REVEAL || command == IDC_PASSWORDS_COPY) {
        std::wstring password;
        if (!mtk::passwords::Store::Get().Reveal(credential.origin,
                                                credential.user_name, &password)) {
          MessageBoxW(dialog,
                      L"That password could not be decrypted. It was saved by a "
                      L"different Windows account or a different profile.",
                      L"Saved passwords", MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        if (command == IDC_PASSWORDS_COPY) {
          const bool copied = CopyTextToClipboard(dialog, password);
          // The plaintext is wiped from this process even though the clipboard
          // now holds a copy, which the person chose.
          mtk::passwords::Scrub(&password);
          MessageBoxW(dialog,
                      copied ? L"Password copied. Clear your clipboard when you "
                               L"are done."
                             : L"The password could not be copied.",
                      L"Saved passwords",
                      MB_OK | (copied ? MB_ICONINFORMATION : MB_ICONWARNING));
          return TRUE;
        }
        std::wstring shown = Utf8ToWide(credential.origin) + L"\n" +
                             credential.user_name + L"\n\n" + password;
        mtk::passwords::Scrub(&password);
        MessageBoxW(dialog, shown.c_str(), L"Saved password",
                    MB_OK | MB_ICONINFORMATION);
        mtk::passwords::Scrub(&shown);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, 0);
      return TRUE;
  }
  return FALSE;
}

struct SiteSettingsContext {
  mtk::sites::Rule rule;
  // Set when the dialog asked for the site's stored rule to be dropped.
  bool forget = false;
};

// Permission combo boxes all offer the same three choices in the same order.
void FillPermissionCombo(HWND dialog, int control, mtk::sites::Permission value) {
  HWND combo = GetDlgItem(dialog, control);
  if (!combo) return;
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (const Text id : {Text::kPermissionBlockedDefault, Text::kPermissionAllowed,
                        Text::kPermissionBlockedNeverAsk}) {
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(mtk::strings::Get(id)));
  }
  const int selection = value == mtk::sites::Permission::kAllow
                            ? 1
                            : (value == mtk::sites::Permission::kBlock ? 2 : 0);
  SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
  SetWindowTheme(combo, L"Explorer", nullptr);
}

mtk::sites::Permission PermissionFromCombo(HWND dialog, int control) {
  const LRESULT selection =
      SendMessageW(GetDlgItem(dialog, control), CB_GETCURSEL, 0, 0);
  if (selection == 1) return mtk::sites::Permission::kAllow;
  if (selection == 2) return mtk::sites::Permission::kBlock;
  return mtk::sites::Permission::kAsk;
}

INT_PTR CALLBACK SiteSettingsDialogProc(HWND dialog, UINT message,
                                       WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* context = reinterpret_cast<SiteSettingsContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      if (!context) return TRUE;
      SetDlgItemTextW(dialog, IDC_SITE_ORIGIN,
                      Utf8ToWide(context->rule.origin).c_str());
      {
        static constexpr mtk::dialog_text::Item kText[] = {
            {IDC_SITE_FOR_LABEL, Text::kSettingsForLabel},
            {IDC_SITE_SCOPE_NOTE, Text::kSiteScopeNote},
            {IDC_SITE_CAMERA_LABEL, Text::kCamera},
            {IDC_SITE_MICROPHONE_LABEL, Text::kMicrophone},
            {IDC_SITE_LOCATION_LABEL, Text::kLocation},
            {IDC_SITE_NOTIFICATIONS_LABEL, Text::kNotifications},
            {IDC_SITE_AD_FILTERING, Text::kBlockAdsOnSite},
            {IDC_SITE_COOKIES, Text::kAllowCookiesOnSite},
            {IDC_SITE_ALLOW_PLAIN_HTTP, Text::kAllowPlainHttpOnSite},
            {IDC_SITE_SAFETY_NOTE, Text::kSiteSafetyNote},
            {IDC_SITE_ZOOM_LABEL, Text::kZoom},
            {IDC_SITE_ZOOM_RESET, Text::kResetZoom},
            {IDC_SITE_FORGET, Text::kForgetSite},
            {IDOK, Text::kSave},
            {IDCANCEL, Text::kCancel},
        };
        mtk::dialog_text::ApplyCaption(dialog, Text::kSiteSettingsTitle);
        mtk::dialog_text::Apply(dialog, kText);
      }
      FillPermissionCombo(dialog, IDC_SITE_CAMERA, context->rule.camera);
      FillPermissionCombo(dialog, IDC_SITE_MICROPHONE, context->rule.microphone);
      FillPermissionCombo(dialog, IDC_SITE_LOCATION, context->rule.location);
      FillPermissionCombo(dialog, IDC_SITE_NOTIFICATIONS,
                          context->rule.notifications);
      CheckDlgButton(dialog, IDC_SITE_AD_FILTERING,
                     context->rule.ad_filtering ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_SITE_COOKIES,
                     context->rule.cookies ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_SITE_ALLOW_PLAIN_HTTP,
                     context->rule.allow_plain_http ? BST_CHECKED
                                                     : BST_UNCHECKED);
      SetDlgItemTextW(
          dialog, IDC_SITE_ZOOM_TEXT,
          (std::to_wstring(context->rule.zoom_percent) + L"%").c_str());
      return TRUE;
    }
    case WM_COMMAND: {
      auto* context = reinterpret_cast<SiteSettingsContext*>(
          GetWindowLongPtrW(dialog, GWLP_USERDATA));
      switch (LOWORD(w_param)) {
        case IDC_SITE_ZOOM_RESET:
          if (context) {
            context->rule.zoom_percent = mtk::sites::kDefaultZoomPercent;
            SetDlgItemTextW(
                dialog, IDC_SITE_ZOOM_TEXT,
                (std::to_wstring(context->rule.zoom_percent) + L"%").c_str());
          }
          return TRUE;
        case IDC_SITE_FORGET:
          if (context) context->forget = true;
          EndDialog(dialog, IDOK);
          return TRUE;
        case IDOK:
          if (context) {
            context->rule.camera = PermissionFromCombo(dialog, IDC_SITE_CAMERA);
            context->rule.microphone =
                PermissionFromCombo(dialog, IDC_SITE_MICROPHONE);
            context->rule.location =
                PermissionFromCombo(dialog, IDC_SITE_LOCATION);
            context->rule.notifications =
                PermissionFromCombo(dialog, IDC_SITE_NOTIFICATIONS);
            context->rule.ad_filtering =
                IsDlgButtonChecked(dialog, IDC_SITE_AD_FILTERING) == BST_CHECKED;
            context->rule.cookies =
                IsDlgButtonChecked(dialog, IDC_SITE_COOKIES) == BST_CHECKED;
            context->rule.allow_plain_http =
                IsDlgButtonChecked(dialog, IDC_SITE_ALLOW_PLAIN_HTTP) ==
                BST_CHECKED;
          }
          EndDialog(dialog, IDOK);
          return TRUE;
        case IDCANCEL:
          EndDialog(dialog, IDCANCEL);
          return TRUE;
        default:
          break;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

// Result of the site list dialog: which origin (if any) was chosen for
// "Change..." or "Clear this site's data", read after the dialog closes.
struct SiteListContext {
  // Set on IDOK once, to the origin whose "Change..." button was pressed.
  // Empty when the dialog was simply closed.
  std::string open_origin;
  // Set when "Clear this site's data" was pressed and confirmed.
  std::string clear_origin;
};

void PopulateSiteList(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_SITE_LIST_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  const auto& rules = mtk::sites::Store::Get().rules();
  for (size_t index = 0; index < rules.size(); ++index) {
    const std::wstring row = Utf8ToWide(rules[index].origin);
    const LRESULT added =
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    if (added != LB_ERR && added != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(added), index);
    }
  }
  const bool any = !rules.empty();
  for (const int control : {IDC_SITE_LIST_OPEN, IDC_SITE_LIST_CLEAR}) {
    EnableWindow(GetDlgItem(dialog, control), any ? TRUE : FALSE);
  }
  if (any) SendMessageW(list, LB_SETCURSEL, 0, 0);
  SetDlgItemTextW(dialog, IDC_SITE_LIST_NOTE,
                  any ? L"" : mtk::strings::Get(Text::kSiteListEmptyNote));
}

// Origin at the selected row, or an empty string when nothing is selected.
std::string SelectedSiteListOrigin(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_SITE_LIST_LIST);
  const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR) return {};
  const LRESULT data =
      SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
  if (data == LB_ERR) return {};
  const auto& rules = mtk::sites::Store::Get().rules();
  return static_cast<size_t>(data) < rules.size()
             ? rules[static_cast<size_t>(data)].origin
             : std::string();
}

INT_PTR CALLBACK SiteListDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                    LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_SITE_LIST_INTRO, Text::kSiteListIntro},
          {IDC_SITE_LIST_OPEN, Text::kSiteListOpen},
          {IDC_SITE_LIST_CLEAR, Text::kSiteListClear},
          {IDCANCEL, Text::kClose},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kSiteListTitle);
      mtk::dialog_text::Apply(dialog, kText);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      PopulateSiteList(dialog);
      return TRUE;
    }
    case WM_COMMAND: {
      const WORD command = LOWORD(w_param);
      auto* context = reinterpret_cast<SiteListContext*>(
          GetWindowLongPtrW(dialog, GWLP_USERDATA));
      if (command == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      const std::string origin = SelectedSiteListOrigin(dialog);
      if (origin.empty()) return TRUE;
      if (command == IDC_SITE_LIST_OPEN) {
        if (context) context->open_origin = origin;
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (command == IDC_SITE_LIST_CLEAR) {
        if (MessageBoxW(dialog, mtk::strings::Get(Text::kSiteListClearConfirm),
                        L"Sites with stored settings",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        if (context) context->clear_origin = origin;
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

struct TextPromptContext {
  const wchar_t* caption = nullptr;
  const wchar_t* message = nullptr;
  std::wstring value;
};

INT_PTR CALLBACK TextPromptDialogProc(HWND dialog, UINT message,
                                      WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDOK, Text::kOk},
          {IDCANCEL, Text::kCancel},
      };
      mtk::dialog_text::Apply(dialog, kText);
      auto* context = reinterpret_cast<TextPromptContext*>(l_param);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      if (context) {
        if (context->caption) SetWindowTextW(dialog, context->caption);
        if (context->message) {
          SetDlgItemTextW(dialog, IDC_TEXT_PROMPT_LABEL, context->message);
        }
      }
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* context = reinterpret_cast<TextPromptContext*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        if (context) {
          HWND field = GetDlgItem(dialog, IDC_TEXT_PROMPT_VALUE);
          const int length = GetWindowTextLengthW(field);
          // Bounded because the value is used to build a path.
          std::wstring value(static_cast<size_t>(std::min(length, 64)) + 1,
                             L'\0');
          GetWindowTextW(field, value.data(), std::min(length, 64) + 1);
          value.resize(static_cast<size_t>(std::min(length, 64)));
          context->value = std::move(value);
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

}  // namespace

// One-line text prompt. Returns false when the dialog was cancelled or the
// field was left empty.
bool PromptForText(HINSTANCE instance, HWND owner, const wchar_t* caption,
                   const wchar_t* message, std::wstring* value) {
  TextPromptContext context{caption, message, {}};
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_TEXT_PROMPT), owner,
                      TextPromptDialogProc,
                      reinterpret_cast<LPARAM>(&context)) != IDOK) {
    return false;
  }
  if (context.value.empty()) return false;
  *value = std::move(context.value);
  return true;
}

// Asks for the parent PIN when one is set. Returns true when access is granted,
// which is also the case when no PIN has been configured yet.
bool AuthorizeParentAction(HINSTANCE instance, HWND owner,
                           const wchar_t* message) {
  if (!mtk::preferences::PinIsSet()) return true;
  PinPromptContext context{message, 0};
  return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_PIN_PROMPT), owner,
                         PinPromptDialogProc,
                         reinterpret_cast<LPARAM>(&context)) == IDOK;
}

namespace {

INT_PTR CALLBACK PinSetupDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                    LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_PIN_SETUP_INTRO, Text::kPinSetupIntro},
          {IDC_PIN_SETUP_CURRENT_LABEL, Text::kCurrentPin},
          {IDC_PIN_SETUP_NEW_LABEL, Text::kNewPin},
          {IDC_PIN_SETUP_CONFIRM_LABEL, Text::kConfirmNewPin},
          {IDC_PIN_SETUP_REMOVE, Text::kRemovePin},
          {IDOK, Text::kSavePin},
          {IDCANCEL, Text::kCancel},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kParentPinTitle);
      mtk::dialog_text::Apply(dialog, kText);
      const bool existing = mtk::preferences::PinIsSet();
      EnableWindow(GetDlgItem(dialog, IDC_PIN_SETUP_CURRENT), existing);
      EnableWindow(GetDlgItem(dialog, IDC_PIN_SETUP_CURRENT_LABEL), existing);
      EnableWindow(GetDlgItem(dialog, IDC_PIN_SETUP_REMOVE), existing);
      if (!existing) {
        SetDlgItemTextW(dialog, IDC_PIN_SETUP_CURRENT_LABEL,
                        mtk::strings::Get(Text::kCurrentPinNoneSet));
      }
      return TRUE;
    }
    case WM_COMMAND: {
      const bool existing = mtk::preferences::PinIsSet();
      // Changing or removing a PIN always requires the current one.
      const auto current_ok = [&](HWND owner) {
        if (!existing) return true;
        std::wstring current = TakePinField(dialog, IDC_PIN_SETUP_CURRENT);
        const bool ok = mtk::preferences::VerifyPin(current);
        ScrubPin(&current);
        if (!ok) {
          MessageBoxW(owner, L"The current PIN is not correct.", L"Parent PIN",
                      MB_OK | MB_ICONWARNING);
        }
        return ok;
      };

      if (LOWORD(w_param) == IDC_PIN_SETUP_REMOVE) {
        if (!existing || !current_ok(dialog)) return TRUE;
        if (MessageBoxW(dialog,
                        L"Remove the parent PIN?\n\nSafety settings will be "
                        L"changeable by anyone using this computer.",
                        L"Remove parent PIN?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        mtk::preferences::Values values =
            mtk::preferences::Store::Get().values();
        values.pin_salt.clear();
        values.pin_hash.clear();
        if (!mtk::preferences::Store::Get().Save(values)) {
          MessageBoxW(dialog, L"MTK Browser could not remove the PIN.",
                      L"Parent PIN", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }

      if (LOWORD(w_param) == IDOK) {
        if (!current_ok(dialog)) return TRUE;
        std::wstring fresh = TakePinField(dialog, IDC_PIN_SETUP_NEW);
        std::wstring confirm = TakePinField(dialog, IDC_PIN_SETUP_CONFIRM);
        const bool too_short = fresh.size() < mtk::preferences::kMinPinLength;
        const bool mismatch = fresh != confirm;
        ScrubPin(&confirm);
        if (too_short) {
          ScrubPin(&fresh);
          MessageBoxW(dialog, L"Use a PIN of at least 4 characters.",
                      L"Parent PIN", MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        if (mismatch) {
          ScrubPin(&fresh);
          MessageBoxW(dialog, L"The two new PINs do not match.", L"Parent PIN",
                      MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        mtk::preferences::Values values =
            mtk::preferences::Store::Get().values();
        const bool derived = mtk::preferences::DerivePin(
            fresh, &values.pin_salt, &values.pin_hash);
        ScrubPin(&fresh);
        if (!derived || !mtk::preferences::Store::Get().Save(values)) {
          MessageBoxW(dialog, L"MTK Browser could not save the PIN.",
                      L"Parent PIN", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

struct ClearDataChoice {
  bool history = true;
  bool downloads = true;
  bool cookies = false;
  bool cache = true;
  bool session = false;
  bool bookmarks = false;
  // Per-site permissions, cookie choices, Shields state and zoom. Off by
  // default: these are decisions someone made, not traces left behind.
  bool site_settings = false;
  // Saved sign-ins. Off by default and confirmed separately, because losing
  // them is not recoverable.
  bool passwords = false;
};

INT_PTR CALLBACK ClearDataDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM l_param) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_CLEAR_INTRO, Text::kClearDataIntro},
          {IDC_CLEAR_HISTORY, Text::kClearHistoryItem},
          {IDC_CLEAR_DOWNLOADS, Text::kClearDownloadsItem},
          {IDC_CLEAR_COOKIES, Text::kClearCookiesItem},
          {IDC_CLEAR_CACHE, Text::kClearCacheItem},
          {IDC_CLEAR_SESSION, Text::kClearSessionItem},
          {IDC_CLEAR_BOOKMARKS, Text::kBookmarks},
          {IDC_CLEAR_SITE_SETTINGS, Text::kClearSiteSettingsItem},
          {IDC_CLEAR_PASSWORDS, Text::kClearPasswordsItem},
          {IDC_CLEAR_NOTE, Text::kClearDataNote},
          {IDOK, Text::kClearNow},
          {IDCANCEL, Text::kCancel},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kClearDataTitle);
      mtk::dialog_text::Apply(dialog, kText);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(l_param));
      const auto* choice = reinterpret_cast<const ClearDataChoice*>(l_param);
      if (!choice) return TRUE;
      // Bookmarks stay unchecked by default: they are saved deliberately and
      // are not browsing traces.
      CheckDlgButton(dialog, IDC_CLEAR_HISTORY,
                     choice->history ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_DOWNLOADS,
                     choice->downloads ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_COOKIES,
                     choice->cookies ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_CACHE,
                     choice->cache ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_SESSION,
                     choice->session ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_BOOKMARKS,
                     choice->bookmarks ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_SITE_SETTINGS,
                     choice->site_settings ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_CLEAR_PASSWORDS,
                     choice->passwords ? BST_CHECKED : BST_UNCHECKED);
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDOK) {
        auto* choice = reinterpret_cast<ClearDataChoice*>(
            GetWindowLongPtrW(dialog, GWLP_USERDATA));
        if (choice) {
          const auto checked = [dialog](int control) {
            return IsDlgButtonChecked(dialog, control) == BST_CHECKED;
          };
          choice->history = checked(IDC_CLEAR_HISTORY);
          choice->downloads = checked(IDC_CLEAR_DOWNLOADS);
          choice->cookies = checked(IDC_CLEAR_COOKIES);
          choice->cache = checked(IDC_CLEAR_CACHE);
          choice->session = checked(IDC_CLEAR_SESSION);
          choice->bookmarks = checked(IDC_CLEAR_BOOKMARKS);
          choice->site_settings = checked(IDC_CLEAR_SITE_SETTINGS);
          choice->passwords = checked(IDC_CLEAR_PASSWORDS);
          const bool anything = choice->history || choice->downloads ||
                                choice->cookies || choice->cache ||
                                choice->session || choice->bookmarks ||
                                choice->site_settings || choice->passwords;
          if (!anything) {
            MessageBoxW(dialog, L"Select at least one item to clear.",
                        L"Clear browsing data", MB_OK | MB_ICONINFORMATION);
            return TRUE;
          }
          if (choice->bookmarks &&
              MessageBoxW(dialog,
                          L"Delete all bookmarks as well?\n\nThis cannot be "
                          L"undone.",
                          L"Clear browsing data",
                          MB_YESNO | MB_ICONWARNING) != IDYES) {
            return TRUE;
          }
          if (choice->passwords &&
              MessageBoxW(dialog,
                          L"Delete every saved password?\n\nThey cannot be "
                          L"recovered afterwards.",
                          L"Clear browsing data",
                          MB_YESNO | MB_ICONWARNING) != IDYES) {
            return TRUE;
          }
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

Text DohText(mtk::preferences::DohProvider provider) {
  switch (provider) {
    case mtk::preferences::DohProvider::kCloudflareFamily:
      return Text::kDohCloudflareFamily;
    case mtk::preferences::DohProvider::kCloudflare:
      return Text::kDohCloudflare;
    case mtk::preferences::DohProvider::kQuad9:
      return Text::kDohQuad9;
    case mtk::preferences::DohProvider::kGoogle:
      return Text::kDohGoogle;
    case mtk::preferences::DohProvider::kSystem:
      break;
  }
  return Text::kUseWindowsDns;
}

void RefreshPinState(HWND dialog) {
  SetDlgItemTextW(dialog, IDC_SETTINGS_PIN_STATE,
                  mtk::strings::Get(mtk::preferences::PinIsSet()
                                        ? Text::kPinRequiredState
                                        : Text::kPinMissingState));
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                    LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_SETTINGS_GENERAL_HEAD, Text::kGeneralHeading},
          {IDC_SETTINGS_HOME_LABEL, Text::kHomePageLabel},
          {IDC_SETTINGS_STARTUP_LABEL, Text::kOnStartup},
          {IDC_SETTINGS_LANGUAGE_LABEL, Text::kWebsiteLanguage},
          {IDC_SETTINGS_DOWNLOADS_LABEL, Text::kDownloads},
          {IDC_SETTINGS_DOWNLOADS_NOTE, Text::kDownloadsNote},
          {IDC_SETTINGS_SAFETY_NOTE, Text::kSafetyStaysOnNote},
          {IDC_SETTINGS_LANGUAGE_NOTE, Text::kLanguageRestartNote},
          {IDC_SETTINGS_CONNECTION_HEAD, Text::kConnectionHeading},
          {IDC_SETTINGS_HTTPS_ONLY, Text::kHttpsOnlyOption},
          {IDC_SETTINGS_FINGERPRINT_PROTECTION, Text::kFingerprintProtectionOption},
          {IDC_SETTINGS_DOH_LABEL, Text::kEncryptedDns},
          {IDC_SETTINGS_DOH_NOTE, Text::kEncryptedDnsRestartNote},
          {IDC_SETTINGS_AUTO_UPDATE, Text::kAutoUpdateOption},
          {IDC_SETTINGS_PARENT_HEAD, Text::kParentControlsHeading},
          {IDC_SETTINGS_PIN, Text::kParentPinButton},
          {IDOK, Text::kSave},
          {IDCANCEL, Text::kCancel},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kSettingsTitle);
      mtk::dialog_text::Apply(dialog, kText);
      const auto& values = mtk::preferences::Store::Get().values();
      SetDlgItemTextW(dialog, IDC_SETTINGS_HOME_PAGE, values.home_page.c_str());
      // Language names stay in their own language, which is how a person finds
      // the one they can read.
      HWND language = GetDlgItem(dialog, IDC_SETTINGS_LANGUAGE);
      SendMessageW(language, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"English (United States) and Hindi"));
      SendMessageW(language, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"Hindi and English (United States)"));
      const int choice = values.accept_language == "hi,en-US,en" ? 1 : 0;
      SendMessageW(language, CB_SETCURSEL, choice, 0);
      SetWindowTheme(language, L"Explorer", nullptr);

      HWND startup = GetDlgItem(dialog, IDC_SETTINGS_STARTUP);
      SendMessageW(startup, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(
                       mtk::strings::Get(Text::kStartupHomePage)));
      SendMessageW(startup, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(
                       mtk::strings::Get(Text::kStartupContinue)));
      SendMessageW(startup, CB_SETCURSEL,
                   values.startup_mode ==
                           mtk::preferences::StartupMode::kContinueSession ? 1 : 0,
                   0);
      SetWindowTheme(startup, L"Explorer", nullptr);

      HWND downloads = GetDlgItem(dialog, IDC_SETTINGS_DOWNLOADS);
      SendMessageW(downloads, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(
                       mtk::strings::Get(Text::kSafeDownloadsOption)));
      SendMessageW(downloads, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(
                       mtk::strings::Get(Text::kStandardDownloadsOption)));
      SendMessageW(downloads, CB_SETCURSEL,
                   values.download_mode ==
                           mtk::preferences::DownloadMode::kStandard ? 1 : 0,
                   0);
      SetWindowTheme(downloads, L"Explorer", nullptr);
      CheckDlgButton(dialog, IDC_SETTINGS_AUTO_UPDATE,
                     values.auto_update ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_SETTINGS_HTTPS_ONLY,
                     values.https_only ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_SETTINGS_FINGERPRINT_PROTECTION,
                     values.fingerprint_protection ? BST_CHECKED
                                                    : BST_UNCHECKED);
      {
        // Providers in a fixed order so the stored value maps to an index. The
        // label is taken from the UI string table rather than from the
        // preference layer, which has no business holding display text.
        HWND doh = GetDlgItem(dialog, IDC_SETTINGS_DOH);
        for (const auto provider : kDohOrder) {
          SendMessageW(doh, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(
                           mtk::strings::Get(DohText(provider))));
        }
        int selected = 0;
        for (size_t index = 0; index < std::size(kDohOrder); ++index) {
          if (kDohOrder[index] == values.doh_provider) {
            selected = static_cast<int>(index);
            break;
          }
        }
        SendMessageW(doh, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        SetWindowTheme(doh, L"Explorer", nullptr);
      }
      RefreshPinState(dialog);
      return TRUE;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == IDC_SETTINGS_PIN) {
        DialogBoxParamW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(
                            dialog, GWLP_HINSTANCE)),
                        MAKEINTRESOURCEW(IDD_PIN_SETUP), dialog,
                        PinSetupDialogProc, 0);
        RefreshPinState(dialog);
        return TRUE;
      }
      if (LOWORD(w_param) == IDOK) {
        wchar_t home_page[2049]{};
        const int length = GetDlgItemTextW(
            dialog, IDC_SETTINGS_HOME_PAGE, home_page,
            static_cast<int>(sizeof(home_page) / sizeof(home_page[0])));
        if (length == 0 ||
            length >= static_cast<int>(sizeof(home_page) / sizeof(home_page[0]))) {
          MessageBoxW(dialog, L"Enter a home page or search address.",
                      L"MTK Browser Settings", MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        const std::string entered_home = WideToUtf8(home_page);
        const bool built_in_home = mtk::home::IsHomeUrl(entered_home);
        const auto decision = built_in_home
                                  ? mtk::policy::NavigationDecision{
                                        true, mtk::home::kHomeUrl, {}}
                                  : mtk::policy::ResolveInput(entered_home);
        if (!decision.allow) {
          MessageBoxW(dialog, decision.reason.c_str(), L"Home page blocked",
                      MB_OK | MB_ICONWARNING);
          return TRUE;
        }
        mtk::preferences::Values values = mtk::preferences::Store::Get().values();
        values.home_page = built_in_home ? mtk::home::kHomeUrlWide
                                         : Utf8ToWide(decision.url);
        values.home_page_custom = !built_in_home;
        const int selected = static_cast<int>(SendMessageW(
            GetDlgItem(dialog, IDC_SETTINGS_LANGUAGE), CB_GETCURSEL, 0, 0));
        values.accept_language = selected == 1 ? "hi,en-US,en" : "en-US,en,hi";

        const bool standard_downloads =
            SendMessageW(GetDlgItem(dialog, IDC_SETTINGS_DOWNLOADS),
                         CB_GETCURSEL, 0, 0) == 1;
        const bool was_standard =
            values.download_mode == mtk::preferences::DownloadMode::kStandard;
        // Leaving safe downloads is an explicit, acknowledged choice.
        if (standard_downloads && !was_standard &&
            MessageBoxW(dialog,
                        L"Standard downloads save any file type, including "
                        L"programs, scripts and installers.\n\nOnly files you "
                        L"trust should be opened. Continue?",
                        L"Turn on standard downloads?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        values.download_mode = standard_downloads
                                   ? mtk::preferences::DownloadMode::kStandard
                                   : mtk::preferences::DownloadMode::kSafe;
        values.startup_mode =
            SendMessageW(GetDlgItem(dialog, IDC_SETTINGS_STARTUP),
                         CB_GETCURSEL, 0, 0) == 1
                ? mtk::preferences::StartupMode::kContinueSession
                : mtk::preferences::StartupMode::kHomePage;
        const bool auto_update =
            IsDlgButtonChecked(dialog, IDC_SETTINGS_AUTO_UPDATE) == BST_CHECKED;
        // Turning update checks off means security fixes stop arriving, so it
        // has to be acknowledged rather than toggled silently.
        if (!auto_update && values.auto_update &&
            MessageBoxW(dialog,
                        L"With update checks off, MTK Browser will not receive "
                        L"security or safety fixes automatically.\n\nTurn them "
                        L"off anyway?",
                        L"Turn off update checks?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          CheckDlgButton(dialog, IDC_SETTINGS_AUTO_UPDATE, BST_CHECKED);
          return TRUE;
        }
        values.auto_update = auto_update;

        const bool https_only =
            IsDlgButtonChecked(dialog, IDC_SETTINGS_HTTPS_ONLY) == BST_CHECKED;
        // Allowing plain HTTP content back in is a safety decision, so it is
        // acknowledged rather than flipped quietly.
        if (!https_only && values.https_only &&
            MessageBoxW(dialog,
                        L"Pages will be allowed to load content over plain "
                        L"HTTP, which anyone on the network can read or "
                        L"change.\n\nAllow it anyway?",
                        L"Allow plain HTTP?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          CheckDlgButton(dialog, IDC_SETTINGS_HTTPS_ONLY, BST_CHECKED);
          return TRUE;
        }
        values.https_only = https_only;

        const bool fingerprint_protection =
            IsDlgButtonChecked(dialog, IDC_SETTINGS_FINGERPRINT_PROTECTION) ==
            BST_CHECKED;
        // Same acknowledgement pattern as HTTPS-only: turning off a safety
        // default should not be one accidental click.
        if (!fingerprint_protection && values.fingerprint_protection &&
            MessageBoxW(dialog,
                        L"Sites will be able to read more detailed device and "
                        L"browser information, which makes it easier to "
                        L"recognise you across visits.\n\nTurn it off anyway?",
                        L"Turn off fingerprinting protection?",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          CheckDlgButton(dialog, IDC_SETTINGS_FINGERPRINT_PROTECTION,
                        BST_CHECKED);
          return TRUE;
        }
        values.fingerprint_protection = fingerprint_protection;

        const LRESULT doh_index =
            SendMessageW(GetDlgItem(dialog, IDC_SETTINGS_DOH), CB_GETCURSEL, 0, 0);
        if (doh_index >= 0 &&
            static_cast<size_t>(doh_index) < std::size(kDohOrder)) {
          values.doh_provider = kDohOrder[static_cast<size_t>(doh_index)];
        }

        if (!mtk::preferences::Store::Get().Save(values)) {
          MessageBoxW(dialog, L"MTK Browser could not save these settings.",
                      L"MTK Browser Settings", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        RefreshHomeUrl();
        HWND owner = GetParent(dialog);
        EndDialog(dialog, IDOK);
        MessageBoxW(owner,
                    L"Saved. Home and new tabs use the new page. Website "
                    L"language, encrypted DNS and fingerprinting protection "
                    L"apply after you restart MTK Browser.",
                    L"MTK Browser Settings", MB_OK | MB_ICONINFORMATION);
        return TRUE;
      }
      if (LOWORD(w_param) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

std::wstring LibraryDisplayText(const mtk::library::Entry& entry) {
  std::wstring text = entry.title.empty() ? Utf8ToWide(entry.url) : entry.title;
  std::replace_if(text.begin(), text.end(), [](wchar_t character) {
    return character == L'\r' || character == L'\n' || character == L'\t';
  }, L' ');
  if (text.size() > 180) text.resize(180);
  const std::wstring url = Utf8ToWide(entry.url);
  if (url != text) text += L"  —  " + url;
  return text;
}

bool MatchesLibraryQuery(const mtk::library::Entry& entry,
                         const std::wstring& lowered_query) {
  if (lowered_query.empty()) return true;
  std::wstring haystack = entry.title + L" " + Utf8ToWide(entry.url);
  std::transform(haystack.begin(), haystack.end(), haystack.begin(), towlower);
  return haystack.find(lowered_query) != std::wstring::npos;
}

// Item data is entry index + 1, so a row that cannot be opened or removed (the
// "nothing matches" line) can use 0 and never be mistaken for a real entry.
constexpr LRESULT kLibraryHeaderData = 0;

// Bookmarks only. History has its own browsable document at mtk://history/.
void PopulateLibraryList(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_LIBRARY_LIST);
  HWND search = GetDlgItem(dialog, IDC_LIBRARY_SEARCH);
  std::wstring query;
  if (search) {
    const int length = GetWindowTextLengthW(search);
    query.resize(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(search, query.data(), length + 1);
    query.resize(static_cast<size_t>(length));
    std::transform(query.begin(), query.end(), query.begin(), towlower);
  }

  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  const auto& entries = mtk::library::Store::Get().bookmarks();
  size_t shown = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (!MatchesLibraryQuery(entries[index], query)) continue;
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(LibraryDisplayText(entries[index]).c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                   static_cast<LPARAM>(index + 1));
      if (shown == 0) SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(row), 0);
      ++shown;
    }
  }
  if (shown == 0 && !query.empty()) {
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"Nothing matches that search."));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                   kLibraryHeaderData);
    }
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

// Entry index behind the selected row, or -1 when a header row is selected.
ptrdiff_t SelectedLibraryIndex(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_LIBRARY_LIST);
  const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR) return -1;
  const LRESULT data =
      SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
  if (data == LB_ERR || data == kLibraryHeaderData) return -1;
  return static_cast<ptrdiff_t>(data) - 1;
}

INT_PTR CALLBACK LibraryDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                   LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      static constexpr mtk::dialog_text::Item kText[] = {
          {IDC_LIBRARY_HINT, Text::kBookmarksHint},
          {IDC_LIBRARY_SEARCH_LABEL, Text::kSearch},
          {IDC_LIBRARY_OPEN, Text::kOpen},
          {IDC_LIBRARY_REMOVE, Text::kRemove},
          {IDC_LIBRARY_CLEAR, Text::kRemoveAll},
          {IDCANCEL, Text::kClose},
      };
      mtk::dialog_text::ApplyCaption(dialog, Text::kBookmarksTitle);
      mtk::dialog_text::Apply(dialog, kText);
      PopulateLibraryList(dialog);
      return TRUE;
    }
    case WM_COMMAND: {
      const WORD command = LOWORD(w_param);
      const WORD notification = HIWORD(w_param);
      if (command == IDC_LIBRARY_LIST && notification == LBN_DBLCLK) {
        SendMessageW(dialog, WM_COMMAND, IDC_LIBRARY_OPEN, 0);
        return TRUE;
      }
      if (command == IDC_LIBRARY_SEARCH && notification == EN_CHANGE) {
        PopulateLibraryList(dialog);
        return TRUE;
      }
      const ptrdiff_t selected = SelectedLibraryIndex(dialog);
      if (command == IDC_LIBRARY_OPEN) {
        if (selected < 0) return TRUE;
        EndDialog(dialog, selected + 1);
        return TRUE;
      }
      if (command == IDC_LIBRARY_REMOVE) {
        if (selected < 0) return TRUE;
        const size_t index = static_cast<size_t>(selected);
        if (mtk::library::Store::Get().RemoveBookmarkAt(index) &&
            !mtk::library::Store::Get().Flush()) {
          MessageBoxW(dialog, L"MTK Browser could not save the library change.",
                      L"MTK Browser", MB_OK | MB_ICONERROR);
        }
        PopulateLibraryList(dialog);
        return TRUE;
      }
      if (command == IDC_LIBRARY_CLEAR) {
        // Folders go with the bookmarks, so the prompt says so rather than
        // leaving empty folders behind unexplained.
        if (MessageBoxW(dialog,
                        mtk::strings::Get(Text::kRemoveAllBookmarksQuestion),
                        L"MTK Browser", MB_YESNO | MB_ICONWARNING) != IDYES) {
          return TRUE;
        }
        if (mtk::library::Store::Get().ClearBookmarks() &&
            !mtk::library::Store::Get().Flush()) {
          MessageBoxW(dialog, L"MTK Browser could not save the library change.",
                      L"MTK Browser", MB_OK | MB_ICONERROR);
        }
        PopulateLibraryList(dialog);
        return TRUE;
      }
      if (command == IDCANCEL) {
        EndDialog(dialog, 0);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      EndDialog(dialog, 0);
      return TRUE;
  }
  return FALSE;
}

// Set while the modeless-feeling modal Downloads dialog is on screen so live
// progress updates from the CEF download handler can refresh the list.
}  // namespace

HWND g_downloads_dialog = nullptr;

namespace {

void RefreshDownloadControls(HWND dialog);

// The header line and the "Open file" button follow the mode chosen in
// Settings, so the dialog always states what the browser will actually do.
void RefreshDownloadModeUi(HWND dialog) {
  const bool standard =
      mtk::downloads::CurrentMode() == mtk::downloads::Mode::kStandard;
  SetDlgItemTextW(
      dialog, IDC_DOWNLOADS_MODE_TEXT,
      standard
          ? L"Standard downloads: any file type is saved. Opening a file is "
            L"your choice, and risky types are confirmed first."
          : L"Safe downloads: only known file types are saved, and MTK Browser "
            L"never opens a downloaded file.");
  EnableWindow(GetDlgItem(dialog, IDC_DOWNLOADS_OPEN), standard ? TRUE : FALSE);
}

}  // namespace

void PopulateDownloadList(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_DOWNLOADS_LIST);
  const LRESULT previous = SendMessageW(list, LB_GETCURSEL, 0, 0);
  const LRESULT top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);

  const auto& items = mtk::downloads::Manager::Get().items();
  // Newest first: an active download should never be buried under old rows.
  for (size_t offset = items.size(); offset > 0; --offset) {
    const size_t index = offset - 1;
    const LRESULT row = SendMessageW(
        list, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(
            mtk::downloads::DescribeItem(items[index]).c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
      SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                   static_cast<LPARAM>(items[index].id));
    }
  }
  if (items.empty()) {
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"No download history yet."));
    SendMessageW(list, LB_SETITEMDATA, 0, static_cast<LPARAM>(-1));
  } else {
    const LRESULT count = SendMessageW(list, LB_GETCOUNT, 0, 0);
    SendMessageW(list, LB_SETTOPINDEX, static_cast<WPARAM>(std::max<LRESULT>(top, 0)), 0);
    SendMessageW(list, LB_SETCURSEL,
                 static_cast<WPARAM>(previous >= 0 && previous < count ? previous : 0),
                 0);
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  RefreshDownloadControls(dialog);
}

namespace {

const mtk::downloads::Item* SelectedDownload(HWND dialog) {
  HWND list = GetDlgItem(dialog, IDC_DOWNLOADS_LIST);
  const LRESULT row = SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR) return nullptr;
  const LRESULT data =
      SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
  if (data == LB_ERR || data < 0) return nullptr;
  for (const auto& item : mtk::downloads::Manager::Get().items()) {
    if (item.id == static_cast<uint64_t>(data)) return &item;
  }
  return nullptr;
}

void RefreshDownloadControls(HWND dialog) {
  const auto* item = SelectedDownload(dialog);
  const bool live_callback = item && (item->id >> 32) == 0 &&
      BrowserClient::CanControlDownload(static_cast<uint32_t>(item->id));
  const bool paused = live_callback &&
      item->state == mtk::downloads::State::kPaused;
  const bool controllable = live_callback &&
      (item->state == mtk::downloads::State::kStarting ||
       item->state == mtk::downloads::State::kInProgress || paused);
  SetDlgItemTextW(dialog, IDC_DOWNLOADS_PAUSE, paused ? L"Resume" : L"Pause");
  EnableWindow(GetDlgItem(dialog, IDC_DOWNLOADS_PAUSE), controllable ? TRUE : FALSE);
  EnableWindow(GetDlgItem(dialog, IDC_DOWNLOADS_CANCEL), controllable ? TRUE : FALSE);
}

// Reveals the file in Explorer instead of launching it. MTK Browser never
// starts downloaded content itself, so a saved file cannot execute by mistake.
}  // namespace

void RevealInExplorer(HWND owner, const std::wstring& path) {
  if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(owner, L"This file is no longer on disk.", L"MTK Browser",
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  const std::wstring arguments = L"/select,\"" + path + L"\"";
  ShellExecuteW(owner, L"open", L"explorer.exe", arguments.c_str(), nullptr,
                SW_SHOWNORMAL);
}

// Standard mode only. Launching is left to the shell exactly like a mainstream
// browser, but a file type that can execute code is confirmed first and MTK
// Browser still never starts a download on its own.
void OpenDownloadedFile(HWND owner, const mtk::downloads::Item& item) {
  if (item.full_path.empty() ||
      GetFileAttributesW(item.full_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(owner, L"This file is no longer on disk.", L"MTK Browser",
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (item.risky || mtk::downloads::IsRiskyFileName(item.file_name)) {
    const std::wstring warning =
        L"This file can run programs on your computer.\n\n" + item.file_name +
        L"\n\nOpen it anyway?";
    if (MessageBoxW(owner, warning.c_str(), L"MTK Browser",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
      return;
    }
  }
  ShellExecuteW(owner, L"open", item.full_path.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

namespace {

INT_PTR CALLBACK DownloadsDialogProc(HWND dialog, UINT message, WPARAM w_param,
                                     LPARAM) {
  switch (message) {
    case WM_INITDIALOG:
      g_downloads_dialog = dialog;
      {
        static constexpr mtk::dialog_text::Item kText[] = {
            {IDC_DOWNLOADS_FOLDER, Text::kOpenFolder},
            {IDC_DOWNLOADS_OPEN, Text::kOpenFile},
            {IDC_DOWNLOADS_SHOW, Text::kShowInFolder},
            {IDC_DOWNLOADS_PAUSE, Text::kPause},
            {IDC_DOWNLOADS_CANCEL, Text::kCancel},
            {IDC_DOWNLOADS_CLEAR, Text::kClearFinished},
            {IDCANCEL, Text::kClose},
        };
        mtk::dialog_text::ApplyCaption(dialog, Text::kDownloadsTitle);
        mtk::dialog_text::Apply(dialog, kText);
      }
      RefreshDownloadModeUi(dialog);
      PopulateDownloadList(dialog);
      return TRUE;
    case WM_COMMAND: {
      const WORD command = LOWORD(w_param);
      if (command == IDC_DOWNLOADS_LIST && HIWORD(w_param) == LBN_SELCHANGE) {
        RefreshDownloadControls(dialog);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_FOLDER) {
        const std::wstring directory = mtk::downloads::DownloadDirectory();
        if (directory.empty()) {
          MessageBoxW(dialog, L"MTK Browser could not open the downloads folder.",
                      L"MTK Browser", MB_OK | MB_ICONERROR);
          return TRUE;
        }
        ShellExecuteW(dialog, L"open", directory.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_OPEN) {
        if (mtk::downloads::CurrentMode() != mtk::downloads::Mode::kStandard) {
          MessageBoxW(dialog,
                      L"Safe downloads never open a saved file. Switch "
                      L"Downloads to standard mode in Settings to open files "
                      L"from here.",
                      L"MTK Browser", MB_OK | MB_ICONINFORMATION);
          return TRUE;
        }
        const auto* item = SelectedDownload(dialog);
        if (!item) return TRUE;
        if (item->state != mtk::downloads::State::kComplete) {
          MessageBoxW(dialog, L"This download has not finished yet.",
                      L"MTK Browser", MB_OK | MB_ICONINFORMATION);
          return TRUE;
        }
        OpenDownloadedFile(dialog, *item);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_SHOW ||
          (command == IDC_DOWNLOADS_LIST && HIWORD(w_param) == LBN_DBLCLK)) {
        const auto* item = SelectedDownload(dialog);
        if (!item) return TRUE;
        if (item->state != mtk::downloads::State::kComplete) {
          MessageBoxW(dialog, L"This download has not finished yet.",
                      L"MTK Browser", MB_OK | MB_ICONINFORMATION);
          return TRUE;
        }
        RevealInExplorer(dialog, item->full_path);
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_PAUSE) {
        const auto* item = SelectedDownload(dialog);
        if (!item || (item->id >> 32) != 0) return TRUE;
        const uint32_t live_id = static_cast<uint32_t>(item->id);
        if (item->state == mtk::downloads::State::kPaused) {
          BrowserClient::ResumeDownload(live_id);
        } else if (item->state == mtk::downloads::State::kStarting ||
                   item->state == mtk::downloads::State::kInProgress) {
          BrowserClient::PauseDownload(live_id);
        }
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_CANCEL) {
        const auto* item = SelectedDownload(dialog);
        if (!item) return TRUE;
        if (item->state != mtk::downloads::State::kStarting &&
            item->state != mtk::downloads::State::kInProgress &&
            item->state != mtk::downloads::State::kPaused) {
          mtk::downloads::Manager::Get().Remove(item->id);
          mtk::downloads::Manager::Get().Flush();
          PopulateDownloadList(dialog);
          return TRUE;
        }
        if ((item->id >> 32) == 0) {
          BrowserClient::CancelDownload(static_cast<uint32_t>(item->id));
        }
        return TRUE;
      }
      if (command == IDC_DOWNLOADS_CLEAR) {
        mtk::downloads::Manager::Get().ClearFinished();
        mtk::downloads::Manager::Get().Flush();
        PopulateDownloadList(dialog);
        return TRUE;
      }
      if (command == IDCANCEL) {
        EndDialog(dialog, 0);
        return TRUE;
      }
      break;
    }
    case WM_DESTROY:
      if (g_downloads_dialog == dialog) g_downloads_dialog = nullptr;
      break;
    case WM_CLOSE:
      EndDialog(dialog, 0);
      return TRUE;
  }
  return FALSE;
}

}  // namespace

void BrowserWindow::StartUpdateCheck(bool user_initiated) {
  if (update_check_running_) return;
  if (!user_initiated &&
      !mtk::preferences::Store::Get().values().auto_update) {
    return;
  }
  update_check_running_ = true;
  update_check_user_initiated_ = user_initiated;
  HWND window = window_;
  // The check does network I/O, so it must not run on the UI thread. The worker
  // only fills a result and posts a message; it never touches browser state.
  std::thread([window]() {
    (void)mtk::update::CheckAndUpdateFilters();
    g_update_result = mtk::update::Check();
    PostMessageW(window, kUpdateResultMessage, 0, 0);
  }).detach();
}

void BrowserWindow::HandleUpdateResult() {
  update_check_running_ = false;
  const mtk::update::Result result = g_update_result;
  const bool user_initiated = update_check_user_initiated_;
  update_check_user_initiated_ = false;

  switch (result.status) {
    case mtk::update::Status::kUpdateReady: {
      // The installer has already been signature- and checksum-verified, but it
      // is still the user who decides when to install.
      const std::wstring message =
          result.detail +
          L"\n\nInstalling closes MTK Browser and runs the verified installer."
          L"\n\nInstall now?";
      if (MessageBoxW(window_, message.c_str(), L"MTK Browser update",
                      MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        if (mtk::update::LaunchInstaller(result.installer_path)) {
          PostMessageW(window_, WM_CLOSE, 0, 0);
        } else {
          MessageBoxW(window_, L"The update could not be started.",
                      L"MTK Browser update", MB_OK | MB_ICONERROR);
        }
      }
      break;
    }
    case mtk::update::Status::kRejected:
      // A failed signature is a security event, so it is always surfaced.
      MessageBoxW(window_, result.detail.c_str(), L"MTK Browser update refused",
                  MB_OK | MB_ICONWARNING);
      break;
    case mtk::update::Status::kUpToDate:
    case mtk::update::Status::kDisabled:
    case mtk::update::Status::kCheckFailed:
      // Background noise stays silent; an explicit check always answers.
      if (user_initiated) {
        MessageBoxW(window_, result.detail.c_str(), L"MTK Browser update",
                    MB_OK | MB_ICONINFORMATION);
      }
      break;
  }
}

void BrowserWindow::ShowClearDataDialog() {
  // Erasing history is also how a supervised user would hide activity, so this
  // sits behind the parent PIN when one has been set.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Clearing browsing data is protected. Enter the parent PIN.")) {
    return;
  }
  ClearDataChoice choice;
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_CLEAR_DATA), window_,
                      ClearDataDialogProc,
                      reinterpret_cast<LPARAM>(&choice)) != IDOK) {
    return;
  }

  std::wstring report;
  auto& library = mtk::library::Store::Get();
  bool library_changed = false;
  if (choice.history && library.ClearHistory()) {
    library_changed = true;
    report += L"\n• Browsing history removed";
  }
  if (choice.bookmarks && library.ClearBookmarks()) {
    library_changed = true;
    report += L"\n• Bookmarks removed";
  }
  if (library_changed && !library.Flush()) {
    MessageBoxW(window_,
                L"MTK Browser could not save the cleared history to disk.",
                L"Clear browsing data", MB_OK | MB_ICONERROR);
  }

  if (choice.downloads) {
    // Only finished rows are removed; a running transfer keeps its entry so it
    // can still be paused or canceled.
    const size_t removed = mtk::downloads::Manager::Get().ClearFinished();
    mtk::downloads::Manager::Get().Flush();
    report += L"\n• Download history cleared (" + std::to_wstring(removed) +
              L" finished entries; files on disk kept)";
    UpdateControls();
  }

  if (choice.cookies) {
    if (auto manager = CefCookieManager::GetGlobalManager(nullptr);
        manager && manager->DeleteCookies(CefString(), CefString(), nullptr)) {
      report += L"\n• Cookies and site data cleared";
    } else {
      report += L"\n• Cookies could not be cleared";
    }
  }

  if (choice.cache) {
    // CEF exposes no direct cache-clearing API, so the DevTools command is used
    // through the active browser. It applies to the whole request context.
    if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
      tab->client->browser()->GetHost()->ExecuteDevToolsMethod(
          0, "Network.clearBrowserCache", nullptr);
      report += L"\n• Cached files and images cleared";
    } else {
      report += L"\n• Cache could not be cleared: no page is open";
    }
  }

  if (choice.session) {
    report += mtk::session::Clear() ? L"\n• Saved tabs from the last session removed"
                                    : L"\n• No saved session to remove";
  }

  if (choice.site_settings) {
    auto& sites = mtk::sites::Store::Get();
    report += sites.Clear() && sites.Flush()
                  ? L"\n• Per-site settings reset to the defaults"
                  : L"\n• Per-site settings could not be reset";
  }

  if (choice.passwords) {
    auto& store = mtk::passwords::Store::Get();
    report += store.Clear() && store.Flush()
                  ? L"\n• Saved passwords deleted"
                  : L"\n• Saved passwords could not be deleted";
  }

  // The home dashboard renders from the stores, so it must not keep showing
  // rows that were just deleted.
  RefreshHomeSnapshot();

  MessageBoxW(window_, (L"Clear browsing data finished." + report).c_str(),
              L"Clear browsing data", MB_OK | MB_ICONINFORMATION);
}

void BrowserWindow::ShowSettingsDialog() {
  // Settings hold the safety-relevant choices (home page, download mode), so
  // they sit behind the parent PIN once one has been set.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Settings are protected. Enter the parent PIN to open them.")) {
    return;
  }
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SETTINGS), window_,
                      SettingsDialogProc, 0) == IDOK &&
      mtk::preferences::Store::Get().values().startup_mode ==
          mtk::preferences::StartupMode::kHomePage) {
    mtk::session::Clear();
  }
}

void BrowserWindow::ShowBrowserMenu() {
  HMENU popup = CreatePopupMenu();
  if (!popup) return;
  auto* tab = ActiveTab();
  const auto decision = tab ? mtk::policy::EvaluateNavigation(WideToUtf8(tab->url))
                            : mtk::policy::NavigationDecision{false, {}, L""};
  const bool can_bookmark = decision.allow && !tab->url.starts_with(L"data:");
  const bool bookmarked = can_bookmark &&
      mtk::library::Store::Get().IsBookmarked(decision.url);
  using Text = mtk::strings::Id;
  const auto label = [](Text id, const wchar_t* accelerator = nullptr) {
    std::wstring text = mtk::strings::Get(id);
    if (accelerator) text += accelerator;
    return text;
  };
  const bool hindi = mtk::strings::IsHindi();

  AppendMenuW(popup, MF_STRING, IDM_TAB_SEARCH,
              hindi ? L"टैब खोजें\tAlt+Q" : L"Search tabs\tAlt+Q");
  AppendMenuW(popup, MF_STRING | (can_bookmark ? MF_ENABLED : MF_GRAYED),
              IDM_ADD_BOOKMARK,
              label(bookmarked ? Text::kRemoveBookmark : Text::kAddBookmark,
                    L"\tCtrl+D").c_str());
  AppendMenuW(popup, MF_STRING, IDM_BOOKMARKS,
              label(Text::kBookmarks, L"\tAlt+O").c_str());
  AppendMenuW(popup, MF_STRING, IDM_VERTICAL_TABS,
              hindi ? (mtk::preferences::Store::Get().values().vertical_tabs
                          ? L"वर्टिकल टैब छिपाएँ\tAlt+Shift+V"
                          : L"वर्टिकल टैब दिखाएँ\tAlt+Shift+V")
                    : (mtk::preferences::Store::Get().values().vertical_tabs
                          ? L"Hide vertical tabs\tAlt+Shift+V"
                          : L"Show vertical tabs\tAlt+Shift+V"));
  if (mtk::preferences::Store::Get().values().vertical_tabs) {
    AppendMenuW(popup, MF_STRING, IDM_VERTICAL_TABS_COLLAPSE,
                hindi ? (mtk::preferences::Store::Get().values().vertical_tabs_collapsed
                            ? L"वर्टिकल टैब फैलाएँ"
                            : L"वर्टिकल टैब समेटें")
                      : (mtk::preferences::Store::Get().values().vertical_tabs_collapsed
                            ? L"Expand vertical tabs"
                            : L"Collapse vertical tabs"));
  }
  AppendMenuW(popup, MF_STRING, IDM_IMPORT_BOOKMARKS,
              label(Text::kImportBookmarks).c_str());
  AppendMenuW(popup, MF_STRING, IDM_HISTORY,
              label(Text::kHistory, L"\tCtrl+H").c_str());
  AppendMenuW(popup, MF_STRING, IDM_PASSWORDS,
              label(Text::kSavedPasswords, L"\tAlt+K").c_str());

  const size_t active_downloads = mtk::downloads::Manager::Get().ActiveCount();
  std::wstring downloads_label = label(Text::kDownloads, L"\tAlt+J");
  if (active_downloads > 0) {
    std::wstring pattern = mtk::strings::Get(Text::kDownloadsActive);
    if (const size_t placeholder = pattern.find(L"%d");
        placeholder != std::wstring::npos) {
      pattern.replace(placeholder, 2, std::to_wstring(active_downloads));
    }
    downloads_label = pattern + L"\tAlt+J";
  }
  AppendMenuW(popup, MF_STRING, IDM_DOWNLOADS, downloads_label.c_str());
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);

  AppendMenuW(popup, MF_STRING, IDM_NEW_WINDOW,
              label(Text::kNewWindow, L"\tAlt+W").c_str());
  AppendMenuW(popup, MF_STRING, IDM_NEW_PRIVATE_WINDOW,
              label(Text::kNewPrivateWindow, L"\tAlt+Shift+W").c_str());
  AppendMenuW(popup,
              MF_STRING | (closed_tab_urls_.empty() ? MF_GRAYED : MF_ENABLED),
              IDM_REOPEN_TAB,
              label(Text::kReopenClosedTab, L"\tAlt+Shift+T").c_str());
  AppendMenuW(popup, MF_STRING, IDM_FULL_SCREEN,
              label(full_screen_ ? Text::kExitFullScreen : Text::kFullScreen,
                    L"\tAlt+Enter").c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_PRINT,
              label(Text::kPrint, L"\tCtrl+P").c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_SAVE_PAGE_PDF,
              label(Text::kSavePageAsPdf).c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_TAKE_SCREENSHOT,
              label(Text::kTakeScreenshot, L"\tCtrl+Shift+S").c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_READER_MODE,
              label(tab && tab->reader_mode ? Text::kExitReaderMode : Text::kReaderMode,
                    L"\tAlt+E").c_str());

  const bool focus_on = tab ? tab->focus_mode : mtk::preferences::Store::Get().values().focus_mode;
  AppendMenuW(popup, MF_STRING | (focus_on ? MF_CHECKED : MF_UNCHECKED), IDM_FOCUS_MODE,
              label(focus_on ? Text::kFocusModeOn : Text::kFocusModeOff, L"\tAlt+Shift+F").c_str());

  const bool in_reading_list = decision.allow &&
      mtk::library::Store::Get().IsInReadingList(decision.url);
  AppendMenuW(popup, MF_STRING | (decision.allow ? MF_ENABLED : MF_GRAYED),
              IDM_READING_LIST_TOGGLE,
              label(in_reading_list ? Text::kRemoveFromReadingList : Text::kSaveToReadingList).c_str());
  AppendMenuW(popup, MF_STRING, IDM_READING_LIST,
              label(Text::kReadingList).c_str());

  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_TRANSLATE_HINDI,
              label(Text::kTranslateToHindi).c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_TRANSLATE_ENGLISH,
              label(Text::kTranslateToEnglish).c_str());

  const bool can_web_app = tab && !tab->url.empty() && tab->url.starts_with(L"https://");
  AppendMenuW(popup, MF_STRING | (can_web_app ? MF_ENABLED : MF_GRAYED),
              IDM_OPEN_AS_WEB_APP, label(Text::kInstallWebApp).c_str());
  AppendMenuW(popup, MF_STRING, IDM_MANAGE_WEB_APPS,
              label(Text::kManageWebApps).c_str());
  AppendMenuW(popup, MF_STRING, IDM_PASSKEY_CHECK,
              label(Text::kPasskeyCheck).c_str());
  AppendMenuW(popup, MF_STRING, IDM_TASK_MANAGER,
              label(Text::kTaskManager, L"\tShift+Esc").c_str());
  AppendMenuW(popup,
              MF_STRING | (!tabs_.empty() ? MF_ENABLED : MF_GRAYED),
              IDM_SPLIT_VIEW,
              label(IsSplitViewActive() ? Text::kExitSplitView : Text::kSplitView,
                    L"\tAlt+Shift+S").c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_TAB_GROUP,
              hindi ? L"टैब समूह बदलें..." : L"Set tab group...");
  AppendMenuW(popup, MF_STRING, IDM_TAB_GROUPS,
              hindi ? L"टैब समूह..." : L"Tab groups...");
  AppendMenuW(popup, MF_STRING, IDM_WORKSPACES_MENU,
              hindi ? L"वर्कस्पेस..." : L"Workspaces...");
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED), IDM_SET_WORKSPACE,
              hindi ? L"वर्कस्पेस बदलें..." : L"Set workspace...");
  AppendMenuW(popup, MF_STRING, IDM_DIAGNOSTICS,
              hindi ? L"ब्राउज़र जानकारी..." : L"Browser diagnostics...");
  AppendMenuW(popup, MF_STRING, IDM_SHORTCUTS,
              label(Text::kKeyboardShortcuts, L"\tAlt+/").c_str());
  AppendMenuW(popup, MF_STRING | (tab ? MF_ENABLED : MF_GRAYED),
              IDM_SITE_SETTINGS, label(Text::kSiteSettings, L"\tAlt+S").c_str());
  AppendMenuW(popup, MF_STRING, IDM_SITE_LIST, label(Text::kSiteList).c_str());
  AppendMenuW(popup, MF_STRING, IDM_USER_FILTERS,
              label(Text::kUserFilters, L"\tAlt+Y").c_str());
  AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);

  // Profiles
  if (HMENU profiles = CreatePopupMenu()) {
    const auto available = mtk::profile::List();
    profile_menu_ids_.clear();
    for (size_t index = 0; index < available.size() &&
                           index < kMaxProfileMenuEntries;
         ++index) {
      const bool active = available[index] == mtk::profile::ActiveId();
      AppendMenuW(profiles,
                  MF_STRING | (active ? MF_CHECKED | MF_GRAYED : MF_ENABLED),
                  static_cast<UINT_PTR>(IDM_PROFILE_FIRST + index),
                  available[index].c_str());
      profile_menu_ids_.push_back(available[index]);
    }
    AppendMenuW(profiles, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(profiles, MF_STRING, IDM_PROFILE_NEW,
                label(Text::kNewProfile).c_str());
    const std::wstring profile_label =
        std::wstring(mtk::strings::Get(Text::kProfilePrefix)) +
        mtk::profile::ActiveId();
    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles),
                profile_label.c_str());
  }
  AppendMenuW(popup, MF_STRING, IDM_CLEAR_DATA,
              label(Text::kClearBrowsingData).c_str());
  AppendMenuW(popup,
              MF_STRING | (update_check_running_ ? MF_GRAYED : MF_ENABLED),
              IDM_CHECK_UPDATES,
              label(update_check_running_ ? Text::kCheckingForUpdates
                                          : Text::kCheckForUpdates)
                  .c_str());
  const bool is_default = mtk::default_browser::IsDefault();
  AppendMenuW(popup, MF_STRING | (is_default ? MF_GRAYED : MF_ENABLED),
              IDM_MAKE_DEFAULT,
              label(is_default ? Text::kIsDefaultBrowser
                               : Text::kMakeDefaultBrowser)
                  .c_str());
  AppendMenuW(popup, MF_STRING, IDM_SETTINGS, label(Text::kSettings).c_str());

  RECT menu_rc{};
  if (menu_) GetWindowRect(menu_, &menu_rc);
  POINT point{menu_rc.right, menu_rc.bottom};
  if (!menu_) GetCursorPos(&point);
  const UINT command = TrackPopupMenu(
      popup, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
      point.x, point.y, 0, window_, nullptr);
  DestroyMenu(popup);
  if (command) PostMessageW(window_, WM_COMMAND, command, 0);
}


void BrowserWindow::ShowLibraryDialog() {
  const INT_PTR selection = DialogBoxParamW(
      instance_, MAKEINTRESOURCEW(IDD_LIBRARY), window_, LibraryDialogProc, 0);
  if (selection > 0) OpenLibraryEntry(static_cast<size_t>(selection - 1));
}

void BrowserWindow::OpenLibraryEntry(size_t index) {
  const auto& entries = mtk::library::Store::Get().bookmarks();
  if (index >= entries.size()) return;
  const auto decision = mtk::policy::EvaluateNavigation(entries[index].url);
  if (!decision.allow) {
    MessageBoxW(window_, decision.reason.c_str(), L"MTK Browser blocked this link",
                MB_OK | MB_ICONWARNING);
    return;
  }
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    tab->client->browser()->GetMainFrame()->LoadURL(decision.url);
  }
}

void BrowserWindow::OpenHistoryPage() {
  // The snapshot is republished first, so the page cannot open showing a list
  // that is one visit out of date.
  RefreshHomeSnapshot();
  auto* tab = ActiveTab();
  if (tab && tab->client->browser() &&
      !mtk::history::IsHistoryUrl(WideToUtf8(tab->url))) {
    tab->client->browser()->GetMainFrame()->LoadURL(mtk::history::kUrl);
    return;
  }
  if (!tab) CreateTab(mtk::history::kUrl);
}

void BrowserWindow::ShowDownloadsDialog() {
  DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_DOWNLOADS), window_,
                  DownloadsDialogProc, 0);
}


void BrowserWindow::ShowUserFiltersDialog() {
  // A rule can allow as well as block, so editing them is a parent action even
  // though the safety rules stay in force either way.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Filter rules are protected. Enter the parent PIN to change them.")) {
    return;
  }
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_USER_FILTERS), window_,
                      UserFiltersDialogProc, 0) != IDOK) {
    return;
  }
  // Rules apply to requests from now on, and a reload is what makes the change
  // visible on the page in front of you.
  if (auto* tab = ActiveTab(); tab && tab->client->browser()) {
    tab->client->browser()->ReloadIgnoreCache();
  }
}

void BrowserWindow::MakeDefaultBrowser() {
  if (!mtk::default_browser::Register()) {
    MessageBoxW(window_,
                L"MTK Browser could not register itself with Windows, so it "
                L"cannot be offered as the default browser.",
                L"Default browser", MB_OK | MB_ICONERROR);
    return;
  }
  // Windows 10 and 11 do not allow a program to make itself the default, so the
  // honest step is to say what happens next rather than to appear to do it.
  if (MessageBoxW(window_,
                  L"Windows asks you to confirm this yourself.\n\nMTK Browser "
                  L"is now registered, and the Windows \"Default apps\" page "
                  L"will open. Choose MTK Browser for HTTP and HTTPS there.",
                  L"Default browser", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
    return;
  }
  if (!mtk::default_browser::PromptUserToChoose(window_)) {
    MessageBoxW(window_,
                L"The Windows settings page could not be opened. Open Settings "
                L"> Apps > Default apps and choose MTK Browser there.",
                L"Default browser", MB_OK | MB_ICONWARNING);
  }
}

void BrowserWindow::ShowPasswordsDialog() {
  // Filling a saved password is the everyday case and is not gated. Reading the
  // list is, because that is where someone could walk off with a parent's
  // credentials.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Saved passwords are protected. Enter the parent PIN to see them.")) {
    return;
  }
  DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_PASSWORDS), window_,
                  PasswordsDialogProc, 0);
}

void BrowserWindow::ShowSiteSettingsDialog() {
  auto* tab = ActiveTab();
  const std::string origin =
      tab ? mtk::sites::OriginOf(WideToUtf8(tab->url)) : std::string();
  if (origin.empty()) {
    MessageBoxW(window_,
                L"This page has no site settings. Open an ordinary web page "
                L"first.",
                L"Site settings", MB_OK | MB_ICONINFORMATION);
    return;
  }
  ShowSiteSettingsDialogFor(origin);
}

void BrowserWindow::ShowSiteSettingsDialogFor(const std::string& origin) {
  if (origin.empty()) return;
  // Allowing a camera or a microphone is a safety decision, so the dialog sits
  // behind the parent PIN like the rest of Settings.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Site settings are protected. Enter the parent PIN to change them.")) {
    return;
  }

  auto& store = mtk::sites::Store::Get();
  SiteSettingsContext context{store.For(origin), false};
  if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SITE_SETTINGS), window_,
                      SiteSettingsDialogProc,
                      reinterpret_cast<LPARAM>(&context)) != IDOK) {
    return;
  }

  const bool changed = context.forget ? store.Forget(origin)
                                      : store.Set(context.rule);
  if (!changed || !store.Flush()) {
    MessageBoxW(window_, L"MTK Browser could not save the site settings.",
                L"Site settings", MB_OK | MB_ICONERROR);
    return;
  }
  // Cookie and filtering changes take effect on the next load, and the zoom is
  // applied straight away so the result is visible, but only for a tab that is
  // actually showing this origin: a rule opened from the site list may not
  // match the page in front of the user right now.
  auto* tab = ActiveTab();
  if (tab && mtk::sites::OriginOf(WideToUtf8(tab->url)) == origin) {
    ApplySiteZoom(*tab);
    if (tab->client->browser()) tab->client->browser()->ReloadIgnoreCache();
  }
}

void BrowserWindow::ShowSiteListDialog() {
  // Reviewing or clearing another site's stored data is exactly the kind of
  // decision the parent PIN already protects for the single-site dialog.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Site settings are protected. Enter the parent PIN to see this "
          L"list.")) {
    return;
  }
  for (;;) {
    SiteListContext context;
    const INT_PTR result =
        DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SITE_LIST), window_,
                        SiteListDialogProc,
                        reinterpret_cast<LPARAM>(&context));
    if (result != IDOK) return;

    if (!context.open_origin.empty()) {
      ShowSiteSettingsDialogFor(context.open_origin);
      continue;  // Back to the (possibly now-different) list.
    }
    if (context.clear_origin.empty()) return;

    const std::string& origin = context.clear_origin;
    std::wstring report;

    auto& sites = mtk::sites::Store::Get();
    if (sites.Forget(origin) && sites.Flush()) {
      report += L"\n• Stored settings for this site removed";
    }

    auto& library = mtk::library::Store::Get();
    const size_t history_removed = library.RemoveHistoryForOrigin(origin);
    if (history_removed > 0) {
      if (library.Flush()) {
        report += L"\n• " + std::to_wstring(history_removed) +
                  L" history entries for this site removed";
      }
    }

    auto& passwords = mtk::passwords::Store::Get();
    const size_t passwords_removed = passwords.RemoveAllForOrigin(origin);
    if (passwords_removed > 0) {
      if (passwords.Flush()) {
        report += L"\n• " + std::to_wstring(passwords_removed) +
                  L" saved password(s) for this site removed";
      }
    }

    // Cookies are scoped to the origin's own URL; CEF matches on host, so
    // this does not touch a different site.
    if (auto manager = CefCookieManager::GetGlobalManager(nullptr);
        manager && manager->DeleteCookies(CefString(Utf8ToWide(origin) + L"/"),
                                          CefString(), nullptr)) {
      report += L"\n• Cookies for this site cleared";
    }

    if (!report.empty()) {
      MessageBoxW(window_, (L"Cleared for this site:" + report).c_str(),
                  L"Sites with stored settings", MB_OK | MB_ICONINFORMATION);
    }
    // Reopens the list, now missing the site that was just cleared (unless
    // clearing partly failed and the rule is still there).
  }
}

bool BrowserWindow::PromptForFolderName(const wchar_t* caption,
                                        const wchar_t* message,
                                        std::wstring* name) {
  std::wstring entered;
  if (!PromptForText(instance_, window_, caption, message, &entered)) {
    return false;
  }
  const std::wstring normalized = mtk::library::NormalizeFolderName(entered);
  if (normalized.empty()) {
    MessageBoxW(window_,
                L"That folder name cannot be used.\n\nUse up to 64 characters, "
                L"with no control characters.",
                L"Bookmark folders", MB_OK | MB_ICONWARNING);
    return false;
  }
  *name = normalized;
  return true;
}


void BrowserWindow::LaunchProfile(const std::wstring& profile_id) {
  if (!mtk::profile::IsValidId(profile_id)) return;
  wchar_t executable[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    MessageBoxW(window_, L"MTK Browser could not find its own program file.",
                L"Profiles", MB_OK | MB_ICONERROR);
    return;
  }
  // The id is validated above, so it cannot contain a quote or a space and the
  // command line cannot be broken out of.
  std::wstring command_line = L"\"" + std::wstring(executable) + L"\"";
  if (profile_id != mtk::profile::kDefaultId) {
    command_line += L" --profile=" + profile_id;
  }
  STARTUPINFOW startup{sizeof(STARTUPINFOW)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable, command_line.data(), nullptr, nullptr, FALSE,
                      0, nullptr, nullptr, &startup, &process)) {
    MessageBoxW(window_, L"That profile could not be opened.", L"Profiles",
                MB_OK | MB_ICONERROR);
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

void BrowserWindow::CreateProfile() {
  // Profiles hold separate browsing data, so creating one is a parent-level
  // action and sits behind the PIN like the other safety settings.
  if (!AuthorizeParentAction(
          instance_, window_,
          L"Profiles are protected. Enter the parent PIN to add one.")) {
    return;
  }
  std::wstring name;
  if (!PromptForText(instance_, window_, L"New profile",
                     L"Profile name (letters, digits, '-' and '_')", &name)) {
    return;
  }
  if (!mtk::profile::IsValidId(name) || name == mtk::profile::kDefaultId) {
    MessageBoxW(window_,
                L"That name cannot be used. Use up to 32 letters, digits, '-' "
                L"or '_', and not \"Default\".",
                L"Profiles", MB_OK | MB_ICONWARNING);
    return;
  }
  if (!mtk::profile::Create(name)) {
    MessageBoxW(window_, L"That profile could not be created.", L"Profiles",
                MB_OK | MB_ICONERROR);
    return;
  }
  LaunchProfile(name);
}

