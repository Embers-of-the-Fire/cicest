add_library(cstc_hir_builder INTERFACE)
add_library(cicest::compiler::hir_builder ALIAS cstc_hir_builder)

target_include_directories(cstc_hir_builder
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_hir_builder
    INTERFACE
        cstc_ast
        cstc_hir
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(${test_name} ${test_source})
        target_link_libraries(
            ${test_name}
            PRIVATE
                cstc_hir_builder
                cstc_hir
                cstc_parser
                cicest_options
                cicest_warnings)
    endforeach()
endif()

