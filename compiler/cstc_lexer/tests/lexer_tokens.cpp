#include <cassert>
#include <vector>

#include <cstc_lexer/lexer.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_symbol/symbol.hpp>

namespace {

// Returns the non-EOF tokens from a source string (trivia stripped).
std::vector<cstc::lexer::Token> lex(std::string_view src) {
    auto tokens = cstc::lexer::lex_source(src, false);
    // Remove the trailing EOF for easier indexing.
    if (!tokens.empty() && tokens.back().kind == cstc::lexer::TokenKind::EndOfFile)
        tokens.pop_back();
    return tokens;
}

void test_all_keywords() {
    cstc::symbol::SymbolSession session;

    const struct {
        const char* text;
        cstc::lexer::TokenKind kind;
    } cases[] = {
        {  "struct",   cstc::lexer::TokenKind::KwStruct},
        {    "enum",     cstc::lexer::TokenKind::KwEnum},
        {      "fn",       cstc::lexer::TokenKind::KwFn},
        {     "let",      cstc::lexer::TokenKind::KwLet},
        {      "if",       cstc::lexer::TokenKind::KwIf},
        {    "else",     cstc::lexer::TokenKind::KwElse},
        {     "for",      cstc::lexer::TokenKind::KwFor},
        {   "while",    cstc::lexer::TokenKind::KwWhile},
        {    "loop",     cstc::lexer::TokenKind::KwLoop},
        {   "break",    cstc::lexer::TokenKind::KwBreak},
        {"continue", cstc::lexer::TokenKind::KwContinue},
        {  "return",   cstc::lexer::TokenKind::KwReturn},
        {    "true",     cstc::lexer::TokenKind::KwTrue},
        {   "false",    cstc::lexer::TokenKind::KwFalse},
        {    "Unit",     cstc::lexer::TokenKind::KwUnit},
        {     "num",      cstc::lexer::TokenKind::KwNum},
        {     "str",      cstc::lexer::TokenKind::KwStr},
        {    "bool",     cstc::lexer::TokenKind::KwBool},
        {  "extern",   cstc::lexer::TokenKind::KwExtern},
    };

    for (const auto& [text, expected_kind] : cases) {
        const auto tokens = lex(text);
        assert(tokens.size() == 1);
        assert(tokens[0].kind == expected_kind);
        assert(tokens[0].symbol.as_str() == text);
    }
}

void test_identifier() {
    cstc::symbol::SymbolSession session;

    const auto tokens = lex("myVar _private x123");
    assert(tokens.size() == 3);
    assert(tokens[0].kind == cstc::lexer::TokenKind::Identifier);
    assert(tokens[0].symbol.as_str() == "myVar");
    assert(tokens[1].kind == cstc::lexer::TokenKind::Identifier);
    assert(tokens[1].symbol.as_str() == "_private");
    assert(tokens[2].kind == cstc::lexer::TokenKind::Identifier);
    assert(tokens[2].symbol.as_str() == "x123");
}

void test_number_literals() {
    cstc::symbol::SymbolSession session;

    const auto tokens = lex("42 3.14 0 100");
    assert(tokens.size() == 4);
    assert(tokens[0].kind == cstc::lexer::TokenKind::Number);
    assert(tokens[0].symbol.as_str() == "42");
    assert(tokens[1].kind == cstc::lexer::TokenKind::Number);
    assert(tokens[1].symbol.as_str() == "3.14");
    assert(tokens[2].kind == cstc::lexer::TokenKind::Number);
    assert(tokens[2].symbol.as_str() == "0");
    assert(tokens[3].kind == cstc::lexer::TokenKind::Number);
    assert(tokens[3].symbol.as_str() == "100");
}

void test_string_literal() {
    cstc::symbol::SymbolSession session;

    const auto tokens = lex(R"("hello" "with \"escape\"")");
    assert(tokens.size() == 2);
    assert(tokens[0].kind == cstc::lexer::TokenKind::String);
    assert(tokens[0].symbol.as_str() == "\"hello\"");
    assert(tokens[1].kind == cstc::lexer::TokenKind::String);
}

void test_all_single_char_operators() {
    cstc::symbol::SymbolSession session;

    const struct {
        const char* text;
        cstc::lexer::TokenKind kind;
    } cases[] = {
        {"{",    cstc::lexer::TokenKind::LBrace},
        {"}",    cstc::lexer::TokenKind::RBrace},
        {"(",    cstc::lexer::TokenKind::LParen},
        {")",    cstc::lexer::TokenKind::RParen},
        {",",     cstc::lexer::TokenKind::Comma},
        {";", cstc::lexer::TokenKind::Semicolon},
        {":",     cstc::lexer::TokenKind::Colon},
        {".",       cstc::lexer::TokenKind::Dot},
        {"+",      cstc::lexer::TokenKind::Plus},
        {"-",     cstc::lexer::TokenKind::Minus},
        {"*",      cstc::lexer::TokenKind::Star},
        {"/",     cstc::lexer::TokenKind::Slash},
        {"%",   cstc::lexer::TokenKind::Percent},
        {"!",      cstc::lexer::TokenKind::Bang},
        {"<",        cstc::lexer::TokenKind::Lt},
        {">",        cstc::lexer::TokenKind::Gt},
        {"=",    cstc::lexer::TokenKind::Assign},
    };

    for (const auto& [text, expected_kind] : cases) {
        const auto tokens = lex(text);
        assert(tokens.size() == 1);
        assert(tokens[0].kind == expected_kind);
    }
}

void test_all_two_char_operators() {
    cstc::symbol::SymbolSession session;

    const struct {
        const char* text;
        cstc::lexer::TokenKind kind;
    } cases[] = {
        {"::", cstc::lexer::TokenKind::ColonColon},
        {"->",      cstc::lexer::TokenKind::Arrow},
        {"&&",     cstc::lexer::TokenKind::AndAnd},
        {"||",       cstc::lexer::TokenKind::OrOr},
        {"==",       cstc::lexer::TokenKind::EqEq},
        {"!=",      cstc::lexer::TokenKind::NotEq},
        {"<=",       cstc::lexer::TokenKind::LtEq},
        {">=",       cstc::lexer::TokenKind::GtEq},
    };

    for (const auto& [text, expected_kind] : cases) {
        const auto tokens = lex(text);
        assert(tokens.size() == 1);
        assert(tokens[0].kind == expected_kind);
    }
}

void test_colon_vs_colon_colon() {
    cstc::symbol::SymbolSession session;

    const auto tokens = lex(": ::");
    assert(tokens.size() == 2);
    assert(tokens[0].kind == cstc::lexer::TokenKind::Colon);
    assert(tokens[1].kind == cstc::lexer::TokenKind::ColonColon);
}

void test_eof_always_present() {
    cstc::symbol::SymbolSession session;

    auto tokens = cstc::lexer::lex_source("", false);
    assert(!tokens.empty());
    assert(tokens.back().kind == cstc::lexer::TokenKind::EndOfFile);

    tokens = cstc::lexer::lex_source("fn", false);
    assert(tokens.back().kind == cstc::lexer::TokenKind::EndOfFile);
}

void test_spans_are_correct() {
    cstc::symbol::SymbolSession session;

    // "fn foo" → fn=[0,2), ws, foo=[3,6)
    const auto tokens = lex("fn foo");
    assert(tokens.size() == 2);
    assert(tokens[0].span.start == 0);
    assert(tokens[0].span.end == 2);
    assert(tokens[1].span.start == 3);
    assert(tokens[1].span.end == 6);
}

void test_span_base_position() {
    cstc::symbol::SymbolSession session;

    const auto tokens = cstc::lexer::lex_source_at("fn", 1000, false);
    // tokens[0] = fn at absolute [1000, 1002), tokens[1] = EOF
    assert(tokens.size() == 2);
    assert(tokens[0].span.start == 1000);
    assert(tokens[0].span.end == 1002);
    assert(tokens[1].span.start == 1002);
    assert(tokens[1].span.end == 1002);
}

void test_unknown_unterminated_string() {
    cstc::symbol::SymbolSession session;

    // An unterminated string literal becomes an Unknown token and is always emitted.
    const auto tokens = cstc::lexer::lex_source("\"unterminated", false);
    assert(!tokens.empty());
    // Unknown tokens are always included regardless of keep_trivia.
    bool found_unknown = false;
    for (const auto& t : tokens)
        if (t.kind == cstc::lexer::TokenKind::Unknown)
            found_unknown = true;
    assert(found_unknown);
}

void test_unknown_unterminated_block_comment() {
    cstc::symbol::SymbolSession session;

    const auto tokens = cstc::lexer::lex_source("/* not closed", false);
    bool found_unknown = false;
    for (const auto& t : tokens)
        if (t.kind == cstc::lexer::TokenKind::Unknown)
            found_unknown = true;
    assert(found_unknown);
}

void test_unknown_char() {
    cstc::symbol::SymbolSession session;

    // '$' is not a valid token character
    const auto tokens = cstc::lexer::lex_source("$", false);
    assert(!tokens.empty());
    assert(tokens[0].kind == cstc::lexer::TokenKind::Unknown);
}

void test_trivia_included_when_requested() {
    cstc::symbol::SymbolSession session;

    const auto with = cstc::lexer::lex_source("fn /* c */ foo // end\n", true);
    const auto without = cstc::lexer::lex_source("fn /* c */ foo // end\n", false);

    assert(with.size() > without.size());

    bool has_block_comment = false;
    bool has_line_comment = false;
    bool has_whitespace = false;
    for (const auto& t : with) {
        if (t.kind == cstc::lexer::TokenKind::BlockComment)
            has_block_comment = true;
        if (t.kind == cstc::lexer::TokenKind::LineComment)
            has_line_comment = true;
        if (t.kind == cstc::lexer::TokenKind::Whitespace)
            has_whitespace = true;
    }
    assert(has_block_comment);
    assert(has_line_comment);
    assert(has_whitespace);
}

void test_is_trivia() {
    assert(cstc::lexer::is_trivia(cstc::lexer::TokenKind::Whitespace));
    assert(cstc::lexer::is_trivia(cstc::lexer::TokenKind::LineComment));
    assert(cstc::lexer::is_trivia(cstc::lexer::TokenKind::BlockComment));
    assert(!cstc::lexer::is_trivia(cstc::lexer::TokenKind::Identifier));
    assert(!cstc::lexer::is_trivia(cstc::lexer::TokenKind::Number));
    assert(!cstc::lexer::is_trivia(cstc::lexer::TokenKind::KwFn));
    assert(!cstc::lexer::is_trivia(cstc::lexer::TokenKind::EndOfFile));
}

void test_token_kind_name() {
    cstc::symbol::SymbolSession session;

    assert(!cstc::lexer::token_kind_name(cstc::lexer::TokenKind::KwFn).empty());
    assert(!cstc::lexer::token_kind_name(cstc::lexer::TokenKind::Identifier).empty());
    assert(!cstc::lexer::token_kind_name(cstc::lexer::TokenKind::EndOfFile).empty());
}

} // namespace

int main() {
    test_all_keywords();
    test_identifier();
    test_number_literals();
    test_string_literal();
    test_all_single_char_operators();
    test_all_two_char_operators();
    test_colon_vs_colon_colon();
    test_eof_always_present();
    test_spans_are_correct();
    test_span_base_position();
    test_unknown_unterminated_string();
    test_unknown_unterminated_block_comment();
    test_unknown_char();
    test_trivia_included_when_requested();
    test_is_trivia();
    test_token_kind_name();
    return 0;
}
