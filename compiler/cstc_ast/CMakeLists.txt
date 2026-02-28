add_library(cicest_ast INTERFACE)
add_library(cicest::compiler::ast ALIAS cicest_ast)

target_include_directories(cicest_ast INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cicest_ast
    INTERFACE
        cicest_options
        cicest_warnings
        cicest_pegtl
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(${test_name} ${test_source})
        target_link_libraries(${test_name} PRIVATE cicest_ast cicest_options cicest_warnings cicest_pegtl)
    endforeach()
endif()

