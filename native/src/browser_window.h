#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "browser_client.h"

class BrowserWindow final : public BrowserClientDelegate {
 public:
  explicit BrowserWindow(HINSTANCE instance);
  ~BrowserWindow();

  bool Create(int show_command);

  void OnBrowserCreated(BrowserClient* client) override;
  void OnBrowserCloseReady(BrowserClient* client) override;
  void OnBrowserClosed(BrowserClient* client) override;
  void OnBrowserAddressChanged(BrowserClient* client,
                               const std::wstring& url) override;
  void OnBrowserTitleChanged(BrowserClient* client,
                             const std::wstring& title) override;
  void OnBrowserLoadingChanged(BrowserClient* client, bool loading,
                               bool can_go_back,
                               bool can_go_forward) override;
  void OnBrowserNavigationCompleted(BrowserClient* client,
                                    const std::string& url) override;
  void OnBrowserFindResult(BrowserClient* client, int identifier, int count,
                           int active_match_ordinal,
                           bool final_update) override;
  void OnBrowserPopupRequested(BrowserClient* client,
                               const std::string& url) override;
  bool OnBrowserShortcut(BrowserClient* client, int virtual_key,
                         bool control, bool alt, bool shift) override;

 private:
  struct Tab {
    CefRefPtr<BrowserClient> client;
    std::wstring title = L"New Tab";
    std::wstring url;
    bool loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool close_ready = false;
  };

  static LRESULT CALLBACK WindowProc(HWND window, UINT message,
                                     WPARAM w_param, LPARAM l_param);
  static LRESULT CALLBACK AddressProc(HWND window, UINT message,
                                      WPARAM w_param, LPARAM l_param,
                                      UINT_PTR subclass_id,
                                      DWORD_PTR reference_data);
  static LRESULT CALLBACK FindProc(HWND window, UINT message,
                                   WPARAM w_param, LPARAM l_param,
                                   UINT_PTR subclass_id,
                                   DWORD_PTR reference_data);
  LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
  bool HandleShortcut(int virtual_key, bool control, bool alt, bool shift);
  void CreateControls();
  void CreateTab(const std::string& initial_url);
  void ShowFindBar();
  void HideFindBar();
  void RunFind(bool forward, bool find_next);
  void ZoomActiveTab(cef_zoom_command_t command);
  void ShowBrowserMenu();
  void ShowSettingsDialog();
  void ToggleBookmark();
  void ShowLibraryDialog(bool bookmarks);
  void OpenLibraryEntry(bool bookmarks, size_t index);
  void ScheduleLibrarySave();
  void SelectTab(size_t index);
  void CloseTab(size_t index);
  void NavigateFromOmnibox();
  void Layout();
  void Paint(HDC dc);
  void PaintTabStrip(HDC dc, const RECT& client);
  void PaintOmniboxFrame(HDC dc);
  void PaintProgress(HDC dc, const RECT& client);
  void DrawButton(const DRAWITEMSTRUCT& item);
  void TrackHover(POINT point);
  void ClearHover();
  void UpdateControls();
  void ResizeBrowser(Tab& tab);
  size_t FindTab(BrowserClient* client) const;
  Tab* ActiveTab();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND address_ = nullptr;
  HWND find_ = nullptr;
  HWND find_status_ = nullptr;
  HWND find_previous_ = nullptr;
  HWND find_next_ = nullptr;
  HWND find_close_ = nullptr;
  HWND go_ = nullptr;
  HWND back_ = nullptr;
  HWND forward_ = nullptr;
  HWND reload_ = nullptr;
  HWND home_ = nullptr;
  HWND shield_ = nullptr;
  HWND menu_ = nullptr;
  HFONT ui_font_ = nullptr;
  HFONT tab_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT badge_font_ = nullptr;
  HBRUSH edit_brush_ = nullptr;
  std::vector<Tab> tabs_;
  size_t active_tab_ = 0;
  size_t hovered_tab_ = static_cast<size_t>(-1);
  bool hovered_tab_close_ = false;
  bool hovered_new_tab_ = false;
  bool mouse_tracking_ = false;
  bool find_visible_ = false;
  bool find_active_ = false;
  int active_find_identifier_ = -1;
  bool progress_active_ = false;
  int progress_phase_ = 0;
  bool closing_ = false;
};
