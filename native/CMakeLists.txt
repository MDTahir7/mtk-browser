set(MTK_BROWSER_SOURCES
  src/main.cpp
  src/mtk_browser.rc
  src/resource.h
  resources/mtk_browser.ico
  src/preferences.cpp
  src/preferences.h
  src/library_store.cpp
  src/library_store.h
  src/mtk_app.cpp
  src/mtk_app.h
  src/browser_client.cpp
  src/browser_client.h
  src/browser_window.cpp
  src/browser_window.h
  src/policy.cpp
  src/policy.h
  src/filter_engine.cpp
  src/filter_engine.h
  resources/mtk_filters.txt
)

SET_CEF_TARGET_OUT_DIR()

add_executable(mtk_browser WIN32 ${MTK_BROWSER_SOURCES})

SET_EXECUTABLE_TARGET_PROPERTIES(mtk_browser)

target_include_directories(mtk_browser PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(mtk_browser PRIVATE
  libcef_lib
  libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}
  dwmapi.lib
  uxtheme.lib
)

set_target_properties(mtk_browser PROPERTIES
  OUTPUT_NAME "MTKBrowser"
  VS_DEBUGGER_WORKING_DIRECTORY "${CEF_TARGET_OUT_DIR}"
)

# Stage the MTK Shields filter list beside the executable so the engine can
# load it at startup.
add_custom_command(TARGET mtk_browser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/mtk_filters.txt"
          "$<TARGET_FILE_DIR:mtk_browser>/mtk_filters.txt"
  VERBATIM
)

COPY_FILES(mtk_browser "${CEF_BINARY_FILES}"
           "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(mtk_browser "${CEF_RESOURCE_FILES}"
           "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
