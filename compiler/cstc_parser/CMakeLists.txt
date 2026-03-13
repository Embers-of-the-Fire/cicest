add_library(cstc_parser INTERFACE)
add_library(cicest::compiler::parser ALIAS cstc_parser)

target_include_directories(cstc_parser
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_parser
    INTERFACE
        cstc_ast
        cstc_lexer
        cstc_span
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_parser_${test_name} ${test_source})
        target_compile_definitions(cstc_parser_${test_name} PRIVATE CICEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
        target_link_libraries(cstc_parser_${test_name} PRIVATE cstc_parser cicest_options cicest_warnings)
    endforeach()
endif()
