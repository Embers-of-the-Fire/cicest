add_library(cstc_error_report INTERFACE)
add_library(cicest::library::error_report ALIAS cstc_error_report)

target_include_directories(cstc_error_report
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_error_report
    INTERFACE
        cstc_ansi_color
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_error_report_${test_name} ${test_source})
        target_link_libraries(cstc_error_report_${test_name}
            PRIVATE
                cstc_error_report
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_error_report_${test_name} COMMAND cstc_error_report_${test_name})
    endforeach()
endif()
