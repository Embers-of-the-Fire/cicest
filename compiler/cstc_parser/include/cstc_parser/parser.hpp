#ifndef CICEST_COMPILER_CSTC_PARSER_PARSER_HPP
#define CICEST_COMPILER_CSTC_PARSER_PARSER_HPP

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_span/registry.hpp>

namespace cstc::parser {

/// A lexer token enriched with source range and original text.
struct LexedToken {
    cstc::lexer::TokenKind kind;
    cstc::span::SourceSpan span;
    std::string text;
};

/// First-failure parse error.
struct ParseError {
    cstc::span::SourceSpan span;
    std::string message;
};

/// Lex source text into parser tokens.
/// Trivia tokens (`Whitespace`, `LineComment`) are dropped by default.
[[nodiscard]] std::vector<LexedToken>
    lex_source(std::string_view source, bool keep_trivia = false);

/// Parse a token stream into an AST crate.
[[nodiscard]] std::expected<cstc::ast::Crate, ParseError>
    parse_tokens(std::span<const LexedToken> tokens, cstc::ast::SymbolTable& symbols);

/// Convenience helper: lex + parse.
[[nodiscard]] std::expected<cstc::ast::Crate, ParseError>
    parse_source(std::string_view source, cstc::ast::SymbolTable& symbols);

} // namespace cstc::parser

#include <cstc_parser/parser_impl.hpp>

#endif // CICEST_COMPILER_CSTC_PARSER_PARSER_HPP
