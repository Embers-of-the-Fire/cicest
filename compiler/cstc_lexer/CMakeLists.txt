add_library(cstc_lexer INTERFACE)
add_library(cicest::compiler::lexer ALIAS cstc_lexer)

target_include_directories(cstc_lexer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_lexer
    INTERFACE
        cstc_span
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_lexer_${test_name} ${test_source})
        target_link_libraries(cstc_lexer_${test_name} PRIVATE cstc_lexer cicest_options cicest_warnings)
    endforeach()
endif()
