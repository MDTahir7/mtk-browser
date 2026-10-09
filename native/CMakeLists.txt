set(MTK_BROWSER_SOURCES
  src/main.cpp
  src/mtk_browser.rc
  src/resource.h
  resources/mtk_browser.ico
  src/preferences.cpp
  src/preferences.h
  src/profile.cpp
  src/profile.h
  src/site_settings.cpp
  src/site_settings.h
  src/downloads_panel.cpp
  src/downloads_panel.h
  src/screenshot_panel.cpp
  src/screenshot_panel.h
  src/strings.cpp
  src/strings.h
  src/dialog_text.cpp
  src/dialog_text.h
  src/password_store.cpp
  src/password_store.h
  src/private_session.cpp
  src/private_session.h
  src/user_filters.cpp
  src/user_filters.h
  src/crash_guard.cpp
  src/crash_guard.h
  src/default_browser.cpp
  src/default_browser.h
  src/password_bridge.cpp
  src/password_bridge.h
  src/library_store.cpp
  src/library_store.h
  src/bookmark_import.cpp
  src/bookmark_import.h
  src/download_manager.cpp
  src/download_manager.h
  src/downloads_page.cpp
  src/downloads_page.h
  src/home_page.cpp
  src/home_page.h
  src/history_page.cpp
  src/history_page.h
  src/home_scheme.cpp
  src/home_scheme.h
  src/passkey_page.cpp
  src/passkey_page.h
  src/settings_page.cpp
  src/settings_page.h
  src/session_store.cpp
  src/session_store.h
  src/icon_strip.cpp
  src/icon_strip.h
  src/mtk_app.cpp
  src/mtk_app.h
  src/fingerprint_script.cpp
  src/fingerprint_script.h
  src/browser_client.cpp
  src/browser_client.h
  src/fullpage_screenshot.cpp
  src/browser_window.cpp
  src/browser_window.h
  src/memory_saver.h
  src/tab_search.cpp
  src/tab_search.h
  src/task_manager_dialog.cpp
  src/task_manager_dialog.h
  src/translation.cpp
  src/translation.h
  src/web_app.cpp
  src/web_app.h
  src/autofill_store.cpp
  src/autofill_store.h
  src/windows_hello.cpp
  src/windows_hello.h
  src/inline_asset.cpp
  src/inline_asset.h
  src/shields_panel.cpp
  src/shields_panel.h
  src/policy.cpp
  src/policy.h
  src/filter_engine.cpp
  src/filter_engine.h
  src/updater.cpp
  src/updater.h
  src/update_public_key.h
  src/builtin_favicons.cpp
  src/builtin_favicons.h
  src/favicon_store.cpp
  src/favicon_store.h
  resources/mtk_filters.txt
  resources/mtk_block_shield.webm
  resources/mtk_siren_alert.webm
  resources/mtk_shield_hover.png
  resources/mtk_home_hover.png
  resources/mtk_search_3d.webp
  resources/mtk_search_3d_still.webp
  resources/mtk_search_web_3d.webp
  resources/mtk_search_web_3d_still.webp
  resources/mtk_ask_ai_3d.webp
  resources/mtk_ask_ai_3d_still.webp
  resources/mtk_publishing_3d.webp
  resources/mtk_publishing_3d_still.webp
  resources/mtk_home_3d.webp
  resources/mtk_home_3d_still.webp
  resources/mtk_store_3d.webp
  resources/mtk_store_3d_still.webp
  resources/mtk_store_hover.png
  resources/mtk_notes_3d.webp
  resources/mtk_notes_3d_still.webp
  resources/mtk_notes_hover.png
  resources/mtk_download_3d.webp
  resources/mtk_download_3d_still.webp
  resources/mtk_download_hover.png
  resources/mtk_bookmarks_3d.webp
  resources/mtk_bookmarks_3d_still.webp
  resources/mtk_bookmarks_hover.png
  resources/mtk_focus_3d.webp
  resources/mtk_focus_3d_still.webp
  resources/mtk_focus_strip.png
  resources/mtk_refresh_3d.webp
  resources/mtk_refresh_3d_still.webp
  resources/mtk_screenshot_3d.webp
  resources/mtk_screenshot_3d_still.webp
  resources/mtk_screenshot_hover.png
  resources/mtk_screenshot_strip.png
)


SET_CEF_TARGET_OUT_DIR()

if(MTK_ENABLE_CEF_SANDBOX)
  # Sandboxed packaging: this application is a DLL loaded by CEF's bootstrap
  # executable, which sets up the Chromium sandbox before calling RunWinMain.
  add_library(mtk_browser SHARED ${MTK_BROWSER_SOURCES})
  SET_LIBRARY_TARGET_PROPERTIES(mtk_browser)
