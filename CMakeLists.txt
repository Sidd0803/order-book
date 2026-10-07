cmake_minimum_required(VERSION 3.20)
project(order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # compile_commands.json for clangd / editors

# Default to a Debug build (-O0 -g) so LLDB sees every variable.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
endif()

# Warnings. MSVC uses different spellings; /W4 is its "-Wall -Wextra".
if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sanitizers: -DOB_SANITIZE=address,undefined  or  -DOB_SANITIZE=thread
# Must be applied to BOTH compile and link steps, or the runtime library
# is never linked in and you get undefined-symbol errors.
set(OB_SANITIZE "" CACHE STRING "Comma-separated sanitizers to enable (address,undefined | thread)")
# Note: native Windows/MSVC supports only "address"; thread and undefined
# need Clang or GCC (use WSL on Windows).
if(OB_SANITIZE)
  message(STATUS "Sanitizers enabled: ${OB_SANITIZE}")
  if(MSVC)
    add_compile_options(/fsanitize=${OB_SANITIZE})
  else()
    add_compile_options(-fsanitize=${OB_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${OB_SANITIZE})
  endif()
endif()

# --- Core library -----------------------------------------------------------
add_library(ob
  src/order_book.cpp
)
target_include_directories(ob PUBLIC include)

# --- CLI --------------------------------------------------------------------
add_executable(ob_cli src/main.cpp)
target_link_libraries(ob_cli PRIVATE ob)

# --- Tests (Catch2 v3 via FetchContent) ------------------------------------
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(ob_tests
  tests/test_order_book.cpp
)
target_link_libraries(ob_tests PRIVATE ob Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(ob_tests)

# Phase 3: add_subdirectory(bench) once the harness exists.
