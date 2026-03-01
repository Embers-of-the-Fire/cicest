
#include "cstc_lexer/cursor.hpp"
#include "cstc_lexer/token.hpp"
#include <cassert>
#include <cstdint>
#include <print>
#include <string>
#include <vector>

using namespace cstc::lexer;

static void check_lexing(const std::string& src, const std::vector<Token>& expected) {
    Cursor cursor(src);
    std::size_t idx = 0;
    while (true) {
        const auto tok = cursor.advance_token();
        assert(idx < expected.size());
        const auto& exp = expected[idx++];
        std::print(
            "Token: kind={}, len={}\tExpected: kind={}, len={}\n",
            static_cast<std::uint32_t>(tok.kind), tok.len, static_cast<std::uint32_t>(exp.kind),
            exp.len);
        assert(tok.kind == exp.kind);
        assert(tok.len == exp.len);
        if (tok.kind == TokenKind::Eof)
            break;
    }
}

// Original test case preserved.
static void test_original() {
    check_lexing(
        "&&  =>sfoo 1.25=1", {
                                 {       .kind = TokenKind::Amp, .len = 1},
                                 {       .kind = TokenKind::Amp, .len = 1},
                                 {.kind = TokenKind::Whitespace, .len = 2},
                                 {        .kind = TokenKind::Eq, .len = 1},
                                 {        .kind = TokenKind::Gt, .len = 1},
                                 {     .kind = TokenKind::Ident, .len = 4},
                                 {.kind = TokenKind::Whitespace, .len = 1},
                                 {  .kind = TokenKind::LitFloat, .len = 4},
                                 {        .kind = TokenKind::Eq, .len = 1},
                                 {    .kind = TokenKind::LitInt, .len = 1},
                                 {       .kind = TokenKind::Eof, .len = 0},
    });
}

// Basic program-like snippet: identifiers, brackets, operator, integer, semicolon.
static void test_smoke() {
    check_lexing(
        "fn main() { x = 1; }", {
                                    {     .kind = TokenKind::Ident, .len = 2},
                                    {.kind = TokenKind::Whitespace, .len = 1},
                                    {     .kind = TokenKind::Ident, .len = 4},
                                    { .kind = TokenKind::OpenParen, .len = 1},
                                    {.kind = TokenKind::CloseParen, .len = 1},
                                    {.kind = TokenKind::Whitespace, .len = 1},
                                    { .kind = TokenKind::OpenBrace, .len = 1},
                                    {.kind = TokenKind::Whitespace, .len = 1},
                                    {     .kind = TokenKind::Ident, .len = 1},
                                    {.kind = TokenKind::Whitespace, .len = 1},
                                    {        .kind = TokenKind::Eq, .len = 1},
                                    {.kind = TokenKind::Whitespace, .len = 1},
                                    {    .kind = TokenKind::LitInt, .len = 1},
                                    {      .kind = TokenKind::Semi, .len = 1},
                                    {.kind = TokenKind::Whitespace, .len = 1},
                                    {.kind = TokenKind::CloseBrace, .len = 1},
                                    {       .kind = TokenKind::Eof, .len = 0},
    });
}

// `#` introduces a line comment that runs to (but does not include) the newline.
static void test_line_comment() {
    check_lexing(
        "# line comment\nfoo", {
                                   {.kind = TokenKind::LineComment, .len = 14},
                                   { .kind = TokenKind::Whitespace,  .len = 1},
                                   {      .kind = TokenKind::Ident,  .len = 3},
                                   {        .kind = TokenKind::Eof,  .len = 0},
    });
    // Comment at end of file with no trailing newline.
    check_lexing(
        "# just a comment", {
                                {.kind = TokenKind::LineComment, .len = 16},
                                {        .kind = TokenKind::Eof,  .len = 0},
    });
    // Empty comment.
    check_lexing(
        "#\nok", {
                     {.kind = TokenKind::LineComment, .len = 1},
                     { .kind = TokenKind::Whitespace, .len = 1},
                     {      .kind = TokenKind::Ident, .len = 2},
                     {        .kind = TokenKind::Eof, .len = 0},
    });
}

// All ASCII whitespace characters collapse into a single Whitespace token.
static void test_whitespace() {
    check_lexing(
        " \t\n\r\x0b\x0c", {
                               {.kind = TokenKind::Whitespace, .len = 6},
                               {       .kind = TokenKind::Eof, .len = 0},
    });
}

// Integer literals.
static void test_integer_literals() {
    // Single-digit integers.
    check_lexing(
        "0 1 5", {
                     {    .kind = TokenKind::LitInt, .len = 1},
                     {.kind = TokenKind::Whitespace, .len = 1},
                     {    .kind = TokenKind::LitInt, .len = 1},
                     {.kind = TokenKind::Whitespace, .len = 1},
                     {    .kind = TokenKind::LitInt, .len = 1},
                     {       .kind = TokenKind::Eof, .len = 0},
    });
    // Multi-digit integer starting with 0: subsequent digits are consumed.
    check_lexing(
        "042", {
                   {.kind = TokenKind::LitInt, .len = 3},
                   {   .kind = TokenKind::Eof, .len = 0},
    });
    // Integer with underscore separator.
    check_lexing(
        "0_42", {
                    {.kind = TokenKind::LitInt, .len = 4},
                    {   .kind = TokenKind::Eof, .len = 0},
    });
}

