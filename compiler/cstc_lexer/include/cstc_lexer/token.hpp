#ifndef CICEST_COMPILER_CSTC_LEXER_TOKEN_HPP
#define CICEST_COMPILER_CSTC_LEXER_TOKEN_HPP

#include <cstdint>

namespace cstc::lexer {

enum class TokenKind {
    // ---- Operators ----

    /// `!`
    Bang,
    /// `+`
    Plus,
    /// `-`
    Minus,
    /// `*`
    Star,
    /// `/`
    Slash,
    /// `=`
    Eq,
    /// `^`
    Caret,
    /// `~`
    Tlide,
    /// `:`
    Colon,
    /// `|`
    Pipe,
    /// `&`
    Amp,
    /// `%`
    Percent,
    /// `.`
    Dot,
    /// `::`
    ColonColon,
    /// `,`
    Comma,
    /// `;`
    Semi,
    /// `@`
    At,
    /// `<`
    Lt,
    /// `>`
    Gt,
    /// `(`
    OpenParen,
    /// `)`
    CloseParen,
    /// `[`
    OpenBracket,
    /// `]`
    CloseBracket,
    /// `{`
    OpenBrace,
    /// `}`
    CloseBrace,

    // ---- Misc ----

    // --- Literals ---
    // Note that literal bools are treated as identifiers from lexer level.
    LitInt,
    LitFloat,
    LitStr,

    // --- Identifiers ---
    Ident,

    // --- Comments ---
    /// `# foo` comments
    LineComment,

    Whitespace,
    Eof,
    Unknown,
}; // enum TokenKind

struct Token {
    TokenKind kind;
    std::uint32_t len;
};

} // namespace cstc::lexer

#endif // CICEST_COMPILER_CSTC_LEXER_TOKEN_HPP
