cmake_minimum_required(VERSION 3.21)

# Static CRT everywhere (our targets, FTXUI and vcpkg's x64-windows-static libsodium),
# so keyfox.exe is a single self-contained file.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(keyfox VERSION 1.0.0 DESCRIPTION "KeyFox - console password manager" LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "KeyFox is a Windows application (uses Win32 clipboard, BCrypt and console APIs).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KEYFOX_BUILD_TESTS "Build unit tests" ON)
option(KEYFOX_USE_SYSTEM_FTXUI "Use FTXUI from vcpkg/find_package instead of downloading v5.0.0" OFF)

# ---------------------------------------------------------------- dependencies
find_package(unofficial-sodium CONFIG QUIET)
if(NOT unofficial-sodium_FOUND)
  message(FATAL_ERROR
    "libsodium not found.\n"
    "Configure with the vcpkg toolchain, e.g.:\n"
    "  cmake --preset release\n"
    "or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake "
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static. See README.md.")
endif()

if(KEYFOX_USE_SYSTEM_FTXUI)
  find_package(ftxui CONFIG REQUIRED)
else()
  include(FetchContent)
  set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(FTXUI_QUIET ON CACHE BOOL "" FORCE)
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(ftxui)
endif()

# ---------------------------------------------------------------- warnings / hardening
function(keyfox_harden target)
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /sdl /guard:cf /Zc:__cplusplus)
    target_link_options(${target} PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_options(${target} PRIVATE /HIGHENTROPYVA /CETCOMPAT)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# ---------------------------------------------------------------- core (no UI)
add_library(keyfox_core STATIC
  src/core/crypto.cpp
  src/core/vault.cpp
  src/core/generator.cpp
  src/core/strength.cpp
  src/core/totp.cpp
  src/platform/clipboard.cpp
  src/platform/win.cpp
)
target_include_directories(keyfox_core PUBLIC src)
target_link_libraries(keyfox_core PUBLIC unofficial-sodium::sodium bcrypt user32 shell32 ole32)
keyfox_harden(keyfox_core)

# ---------------------------------------------------------------- application
add_executable(keyfox
  src/main.cpp
  src/ui/app.cpp
  src/keyfox.rc
)
target_link_libraries(keyfox PRIVATE keyfox_core ftxui::component ftxui::dom ftxui::screen)
keyfox_harden(keyfox)
if(MSVC)
  # The manifest is embedded through keyfox.rc
  target_link_options(keyfox PRIVATE /MANIFEST:NO)
endif()

# ---------------------------------------------------------------- tests
if(KEYFOX_BUILD_TESTS)
  enable_testing()
  add_executable(keyfox_tests tests/tests.cpp)
  target_link_libraries(keyfox_tests PRIVATE keyfox_core)
  keyfox_harden(keyfox_tests)
  add_test(NAME keyfox_tests COMMAND keyfox_tests)
endif()
