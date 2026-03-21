add_library(cstc_end_to_end STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/end_to_end.cpp
)
add_library(cicest::testing::end_to_end ALIAS cstc_end_to_end)

target_include_directories(cstc_end_to_end
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_end_to_end
    PUBLIC
        cicest_options
        cicest_warnings
)
