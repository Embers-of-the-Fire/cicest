find_package(ZLIB QUIET)
find_package(LibXml2 QUIET)

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    target_link_libraries(ZLIB::ZLIB INTERFACE z)
endif()

if(NOT TARGET LibXml2::LibXml2)
    add_library(LibXml2::LibXml2 INTERFACE IMPORTED)
    target_link_libraries(LibXml2::LibXml2 INTERFACE xml2)
endif()

if(NOT TARGET FFI::ffi)
    add_library(FFI::ffi INTERFACE IMPORTED)
    target_link_libraries(FFI::ffi INTERFACE ffi)
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

set(LLVM_LIBS LLVM)

add_library(cstc_codegen STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen.cpp
)
add_library(cicest::compiler::codegen ALIAS cstc_codegen)

target_include_directories(cstc_codegen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(cstc_codegen
    PRIVATE
        ${LLVM_DEFINITIONS}
)

target_link_libraries(cstc_codegen
    PUBLIC
        cstc_lir
    PRIVATE
        ${LLVM_LIBS}
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_codegen_${test_name} ${test_source})
        target_link_libraries(cstc_codegen_${test_name}
            PRIVATE
                cstc_codegen
                cstc_lir_builder
                cstc_tyir_builder
                cstc_parser
                cstc_lir
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_codegen_${test_name} COMMAND cstc_codegen_${test_name})
    endforeach()
endif()
