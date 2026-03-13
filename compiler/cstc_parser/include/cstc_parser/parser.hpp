#ifndef CICEST_COMPILER_CSTC_PARSER_PARSER_HPP
#define CICEST_COMPILER_CSTC_PARSER_PARSER_HPP

#include <expected>
#include <span>
#include <string>
#include <string_view>

#include <cstc_ast/ast.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_span/span.hpp>

namespace cstc::parser {

/// Parse failure returned from token or source parsing.
struct ParseError {
    /// Source span where parsing failed.
    cstc::span::SourceSpan span;
    /// Human-readable diagnostic message.
    std::string message;
};

/// Parses a token stream into an AST program.
[[nodiscard]] inline std::expected<ast::Program, ParseError>
    parse_tokens(std::span<const lexer::Token> tokens);

/// Lexes then parses source text into an AST program.
[[nodiscard]] inline std::expected<ast::Program, ParseError>
    parse_source(std::string_view source);

} // namespace cstc::parser

#include <cstc_parser/parser_impl.hpp>

#endif // CICEST_COMPILER_CSTC_PARSER_PARSER_HPP
