add_library(cicest_ast

)

target_link_libraries(cicest_ast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(cicest_ast 
    PRIVATE
        cicest_options
        cicest_warnings
)
