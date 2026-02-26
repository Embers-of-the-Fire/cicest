add_library(cicest_parser

)

target_link_libraries(cicest_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(cicest_parser
    PRIVATE
        cicest_options
        cicest_warnings
)
