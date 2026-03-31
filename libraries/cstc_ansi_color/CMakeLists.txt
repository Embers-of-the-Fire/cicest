add_library(cstc_ansi_color INTERFACE)
add_library(cicest::library::ansi_color ALIAS cstc_ansi_color)

target_include_directories(cstc_ansi_color
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_ansi_color
    INTERFACE
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_ansi_color_${test_name} ${test_source})
        target_link_libraries(cstc_ansi_color_${test_name}
            PRIVATE
                cstc_ansi_color
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_ansi_color_${test_name} COMMAND cstc_ansi_color_${test_name})
    endforeach()
endif()
