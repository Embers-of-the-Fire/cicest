#ifndef CICEST_COMPILER_CSTC_HIR_INTERPRETER_TESTS_SUPPORT_HPP
#define CICEST_COMPILER_CSTC_HIR_INTERPRETER_TESTS_SUPPORT_HPP

#include <cassert>
#include <string_view>

#include <cstc_hir_builder/builder.hpp>
#include <cstc_parser/parser.hpp>

inline cstc::hir::Module lower_source_to_hir_module(std::string_view source) {
    cstc::ast::SymbolTable symbols;
    const auto parsed = cstc::parser::parse_source(source, symbols);
    assert(parsed.has_value());
    return cstc::hir::builder::lower_ast_to_hir(parsed.value(), &symbols);
}

#endif // CICEST_COMPILER_CSTC_HIR_INTERPRETER_TESTS_SUPPORT_HPP
