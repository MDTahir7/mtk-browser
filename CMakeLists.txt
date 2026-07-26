cmake_minimum_required(VERSION 3.21)

project(MTKBrowser VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The first MTK Browser target is Windows-only.")
endif()

if(NOT DEFINED CEF_ROOT)
  message(FATAL_ERROR "Set CEF_ROOT to the extracted CEF binary distribution directory.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(TO_CMAKE_PATH "${CEF_ROOT}" CEF_ROOT)

option(MTK_ENABLE_CEF_SANDBOX "Use the CEF 150 Windows bootstrap sandbox" OFF)
if(MTK_ENABLE_CEF_SANDBOX)
  message(FATAL_ERROR
    "CEF 150 sandboxing uses bootstrap.exe plus an application DLL. "
    "The current development executable target does not implement that packaging yet.")
endif()
set(USE_SANDBOX OFF CACHE BOOL "CEF bootstrap sandbox" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}"
                 "${CMAKE_CURRENT_BINARY_DIR}/libcef_dll_wrapper")
add_subdirectory(native)

PRINT_CEF_CONFIG()
