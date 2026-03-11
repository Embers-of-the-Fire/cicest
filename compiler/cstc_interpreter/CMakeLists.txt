add_executable(cstc_interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(cstc_interpreter
    PRIVATE
        cstc_hir_interpreter
        cstc_hir
        cstc_hir_builder
        cstc_parser
        cicest_options
        cicest_warnings
)
