add_executable(cstc_inspect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(cstc_inspect
    PRIVATE
        cstc_parser
        cstc_hir_builder
        cstc_hir
        cicest_options
        cicest_warnings
)
