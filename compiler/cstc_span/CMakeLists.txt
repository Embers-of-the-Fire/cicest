add_library(cstc_span INTERFACE)
add_library(cicest::compiler::span ALIAS cstc_span)

target_include_directories(cstc_span
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_span
    INTERFACE
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_span_${test_name} ${test_source})
        target_link_libraries(cstc_span_${test_name} PRIVATE cstc_span cicest_options cicest_warnings)
        add_test(NAME cstc_span_${test_name} COMMAND cstc_span_${test_name})
    endforeach()
endif()