else()
  add_executable(mtk_browser WIN32 ${MTK_BROWSER_SOURCES})
  SET_EXECUTABLE_TARGET_PROPERTIES(mtk_browser)
endif()

target_include_directories(mtk_browser PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The sources are UTF-8 without a byte order mark. Without this, MSVC reads them
# in the system ANSI code page, which silently turns every non-ASCII literal
# into mojibake: an em dash became "â€”" and the Hindi UI strings would be
# unreadable. It compiles either way, so the flag is the only thing that makes
# the text correct.
if(MSVC)
  target_compile_options(mtk_browser PRIVATE /utf-8)
endif()

target_link_libraries(mtk_browser PRIVATE
  libcef_lib
  libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}
  dwmapi.lib
  uxtheme.lib
  shell32.lib
  advapi32.lib
  ole32.lib
  comctl32.lib
  comdlg32.lib
  # Windows CNG, used for the parent-PIN PBKDF2 verifier and update signatures.
  bcrypt.lib
  # HTTPS update checks and base64 decoding, both from the OS.
  winhttp.lib
  crypt32.lib
  # Windows Imaging Component decodes and scales the toolbar icon strip, and
  # msimg32 provides AlphaBlend for compositing its premultiplied frames.
  windowscodecs.lib
  msimg32.lib
  # Windows Runtime implementation of the optional Windows Hello verification.
  windowsapp.lib
  # AssocQueryString, used to ask Windows which program currently handles https.
  shlwapi.lib
  gdiplus.lib
)

set_target_properties(mtk_browser PROPERTIES
  OUTPUT_NAME "MTKBrowser"
  VS_DEBUGGER_WORKING_DIRECTORY "${CEF_TARGET_OUT_DIR}"
)

# Stage the MTK Shields filter list and the two rendered loops beside the
# executable so all three can be loaded at runtime.
add_custom_command(TARGET mtk_browser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_filters.txt"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_filters.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_block_shield.webm"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_block_shield.webm"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_siren_alert.webm"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_siren_alert.webm"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_shield_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_shield_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_search_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_search_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_search_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_search_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_search_web_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_search_web_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_search_web_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_search_web_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_ask_ai_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_ask_ai_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_ask_ai_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_ask_ai_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_home_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_home_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_home_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_home_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_home_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_home_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_store_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_store_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_store_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_store_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_store_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_store_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_notes_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_notes_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_notes_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_notes_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_notes_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_notes_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_download_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_download_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_download_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_download_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_download_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_download_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_history_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_history_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_history_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_history_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_history_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_history_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_settings_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_settings_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_settings_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_settings_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_settings_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_settings_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_bookmarks_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_bookmarks_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_bookmarks_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_bookmarks_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_bookmarks_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_bookmarks_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_focus_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_focus_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_focus_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_focus_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_focus_strip.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_focus_strip.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_refresh_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_refresh_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_refresh_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_refresh_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_refresh_strip.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_refresh_strip.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_screenshot_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_screenshot_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_screenshot_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_screenshot_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_screenshot_hover.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_screenshot_hover.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_screenshot_strip.png"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_screenshot_strip.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_search_web_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_search_web_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_search_web_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_search_web_3d_still.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_publishing_3d.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_publishing_3d.webp"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_publishing_3d_still.webp"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_publishing_3d_still.webp"
  VERBATIM
)

if(MTK_ENABLE_CEF_SANDBOX)
  # CEF's bootstrap loads "<exe base name>.dll", so the shipped executable must
  # be named MTKBrowser.exe next to MTKBrowser.dll. The bootstrap refuses to run
  # under its original name, which also stops a stray copy from doing anything.
  add_custom_command(TARGET mtk_browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_BINARY_DIR}/bootstrap.exe"
            "$<TARGET_FILE_DIR:mtk_browser>/MTKBrowser.exe"
    COMMENT "Staging CEF bootstrap as MTKBrowser.exe..."
    VERBATIM
  )

  # Icon, version details and the process-wide manifest (per-monitor DPI, long
  # paths, common controls) all belong on the executable that users see, which
  # is now CEF's binary, so they are stamped onto our copy after staging.
  add_custom_command(TARGET mtk_browser POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E env python
            "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/apply_exe_resources.py"
            "$<TARGET_FILE:mtk_browser>"
            "$<TARGET_FILE_DIR:mtk_browser>/MTKBrowser.exe"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/mtk_browser.manifest"
    COMMENT "Applying MTK Browser icon, version and manifest to MTKBrowser.exe..."
    VERBATIM
  )

  # Grant the LPAC ACLs that Chromium's sandboxed processes need to read the
  # application directory.
  SET_LPAC_ACLS(mtk_browser)
endif()

COPY_FILES(mtk_browser "${CEF_BINARY_FILES}"
           "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(mtk_browser "${CEF_RESOURCE_FILES}"
           "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
