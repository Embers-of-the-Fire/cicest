cmake_minimum_required(VERSION 3.21)
project(cicest_language_scoped LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cicest_options INTERFACE)
target_compile_features(cicest_options INTERFACE cxx_std_23)

if(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_options(cicest_options INTERFACE
        $<$<COMPILE_LANGUAGE:C>:-Wa,-mbig-obj>
        $<$<COMPILE_LANGUAGE:CXX>:-Wa,-mbig-obj>
    )
endif()

add_library(cicest_warnings INTERFACE)
if(MSVC)
    target_compile_options(cicest_warnings INTERFACE /W4)
else()
    target_compile_options(cicest_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

option(CICEST_BUILD_TESTS "Build tests" OFF)
option(CICEST_BUILD_E2E_TESTS "Build end-to-end tests" ON)

if(CICEST_BUILD_TESTS)
    enable_testing()
    if(CICEST_BUILD_E2E_TESTS)
        add_subdirectory(test/e2e)
        add_subdirectory(test/availability_evidence)
    endif()
endif()

add_subdirectory(libraries)
add_subdirectory(compiler)
