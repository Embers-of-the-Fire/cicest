cmake_minimum_required(VERSION 3.21)
project(cicest_language_scoped LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cicest_options INTERFACE)
target_compile_features(cicest_options INTERFACE cxx_std_23)

add_library(cicest_warnings INTERFACE)
if(MSVC)
    target_compile_options(cicest_warnings INTERFACE /W4)
else()
    target_compile_options(cicest_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

option(CICEST_BUILD_TESTS "Build tests" OFF)

if(CICEST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test/e2e)
endif()

add_subdirectory(libraries)
add_subdirectory(compiler)
