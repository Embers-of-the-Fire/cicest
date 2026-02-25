cmake_minimum_required(VERSION 4.1.2)
project(cicest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(LLVM_CONFIG llvm-config REQUIRED)

execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --system-libs OUTPUT_VARIABLE LLVM_SYSTEM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs core orcjit native OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(LLVM_CXX_FLAGS NATIVE_COMMAND ${LLVM_CXX_FLAGS})
separate_arguments(LLVM_LD_FLAGS NATIVE_COMMAND ${LLVM_LD_FLAGS})
separate_arguments(LLVM_SYSTEM_LIBS NATIVE_COMMAND ${LLVM_SYSTEM_LIBS})
separate_arguments(LLVM_LIBS NATIVE_COMMAND ${LLVM_LIBS})

add_executable(cicest src/entrypoint/main.cpp)
target_compile_options(cicest PRIVATE ${LLVM_CXX_FLAGS})
target_link_options(cicest PRIVATE ${LLVM_LD_FLAGS})

target_include_directories(cicest PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(cicest PRIVATE ${LLVM_DEFINITIONS})

target_link_libraries(cicest PRIVATE ${LLVM_SYSTEM_LIBS} ${LLVM_LIBS})

