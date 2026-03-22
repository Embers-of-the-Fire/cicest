find_package(ZLIB QUIET)
find_package(LibXml2 QUIET)

if(NOT TARGET ZLIB::ZLIB)
    find_library(CICEST_ZLIB_LIBRARY NAMES z zlib zlibstatic zlib1)
    if(CICEST_ZLIB_LIBRARY)
        add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
        set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION "${CICEST_ZLIB_LIBRARY}")
    else()
        add_library(ZLIB::ZLIB INTERFACE IMPORTED)
        if(MSVC)
            target_link_libraries(ZLIB::ZLIB INTERFACE zlib)
        else()
            target_link_libraries(ZLIB::ZLIB INTERFACE z)
        endif()
    endif()
endif()

if(NOT TARGET LibXml2::LibXml2)
    find_library(CICEST_LIBXML2_LIBRARY NAMES xml2 libxml2 libxml2s)
    if(CICEST_LIBXML2_LIBRARY)
        add_library(LibXml2::LibXml2 UNKNOWN IMPORTED)
        set_target_properties(LibXml2::LibXml2 PROPERTIES IMPORTED_LOCATION "${CICEST_LIBXML2_LIBRARY}")
    else()
        add_library(LibXml2::LibXml2 INTERFACE IMPORTED)
        if(MSVC)
            target_link_libraries(LibXml2::LibXml2 INTERFACE libxml2)
        else()
            target_link_libraries(LibXml2::LibXml2 INTERFACE xml2)
        endif()
    endif()
endif()

if(NOT TARGET FFI::ffi)
    find_library(CICEST_LIBFFI_LIBRARY NAMES ffi libffi libffi-8 libffi-7)
    if(CICEST_LIBFFI_LIBRARY)
        add_library(FFI::ffi UNKNOWN IMPORTED)
        set_target_properties(FFI::ffi PROPERTIES IMPORTED_LOCATION "${CICEST_LIBFFI_LIBRARY}")
    else()
        add_library(FFI::ffi INTERFACE IMPORTED)
        target_link_libraries(FFI::ffi INTERFACE ffi)
    endif()
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

add_library(cstc_codegen STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen.cpp
)
add_library(cicest::compiler::codegen ALIAS cstc_codegen)

target_include_directories(cstc_codegen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_include_directories(cstc_codegen
    SYSTEM PRIVATE
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
        $<$<NOT:$<BOOL:${MSVC}>>:LLVM>
        cicest_options
        cicest_warnings
)

if(MSVC)
    llvm_config(cstc_codegen
        core
        native
        nativecodegen
        passes
        support
        targetparser
        transformutils
    )
endif()

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
