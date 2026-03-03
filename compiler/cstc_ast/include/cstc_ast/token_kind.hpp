#ifndef CICEST_COMPILER_CSTC_AST_TOKEN_KIND_HPP
#define CICEST_COMPILER_CSTC_AST_TOKEN_KIND_HPP

namespace cstc::ast {

enum class OperatorToken {
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
}; // enum class OperatorToken

struct IdentToken {}

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_TOKEN_KIND_HPP
