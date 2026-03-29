add_subdirectory(cstc_resource_path)
add_subdirectory(cstc_end_to_end)

add_library(cicest_rt STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/std/runtime.c
)

# Build with PIC so the archive can be linked into PIE executables.
set_target_properties(cicest_rt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/std/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cicest_rt_${test_name} ${test_source})
        target_link_libraries(cicest_rt_${test_name}
            PRIVATE
                cicest_rt
                cicest_options
                cicest_warnings
        )
        add_test(NAME cicest_rt_${test_name} COMMAND cicest_rt_${test_name})
    endforeach()
endif()
