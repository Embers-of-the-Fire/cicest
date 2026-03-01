#include "cstc_lexer/cursor.hpp"
#include "cstc_lexer/chars.hpp"
#include "cstc_lexer/token.hpp"
#include <tuple>

namespace cstc::lexer {

Cursor::Cursor(const std::string& str_source)
    : len_remaining(static_cast<std::uint32_t>(str_source.size()))
    , source(str_source)
    , chars(source.begin()) {}

[[nodiscard]] std::uint32_t Cursor::get_len_remaining() const noexcept { return len_remaining; }
[[nodiscard]] std::string_view Cursor::get_source() const noexcept { return source; }
[[nodiscard]] Cursor::Chars Cursor::get_chars() const noexcept { return chars; }

[[nodiscard]] Cursor::Chars Cursor::get_end() const noexcept { return source.end(); }
[[nodiscard]] std::uint32_t Cursor::pos_within_token() const noexcept {
    return len_remaining - static_cast<std::uint32_t>(get_end() - chars);
}
void Cursor::reset_pos_within_token() noexcept {
    len_remaining = static_cast<std::uint32_t>(get_end() - chars);
}

[[nodiscard]] char Cursor::first() const noexcept {
    if (chars == get_end()) {
        return EOF_CHAR;
    }
    return *chars;
}

[[nodiscard]] char Cursor::second() const noexcept {
    if (chars == get_end()) {
        return EOF_CHAR;
    }
    const auto next = std::next(chars);
    if (next == get_end()) {
        return EOF_CHAR;
    }
    return *next;
}

[[nodiscard]] char Cursor::third() const noexcept {
    if (chars == get_end()) {
        return EOF_CHAR;
    }
    const auto next = std::next(chars);
    if (next == get_end()) {
        return EOF_CHAR;
    }
    const auto next2 = std::next(next);
    if (next2 == get_end()) {
        return EOF_CHAR;
    }
    return *next2;
}

[[nodiscard]] char Cursor::bump() noexcept {
    if (chars == get_end()) {
        return EOF_CHAR;
    }
    return *(chars++);
}

void Cursor::eat_while(std::function<bool(char)> predicate) noexcept {
    chars = std::ranges::find_if_not(chars, get_end(), predicate);
}

void Cursor::eat_until(char ch) noexcept {
    chars = std::ranges::find_if(chars, get_end(), [ch](char c) { return c == ch; });
}

bool Cursor::eat_decimal_digits() noexcept {
    bool has_digits = false;

    while (true) {
        const auto first_char = first();
        if (first_char == '_') {
            std::ignore = bump();
            continue;
        } else if ('0' <= first_char && first_char <= '9') {
            has_digits = true;
            std::ignore = bump();
            continue;
        } else {
            break;
        }
    }

    return has_digits;
}

bool Cursor::eat_float_exponent() noexcept {
    const auto first_char = first();
    if (first_char == '-' || first_char == '+') {
        std::ignore = bump();
    }
    return eat_decimal_digits();
}

bool Cursor::quoted_string(char quote) noexcept {
    while (const auto ch = bump()) {
        if (ch == quote)
            return true;
        if (ch == '\\') {
            if (const auto first_char = first(); first_char == '\\' || first_char == quote) {
                std::ignore = bump();
                continue;
            }
        }
    }

    return false;
}

[[nodiscard]] TokenKind Cursor::number(char first_digit) noexcept {
    if (first_digit == '0') {
        switch (first()) {
        case '0' ... '9':
        case '_': eat_decimal_digits(); break;
        case '.':
        case 'e':
        case 'E': break;
        default: return TokenKind::LitInt;
        }
        return TokenKind::LitInt;
    }

    if (const auto first_char = first(); first_char == '.') {
        if (const auto second_char = second();
            second_char != '.' && !chars::is_id_start(second_char)) {
            std::ignore = bump();
            if (const auto first_char = first(); '0' <= first_char && first_char <= '9') {
                eat_decimal_digits();
                switch (first()) {
                case 'e':
                case 'E':
                    std::ignore = bump();
                    eat_float_exponent();
                    break;
                default: break;
                }
            }
            return TokenKind::LitFloat;
        } else if (first_char == 'e' || first_char == 'E') {
            std::ignore = bump();
            eat_float_exponent();
            return TokenKind::LitFloat;
        }
    }
    return TokenKind::LitInt;
}

[[nodiscard]] TokenKind Cursor::line_comment() noexcept {
    eat_until('\n');
    return TokenKind::LineComment;
}

[[nodiscard]] TokenKind Cursor::whitespace() noexcept {
    eat_while(chars::is_whitespace);
    return TokenKind::Whitespace;
}

[[nodiscard]] TokenKind Cursor::ident() noexcept {
    eat_while(chars::is_id_continue);
    return TokenKind::Ident;
}

[[nodiscard]] Token Cursor::advance_token() noexcept {
    const auto first_char = bump();
    if (first_char == EOF_CHAR) {
        return {.kind = TokenKind::Eof, .len = 0};
    }

    const auto get_kind = [this, first_char]() {
        switch (first_char) {
        case '0' ... '9': return number(first_char);
        case '#': return line_comment();

        case '.': return TokenKind::Dot;
        case ',': return TokenKind::Comma;
        case ';': return TokenKind::Semi;
        case ':': return TokenKind::Colon;
        case '(': return TokenKind::OpenParen;
        case ')': return TokenKind::CloseParen;
        case '{': return TokenKind::OpenBrace;
        case '}': return TokenKind::CloseBrace;
        case '[': return TokenKind::OpenBracket;
        case ']': return TokenKind::CloseBracket;
        case '@': return TokenKind::At;
        case '=': return TokenKind::Eq;
        case '!': return TokenKind::Bang;
        case '<': return TokenKind::Lt;
        case '>': return TokenKind::Gt;
        case '&': return TokenKind::Amp;
        case '|': return TokenKind::Pipe;
        case '+': return TokenKind::Plus;
        case '-': return TokenKind::Minus;
        case '*': return TokenKind::Star;
        case '/': return TokenKind::Slash;
        case '^': return TokenKind::Caret;
        case '~': return TokenKind::Tlide;
        case '%': return TokenKind::Percent;

        case '\'': quoted_string('\''); return TokenKind::LitStr;
        case '"': quoted_string('"'); return TokenKind::LitStr;

        default:
            if (chars::is_id_start(first_char)) {
                return ident();
            }
            if (chars::is_whitespace(first_char)) {
                return whitespace();
            }
            return TokenKind::Unknown;
        }

        return TokenKind::Eof;
    };

    TokenKind kind = get_kind();
    const auto pos = pos_within_token();
    reset_pos_within_token();

    return {.kind = kind, .len = pos};
}
}; // namespace cstc::lexer
