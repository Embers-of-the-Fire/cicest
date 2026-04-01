add_subdirectory(cstc_resource_path)
add_subdirectory(cstc_end_to_end)
add_subdirectory(cstc_ansi_color)
add_subdirectory(cstc_error_report)

add_library(cicest_rt STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/std/runtime.c
)

# Build with PIC so the archive can be linked into PIE executables.
set_target_properties(cicest_rt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS cicest_rt
    # The installed runtime probe resolves ../lib/cicest relative to the
    # compiler binaries, so keep the archive in lib even on lib64 hosts.
    ARCHIVE DESTINATION lib/cicest
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/std/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/cicest/std
    FILES_MATCHING
    PATTERN "tests" EXCLUDE
    PATTERN "*.cst"
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
