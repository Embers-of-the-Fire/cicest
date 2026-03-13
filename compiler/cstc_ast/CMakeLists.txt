add_library(cstc_ast INTERFACE)
add_library(cicest::compiler::ast ALIAS cstc_ast)

target_include_directories(cstc_ast INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_ast
    INTERFACE
        cstc_span
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_ast_${test_name} ${test_source})
        target_link_libraries(cstc_ast_${test_name} PRIVATE cstc_ast cicest_options cicest_warnings)
    endforeach()
endif()
