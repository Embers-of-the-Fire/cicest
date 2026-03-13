#include <cassert>
#include <vector>

#include <cstc_lexer/lexer.hpp>
#include <cstc_lexer/token.hpp>

namespace {

void test_smoke_tokens() {
    const auto tokens = cstc::lexer::lex_source("fn main() -> Unit { let x: num = 1; }", false);

    assert(!tokens.empty());
    assert(tokens[0].kind == cstc::lexer::TokenKind::KwFn);
    assert(tokens[1].kind == cstc::lexer::TokenKind::Identifier);
    assert(tokens[2].kind == cstc::lexer::TokenKind::LParen);
    assert(tokens[3].kind == cstc::lexer::TokenKind::RParen);
    assert(tokens[4].kind == cstc::lexer::TokenKind::Arrow);
    assert(tokens[5].kind == cstc::lexer::TokenKind::KwUnit);
    assert(tokens.back().kind == cstc::lexer::TokenKind::EndOfFile);
}

void test_keep_trivia() {
    const auto without_trivia = cstc::lexer::lex_source("let x = 1 // c\n", false);
    const auto with_trivia = cstc::lexer::lex_source("let x = 1 // c\n", true);

    assert(with_trivia.size() > without_trivia.size());

    bool found_whitespace = false;
    bool found_comment = false;
    for (const auto& token : with_trivia) {
        if (token.kind == cstc::lexer::TokenKind::Whitespace)
            found_whitespace = true;
        if (token.kind == cstc::lexer::TokenKind::LineComment)
            found_comment = true;
    }

    assert(found_whitespace);
    assert(found_comment);
}

void test_base_position() {
    const auto tokens = cstc::lexer::lex_source_at("let x = 1;", 100, false);
    assert(!tokens.empty());
    assert(tokens.front().span.start == 100);
    assert(tokens.front().span.end == 103);
}

} // namespace

int main() {
    test_smoke_tokens();
    test_keep_trivia();
    test_base_position();
    return 0;
}