// Float literals: require a non-zero leading digit followed by `.`.
static void test_float_literals() {
    check_lexing(
        "1.0 1.25 2.5e3 1.0e-2", {
                                     {  .kind = TokenKind::LitFloat, .len = 3},
                                     {.kind = TokenKind::Whitespace, .len = 1},
                                     {  .kind = TokenKind::LitFloat, .len = 4},
                                     {.kind = TokenKind::Whitespace, .len = 1},
                                     {  .kind = TokenKind::LitFloat, .len = 5},
                                     {.kind = TokenKind::Whitespace, .len = 1},
                                     {  .kind = TokenKind::LitFloat, .len = 6},
                                     {       .kind = TokenKind::Eof, .len = 0},
    });
    // Float with no fractional digits after dot.
    check_lexing(
        "1.", {
                  {.kind = TokenKind::LitFloat, .len = 2},
                  {     .kind = TokenKind::Eof, .len = 0},
    });
    // Float with positive exponent sign.
    check_lexing(
        "3.0e+4", {
                      {.kind = TokenKind::LitFloat, .len = 6},
                      {     .kind = TokenKind::Eof, .len = 0},
    });
}

// String literals: both `"` and `'` delimit a LitStr token.
// Only `\\` and the matching quote character are treated as escape sequences.
static void test_string_literals() {
    // Double-quoted string.
    check_lexing(
        "\"hello\"", {
                         {.kind = TokenKind::LitStr, .len = 7},
                         {   .kind = TokenKind::Eof, .len = 0},
    });
    // Single-quoted string.
    check_lexing(
        "'world'", {
                       {.kind = TokenKind::LitStr, .len = 7},
                       {   .kind = TokenKind::Eof, .len = 0},
    });
    // Escaped backslash inside a double-quoted string: `"he\\lo"` (8 chars).
    check_lexing(
        "\"he\\\\lo\"", {
                            {.kind = TokenKind::LitStr, .len = 8},
                            {   .kind = TokenKind::Eof, .len = 0},
    });
    // Escaped quote inside a double-quoted string: `"say \"hi\""` (12 chars).
    check_lexing(
        "\"say \\\"hi\\\"\"", {
                                  {.kind = TokenKind::LitStr, .len = 12},
                                  {   .kind = TokenKind::Eof,  .len = 0},
    });
    // Two adjacent string literals.
    check_lexing(
        "\"a\"'b'", {
                        {.kind = TokenKind::LitStr, .len = 3},
                        {.kind = TokenKind::LitStr, .len = 3},
                        {   .kind = TokenKind::Eof, .len = 0},
    });
}

// Every single-character operator/punctuation token.
static void test_operators() {
    check_lexing(
        "!+*/%=^~<>&|.,:;@()[]{}-",
        {
            {        .kind = TokenKind::Bang, .len = 1},
            {        .kind = TokenKind::Plus, .len = 1},
            {        .kind = TokenKind::Star, .len = 1},
            {       .kind = TokenKind::Slash, .len = 1},
            {     .kind = TokenKind::Percent, .len = 1},
            {          .kind = TokenKind::Eq, .len = 1},
            {       .kind = TokenKind::Caret, .len = 1},
            {       .kind = TokenKind::Tlide, .len = 1},
            {          .kind = TokenKind::Lt, .len = 1},
            {          .kind = TokenKind::Gt, .len = 1},
            {         .kind = TokenKind::Amp, .len = 1},
            {        .kind = TokenKind::Pipe, .len = 1},
            {         .kind = TokenKind::Dot, .len = 1},
            {       .kind = TokenKind::Comma, .len = 1},
            {       .kind = TokenKind::Colon, .len = 1},
            {        .kind = TokenKind::Semi, .len = 1},
            {          .kind = TokenKind::At, .len = 1},
            {   .kind = TokenKind::OpenParen, .len = 1},
            {  .kind = TokenKind::CloseParen, .len = 1},
            { .kind = TokenKind::OpenBracket, .len = 1},
            {.kind = TokenKind::CloseBracket, .len = 1},
            {   .kind = TokenKind::OpenBrace, .len = 1},
            {  .kind = TokenKind::CloseBrace, .len = 1},
            {       .kind = TokenKind::Minus, .len = 1},
            {         .kind = TokenKind::Eof, .len = 0},
    });
}

// Identifier lexing: id_start followed by id_continue characters.
static void test_identifiers() {
    check_lexing(
        "foo _bar abc_123 _", {
                                  {     .kind = TokenKind::Ident, .len = 3},
                                  {.kind = TokenKind::Whitespace, .len = 1},
                                  {     .kind = TokenKind::Ident, .len = 4},
                                  {.kind = TokenKind::Whitespace, .len = 1},
                                  {     .kind = TokenKind::Ident, .len = 7},
                                  {.kind = TokenKind::Whitespace, .len = 1},
                                  {     .kind = TokenKind::Ident, .len = 1},
                                  {       .kind = TokenKind::Eof, .len = 0},
    });
}

// Characters that are not part of any recognised token become Unknown.
static void test_unknown_tokens() {
    check_lexing(
        "$?", {
                  {.kind = TokenKind::Unknown, .len = 1},
                  {.kind = TokenKind::Unknown, .len = 1},
                  {    .kind = TokenKind::Eof, .len = 0},
    });
}

int main() {
    test_original();
    test_smoke();
    test_line_comment();
    test_whitespace();
    test_integer_literals();
    test_float_literals();
    test_string_literals();
    test_operators();
    test_identifiers();
    test_unknown_tokens();

    std::print("All tests passed.\n");
    return 0;
}
