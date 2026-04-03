#include <cstc_lexer/lexer.hpp>

#include <cctype>
#include <cstddef>
#include <string_view>
#include <vector>

namespace cstc::lexer {

namespace detail {

[[nodiscard]] static bool is_ident_start(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalpha(value) != 0 || ch == '_';
}

[[nodiscard]] static bool is_ident_continue(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] static TokenKind keyword_kind(std::string_view text) {
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
    if (text == "extern")
        return TokenKind::KwExtern;
    if (text == "runtime")
        return TokenKind::KwRuntime;
    if (text == "pub")
        return TokenKind::KwPub;
    if (text == "import")
        return TokenKind::KwImport;
    if (text == "from")
        return TokenKind::KwFrom;
    if (text == "as")
        return TokenKind::KwAs;
    if (text == "where")
        return TokenKind::KwWhere;

    return TokenKind::Identifier;
}

static void append_token(
    std::vector<Token>& out, TokenKind kind, cstc::span::BytePos base_pos, std::size_t local_start,
    std::size_t local_end, std::string_view source, bool keep_trivia) {
    if (!keep_trivia && is_trivia(kind))
        return;

    const std::string_view lexeme = source.substr(local_start, local_end - local_start);
    out.push_back(
        Token{
            .kind = kind,
            .span =
                {
                       .start = base_pos + local_start,
                       .end = base_pos + local_end,
                       },
            .symbol = cstc::symbol::Symbol::intern(lexeme),
    });
}

} // namespace detail

std::vector<Token>
    lex_source_at(std::string_view source, cstc::span::BytePos base_pos, bool keep_trivia) {
    std::vector<Token> tokens;
    std::size_t index = 0;

    while (index < source.size()) {
        const char ch = source[index];

        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            const std::size_t start = index;
            while (index < source.size()
                   && std::isspace(static_cast<unsigned char>(source[index])) != 0)
                ++index;
            detail::append_token(
                tokens, TokenKind::Whitespace, base_pos, start, index, source, keep_trivia);
            continue;
        }

        if (ch == '/' && index + 1 < source.size() && source[index + 1] == '/') {
            const std::size_t start = index;
            index += 2;
            while (index < source.size() && source[index] != '\n')
                ++index;
            detail::append_token(
                tokens, TokenKind::LineComment, base_pos, start, index, source, keep_trivia);
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
                detail::append_token(
                    tokens, TokenKind::BlockComment, base_pos, start, index, source, keep_trivia);
            } else {
                index = source.size();
                detail::append_token(
                    tokens, TokenKind::Unknown, base_pos, start, index, source, true);
            }
            continue;
        }

        if (detail::is_ident_start(ch)) {
            const std::size_t start = index;
            ++index;
            while (index < source.size() && detail::is_ident_continue(source[index]))
                ++index;

            const std::string_view text = source.substr(start, index - start);
            detail::append_token(
                tokens, detail::keyword_kind(text), base_pos, start, index, source, keep_trivia);
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            const std::size_t start = index;
            while (index < source.size()
                   && std::isdigit(static_cast<unsigned char>(source[index])) != 0)
                ++index;

            if (index + 1 < source.size() && source[index] == '.'
                && std::isdigit(static_cast<unsigned char>(source[index + 1])) != 0) {
                ++index;
                while (index < source.size()
                       && std::isdigit(static_cast<unsigned char>(source[index])) != 0)
                    ++index;
            }

            detail::append_token(
                tokens, TokenKind::Number, base_pos, start, index, source, keep_trivia);
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
                tokens, terminated ? TokenKind::String : TokenKind::Unknown, base_pos, start,
                terminated ? index : source.size(), source, keep_trivia || !terminated);
            continue;
        }

        const std::size_t start = index;

        if (index + 1 < source.size()) {
            const std::string_view two = source.substr(index, 2);
            if (two == "::") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::ColonColon, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == "->") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::Arrow, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == "&&") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::AndAnd, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == "||") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::OrOr, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == "==") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::EqEq, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == "!=") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::NotEq, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == "<=") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::LtEq, base_pos, start, index, source, keep_trivia);
                continue;
            }
            if (two == ">=") {
                index += 2;
                detail::append_token(
                    tokens, TokenKind::GtEq, base_pos, start, index, source, keep_trivia);
                continue;
            }
        }

        ++index;
        switch (ch) {
        case '{':
            detail::append_token(
                tokens, TokenKind::LBrace, base_pos, start, index, source, keep_trivia);
            break;
        case '}':
            detail::append_token(
                tokens, TokenKind::RBrace, base_pos, start, index, source, keep_trivia);
            break;
        case '(':
            detail::append_token(
                tokens, TokenKind::LParen, base_pos, start, index, source, keep_trivia);
            break;
        case ')':
            detail::append_token(
                tokens, TokenKind::RParen, base_pos, start, index, source, keep_trivia);
            break;
        case '[':
            detail::append_token(
                tokens, TokenKind::LBracket, base_pos, start, index, source, keep_trivia);
            break;
        case ']':
            detail::append_token(
                tokens, TokenKind::RBracket, base_pos, start, index, source, keep_trivia);
            break;
        case ',':
            detail::append_token(
                tokens, TokenKind::Comma, base_pos, start, index, source, keep_trivia);
            break;
        case ';':
            detail::append_token(
                tokens, TokenKind::Semicolon, base_pos, start, index, source, keep_trivia);
            break;
        case ':':
            detail::append_token(
                tokens, TokenKind::Colon, base_pos, start, index, source, keep_trivia);
            break;
        case '.':
            detail::append_token(
                tokens, TokenKind::Dot, base_pos, start, index, source, keep_trivia);
            break;
        case '&':
            detail::append_token(
                tokens, TokenKind::Amp, base_pos, start, index, source, keep_trivia);
            break;
        case '+':
            detail::append_token(
                tokens, TokenKind::Plus, base_pos, start, index, source, keep_trivia);
            break;
        case '-':
            detail::append_token(
                tokens, TokenKind::Minus, base_pos, start, index, source, keep_trivia);
            break;
        case '*':
            detail::append_token(
                tokens, TokenKind::Star, base_pos, start, index, source, keep_trivia);
            break;
        case '/':
            detail::append_token(
                tokens, TokenKind::Slash, base_pos, start, index, source, keep_trivia);
            break;
        case '%':
            detail::append_token(
                tokens, TokenKind::Percent, base_pos, start, index, source, keep_trivia);
            break;
        case '!':
            detail::append_token(
                tokens, TokenKind::Bang, base_pos, start, index, source, keep_trivia);
            break;
        case '<':
            detail::append_token(
                tokens, TokenKind::Lt, base_pos, start, index, source, keep_trivia);
            break;
        case '>':
            detail::append_token(
                tokens, TokenKind::Gt, base_pos, start, index, source, keep_trivia);
            break;
        case '=':
            detail::append_token(
                tokens, TokenKind::Assign, base_pos, start, index, source, keep_trivia);
            break;
        default:
            detail::append_token(tokens, TokenKind::Unknown, base_pos, start, index, source, true);
            break;
        }
    }

    tokens.push_back(
        Token{
            .kind = TokenKind::EndOfFile,
            .span =
                {
                       .start = base_pos + source.size(),
                       .end = base_pos + source.size(),
                       },
            .symbol = cstc::symbol::kInvalidSymbol,
    });

    return tokens;
}

std::vector<Token> lex_source(std::string_view source, bool keep_trivia) {
    return lex_source_at(source, 0, keep_trivia);
}

} // namespace cstc::lexer
