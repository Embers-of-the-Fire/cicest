#ifndef CICEST_COMPILER_CSTC_LEXER_LEXER_HPP
#define CICEST_COMPILER_CSTC_LEXER_LEXER_HPP

#include <string_view>
#include <vector>

#include <cstc_lexer/token.hpp>

namespace cstc::lexer {

/// Lexes source text into a token stream and appends `EndOfFile`.
///
/// When `keep_trivia` is false, whitespace and comments are omitted.
[[nodiscard]] inline std::vector<Token>
    lex_source(std::string_view source, bool keep_trivia = false);

} // namespace cstc::lexer

#include <cstc_lexer/lexer_impl.hpp>

#endif // CICEST_COMPILER_CSTC_LEXER_LEXER_HPP
