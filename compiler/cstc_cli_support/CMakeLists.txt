add_library(cstc_cli_support INTERFACE)
add_library(cicest::compiler::cli_support ALIAS cstc_cli_support)

target_include_directories(cstc_cli_support
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_cli_support
    INTERFACE
        cstc_ast
        cstc_error_report
        cstc_module
        cstc_parser
        cstc_resource_path
        cstc_span
        cstc_tyir_interp
        cstc_tyir_builder
        cicest_options
        cicest_warnings
)
