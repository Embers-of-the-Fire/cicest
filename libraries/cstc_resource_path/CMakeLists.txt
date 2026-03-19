add_library(cstc_resource_path INTERFACE)
add_library(cicest::resource_path ALIAS cstc_resource_path)

target_include_directories(cstc_resource_path
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_resource_path
    INTERFACE
        cicest_options
        cicest_warnings
)
