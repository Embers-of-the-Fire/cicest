add_library(cstc_resource_path STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resource_path.cpp
)
add_library(cicest::resource_path ALIAS cstc_resource_path)

target_include_directories(cstc_resource_path
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_resource_path
    PUBLIC
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_resource_path_${test_name} ${test_source})
        target_link_libraries(cstc_resource_path_${test_name}
            PRIVATE
                cstc_resource_path
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_resource_path_${test_name} COMMAND cstc_resource_path_${test_name})
    endforeach()
endif()
