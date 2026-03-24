add_library(cstc_module INTERFACE)
add_library(cicest::compiler::module ALIAS cstc_module)

target_include_directories(cstc_module
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_module
    INTERFACE
        cstc_ast
        cstc_parser
        cstc_resource_path
        cstc_span
        cstc_symbol
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_module_${test_name} ${test_source})
        target_link_libraries(cstc_module_${test_name}
            PRIVATE
                cstc_module
                cstc_tyir_builder
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_module_${test_name} COMMAND cstc_module_${test_name})
    endforeach()
endif()
