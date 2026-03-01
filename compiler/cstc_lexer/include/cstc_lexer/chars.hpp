#ifndef CICEST_COMPILER_CSTC_LEXER_CHARS_HPP
#define CICEST_COMPILER_CSTC_LEXER_CHARS_HPP

#include <algorithm>
#include <iterator>
#include <string_view>
namespace cstc::lexer::chars {

constexpr bool is_whitespace(char ch) {
    switch (ch) {
    case 0x09: // horizontal tab (\t)
    case 0x0A: // line feed (\n)
    case 0x0B: // vertical tab
    case 0x0C: // form feed
    case 0x0D: // carriage return (\r)
    case 0x20: // space
        return true;
    default: return false;
    }
}

constexpr bool is_horizontal_whitespace(char ch) {
    switch (ch) {
    case 0x09: // horizontal tab (\t)
    case 0x20: // space
        return true;
    default: return false;
    }
}

constexpr bool is_xid_start(char ch) {
    return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z');
}

constexpr bool is_id_start(char ch) { return is_xid_start(ch) || ch == '_'; }

constexpr bool is_id_continue(char ch) {
    return is_xid_start(ch) || ('0' <= ch && ch <= '9') || ch == '_';
}

constexpr bool is_ident(std::string_view str) {
    const auto iter = str.begin();
    const auto first = std::next(iter);
    if (first == str.end())
        return false;
    if (!is_id_start(*iter))
        return false;
    return std::ranges::all_of(std::ranges::subrange(first, str.end()), is_id_continue);
}

} // namespace cstc::lexer::chars

#endif // CICEST_COMPILER_CSTC_LEXER_CHARS_HPP
