add_library(cstc_repl_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/repl.cpp
)
add_library(cicest::compiler::repl ALIAS cstc_repl_lib)

target_include_directories(cstc_repl_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/compiler/cstc/include>
)

target_compile_definitions(cstc_repl_lib PRIVATE
    CICEST_STD_PATH="${CMAKE_SOURCE_DIR}/libraries/std"
    CICEST_RT_PATH="$<TARGET_FILE:cicest_rt>"
)

target_link_libraries(cstc_repl_lib
    PUBLIC
        cstc_cli_support
        cstc_codegen
        cstc_lexer
        cstc_lir_builder
        cstc_module
        cstc_parser
        cstc_resource_path
        cstc_symbol
        cstc_tyir
        cstc_tyir_builder
        cicest_options
        cicest_warnings
)

add_executable(cstc_repl
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(cstc_repl
    PRIVATE
        cstc_repl_lib
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    add_executable(cstc_repl_session
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/repl_session.cpp
    )

    target_link_libraries(cstc_repl_session
        PRIVATE
            cstc_repl_lib
            cicest_options
            cicest_warnings
    )

    add_test(NAME cstc_repl_session COMMAND cstc_repl_session)

    add_test(NAME cstc_repl_help
        COMMAND ${CMAKE_COMMAND}
            -DREPL_BINARY=$<TARGET_FILE:cstc_repl>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_help.cmake
    )
endif()
