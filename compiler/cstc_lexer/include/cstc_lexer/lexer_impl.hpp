#ifndef CICEST_COMPILER_CSTC_LEXER_LEXER_IMPL_HPP
#define CICEST_COMPILER_CSTC_LEXER_LEXER_IMPL_HPP

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cstc::lexer {

namespace detail {

[[nodiscard]] inline bool is_ident_start(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalpha(value) != 0 || ch == '_';
}

[[nodiscard]] inline bool is_ident_continue(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] inline TokenKind keyword_kind(std::string_view text) {
    if (text == "struct")
        return TokenKind::KwStruct;
    if (text == "enum")
        return TokenKind::KwEnum;
    if (text == "fn")
        return TokenKind::KwFn;
    if (text == "let")
        return TokenKind::KwLet;
    if (text == "if")
        return TokenKind::KwIf;
    if (text == "else")
        return TokenKind::KwElse;
    if (text == "for")
        return TokenKind::KwFor;
    if (text == "while")
        return TokenKind::KwWhile;
    if (text == "loop")
        return TokenKind::KwLoop;
    if (text == "break")
        return TokenKind::KwBreak;
    if (text == "continue")
        return TokenKind::KwContinue;
    if (text == "return")
        return TokenKind::KwReturn;
    if (text == "true")
        return TokenKind::KwTrue;
    if (text == "false")
        return TokenKind::KwFalse;
    if (text == "Unit")
        return TokenKind::KwUnit;
    if (text == "num")
        return TokenKind::KwNum;
    if (text == "str")
        return TokenKind::KwStr;
    if (text == "bool")
        return TokenKind::KwBool;

    return TokenKind::Identifier;
}

inline void append_token(
    std::vector<Token>& out,
    TokenKind kind,
    std::size_t start,
    std::size_t end,
    std::string_view source,
    bool keep_trivia) {
    if (!keep_trivia && is_trivia(kind))
        return;

    out.push_back(Token{
        .kind = kind,
        .span = {.start = start, .end = end},
        .lexeme = std::string(source.substr(start, end - start)),
    });
}

} // namespace detail

inline std::vector<Token> lex_source(std::string_view source, bool keep_trivia) {
    std::vector<Token> tokens;
    std::size_t index = 0;

    while (index < source.size()) {
        const char ch = source[index];

        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            const std::size_t start = index;
            while (index < source.size() && std::isspace(static_cast<unsigned char>(source[index])) != 0)
                ++index;
            detail::append_token(tokens, TokenKind::Whitespace, start, index, source, keep_trivia);
            continue;
        }

        if (ch == '/' && index + 1 < source.size() && source[index + 1] == '/') {
            const std::size_t start = index;
            index += 2;
            while (index < source.size() && source[index] != '\n')
                ++index;
            detail::append_token(tokens, TokenKind::LineComment, start, index, source, keep_trivia);
            continue;
        }

        if (ch == '/' && index + 1 < source.size() && source[index + 1] == '*') {
            const std::size_t start = index;
            index += 2;
            bool terminated = false;
            while (index + 1 < source.size()) {
                if (source[index] == '*' && source[index + 1] == '/') {
                    index += 2;
                    terminated = true;
                    break;
                }
                ++index;
            }

            if (terminated) {
                detail::append_token(tokens, TokenKind::BlockComment, start, index, source, keep_trivia);
            } else {
                index = source.size();
                detail::append_token(tokens, TokenKind::Unknown, start, index, source, true);
            }
            continue;
        }

        if (detail::is_ident_start(ch)) {
            const std::size_t start = index;
            ++index;
            while (index < source.size() && detail::is_ident_continue(source[index]))
                ++index;

            const std::string_view text = source.substr(start, index - start);
            detail::append_token(tokens, detail::keyword_kind(text), start, index, source, keep_trivia);
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            const std::size_t start = index;
            while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index])) != 0)
                ++index;

            if (index + 1 < source.size() && source[index] == '.'
                && std::isdigit(static_cast<unsigned char>(source[index + 1])) != 0) {
                ++index;
                while (
                    index < source.size()
                    && std::isdigit(static_cast<unsigned char>(source[index])) != 0)
                    ++index;
            }

            detail::append_token(tokens, TokenKind::Number, start, index, source, keep_trivia);
            continue;
        }

        if (ch == '"') {
            const std::size_t start = index;
            ++index;
            bool terminated = false;

            while (index < source.size()) {
                if (source[index] == '\\') {
                    index += 2;
                    continue;
                }
                if (source[index] == '"') {
                    ++index;
                    terminated = true;
                    break;
                }
                ++index;
            }

            detail::append_token(
                tokens,
                terminated ? TokenKind::String : TokenKind::Unknown,
                start,
                terminated ? index : source.size(),
                source,
                keep_trivia || !terminated);
            continue;
        }

        const std::size_t start = index;

        if (index + 1 < source.size()) {
            const std::string_view two = source.substr(index, 2);
            if (two == "::") {
                index += 2;
                detail::append_token(tokens, TokenKind::ColonColon, start, index, source, keep_trivia);
                continue;
            }
            if (two == "->") {
                index += 2;
                detail::append_token(tokens, TokenKind::Arrow, start, index, source, keep_trivia);
                continue;
            }
            if (two == "&&") {
                index += 2;
                detail::append_token(tokens, TokenKind::AndAnd, start, index, source, keep_trivia);
                continue;
            }
            if (two == "||") {
                index += 2;
                detail::append_token(tokens, TokenKind::OrOr, start, index, source, keep_trivia);
                continue;
            }
            if (two == "==") {
                index += 2;
                detail::append_token(tokens, TokenKind::EqEq, start, index, source, keep_trivia);
                continue;
            }
            if (two == "!=") {
                index += 2;
                detail::append_token(tokens, TokenKind::NotEq, start, index, source, keep_trivia);
                continue;
            }
            if (two == "<=") {
                index += 2;
                detail::append_token(tokens, TokenKind::LtEq, start, index, source, keep_trivia);
                continue;
            }
            if (two == ">=") {
                index += 2;
                detail::append_token(tokens, TokenKind::GtEq, start, index, source, keep_trivia);
                continue;
            }
        }

        ++index;
        switch (ch) {
            case '{':
                detail::append_token(tokens, TokenKind::LBrace, start, index, source, keep_trivia);
                break;
            case '}':
                detail::append_token(tokens, TokenKind::RBrace, start, index, source, keep_trivia);
                break;
            case '(':
                detail::append_token(tokens, TokenKind::LParen, start, index, source, keep_trivia);
                break;
            case ')':
                detail::append_token(tokens, TokenKind::RParen, start, index, source, keep_trivia);
                break;
            case ',':
                detail::append_token(tokens, TokenKind::Comma, start, index, source, keep_trivia);
                break;
            case ';':
                detail::append_token(tokens, TokenKind::Semicolon, start, index, source, keep_trivia);
                break;
            case ':':
                detail::append_token(tokens, TokenKind::Colon, start, index, source, keep_trivia);
                break;
            case '.':
                detail::append_token(tokens, TokenKind::Dot, start, index, source, keep_trivia);
                break;
            case '+':
                detail::append_token(tokens, TokenKind::Plus, start, index, source, keep_trivia);
                break;
            case '-':
                detail::append_token(tokens, TokenKind::Minus, start, index, source, keep_trivia);
                break;
            case '*':
                detail::append_token(tokens, TokenKind::Star, start, index, source, keep_trivia);
                break;
            case '/':
                detail::append_token(tokens, TokenKind::Slash, start, index, source, keep_trivia);
                break;
            case '%':
                detail::append_token(tokens, TokenKind::Percent, start, index, source, keep_trivia);
                break;
            case '!':
                detail::append_token(tokens, TokenKind::Bang, start, index, source, keep_trivia);
                break;
            case '<':
                detail::append_token(tokens, TokenKind::Lt, start, index, source, keep_trivia);
                break;
            case '>':
                detail::append_token(tokens, TokenKind::Gt, start, index, source, keep_trivia);
                break;
            case '=':
                detail::append_token(tokens, TokenKind::Assign, start, index, source, keep_trivia);
                break;
            default:
                detail::append_token(tokens, TokenKind::Unknown, start, index, source, true);
                break;
        }
    }

    tokens.push_back(Token{
        .kind = TokenKind::EndOfFile,
        .span = {.start = source.size(), .end = source.size()},
        .lexeme = "",
    });

    return tokens;
}

} // namespace cstc::lexer

#endif // CICEST_COMPILER_CSTC_LEXER_LEXER_IMPL_HPP
