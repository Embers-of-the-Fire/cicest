add_library(cstc_lir_builder INTERFACE)
add_library(cicest::compiler::lir_builder ALIAS cstc_lir_builder)

target_include_directories(cstc_lir_builder
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_lir_builder
    INTERFACE
        cstc_lir
        cstc_tyir
        cstc_ast
        cstc_symbol
        cstc_span
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_lir_builder_${test_name} ${test_source})
        target_link_libraries(cstc_lir_builder_${test_name}
            PRIVATE
                cstc_lir_builder
                cstc_tyir_builder
                cstc_parser
                cstc_lir
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_lir_builder_${test_name} COMMAND cstc_lir_builder_${test_name})
    endforeach()
endif()
