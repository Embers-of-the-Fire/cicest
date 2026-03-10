#ifndef CICEST_COMPILER_CSTC_HIR_BUILDER_TESTS_SUPPORT_HPP
#define CICEST_COMPILER_CSTC_HIR_BUILDER_TESTS_SUPPORT_HPP

#include <cassert>
#include <string>
#include <string_view>

#include <cstc_hir/printer.hpp>
#include <cstc_hir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>

inline std::string lower_source_to_hir(std::string_view source) {
    cstc::ast::SymbolTable symbols;
    const auto parsed = cstc::parser::parse_source(source, symbols);
    assert(parsed.has_value());

    const auto module = cstc::hir::builder::lower_ast_to_hir(parsed.value(), &symbols);
    return cstc::hir::format_hir(module);
}

#endif // CICEST_COMPILER_CSTC_HIR_BUILDER_TESTS_SUPPORT_HPP

