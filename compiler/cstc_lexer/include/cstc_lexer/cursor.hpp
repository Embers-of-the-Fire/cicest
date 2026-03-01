#ifndef CICEST_COMPILER_CSTC_LEXER_CURSOR_HPP
#define CICEST_COMPILER_CSTC_LEXER_CURSOR_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "cstc_lexer/token.hpp"

namespace cstc::lexer {

class Cursor {
    using Chars = std::string::const_iterator;

public:
    Cursor() = delete;
    Cursor(const std::string& str_source);
    ~Cursor() = default;

    static constexpr char EOF_CHAR = '\0';

    [[nodiscard]] std::uint32_t get_len_remaining() const noexcept;
    [[nodiscard]] std::string_view get_source() const noexcept;
    [[nodiscard]] Chars get_chars() const noexcept;

    /// Returns `EOF_CHAR` if the end of the source is reached.
    [[nodiscard]] char first() const noexcept;
    /// Returns `EOF_CHAR` if the end of the source is reached.
    [[nodiscard]] char second() const noexcept;
    /// Returns `EOF_CHAR` if the end of the source is reached.
    [[nodiscard]] char third() const noexcept;

    [[nodiscard]] char bump() noexcept;

    [[nodiscard]] Token advance_token() noexcept;

private:
    /// The raw byte len remaining.
    std::uint32_t len_remaining;
    std::string source;
    Chars chars;

    [[nodiscard]] Chars get_end() const noexcept;
    [[nodiscard]] std::uint32_t pos_within_token() const noexcept;
    void reset_pos_within_token() noexcept;

    bool eat_decimal_digits() noexcept;
    bool eat_float_exponent() noexcept;
    bool quoted_string(char quote) noexcept;
    void eat_while(std::function<bool(char)> predicate) noexcept;
    void eat_until(char ch) noexcept;
    [[nodiscard]] TokenKind number(char first_digit) noexcept;
    [[nodiscard]] TokenKind line_comment() noexcept;
    [[nodiscard]] TokenKind whitespace() noexcept;
    [[nodiscard]] TokenKind ident() noexcept;
}; // class Cursor

} // namespace cstc::lexer

#endif // CICEST_COMPILER_CSTC_LEXER_CURSOR_HPP
