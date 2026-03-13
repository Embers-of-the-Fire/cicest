#ifndef CICEST_COMPILER_CSTC_LEXER_LEXER_HPP
#define CICEST_COMPILER_CSTC_LEXER_LEXER_HPP

#include <string_view>
#include <vector>

#include <cstc_lexer/token.hpp>

namespace cstc::lexer {

/// Lexes source text at a given absolute base position and appends `EndOfFile`.
///
/// Use this overload with `cstc::span::SourceMap` file ranges so produced spans
/// can be resolved back to files via a single span lookup.
[[nodiscard]] inline std::vector<Token>
    lex_source_at(std::string_view source, cstc::span::BytePos base_pos, bool keep_trivia = false);

/// Lexes source text into a token stream and appends `EndOfFile`.
///
/// When `keep_trivia` is false, whitespace and comments are omitted.
[[nodiscard]] inline std::vector<Token>
    lex_source(std::string_view source, bool keep_trivia = false);

} // namespace cstc::lexer

#include <cstc_lexer/lexer_impl.hpp>

#endif // CICEST_COMPILER_CSTC_LEXER_LEXER_HPP
