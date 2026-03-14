#ifndef CICEST_COMPILER_CSTC_LEXER_TOKEN_HPP
#define CICEST_COMPILER_CSTC_LEXER_TOKEN_HPP

#include <string>
#include <string_view>

#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>

namespace cstc::lexer {

/// Lexical token categories produced by the source lexer.
enum class TokenKind {
    /// End-of-input sentinel.
    EndOfFile,
    /// Invalid or unsupported token.
    Unknown,

    /// Whitespace trivia.
    Whitespace,
    /// Line comment trivia (`// ...`).
    LineComment,
    /// Block comment trivia (`/* ... */`).
    BlockComment,

    /// Identifier token.
    Identifier,
    /// Numeric literal token.
    Number,
    /// String literal token.
    String,

    /// `struct` keyword.
    KwStruct,
    /// `enum` keyword.
    KwEnum,
    /// `fn` keyword.
    KwFn,
    /// `let` keyword.
    KwLet,
    /// `if` keyword.
    KwIf,
    /// `else` keyword.
    KwElse,
    /// `for` keyword.
    KwFor,
    /// `while` keyword.
    KwWhile,
    /// `loop` keyword.
    KwLoop,
    /// `break` keyword.
    KwBreak,
    /// `continue` keyword.
    KwContinue,
    /// `return` keyword.
    KwReturn,
    /// `true` keyword.
    KwTrue,
    /// `false` keyword.
    KwFalse,
    /// `Unit` built-in type keyword.
    KwUnit,
    /// `num` built-in type keyword.
    KwNum,
    /// `str` built-in type keyword.
    KwStr,
    /// `bool` built-in type keyword.
    KwBool,

    /// `{`
    LBrace,
    /// `}`
    RBrace,
    /// `(`
    LParen,
    /// `)`
    RParen,
    /// `,`
    Comma,
    /// `;`
    Semicolon,
    /// `:`
    Colon,
    /// `::`
    ColonColon,
    /// `.`
    Dot,
    /// `->`
    Arrow,

    /// `+`
    Plus,
    /// `-`
    Minus,
    /// `*`
    Star,
    /// `/`
    Slash,
    /// `%`
    Percent,
    /// `!`
    Bang,
    /// `&&`
    AndAnd,
    /// `||`
    OrOr,
    /// `==`
    EqEq,
    /// `!=`
    NotEq,
    /// `<`
    Lt,
    /// `<=`
    LtEq,
    /// `>`
    Gt,
    /// `>=`
    GtEq,
    /// `=`
    Assign,
};

/// Concrete token instance with location and source text.
struct Token {
    /// Token classification.
    TokenKind kind = TokenKind::Unknown;
    /// Byte span of the token in the source.
    cstc::span::SourceSpan span;
    /// Interned symbol for the token text.
    cstc::symbol::Symbol symbol = cstc::symbol::kInvalidSymbol;
};

/// Returns true when a token kind is trivia.
[[nodiscard]] constexpr bool is_trivia(TokenKind kind) {
    return kind == TokenKind::Whitespace || kind == TokenKind::LineComment
        || kind == TokenKind::BlockComment;
}

/// Returns a stable debug name for a token kind.
[[nodiscard]] constexpr std::string_view token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile: return "EndOfFile";
    case TokenKind::Unknown: return "Unknown";
    case TokenKind::Whitespace: return "Whitespace";
    case TokenKind::LineComment: return "LineComment";
    case TokenKind::BlockComment: return "BlockComment";
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::Number: return "Number";
    case TokenKind::String: return "String";
    case TokenKind::KwStruct: return "KwStruct";
    case TokenKind::KwEnum: return "KwEnum";
    case TokenKind::KwFn: return "KwFn";
    case TokenKind::KwLet: return "KwLet";
    case TokenKind::KwIf: return "KwIf";
    case TokenKind::KwElse: return "KwElse";
    case TokenKind::KwFor: return "KwFor";
    case TokenKind::KwWhile: return "KwWhile";
    case TokenKind::KwLoop: return "KwLoop";
    case TokenKind::KwBreak: return "KwBreak";
    case TokenKind::KwContinue: return "KwContinue";
    case TokenKind::KwReturn: return "KwReturn";
    case TokenKind::KwTrue: return "KwTrue";
    case TokenKind::KwFalse: return "KwFalse";
    case TokenKind::KwUnit: return "KwUnit";
    case TokenKind::KwNum: return "KwNum";
    case TokenKind::KwStr: return "KwStr";
    case TokenKind::KwBool: return "KwBool";
    case TokenKind::LBrace: return "LBrace";
    case TokenKind::RBrace: return "RBrace";
    case TokenKind::LParen: return "LParen";
    case TokenKind::RParen: return "RParen";
    case TokenKind::Comma: return "Comma";
    case TokenKind::Semicolon: return "Semicolon";
    case TokenKind::Colon: return "Colon";
    case TokenKind::ColonColon: return "ColonColon";
    case TokenKind::Dot: return "Dot";
    case TokenKind::Arrow: return "Arrow";
    case TokenKind::Plus: return "Plus";
    case TokenKind::Minus: return "Minus";
    case TokenKind::Star: return "Star";
    case TokenKind::Slash: return "Slash";
    case TokenKind::Percent: return "Percent";
    case TokenKind::Bang: return "Bang";
    case TokenKind::AndAnd: return "AndAnd";
    case TokenKind::OrOr: return "OrOr";
    case TokenKind::EqEq: return "EqEq";
    case TokenKind::NotEq: return "NotEq";
    case TokenKind::Lt: return "Lt";
    case TokenKind::LtEq: return "LtEq";
    case TokenKind::Gt: return "Gt";
    case TokenKind::GtEq: return "GtEq";
    case TokenKind::Assign: return "Assign";
    }
    return "<invalid-token-kind>";
}

} // namespace cstc::lexer

#endif // CICEST_COMPILER_CSTC_LEXER_TOKEN_HPP
