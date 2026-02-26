cmake_minimum_required(VERSION 4.1.2)
project(cicest LANGUAGES CXX)

set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cicest_options INTERFACE)
target_compile_features(cicest_options INTERFACE cxx_std_20)

add_library(cicest_warnings INTERFACE)
if (MSVC)
    target_compile_options(cicest_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(cicest_warnings INTERFACE -Wall -Wextra -Werror)
endif()

# --- LLVM configuration ---

option(CICEST_ENABLE_LLVM "Enable LLVM-dependent targets" ON)
if (CICEST_ENABLE_LLVM)
    find_program(LLVM_CONFIG llvm-config REQUIRED)

    add_library(cicest_llvm INTERFACE)

    execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --system-libs OUTPUT_VARIABLE LLVM_SYSTEM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --libs core orcjit native OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

    separate_arguments(LLVM_CXX_FLAGS NATIVE_COMMAND ${LLVM_CXX_FLAGS})
    separate_arguments(LLVM_LD_FLAGS NATIVE_COMMAND ${LLVM_LD_FLAGS})
    separate_arguments(LLVM_SYSTEM_LIBS NATIVE_COMMAND ${LLVM_SYSTEM_LIBS})
    separate_arguments(LLVM_LIBS NATIVE_COMMAND ${LLVM_LIBS})

    # Split LLVM_CXX_FLAGS into include dirs and other flags.
    # Include dirs must be added via target_include_directories SYSTEM so that
    # the compiler treats them as system headers and suppresses warnings.
    set(LLVM_CXX_OTHER_FLAGS "")
    set(LLVM_CXX_INCLUDE_DIRS "")
    foreach(flag IN LISTS LLVM_CXX_FLAGS)
        if(flag MATCHES "^-I(.+)")
            list(APPEND LLVM_CXX_INCLUDE_DIRS "${CMAKE_MATCH_1}")
        else()
            list(APPEND LLVM_CXX_OTHER_FLAGS "${flag}")
        endif()
    endforeach()

    target_compile_options(cicest_llvm INTERFACE ${LLVM_CXX_OTHER_FLAGS})
    target_link_options(cicest_llvm INTERFACE ${LLVM_LD_FLAGS})
    target_include_directories(cicest_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS} ${LLVM_CXX_INCLUDE_DIRS})
    target_compile_definitions(cicest_llvm INTERFACE ${LLVM_DEFINITIONS})
    target_link_libraries(cicest_llvm INTERFACE ${LLVM_SYSTEM_LIBS} ${LLVM_LIBS})
else()
    message(STATUS "LLVM support is disabled. Set CICEST_ENABLE_LLVM=ON to enable it.")
endif()

add_subdirectory(compiler)

