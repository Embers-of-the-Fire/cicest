#ifndef CICEST_COMPILER_CSTC_PARSER_PARSER_IMPL_HPP
#define CICEST_COMPILER_CSTC_PARSER_PARSER_IMPL_HPP

#include "cstc_parser/parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <utility>

#include <cstc_lexer/cursor.hpp>

namespace cstc::parser {
namespace {

template <typename T>
using ParseResult = std::expected<T, ParseError>;

constexpr std::array<std::string_view, 26> RESERVED_KEYWORDS = {
    "let",    "runtime", "const",  "fn",      "where",  "extern", "if",    "else",
    "match",  "loop",    "for",    "struct",  "enum",   "type",   "with",  "concept",
    "return", "self",    "lambda", "decl",    "true",   "false",  "_",     "import",
    "from",   "export",
};

[[nodiscard]] bool is_reserved_keyword(std::string_view text) noexcept {
    return std::find(RESERVED_KEYWORDS.begin(), RESERVED_KEYWORDS.end(), text)
        != RESERVED_KEYWORDS.end();
}

[[nodiscard]] bool is_trivia_token(cstc::lexer::TokenKind kind) noexcept {
    return kind == cstc::lexer::TokenKind::Whitespace
        || kind == cstc::lexer::TokenKind::LineComment
        || kind == cstc::lexer::TokenKind::BlockComment;
}

[[nodiscard]] cstc::span::SourceSpan
    merge_spans(cstc::span::SourceSpan lhs, cstc::span::SourceSpan rhs) noexcept {
    return {
        .start = std::min(lhs.start, rhs.start),
        .end = std::max(lhs.end, rhs.end),
    };
}

[[nodiscard]] bool is_bool_literal(std::string_view text) noexcept {
    return text == "true" || text == "false";
}

[[nodiscard]] bool is_char_literal_text(std::string_view text) noexcept {
    if (text.size() < 3 || text.front() != '\'' || text.back() != '\'')
        return false;
    if (text.size() == 3)
        return true;
    if (text.size() == 4 && text[1] == '\\')
        return true;
    return false;
}

struct NameAndSpan {
    cstc::ast::Symbol symbol;
    cstc::span::SourceSpan span;
};

class Parser {
public:
    Parser(std::span<const LexedToken> raw_tokens, cstc::ast::SymbolTable& symbols)
        : symbols_(symbols) {
        tokens_.reserve(raw_tokens.size() + 1);
        for (const auto& token : raw_tokens) {
            if (token.kind == cstc::lexer::TokenKind::Eof) {
                tokens_.push_back(token);
                break;
            }
            if (is_trivia_token(token.kind))
                continue;
            tokens_.push_back(token);
        }

        if (tokens_.empty() || tokens_.back().kind != cstc::lexer::TokenKind::Eof) {
            const auto eof_offset =
                tokens_.empty() ? 0 : static_cast<std::size_t>(tokens_.back().span.end);
            tokens_.push_back(
                LexedToken{
                    .kind = cstc::lexer::TokenKind::Eof,
                    .span = {.start = eof_offset, .end = eof_offset},
                    .text = {},
            });
        }
    }

    [[nodiscard]] ParseResult<cstc::ast::Crate> parse_crate();

private:
    [[nodiscard]] const LexedToken& token_at(std::size_t index) const noexcept;
    [[nodiscard]] const LexedToken& peek(std::size_t offset = 0) const noexcept;
    [[nodiscard]] const LexedToken& previous() const noexcept;

    [[nodiscard]] bool at_end() const noexcept;
    const LexedToken& advance() noexcept;

    [[nodiscard]] bool check(cstc::lexer::TokenKind kind, std::size_t offset = 0) const noexcept;
    [[nodiscard]] bool check_ident(std::string_view text, std::size_t offset = 0) const noexcept;
    [[nodiscard]] bool check_ident_at(std::size_t index, std::string_view text) const noexcept;

    bool match(cstc::lexer::TokenKind kind) noexcept;
    bool match_ident(std::string_view text) noexcept;

    [[nodiscard]] bool check_sequence(
        std::initializer_list<cstc::lexer::TokenKind> kinds, std::size_t offset = 0) const noexcept;
    bool match_sequence(std::initializer_list<cstc::lexer::TokenKind> kinds) noexcept;

    bool match_arrow() noexcept;
    bool match_fat_arrow() noexcept;
    bool match_colon_colon() noexcept;
    [[nodiscard]] bool check_colon_colon_followed_by_ident(std::size_t offset = 0) const noexcept;

    bool match_eq_eq() noexcept;
    bool match_not_eq() noexcept;
    bool match_lt_eq() noexcept;
    bool match_gt_eq() noexcept;
    bool match_and_and() noexcept;
    bool match_or_or() noexcept;
    bool match_shl() noexcept;
    bool match_shr() noexcept;

    bool match_single_eq() noexcept;
    bool match_single_amp() noexcept;
    bool match_single_pipe() noexcept;
    bool match_single_lt() noexcept;
    bool match_single_gt() noexcept;

    [[nodiscard]] bool check_turbofish_start() const noexcept;
    bool match_turbofish_start() noexcept;

    [[nodiscard]] std::optional<std::size_t>
        keyword_modifier_length_at(std::size_t index) const noexcept;
    [[nodiscard]] bool looks_like_keyword_block_expr() const noexcept;
    [[nodiscard]] bool is_item_start() const noexcept;

    template <typename T>
    [[nodiscard]] ParseResult<T> fail(std::string message) const {
        return std::unexpected(
            ParseError{
                .span = peek().span,
                .message = std::move(message),
            });
    }

    [[nodiscard]] ParseResult<LexedToken>
        expect(cstc::lexer::TokenKind kind, std::string_view expectation);

    [[nodiscard]] ParseResult<NameAndSpan>
        parse_identifier(std::string_view expectation, bool allow_reserved = false);
    [[nodiscard]] ParseResult<cstc::ast::PathSegment> parse_path_segment();
    [[nodiscard]] ParseResult<cstc::ast::Path> parse_path();
    [[nodiscard]] ParseResult<cstc::ast::Lit> parse_literal();

    [[nodiscard]] ParseResult<std::optional<cstc::ast::KeywordModifier>> parse_keyword_modifier();
    [[nodiscard]] ParseResult<std::vector<cstc::ast::KeywordModifier>> parse_keyword_modifiers();

    [[nodiscard]] ParseResult<std::optional<cstc::ast::GenericParams>>
        parse_optional_generic_params();
    [[nodiscard]] ParseResult<std::optional<cstc::ast::WhereClause>> parse_optional_where_clause();

    [[nodiscard]] ParseResult<cstc::ast::GenericArgs>
        parse_generic_args_after_open_angle(std::size_t open_start);
    [[nodiscard]] ParseResult<cstc::ast::GenericArgs> parse_turbofish_args_after_prefix();

    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::TypeNode>> parse_type();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::TypeNode>> parse_type_atom();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::TypeNode>>
        parse_parenthesized_type(cstc::span::SourceSpan open_span);

    [[nodiscard]] ParseResult<std::optional<cstc::ast::SelfParam>> parse_optional_self_param();
    [[nodiscard]] ParseResult<cstc::ast::FnParam> parse_fn_param();
    [[nodiscard]] ParseResult<cstc::ast::FnSig> parse_fn_sig();

    [[nodiscard]] ParseResult<cstc::ast::StructField> parse_struct_field();

    [[nodiscard]] ParseResult<cstc::ast::Item> parse_item();
    [[nodiscard]] ParseResult<cstc::ast::Item> parse_import_item(cstc::span::SourceSpan start_span);
    [[nodiscard]] ParseResult<cstc::ast::Item> parse_fn_item(
        std::vector<cstc::ast::KeywordModifier> keywords, cstc::span::SourceSpan start_span,
        bool is_exported);
    [[nodiscard]] ParseResult<cstc::ast::Item> parse_extern_fn_item(
        std::vector<cstc::ast::KeywordModifier> keywords, cstc::span::SourceSpan start_span);
    [[nodiscard]] ParseResult<cstc::ast::Item> parse_struct_item(cstc::span::SourceSpan start_span);
    [[nodiscard]] ParseResult<cstc::ast::Item> parse_enum_item(cstc::span::SourceSpan start_span);
    [[nodiscard]] ParseResult<cstc::ast::Item>
        parse_type_alias_item(cstc::span::SourceSpan start_span);
    [[nodiscard]] ParseResult<cstc::ast::Item>
        parse_concept_item(cstc::span::SourceSpan start_span);
    [[nodiscard]] ParseResult<cstc::ast::ConceptMethod> parse_concept_method();
    [[nodiscard]] ParseResult<cstc::ast::Item> parse_with_item(cstc::span::SourceSpan start_span);

    [[nodiscard]] ParseResult<cstc::ast::Block> parse_block();
    [[nodiscard]] ParseResult<cstc::ast::Stmt> parse_stmt();
    [[nodiscard]] ParseResult<cstc::ast::Stmt> parse_let_stmt(cstc::span::SourceSpan start_span);

    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_logical_or_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_logical_and_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_comparison_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_bitwise_or_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_bitwise_xor_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_bitwise_and_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_shift_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_additive_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_multiplicative_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_unary_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_postfix_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_primary_expr();

    [[nodiscard]] ParseResult<
        std::pair<std::vector<std::unique_ptr<cstc::ast::Expr>>, cstc::span::SourceSpan>>
        parse_expr_arguments_after_open_paren();

    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_group_or_tuple_expr(cstc::span::SourceSpan open_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_constructor_fields_expr(cstc::ast::Path constructor);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>> parse_keyword_block_expr();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_if_expr(cstc::span::SourceSpan if_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_match_expr(cstc::span::SourceSpan match_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_loop_expr(cstc::span::SourceSpan loop_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_for_expr(cstc::span::SourceSpan for_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_return_expr(cstc::span::SourceSpan return_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_lambda_expr(cstc::span::SourceSpan lambda_span);
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Expr>>
        parse_decl_expr(cstc::span::SourceSpan decl_span);

    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Pat>> parse_pattern();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Pat>> parse_or_pattern();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Pat>> parse_as_pattern();
    [[nodiscard]] ParseResult<std::unique_ptr<cstc::ast::Pat>> parse_pattern_atom();

    template <typename Kind>
    [[nodiscard]] std::unique_ptr<cstc::ast::Expr>
        make_expr(cstc::span::SourceSpan span, Kind kind) {
        return std::make_unique<cstc::ast::Expr>(cstc::ast::Expr{
            .id = node_ids_.next(),
            .span = span,
            .kind = cstc::ast::ExprKind{std::move(kind)},
        });
    }

    template <typename Kind>
    [[nodiscard]] std::unique_ptr<cstc::ast::TypeNode>
        make_type(cstc::span::SourceSpan span, Kind kind) {
        return std::make_unique<cstc::ast::TypeNode>(cstc::ast::TypeNode{
            .id = node_ids_.next(),
            .span = span,
            .kind = cstc::ast::TypeKind{std::move(kind)},
        });
    }

    template <typename Kind>
    [[nodiscard]] std::unique_ptr<cstc::ast::Pat> make_pat(cstc::span::SourceSpan span, Kind kind) {
        return std::make_unique<cstc::ast::Pat>(cstc::ast::Pat{
            .id = node_ids_.next(),
            .span = span,
            .kind = cstc::ast::PatKind{std::move(kind)},
        });
    }

    [[nodiscard]] std::unique_ptr<cstc::ast::Expr> make_binary_expr(
        cstc::ast::BinaryOp op, std::unique_ptr<cstc::ast::Expr> lhs,
        std::unique_ptr<cstc::ast::Expr> rhs);
    [[nodiscard]] std::unique_ptr<cstc::ast::Expr> make_unary_expr(
        cstc::ast::UnaryOp op, cstc::span::SourceSpan op_span,
        std::unique_ptr<cstc::ast::Expr> operand);

    void push_local_scope();
    void pop_local_scope();
    void bind_local_symbol(cstc::ast::Symbol symbol);
    [[nodiscard]] bool scope_contains_symbol(
        std::size_t scope_index, cstc::ast::Symbol symbol) const;
    [[nodiscard]] bool is_lambda_capture_path(const cstc::ast::Path& path) const;

    [[nodiscard]] bool is_constructor_path(const cstc::ast::Path& path) const;

    cstc::ast::SymbolTable& symbols_;
    cstc::ast::NodeIdAllocator node_ids_;
    std::vector<std::vector<cstc::ast::Symbol>> local_scopes_;
    std::vector<std::size_t> lambda_scope_base_depths_;
    std::vector<LexedToken> tokens_;
    std::size_t cursor_ = 0;
};

const LexedToken& Parser::token_at(std::size_t index) const noexcept {
    if (index >= tokens_.size())
        return tokens_.back();
    return tokens_[index];
}

const LexedToken& Parser::peek(std::size_t offset) const noexcept {
    return token_at(cursor_ + offset);
}

const LexedToken& Parser::previous() const noexcept {
    if (cursor_ == 0)
        return tokens_.front();
    return tokens_[cursor_ - 1];
}

bool Parser::at_end() const noexcept { return peek().kind == cstc::lexer::TokenKind::Eof; }

const LexedToken& Parser::advance() noexcept {
    if (!at_end())
        ++cursor_;
    return previous();
}

bool Parser::check(cstc::lexer::TokenKind kind, std::size_t offset) const noexcept {
    return peek(offset).kind == kind;
}

bool Parser::check_ident(std::string_view text, std::size_t offset) const noexcept {
    const auto& token = peek(offset);
    return token.kind == cstc::lexer::TokenKind::Ident && token.text == text;
}

bool Parser::check_ident_at(std::size_t index, std::string_view text) const noexcept {
    const auto& token = token_at(index);
    return token.kind == cstc::lexer::TokenKind::Ident && token.text == text;
}

bool Parser::match(cstc::lexer::TokenKind kind) noexcept {
    if (!check(kind))
        return false;
    advance();
    return true;
}

bool Parser::match_ident(std::string_view text) noexcept {
    if (!check_ident(text))
        return false;
    advance();
    return true;
}

bool Parser::check_sequence(
    std::initializer_list<cstc::lexer::TokenKind> kinds, std::size_t offset) const noexcept {
    std::size_t index = offset;
    for (const auto kind : kinds) {
        if (!check(kind, index))
            return false;
        ++index;
    }
    return true;
}

bool Parser::match_sequence(std::initializer_list<cstc::lexer::TokenKind> kinds) noexcept {
    if (!check_sequence(kinds))
        return false;
    cursor_ += kinds.size();
    return true;
}

bool Parser::match_arrow() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Minus, cstc::lexer::TokenKind::Gt});
}

bool Parser::match_fat_arrow() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Eq, cstc::lexer::TokenKind::Gt});
}

bool Parser::match_colon_colon() noexcept {
    if (match(cstc::lexer::TokenKind::ColonColon))
        return true;
    return match_sequence({cstc::lexer::TokenKind::Colon, cstc::lexer::TokenKind::Colon});
}

bool Parser::check_colon_colon_followed_by_ident(std::size_t offset) const noexcept {
    if (check(cstc::lexer::TokenKind::ColonColon, offset)
        && check(cstc::lexer::TokenKind::Ident, offset + 1)) {
        return true;
    }
    return check_sequence(
        {cstc::lexer::TokenKind::Colon, cstc::lexer::TokenKind::Colon,
         cstc::lexer::TokenKind::Ident},
        offset);
}

bool Parser::match_eq_eq() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Eq, cstc::lexer::TokenKind::Eq});
}

bool Parser::match_not_eq() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Bang, cstc::lexer::TokenKind::Eq});
}

bool Parser::match_lt_eq() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Lt, cstc::lexer::TokenKind::Eq});
}

bool Parser::match_gt_eq() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Gt, cstc::lexer::TokenKind::Eq});
}

bool Parser::match_and_and() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Amp, cstc::lexer::TokenKind::Amp});
}

bool Parser::match_or_or() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Pipe, cstc::lexer::TokenKind::Pipe});
}

bool Parser::match_shl() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Lt, cstc::lexer::TokenKind::Lt});
}

bool Parser::match_shr() noexcept {
    return match_sequence({cstc::lexer::TokenKind::Gt, cstc::lexer::TokenKind::Gt});
}

bool Parser::match_single_eq() noexcept {
    if (!check(cstc::lexer::TokenKind::Eq))
        return false;
    if (check(cstc::lexer::TokenKind::Eq, 1) || check(cstc::lexer::TokenKind::Gt, 1))
        return false;
    advance();
    return true;
}

bool Parser::match_single_amp() noexcept {
    if (!check(cstc::lexer::TokenKind::Amp))
        return false;
    if (check(cstc::lexer::TokenKind::Amp, 1))
        return false;
    advance();
    return true;
}

bool Parser::match_single_pipe() noexcept {
    if (!check(cstc::lexer::TokenKind::Pipe))
        return false;
    if (check(cstc::lexer::TokenKind::Pipe, 1))
        return false;
    advance();
    return true;
}

bool Parser::match_single_lt() noexcept {
    if (!check(cstc::lexer::TokenKind::Lt))
        return false;
    if (check(cstc::lexer::TokenKind::Lt, 1) || check(cstc::lexer::TokenKind::Eq, 1))
        return false;
    advance();
    return true;
}

bool Parser::match_single_gt() noexcept {
    if (!check(cstc::lexer::TokenKind::Gt))
        return false;
    if (check(cstc::lexer::TokenKind::Gt, 1) || check(cstc::lexer::TokenKind::Eq, 1))
        return false;
    advance();
    return true;
}

bool Parser::check_turbofish_start() const noexcept {
    if (check(cstc::lexer::TokenKind::ColonColon) && check(cstc::lexer::TokenKind::Lt, 1))
        return true;
    return check_sequence(
        {cstc::lexer::TokenKind::Colon, cstc::lexer::TokenKind::Colon, cstc::lexer::TokenKind::Lt});
}

bool Parser::match_turbofish_start() noexcept {
    if (check(cstc::lexer::TokenKind::ColonColon) && check(cstc::lexer::TokenKind::Lt, 1)) {
        cursor_ += 2;
        return true;
    }
    if (check_sequence(
            {cstc::lexer::TokenKind::Colon, cstc::lexer::TokenKind::Colon,
             cstc::lexer::TokenKind::Lt})) {
        cursor_ += 3;
        return true;
    }
    return false;
}

void Parser::push_local_scope() { local_scopes_.emplace_back(); }

void Parser::pop_local_scope() {
    if (!local_scopes_.empty())
        local_scopes_.pop_back();
}

void Parser::bind_local_symbol(cstc::ast::Symbol symbol) {
    if (local_scopes_.empty())
        return;

    auto& scope = local_scopes_.back();
    if (std::find(scope.begin(), scope.end(), symbol) != scope.end())
        return;
    scope.push_back(symbol);
}

bool Parser::scope_contains_symbol(std::size_t scope_index, cstc::ast::Symbol symbol) const {
    if (scope_index >= local_scopes_.size())
        return false;

    const auto& scope = local_scopes_[scope_index];
    return std::find(scope.begin(), scope.end(), symbol) != scope.end();
}

bool Parser::is_lambda_capture_path(const cstc::ast::Path& path) const {
    if (lambda_scope_base_depths_.empty() || path.segments.size() != 1)
        return false;

    const auto symbol = path.segments.front().name;
    const auto base_depth = lambda_scope_base_depths_.back();

    for (std::size_t index = local_scopes_.size(); index > base_depth; --index) {
        if (scope_contains_symbol(index - 1, symbol))
            return false;
    }

    for (std::size_t index = std::min(base_depth, local_scopes_.size()); index > 0; --index) {
        if (scope_contains_symbol(index - 1, symbol))
            return true;
    }

    return false;
}

std::optional<std::size_t> Parser::keyword_modifier_length_at(std::size_t index) const noexcept {
    const auto& token = token_at(index);
    if (token.kind == cstc::lexer::TokenKind::Ident) {
        if (token.text == "runtime") {
            if (token_at(index + 1).kind == cstc::lexer::TokenKind::Lt
                && token_at(index + 2).kind == cstc::lexer::TokenKind::Ident
                && token_at(index + 3).kind == cstc::lexer::TokenKind::Gt) {
                return 4;
            }
            return 1;
        }
        if (token.text == "const")
            return 1;
        return std::nullopt;
    }

    if (token.kind == cstc::lexer::TokenKind::Bang
        && token_at(index + 1).kind == cstc::lexer::TokenKind::Ident
        && token_at(index + 1).text == "runtime") {
        return 2;
    }

    return std::nullopt;
}

bool Parser::looks_like_keyword_block_expr() const noexcept {
    std::size_t index = cursor_;
    bool has_modifiers = false;
    while (true) {
        const auto modifier_len = keyword_modifier_length_at(index);
        if (!modifier_len.has_value())
            break;
        has_modifiers = true;
        index += *modifier_len;
    }
    return has_modifiers && token_at(index).kind == cstc::lexer::TokenKind::OpenBrace;
}

bool Parser::is_item_start() const noexcept {
    std::size_t index = cursor_;
    if (check_ident_at(index, "import"))
        return true;

    bool has_export = false;
    if (check_ident_at(index, "export")) {
        has_export = true;
        ++index;
    }

    while (true) {
        const auto modifier_len = keyword_modifier_length_at(index);
        if (!modifier_len.has_value())
            break;
        index += *modifier_len;
    }

    if (check_ident_at(index, "extern") && check_ident_at(index + 1, "fn"))
        return !has_export;
    if (check_ident_at(index, "fn"))
        return true;
    if (has_export)
        return false;

    return check_ident_at(index, "struct")
        || check_ident_at(index, "enum");
}

ParseResult<LexedToken> Parser::expect(cstc::lexer::TokenKind kind, std::string_view expectation) {
    if (match(kind))
        return previous();
    return fail<LexedToken>("expected " + std::string(expectation));
}

ParseResult<NameAndSpan>
    Parser::parse_identifier(std::string_view expectation, bool allow_reserved) {
    if (!check(cstc::lexer::TokenKind::Ident))
        return fail<NameAndSpan>("expected " + std::string(expectation));

    const auto& token = peek();
    if (!allow_reserved && is_reserved_keyword(token.text)) {
        return fail<NameAndSpan>(
            "keyword `" + token.text + "` cannot be used as " + std::string(expectation));
    }

    const auto symbol = symbols_.intern(token.text);
    const auto span = token.span;
    advance();
    return NameAndSpan{
        .symbol = symbol,
        .span = span,
    };
}

ParseResult<cstc::ast::PathSegment> Parser::parse_path_segment() {
    auto name_result = parse_identifier("path segment", true);
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    const auto name_text = symbols_.str(name_result->symbol);
    if (is_reserved_keyword(name_text) && name_text != "self") {
        return fail<cstc::ast::PathSegment>(
            "keyword `" + std::string(name_text) + "` cannot be used as path segment");
    }

    return cstc::ast::PathSegment{
        .span = name_result->span,
        .name = name_result->symbol,
    };
}

ParseResult<cstc::ast::Path> Parser::parse_path() {
    auto first_segment_result = parse_path_segment();
    if (!first_segment_result)
        return std::unexpected(std::move(first_segment_result.error()));

    std::vector<cstc::ast::PathSegment> segments;
    segments.push_back(std::move(*first_segment_result));

    while (check_colon_colon_followed_by_ident()) {
        std::ignore = match_colon_colon();
        auto segment_result = parse_path_segment();
        if (!segment_result)
            return std::unexpected(std::move(segment_result.error()));
        segments.push_back(std::move(*segment_result));
    }

    return cstc::ast::Path{
        .span =
            {
                   .start = segments.front().span.start,
                   .end = segments.back().span.end,
                   },
        .segments = std::move(segments),
    };
}

ParseResult<cstc::ast::Lit> Parser::parse_literal() {
    if (!check(cstc::lexer::TokenKind::LitInt) && !check(cstc::lexer::TokenKind::LitFloat)
        && !check(cstc::lexer::TokenKind::LitStr)
        && !(check(cstc::lexer::TokenKind::Ident) && is_bool_literal(peek().text))) {
        return fail<cstc::ast::Lit>("expected literal");
    }

    const auto token = advance();

    cstc::ast::LitKind lit_kind = cstc::ast::LitKind::Int;
    switch (token.kind) {
    case cstc::lexer::TokenKind::LitInt: lit_kind = cstc::ast::LitKind::Int; break;
    case cstc::lexer::TokenKind::LitFloat: lit_kind = cstc::ast::LitKind::Float; break;
    case cstc::lexer::TokenKind::LitStr:
        lit_kind = is_char_literal_text(token.text) ? cstc::ast::LitKind::Char
                                                    : cstc::ast::LitKind::String;
        break;
    case cstc::lexer::TokenKind::Ident: lit_kind = cstc::ast::LitKind::Bool; break;
    default: return fail<cstc::ast::Lit>("expected literal");
    }

    return cstc::ast::Lit{
        .span = token.span,
        .kind = lit_kind,
        .value = token.text,
    };
}

ParseResult<std::optional<cstc::ast::KeywordModifier>> Parser::parse_keyword_modifier() {
    if (check_ident("runtime")) {
        const auto keyword = advance();
        const auto kind = cstc::ast::KeywordKind::Runtime;

        std::optional<cstc::ast::Symbol> type_var;
        auto end_span = keyword.span;

        if (match(cstc::lexer::TokenKind::Lt)) {
            auto type_var_result = parse_identifier("keyword type variable");
            if (!type_var_result)
                return std::unexpected(std::move(type_var_result.error()));

            auto close_result =
                expect(cstc::lexer::TokenKind::Gt, "`>` after keyword type variable");
            if (!close_result)
                return std::unexpected(std::move(close_result.error()));

            type_var = type_var_result->symbol;
            end_span = close_result->span;
        }

        return cstc::ast::KeywordModifier{
            .span = merge_spans(keyword.span, end_span),
            .kind = kind,
            .type_var = type_var,
        };
    }

    if (check_ident("const")) {
        const auto keyword = advance();
        const auto kind = cstc::ast::KeywordKind::NotRuntime;

        return cstc::ast::KeywordModifier{
            .span = keyword.span,
            .kind = kind,
            .type_var = std::nullopt,
        };
    }

    if (check(cstc::lexer::TokenKind::Bang) && check_ident("runtime", 1)) {
        const auto bang = advance();
        const auto keyword = advance();
        const auto kind = cstc::ast::KeywordKind::NotRuntime;

        return cstc::ast::KeywordModifier{
            .span = merge_spans(bang.span, keyword.span),
            .kind = kind,
            .type_var = std::nullopt,
        };
    }

    return std::optional<cstc::ast::KeywordModifier>{};
}

ParseResult<std::vector<cstc::ast::KeywordModifier>> Parser::parse_keyword_modifiers() {
    std::vector<cstc::ast::KeywordModifier> keywords;
    while (true) {
        auto keyword_result = parse_keyword_modifier();
        if (!keyword_result)
            return std::unexpected(std::move(keyword_result.error()));
        if (!keyword_result->has_value())
            break;
        keywords.push_back(std::move(**keyword_result));
    }
    return keywords;
}

ParseResult<std::optional<cstc::ast::GenericParams>> Parser::parse_optional_generic_params() {
    if (!match(cstc::lexer::TokenKind::Lt))
        return std::optional<cstc::ast::GenericParams>{};

    const auto open = previous();
    std::vector<cstc::ast::GenericParam> params;

    if (!check(cstc::lexer::TokenKind::Gt)) {
        while (true) {
            auto param_name_result = parse_identifier("generic parameter");
            if (!param_name_result)
                return std::unexpected(std::move(param_name_result.error()));

            params.push_back(
                cstc::ast::GenericParam{
                    .id = node_ids_.next(),
                    .span = param_name_result->span,
                    .name = param_name_result->symbol,
                });

            if (!match(cstc::lexer::TokenKind::Comma))
                break;
            if (check(cstc::lexer::TokenKind::Gt))
                break;
        }
    }

    auto close_result = expect(cstc::lexer::TokenKind::Gt, "`>` to close generic parameter list");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    return cstc::ast::GenericParams{
        .span = merge_spans(open.span, close_result->span),
        .params = std::move(params),
    };
}

ParseResult<std::optional<cstc::ast::WhereClause>> Parser::parse_optional_where_clause() {
    if (!match_ident("where"))
        return std::optional<cstc::ast::WhereClause>{};

    const auto where_token = previous();
    if (check(cstc::lexer::TokenKind::OpenBrace) || check(cstc::lexer::TokenKind::Semi)
        || check(cstc::lexer::TokenKind::CloseBrace)) {
        return fail<std::optional<cstc::ast::WhereClause>>("expected where-clause predicate");
    }

    std::vector<cstc::ast::WhereExpr> predicates;
    while (true) {
        auto predicate_expr_result = parse_expr();
        if (!predicate_expr_result)
            return std::unexpected(std::move(predicate_expr_result.error()));

        auto predicate_expr = std::move(*predicate_expr_result);
        predicates.push_back(
            cstc::ast::WhereExpr{
                .span = predicate_expr->span,
                .expr = std::move(predicate_expr),
            });

        if (!match(cstc::lexer::TokenKind::Comma))
            break;
        if (check(cstc::lexer::TokenKind::OpenBrace) || check(cstc::lexer::TokenKind::Semi)
            || check(cstc::lexer::TokenKind::CloseBrace) || at_end()) {
            break;
        }
    }

    if (predicates.empty()) {
        return fail<std::optional<cstc::ast::WhereClause>>("expected where-clause predicate");
    }

    return cstc::ast::WhereClause{
        .span = merge_spans(where_token.span, predicates.back().span),
        .predicates = std::move(predicates),
    };
}

ParseResult<cstc::ast::GenericArgs>
    Parser::parse_generic_args_after_open_angle(std::size_t open_start) {
    std::vector<std::unique_ptr<cstc::ast::TypeNode>> args;
    if (!check(cstc::lexer::TokenKind::Gt)) {
        while (true) {
            auto arg_result = parse_type();
            if (!arg_result)
                return std::unexpected(std::move(arg_result.error()));
            args.push_back(std::move(*arg_result));

            if (!match(cstc::lexer::TokenKind::Comma))
                break;
            if (check(cstc::lexer::TokenKind::Gt))
                break;
        }
    }

    auto close_result = expect(cstc::lexer::TokenKind::Gt, "`>` to close generic arguments");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    return cstc::ast::GenericArgs{
        .span =
            {
                   .start = open_start,
                   .end = close_result->span.end,
                   },
        .args = std::move(args),
    };
}

ParseResult<cstc::ast::GenericArgs> Parser::parse_turbofish_args_after_prefix() {
    if (!match_turbofish_start())
        return fail<cstc::ast::GenericArgs>("expected turbofish prefix `::<`");
    return parse_generic_args_after_open_angle(previous().span.start);
}

ParseResult<std::unique_ptr<cstc::ast::TypeNode>> Parser::parse_type() {
    auto keywords_result = parse_keyword_modifiers();
    if (!keywords_result)
        return std::unexpected(std::move(keywords_result.error()));

    if (!keywords_result->empty()) {
        auto inner_result = parse_type();
        if (!inner_result)
            return std::unexpected(std::move(inner_result.error()));

        return make_type(
            merge_spans(keywords_result->front().span, (*inner_result)->span),
            cstc::ast::KeywordType{
                .keywords = std::move(*keywords_result),
                .inner = std::move(*inner_result),
            });
    }

    return parse_type_atom();
}

ParseResult<std::unique_ptr<cstc::ast::TypeNode>> Parser::parse_type_atom() {
    if (match(cstc::lexer::TokenKind::Amp)) {
        const auto amp = previous();
        auto inner_result = parse_type();
        if (!inner_result)
            return std::unexpected(std::move(inner_result.error()));

        return make_type(
            merge_spans(amp.span, (*inner_result)->span), cstc::ast::RefType{
                                                              .inner = std::move(*inner_result),
                                                          });
    }

    if (match_ident("fn")) {
        const auto fn_token = previous();
        auto open_result = expect(cstc::lexer::TokenKind::OpenParen, "`(` after `fn` in type");
        if (!open_result)
            return std::unexpected(std::move(open_result.error()));

        std::vector<std::unique_ptr<cstc::ast::TypeNode>> params;
        if (!check(cstc::lexer::TokenKind::CloseParen)) {
            while (true) {
                auto param_result = parse_type();
                if (!param_result)
                    return std::unexpected(std::move(param_result.error()));
                params.push_back(std::move(*param_result));

                if (!match(cstc::lexer::TokenKind::Comma))
                    break;
                if (check(cstc::lexer::TokenKind::CloseParen))
                    break;
            }
        }

        auto close_result =
            expect(cstc::lexer::TokenKind::CloseParen, "`)` after function pointer type params");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));

        if (!match_arrow())
            return fail<std::unique_ptr<cstc::ast::TypeNode>>(
                "expected `->` in function pointer type");

        auto ret_result = parse_type();
        if (!ret_result)
            return std::unexpected(std::move(ret_result.error()));

        return make_type(
            merge_spans(fn_token.span, (*ret_result)->span), cstc::ast::FnPointerType{
                                                                 .params = std::move(params),
                                                                 .ret = std::move(*ret_result),
                                                             });
    }

    if (match(cstc::lexer::TokenKind::OpenParen)) {
        return parse_parenthesized_type(previous().span);
    }

    if (check(cstc::lexer::TokenKind::Ident) && peek().text == "_") {
        const auto infer = advance();
        return make_type(infer.span, cstc::ast::InferredType{});
    }

    auto path_result = parse_path();
    if (!path_result)
        return std::unexpected(std::move(path_result.error()));

    std::optional<cstc::ast::GenericArgs> generic_args;
    if (match(cstc::lexer::TokenKind::Lt)) {
        auto args_result = parse_generic_args_after_open_angle(previous().span.start);
        if (!args_result)
            return std::unexpected(std::move(args_result.error()));
        generic_args = std::move(*args_result);
    }

    const auto type_span = generic_args.has_value()
                             ? merge_spans(path_result->span, generic_args->span)
                             : path_result->span;

    return make_type(
        type_span, cstc::ast::PathType{
                       .path = std::move(*path_result),
                       .args = std::move(generic_args),
                   });
}

ParseResult<std::unique_ptr<cstc::ast::TypeNode>>
    Parser::parse_parenthesized_type(cstc::span::SourceSpan open_span) {
    if (match(cstc::lexer::TokenKind::CloseParen)) {
        return fail<std::unique_ptr<cstc::ast::TypeNode>>("tuple types are not supported");
    }

    auto first_result = parse_type();
    if (!first_result)
        return std::unexpected(std::move(first_result.error()));

    if (!match(cstc::lexer::TokenKind::Comma)) {
        auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after grouped type");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));
        (*first_result)->span = merge_spans(open_span, close_result->span);
        return std::move(*first_result);
    }

    return fail<std::unique_ptr<cstc::ast::TypeNode>>("tuple types are not supported");
}

ParseResult<std::optional<cstc::ast::SelfParam>> Parser::parse_optional_self_param() {
    const auto saved_cursor = cursor_;

    if (match(cstc::lexer::TokenKind::Amp)) {
        const auto amp_span = previous().span;
        if (!match_ident("self")) {
            cursor_ = saved_cursor;
            return std::optional<cstc::ast::SelfParam>{};
        }

        return cstc::ast::SelfParam{
            .span = merge_spans(amp_span, previous().span),
            .is_ref = true,
            .explicit_ty = std::nullopt,
        };
    }

    if (check_ident("self") && check(cstc::lexer::TokenKind::Colon, 1)) {
        const auto self_token = advance();
        std::ignore = advance(); // ':'

        auto explicit_type_result = parse_type();
        if (!explicit_type_result)
            return std::unexpected(std::move(explicit_type_result.error()));

        auto explicit_type = std::move(*explicit_type_result);
        std::optional<std::unique_ptr<cstc::ast::TypeNode>> explicit_ty;
        explicit_ty = std::move(explicit_type);

        return cstc::ast::SelfParam{
            .span = merge_spans(self_token.span, explicit_ty->get()->span),
            .is_ref = false,
            .explicit_ty = std::move(explicit_ty),
        };
    }

    cursor_ = saved_cursor;
    return std::optional<cstc::ast::SelfParam>{};
}

ParseResult<cstc::ast::FnParam> Parser::parse_fn_param() {
    auto name_result = parse_identifier("function parameter name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    auto colon_result = expect(cstc::lexer::TokenKind::Colon, "`:` after parameter name");
    if (!colon_result)
        return std::unexpected(std::move(colon_result.error()));

    auto type_result = parse_type();
    if (!type_result)
        return std::unexpected(std::move(type_result.error()));

    return cstc::ast::FnParam{
        .span = merge_spans(name_result->span, (*type_result)->span),
        .name = name_result->symbol,
        .ty = std::move(*type_result),
    };
}

ParseResult<cstc::ast::FnSig> Parser::parse_fn_sig() {
    auto open_result = expect(cstc::lexer::TokenKind::OpenParen, "`(` to start parameter list");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::FnParam> params;

    if (!check(cstc::lexer::TokenKind::CloseParen)) {
        if ((check(cstc::lexer::TokenKind::Amp) && check_ident("self", 1))
            || check_ident("self")) {
            return fail<cstc::ast::FnSig>("`self` parameters are not supported");
        }

        auto first_param_result = parse_fn_param();
        if (!first_param_result)
            return std::unexpected(std::move(first_param_result.error()));
        params.push_back(std::move(*first_param_result));

        while (match(cstc::lexer::TokenKind::Comma)) {
            if (check(cstc::lexer::TokenKind::CloseParen))
                break;

            auto param_result = parse_fn_param();
            if (!param_result)
                return std::unexpected(std::move(param_result.error()));
            params.push_back(std::move(*param_result));
        }
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after parameter list");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    if (!match_arrow())
        return fail<cstc::ast::FnSig>("expected `->` after parameter list");

    auto ret_type_result = parse_type();
    if (!ret_type_result)
        return std::unexpected(std::move(ret_type_result.error()));

    return cstc::ast::FnSig{
        .self_param = std::nullopt,
        .params = std::move(params),
        .ret_ty = std::move(*ret_type_result),
    };
}

ParseResult<cstc::ast::StructField> Parser::parse_struct_field() {
    auto name_result = parse_identifier("field name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    auto colon_result = expect(cstc::lexer::TokenKind::Colon, "`:` after field name");
    if (!colon_result)
        return std::unexpected(std::move(colon_result.error()));

    auto type_result = parse_type();
    if (!type_result)
        return std::unexpected(std::move(type_result.error()));

    return cstc::ast::StructField{
        .span = merge_spans(name_result->span, (*type_result)->span),
        .name = name_result->symbol,
        .ty = std::move(*type_result),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_item() {
    const auto start_span = peek().span;

    if (match_ident("import"))
        return parse_import_item(start_span);

    const bool is_exported = match_ident("export");

    auto keywords_result = parse_keyword_modifiers();
    if (!keywords_result)
        return std::unexpected(std::move(keywords_result.error()));
    auto keywords = std::move(*keywords_result);

    if (match_ident("fn")) {
        return parse_fn_item(std::move(keywords), start_span, is_exported);
    }

    if (is_exported)
        return fail<cstc::ast::Item>("expected `fn` after `export`");

    if (match_ident("extern")) {
        if (!match_ident("fn"))
            return fail<cstc::ast::Item>("expected `fn` after `extern`");
        return parse_extern_fn_item(std::move(keywords), start_span);
    }

    if (!keywords.empty()) {
        return fail<cstc::ast::Item>(
            "keyword modifiers are only valid before `fn` / `extern fn` items");
    }

    if (match_ident("struct"))
        return parse_struct_item(start_span);
    if (match_ident("enum"))
        return parse_enum_item(start_span);
    if (check_ident("type"))
        return fail<cstc::ast::Item>("type aliases are not supported");
    if (check_ident("concept"))
        return fail<cstc::ast::Item>("concept declarations are not supported");
    if (check_ident("with"))
        return fail<cstc::ast::Item>("with-block methods are not supported");

    return fail<cstc::ast::Item>("expected item declaration");
}

ParseResult<cstc::ast::Item> Parser::parse_import_item(cstc::span::SourceSpan start_span) {
    std::vector<cstc::ast::ImportSpecifier> specifiers;
    if (!match(cstc::lexer::TokenKind::OpenBrace)) {
        return fail<cstc::ast::Item>(
            "expected `{` after `import`; side-effect-only imports are not supported");
    }

    if (check(cstc::lexer::TokenKind::CloseBrace)) {
        return fail<cstc::ast::Item>("expected at least one import binding");
    }

    while (true) {
        auto imported_name_result = parse_identifier("import binding");
        if (!imported_name_result)
            return std::unexpected(std::move(imported_name_result.error()));

        auto specifier_end = imported_name_result->span;
        std::optional<cstc::ast::Symbol> local_name;
        if (match_ident("as")) {
            auto local_name_result = parse_identifier("local import binding");
            if (!local_name_result)
                return std::unexpected(std::move(local_name_result.error()));

            local_name = local_name_result->symbol;
            specifier_end = local_name_result->span;
        }

        specifiers.push_back(cstc::ast::ImportSpecifier{
            .span = merge_spans(imported_name_result->span, specifier_end),
            .imported_name = imported_name_result->symbol,
            .local_name = local_name,
        });

        if (!match(cstc::lexer::TokenKind::Comma))
            break;
        if (check(cstc::lexer::TokenKind::CloseBrace))
            break;
    }

    auto close_brace_result =
        expect(cstc::lexer::TokenKind::CloseBrace, "`}` after import specifiers");
    if (!close_brace_result)
        return std::unexpected(std::move(close_brace_result.error()));

    if (!match_ident("from"))
        return fail<cstc::ast::Item>("expected `from` after import specifiers");

    if (!check(cstc::lexer::TokenKind::LitStr))
        return fail<cstc::ast::Item>("expected module path string literal after `from`");

    const auto source_token = advance();
    auto semi_result = expect(cstc::lexer::TokenKind::Semi, "`;` after import declaration");
    if (!semi_result)
        return std::unexpected(std::move(semi_result.error()));

    cstc::ast::ImportItem import_item{
        .specifiers = std::move(specifiers),
        .source = source_token.text,
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, semi_result->span),
        .kind = std::move(import_item),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_fn_item(
    std::vector<cstc::ast::KeywordModifier> keywords, cstc::span::SourceSpan start_span,
    bool is_exported) {
    cstc::ast::Generics generics{};
    std::optional<cstc::ast::GenericParams> leading_generic_params;
    if (check(cstc::lexer::TokenKind::Lt)) {
        auto leading_generic_params_result = parse_optional_generic_params();
        if (!leading_generic_params_result)
            return std::unexpected(std::move(leading_generic_params_result.error()));
        leading_generic_params = std::move(*leading_generic_params_result);
    }

    auto name_result = parse_identifier("function name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    auto trailing_generic_params_result = parse_optional_generic_params();
    if (!trailing_generic_params_result)
        return std::unexpected(std::move(trailing_generic_params_result.error()));

    if (leading_generic_params.has_value() && trailing_generic_params_result->has_value()) {
        return fail<cstc::ast::Item>(
            "generic parameters may appear either before or after the function name, not both");
    }

    generics.params = leading_generic_params.has_value() ? std::move(leading_generic_params)
                                                         : std::move(*trailing_generic_params_result);

    auto sig_result = parse_fn_sig();
    if (!sig_result)
        return std::unexpected(std::move(sig_result.error()));

    auto where_clause_result = parse_optional_where_clause();
    if (!where_clause_result)
        return std::unexpected(std::move(where_clause_result.error()));
    generics.where_clause = std::move(*where_clause_result);

    push_local_scope();
    for (const auto& param : sig_result->params)
        bind_local_symbol(param.name);

    auto body_result = parse_block();
    if (!body_result) {
        pop_local_scope();
        return std::unexpected(std::move(body_result.error()));
    }

    pop_local_scope();

    cstc::ast::FnItem fn_item{
        .keywords = std::move(keywords),
        .name = name_result->symbol,
        .generics = std::move(generics),
        .sig = std::move(*sig_result),
        .body = std::move(*body_result),
        .is_exported = is_exported,
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, fn_item.body.span),
        .kind = std::move(fn_item),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_extern_fn_item(
    std::vector<cstc::ast::KeywordModifier> keywords, cstc::span::SourceSpan start_span) {
    auto name_result = parse_identifier("extern function name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    auto sig_result = parse_fn_sig();
    if (!sig_result)
        return std::unexpected(std::move(sig_result.error()));

    auto semi_result =
        expect(cstc::lexer::TokenKind::Semi, "`;` after extern function declaration");
    if (!semi_result)
        return std::unexpected(std::move(semi_result.error()));

    cstc::ast::ExternFnItem extern_fn_item{
        .keywords = std::move(keywords),
        .name = name_result->symbol,
        .sig = std::move(*sig_result),
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, semi_result->span),
        .kind = std::move(extern_fn_item),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_struct_item(cstc::span::SourceSpan start_span) {
    auto name_result = parse_identifier("struct name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    cstc::ast::Generics generics{};
    auto generic_params_result = parse_optional_generic_params();
    if (!generic_params_result)
        return std::unexpected(std::move(generic_params_result.error()));
    generics.params = std::move(*generic_params_result);

    auto where_clause_result = parse_optional_where_clause();
    if (!where_clause_result)
        return std::unexpected(std::move(where_clause_result.error()));
    generics.where_clause = std::move(*where_clause_result);

    if (match(cstc::lexer::TokenKind::Semi)) {
        cstc::ast::MarkerStructItem marker_item{
            .name = name_result->symbol,
            .generics = std::move(generics),
        };
        return cstc::ast::Item{
            .id = node_ids_.next(),
            .span = merge_spans(start_span, previous().span),
            .kind = std::move(marker_item),
        };
    }

    if (match(cstc::lexer::TokenKind::OpenBrace)) {
        std::vector<cstc::ast::StructField> fields;
        if (!check(cstc::lexer::TokenKind::CloseBrace)) {
            while (true) {
                auto field_result = parse_struct_field();
                if (!field_result)
                    return std::unexpected(std::move(field_result.error()));
                fields.push_back(std::move(*field_result));

                if (!match(cstc::lexer::TokenKind::Comma))
                    break;
                if (check(cstc::lexer::TokenKind::CloseBrace))
                    break;
            }
        }

        auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after struct fields");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));

        cstc::ast::NamedStructItem named_item{
            .name = name_result->symbol,
            .generics = std::move(generics),
            .fields = std::move(fields),
        };

        return cstc::ast::Item{
            .id = node_ids_.next(),
            .span = merge_spans(start_span, close_result->span),
            .kind = std::move(named_item),
        };
    }

    if (check(cstc::lexer::TokenKind::OpenParen)) {
        return fail<cstc::ast::Item>("tuple structs are not supported");
    }

    return fail<cstc::ast::Item>("expected `;` or `{` after struct declaration header");
}

ParseResult<cstc::ast::Item> Parser::parse_enum_item(cstc::span::SourceSpan start_span) {
    auto name_result = parse_identifier("enum name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    if (check(cstc::lexer::TokenKind::Lt))
        return fail<cstc::ast::Item>("enum generics are not supported");
    if (check_ident("where"))
        return fail<cstc::ast::Item>("enum `where` clauses are not supported");

    auto open_result = expect(cstc::lexer::TokenKind::OpenBrace, "`{` to start enum body");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::EnumVariant> variants;
    while (!check(cstc::lexer::TokenKind::CloseBrace)) {
        if (match(cstc::lexer::TokenKind::Pipe))
            return fail<cstc::ast::Item>("`|` enum variant prefix is not supported");

        auto variant_name_result = parse_identifier("enum variant name");
        if (!variant_name_result)
            return std::unexpected(std::move(variant_name_result.error()));

        auto variant_span = variant_name_result->span;
        if (check(cstc::lexer::TokenKind::OpenBrace) || check(cstc::lexer::TokenKind::OpenParen)) {
            return fail<cstc::ast::Item>("enum variant payloads are not supported");
        }

        variants.push_back(
            cstc::ast::EnumVariant{
                .span = variant_span,
                .name = variant_name_result->symbol,
                .kind = cstc::ast::UnitVariant{},
            });

        if (!match(cstc::lexer::TokenKind::Comma)) {
            if (!check(cstc::lexer::TokenKind::CloseBrace))
                return fail<cstc::ast::Item>("expected `,` or `}` after enum variant");
            break;
        }

        if (check(cstc::lexer::TokenKind::CloseBrace))
            break;
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after enum body");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    cstc::ast::EnumItem enum_item{
        .name = name_result->symbol,
        .generics = cstc::ast::Generics{},
        .variants = std::move(variants),
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, close_result->span),
        .kind = std::move(enum_item),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_type_alias_item(cstc::span::SourceSpan start_span) {
    auto name_result = parse_identifier("type alias name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    cstc::ast::Generics generics{};
    auto generic_params_result = parse_optional_generic_params();
    if (!generic_params_result)
        return std::unexpected(std::move(generic_params_result.error()));
    generics.params = std::move(*generic_params_result);

    if (!match_single_eq())
        return fail<cstc::ast::Item>("expected `=` in type alias declaration");

    auto aliased_type_result = parse_type();
    if (!aliased_type_result)
        return std::unexpected(std::move(aliased_type_result.error()));

    auto where_clause_result = parse_optional_where_clause();
    if (!where_clause_result)
        return std::unexpected(std::move(where_clause_result.error()));
    generics.where_clause = std::move(*where_clause_result);

    auto semi_result = expect(cstc::lexer::TokenKind::Semi, "`;` after type alias declaration");
    if (!semi_result)
        return std::unexpected(std::move(semi_result.error()));

    cstc::ast::TypeAliasItem alias_item{
        .name = name_result->symbol,
        .generics = std::move(generics),
        .ty = std::move(*aliased_type_result),
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, semi_result->span),
        .kind = std::move(alias_item),
    };
}

ParseResult<cstc::ast::ConceptMethod> Parser::parse_concept_method() {
    auto keywords_result = parse_keyword_modifiers();
    if (!keywords_result)
        return std::unexpected(std::move(keywords_result.error()));

    if (!match_ident("fn"))
        return fail<cstc::ast::ConceptMethod>("expected `fn` in concept body");

    cstc::ast::Generics generics{};
    std::optional<cstc::ast::GenericParams> leading_generic_params;
    if (check(cstc::lexer::TokenKind::Lt)) {
        auto leading_generic_params_result = parse_optional_generic_params();
        if (!leading_generic_params_result)
            return std::unexpected(std::move(leading_generic_params_result.error()));
        leading_generic_params = std::move(*leading_generic_params_result);
    }

    auto name_result = parse_identifier("concept method name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    auto trailing_generic_params_result = parse_optional_generic_params();
    if (!trailing_generic_params_result)
        return std::unexpected(std::move(trailing_generic_params_result.error()));

    if (leading_generic_params.has_value() && trailing_generic_params_result->has_value()) {
        return fail<cstc::ast::ConceptMethod>(
            "generic parameters may appear either before or after the method name, not both");
    }

    generics.params = leading_generic_params.has_value() ? std::move(leading_generic_params)
                                                         : std::move(*trailing_generic_params_result);

    auto sig_result = parse_fn_sig();
    if (!sig_result)
        return std::unexpected(std::move(sig_result.error()));

    auto where_clause_result = parse_optional_where_clause();
    if (!where_clause_result)
        return std::unexpected(std::move(where_clause_result.error()));
    generics.where_clause = std::move(*where_clause_result);

    auto semi_result = expect(cstc::lexer::TokenKind::Semi, "`;` after concept method signature");
    if (!semi_result)
        return std::unexpected(std::move(semi_result.error()));

    return cstc::ast::ConceptMethod{
        .keywords = std::move(*keywords_result),
        .name = name_result->symbol,
        .generics = std::move(generics),
        .sig = std::move(*sig_result),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_concept_item(cstc::span::SourceSpan start_span) {
    auto name_result = parse_identifier("concept name");
    if (!name_result)
        return std::unexpected(std::move(name_result.error()));

    cstc::ast::Generics generics{};
    auto generic_params_result = parse_optional_generic_params();
    if (!generic_params_result)
        return std::unexpected(std::move(generic_params_result.error()));
    generics.params = std::move(*generic_params_result);

    auto where_clause_result = parse_optional_where_clause();
    if (!where_clause_result)
        return std::unexpected(std::move(where_clause_result.error()));
    generics.where_clause = std::move(*where_clause_result);

    auto open_result = expect(cstc::lexer::TokenKind::OpenBrace, "`{` to start concept body");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::ConceptMethod> methods;
    while (!check(cstc::lexer::TokenKind::CloseBrace)) {
        if (at_end())
            return fail<cstc::ast::Item>("unexpected end of input in concept body");

        auto method_result = parse_concept_method();
        if (!method_result)
            return std::unexpected(std::move(method_result.error()));

        methods.push_back(std::move(*method_result));
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after concept body");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    cstc::ast::ConceptItem concept_item{
        .name = name_result->symbol,
        .generics = std::move(generics),
        .methods = std::move(methods),
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, close_result->span),
        .kind = std::move(concept_item),
    };
}

ParseResult<cstc::ast::Item> Parser::parse_with_item(cstc::span::SourceSpan start_span) {
    auto generic_params_result = parse_optional_generic_params();
    if (!generic_params_result)
        return std::unexpected(std::move(generic_params_result.error()));
    auto generic_params = std::move(*generic_params_result);

    auto target_type_result = parse_type();
    if (!target_type_result)
        return std::unexpected(std::move(target_type_result.error()));

    auto where_clause_result = parse_optional_where_clause();
    if (!where_clause_result)
        return std::unexpected(std::move(where_clause_result.error()));
    auto where_clause = std::move(*where_clause_result);

    auto open_result = expect(cstc::lexer::TokenKind::OpenBrace, "`{` to start with-body");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::FnItem> methods;
    while (!check(cstc::lexer::TokenKind::CloseBrace)) {
        if (at_end())
            return fail<cstc::ast::Item>("unexpected end of input in with-body");

        const auto method_start = peek().span;
        auto keywords_result = parse_keyword_modifiers();
        if (!keywords_result)
            return std::unexpected(std::move(keywords_result.error()));

        if (!match_ident("fn"))
            return fail<cstc::ast::Item>("expected `fn` item in with-body");

        auto method_item_result = parse_fn_item(std::move(*keywords_result), method_start, false);
        if (!method_item_result)
            return std::unexpected(std::move(method_item_result.error()));

        auto method_item = std::move(*method_item_result);
        if (!std::holds_alternative<cstc::ast::FnItem>(method_item.kind)) {
            return fail<cstc::ast::Item>("internal parser error: with-body produced non-fn item");
        }

        methods.push_back(std::get<cstc::ast::FnItem>(std::move(method_item.kind)));
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after with-body");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    cstc::ast::WithItem with_item{
        .generic_params = std::move(generic_params),
        .target_ty = std::move(*target_type_result),
        .where_clause = std::move(where_clause),
        .methods = std::move(methods),
    };

    return cstc::ast::Item{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, close_result->span),
        .kind = std::move(with_item),
    };
}

ParseResult<cstc::ast::Block> Parser::parse_block() {
    auto open_result = expect(cstc::lexer::TokenKind::OpenBrace, "`{` to start block");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    push_local_scope();

    std::vector<cstc::ast::Stmt> stmts;
    while (!check(cstc::lexer::TokenKind::CloseBrace)) {
        if (at_end()) {
            pop_local_scope();
            return fail<cstc::ast::Block>("unexpected end of input in block");
        }

        auto stmt_result = parse_stmt();
        if (!stmt_result) {
            pop_local_scope();
            return std::unexpected(std::move(stmt_result.error()));
        }
        stmts.push_back(std::move(*stmt_result));
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` to close block");
    if (!close_result) {
        pop_local_scope();
        return std::unexpected(std::move(close_result.error()));
    }

    pop_local_scope();

    return cstc::ast::Block{
        .id = node_ids_.next(),
        .span = merge_spans(open_result->span, close_result->span),
        .stmts = std::move(stmts),
    };
}

ParseResult<cstc::ast::Stmt> Parser::parse_let_stmt(cstc::span::SourceSpan start_span) {
    std::unique_ptr<cstc::ast::Pat> binding_pattern;
    if (check(cstc::lexer::TokenKind::Ident) && peek().text == "_") {
        const auto wildcard = advance();
        binding_pattern = make_pat(wildcard.span, cstc::ast::WildcardPat{});
    } else {
        auto binding_result = parse_identifier("let binding name");
        if (!binding_result)
            return std::unexpected(std::move(binding_result.error()));

        binding_pattern = make_pat(
            binding_result->span, cstc::ast::BindingPat{
                                     .name = binding_result->symbol,
                                 });
    }

    std::optional<std::unique_ptr<cstc::ast::TypeNode>> stmt_type;
    if (match(cstc::lexer::TokenKind::Colon)) {
        auto type_result = parse_type();
        if (!type_result)
            return std::unexpected(std::move(type_result.error()));
        stmt_type = std::move(*type_result);
    }

    std::optional<std::unique_ptr<cstc::ast::Expr>> init_expr;
    if (match_single_eq()) {
        auto init_result = parse_expr();
        if (!init_result)
            return std::unexpected(std::move(init_result.error()));
        init_expr = std::move(*init_result);
    }

    auto semi_result = expect(cstc::lexer::TokenKind::Semi, "`;` after let statement");
    if (!semi_result)
        return std::unexpected(std::move(semi_result.error()));

    if (const auto* binding = std::get_if<cstc::ast::BindingPat>(&binding_pattern->kind))
        bind_local_symbol(binding->name);

    return cstc::ast::Stmt{
        .id = node_ids_.next(),
        .span = merge_spans(start_span, semi_result->span),
        .kind =
            cstc::ast::LetStmt{
                               .pat = std::move(binding_pattern),
                               .ty = std::move(stmt_type),
                               .init = std::move(init_expr),
                               },
    };
}

ParseResult<cstc::ast::Stmt> Parser::parse_stmt() {
    if (match_ident("let")) {
        return parse_let_stmt(previous().span);
    }

    if (is_item_start()) {
        auto item_result = parse_item();
        if (!item_result)
            return std::unexpected(std::move(item_result.error()));

        auto item_value = std::move(*item_result);
        const auto item_span = item_value.span;

        return cstc::ast::Stmt{
            .id = node_ids_.next(),
            .span = item_span,
            .kind =
                cstc::ast::ItemStmt{
                                    .item = std::make_unique<cstc::ast::Item>(std::move(item_value)),
                                    },
        };
    }

    auto expr_result = parse_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    auto expr = std::move(*expr_result);
    const auto expr_span = expr->span;
    const bool has_semi = match(cstc::lexer::TokenKind::Semi);

    if (!has_semi && !check(cstc::lexer::TokenKind::CloseBrace)) {
        return fail<cstc::ast::Stmt>("expected `;` after expression statement");
    }

    return cstc::ast::Stmt{
        .id = node_ids_.next(),
        .span = has_semi ? merge_spans(expr_span, previous().span) : expr_span,
        .kind =
            cstc::ast::ExprStmt{
                                .expr = std::move(expr),
                                .has_semi = has_semi,
                                },
    };
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_expr() {
    auto expr_result = parse_logical_or_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    if (check(cstc::lexer::TokenKind::Eq)) {
        return fail<std::unique_ptr<cstc::ast::Expr>>(
            "assignment expressions are not supported; bindings are immutable");
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_logical_or_expr() {
    auto expr_result = parse_logical_and_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (match_or_or()) {
        auto rhs_result = parse_logical_and_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(
            cstc::ast::BinaryOp::Or, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_logical_and_expr() {
    auto expr_result = parse_comparison_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (match_and_and()) {
        auto rhs_result = parse_comparison_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(
            cstc::ast::BinaryOp::And, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_comparison_expr() {
    auto expr_result = parse_bitwise_or_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (true) {
        std::optional<cstc::ast::BinaryOp> op;
        if (match_eq_eq()) {
            op = cstc::ast::BinaryOp::Eq;
        } else if (match_not_eq()) {
            op = cstc::ast::BinaryOp::Ne;
        } else if (match_lt_eq()) {
            op = cstc::ast::BinaryOp::Le;
        } else if (match_gt_eq()) {
            op = cstc::ast::BinaryOp::Ge;
        } else if (match_single_lt()) {
            op = cstc::ast::BinaryOp::Lt;
        } else if (match_single_gt()) {
            op = cstc::ast::BinaryOp::Gt;
        }

        if (!op.has_value())
            break;

        auto rhs_result = parse_bitwise_or_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));

        expr_result = make_binary_expr(*op, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_bitwise_or_expr() {
    auto expr_result = parse_bitwise_xor_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (match_single_pipe()) {
        auto rhs_result = parse_bitwise_xor_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(
            cstc::ast::BinaryOp::BitOr, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_bitwise_xor_expr() {
    auto expr_result = parse_bitwise_and_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (match(cstc::lexer::TokenKind::Caret)) {
        auto rhs_result = parse_bitwise_and_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(
            cstc::ast::BinaryOp::BitXor, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_bitwise_and_expr() {
    auto expr_result = parse_shift_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (match_single_amp()) {
        auto rhs_result = parse_shift_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(
            cstc::ast::BinaryOp::BitAnd, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_shift_expr() {
    auto expr_result = parse_additive_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (true) {
        std::optional<cstc::ast::BinaryOp> op;
        if (match_shl()) {
            op = cstc::ast::BinaryOp::Shl;
        } else if (match_shr()) {
            op = cstc::ast::BinaryOp::Shr;
        }

        if (!op.has_value())
            break;

        auto rhs_result = parse_additive_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(*op, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_additive_expr() {
    auto expr_result = parse_multiplicative_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (true) {
        std::optional<cstc::ast::BinaryOp> op;
        if (match(cstc::lexer::TokenKind::Plus)) {
            op = cstc::ast::BinaryOp::Add;
        } else if (match(cstc::lexer::TokenKind::Minus)) {
            op = cstc::ast::BinaryOp::Sub;
        }

        if (!op.has_value())
            break;

        auto rhs_result = parse_multiplicative_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(*op, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_multiplicative_expr() {
    auto expr_result = parse_unary_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    while (true) {
        std::optional<cstc::ast::BinaryOp> op;
        if (match(cstc::lexer::TokenKind::Star)) {
            op = cstc::ast::BinaryOp::Mul;
        } else if (match(cstc::lexer::TokenKind::Slash)) {
            op = cstc::ast::BinaryOp::Div;
        } else if (match(cstc::lexer::TokenKind::Percent)) {
            op = cstc::ast::BinaryOp::Mod;
        }

        if (!op.has_value())
            break;

        auto rhs_result = parse_unary_expr();
        if (!rhs_result)
            return std::unexpected(std::move(rhs_result.error()));
        expr_result = make_binary_expr(*op, std::move(*expr_result), std::move(*rhs_result));
    }

    return std::move(*expr_result);
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_unary_expr() {
    if (looks_like_keyword_block_expr()) {
        return parse_postfix_expr();
    }

    if (match(cstc::lexer::TokenKind::Minus)) {
        const auto op_span = previous().span;
        auto operand_result = parse_unary_expr();
        if (!operand_result)
            return std::unexpected(std::move(operand_result.error()));
        return make_unary_expr(cstc::ast::UnaryOp::Neg, op_span, std::move(*operand_result));
    }

    if (match(cstc::lexer::TokenKind::Bang)) {
        const auto op_span = previous().span;
        auto operand_result = parse_unary_expr();
        if (!operand_result)
            return std::unexpected(std::move(operand_result.error()));
        return make_unary_expr(cstc::ast::UnaryOp::Not, op_span, std::move(*operand_result));
    }

    if (match(cstc::lexer::TokenKind::Amp)) {
        const auto op_span = previous().span;
        auto operand_result = parse_unary_expr();
        if (!operand_result)
            return std::unexpected(std::move(operand_result.error()));
        return make_unary_expr(cstc::ast::UnaryOp::Borrow, op_span, std::move(*operand_result));
    }

    if (match(cstc::lexer::TokenKind::Star)) {
        const auto op_span = previous().span;
        auto operand_result = parse_unary_expr();
        if (!operand_result)
            return std::unexpected(std::move(operand_result.error()));
        return make_unary_expr(cstc::ast::UnaryOp::Deref, op_span, std::move(*operand_result));
    }

    return parse_postfix_expr();
}

ParseResult<std::pair<std::vector<std::unique_ptr<cstc::ast::Expr>>, cstc::span::SourceSpan>>
    Parser::parse_expr_arguments_after_open_paren() {
    std::vector<std::unique_ptr<cstc::ast::Expr>> args;

    if (!check(cstc::lexer::TokenKind::CloseParen)) {
        while (true) {
            auto arg_result = parse_expr();
            if (!arg_result)
                return std::unexpected(std::move(arg_result.error()));
            args.push_back(std::move(*arg_result));

            if (!match(cstc::lexer::TokenKind::Comma))
                break;
            if (check(cstc::lexer::TokenKind::CloseParen))
                break;
        }
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after argument list");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    return std::pair{std::move(args), close_result->span};
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_postfix_expr() {
    auto expr_result = parse_primary_expr();
    if (!expr_result)
        return std::unexpected(std::move(expr_result.error()));

    auto expr = std::move(*expr_result);

    while (true) {
        if (check_turbofish_start()) {
            auto turbofish_result = parse_turbofish_args_after_prefix();
            if (!turbofish_result)
                return std::unexpected(std::move(turbofish_result.error()));

            const auto span = merge_spans(expr->span, turbofish_result->span);
            expr = make_expr(
                span, cstc::ast::TurbofishExpr{
                          .base = std::move(expr),
                          .args = std::move(*turbofish_result),
                      });
            continue;
        }

        if (match(cstc::lexer::TokenKind::OpenParen)) {
            auto args_result = parse_expr_arguments_after_open_paren();
            if (!args_result)
                return std::unexpected(std::move(args_result.error()));

            auto args = std::move(args_result->first);
            const auto close_span = args_result->second;
            const auto call_span = merge_spans(expr->span, close_span);

            expr = make_expr(
                call_span, cstc::ast::CallExpr{
                               .callee = std::move(expr),
                               .args = std::move(args),
                           });
            continue;
        }

        if (match(cstc::lexer::TokenKind::Dot)) {
            auto member_name_result = parse_identifier("field or method name");
            if (!member_name_result)
                return std::unexpected(std::move(member_name_result.error()));

            if (check_turbofish_start())
                return fail<std::unique_ptr<cstc::ast::Expr>>("method turbofish is not supported");
            if (check(cstc::lexer::TokenKind::OpenParen))
                return fail<std::unique_ptr<cstc::ast::Expr>>("method calls are not supported");

            expr = make_expr(
                merge_spans(expr->span, member_name_result->span),
                cstc::ast::FieldExpr{
                    .object = std::move(expr),
                    .field = member_name_result->symbol,
                });
            continue;
        }

        break;
    }

    return expr;
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_primary_expr() {
    if (check(cstc::lexer::TokenKind::OpenBrace)) {
        auto block_result = parse_block();
        if (!block_result)
            return std::unexpected(std::move(block_result.error()));
        const auto block_span = block_result->span;
        return make_expr(block_span, cstc::ast::BlockExpr{.block = std::move(*block_result)});
    }

    if (match_ident("if"))
        return parse_if_expr(previous().span);

    if (match_ident("match"))
        return fail<std::unique_ptr<cstc::ast::Expr>>("match expressions are not supported");

    if (match_ident("loop"))
        return parse_loop_expr(previous().span);

    if (match_ident("for"))
        return parse_for_expr(previous().span);

    if (match_ident("return"))
        return parse_return_expr(previous().span);

    if (match_ident("lambda"))
        return parse_lambda_expr(previous().span);

    if (match_ident("decl"))
        return parse_decl_expr(previous().span);

    if (looks_like_keyword_block_expr())
        return parse_keyword_block_expr();

    if (match_ident("concept")) {
        return fail<std::unique_ptr<cstc::ast::Expr>>("concept intrinsics are not supported");
    }

    if (check(cstc::lexer::TokenKind::LitInt) || check(cstc::lexer::TokenKind::LitFloat)
        || check(cstc::lexer::TokenKind::LitStr)
        || (check(cstc::lexer::TokenKind::Ident) && is_bool_literal(peek().text))) {
        auto literal_result = parse_literal();
        if (!literal_result)
            return std::unexpected(std::move(literal_result.error()));

        return make_expr(
            literal_result->span, cstc::ast::LitExpr{
                                      .lit = std::move(*literal_result),
                                  });
    }

    if (match(cstc::lexer::TokenKind::OpenParen))
        return parse_group_or_tuple_expr(previous().span);

    if (check(cstc::lexer::TokenKind::Ident)) {
        auto path_result = parse_path();
        if (!path_result)
            return std::unexpected(std::move(path_result.error()));

        if (is_lambda_capture_path(*path_result)) {
            const auto name = symbols_.str(path_result->segments.front().name);
            return fail<std::unique_ptr<cstc::ast::Expr>>(
                "lambda cannot capture outer variable `" + std::string(name) + "`");
        }

        if (check(cstc::lexer::TokenKind::OpenBrace) && is_constructor_path(*path_result))
            return parse_constructor_fields_expr(std::move(*path_result));

        return make_expr(
            path_result->span, cstc::ast::PathExpr{
                                   .path = std::move(*path_result),
                               });
    }

    if (check(cstc::lexer::TokenKind::Unknown))
        return fail<std::unique_ptr<cstc::ast::Expr>>("unknown token");

    return fail<std::unique_ptr<cstc::ast::Expr>>("expected expression");
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_group_or_tuple_expr(cstc::span::SourceSpan open_span) {
    if (match(cstc::lexer::TokenKind::CloseParen)) {
        return fail<std::unique_ptr<cstc::ast::Expr>>("tuple expressions are not supported");
    }

    auto first_result = parse_expr();
    if (!first_result)
        return std::unexpected(std::move(first_result.error()));

    if (!match(cstc::lexer::TokenKind::Comma)) {
        auto close_result =
            expect(cstc::lexer::TokenKind::CloseParen, "`)` after grouped expression");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));

        return make_expr(
            merge_spans(open_span, close_result->span), cstc::ast::GroupedExpr{
                                                            .inner = std::move(*first_result),
                                                        });
    }

    return fail<std::unique_ptr<cstc::ast::Expr>>("tuple expressions are not supported");
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_constructor_fields_expr(cstc::ast::Path constructor) {
    auto open_result = expect(cstc::lexer::TokenKind::OpenBrace, "`{` after constructor path");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::ExprField> fields;
    if (!check(cstc::lexer::TokenKind::CloseBrace)) {
        while (true) {
            auto field_name_result = parse_identifier("constructor field name");
            if (!field_name_result)
                return std::unexpected(std::move(field_name_result.error()));

            auto colon_result =
                expect(cstc::lexer::TokenKind::Colon, "`:` after constructor field name");
            if (!colon_result)
                return std::unexpected(std::move(colon_result.error()));

            auto field_expr_result = parse_expr();
            if (!field_expr_result)
                return std::unexpected(std::move(field_expr_result.error()));

            fields.push_back(
                cstc::ast::ExprField{
                    .span = merge_spans(field_name_result->span, (*field_expr_result)->span),
                    .name = field_name_result->symbol,
                    .value = std::move(*field_expr_result),
                });

            if (!match(cstc::lexer::TokenKind::Comma))
                break;
            if (check(cstc::lexer::TokenKind::CloseBrace))
                break;
        }
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after constructor fields");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    return make_expr(
        merge_spans(constructor.span, close_result->span),
        cstc::ast::ConstructorFieldsExpr{
            .constructor = std::move(constructor),
            .fields = std::move(fields),
        });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>> Parser::parse_keyword_block_expr() {
    auto keywords_result = parse_keyword_modifiers();
    if (!keywords_result)
        return std::unexpected(std::move(keywords_result.error()));
    if (keywords_result->empty())
        return fail<std::unique_ptr<cstc::ast::Expr>>("expected keyword block modifier");

    if (!check(cstc::lexer::TokenKind::OpenBrace)) {
        return fail<std::unique_ptr<cstc::ast::Expr>>("expected `{` after keyword block modifier");
    }

    auto body_result = parse_block();
    if (!body_result)
        return std::unexpected(std::move(body_result.error()));

    const auto span = merge_spans(keywords_result->front().span, body_result->span);
    return make_expr(
        span, cstc::ast::KeywordBlockExpr{
                  .keywords = std::move(*keywords_result),
                  .body = std::move(*body_result),
              });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_if_expr(cstc::span::SourceSpan if_span) {
    auto cond_result = parse_expr();
    if (!cond_result)
        return std::unexpected(std::move(cond_result.error()));

    auto then_block_result = parse_block();
    if (!then_block_result)
        return std::unexpected(std::move(then_block_result.error()));

    std::optional<std::unique_ptr<cstc::ast::Expr>> else_expr;
    cstc::span::SourceSpan end_span = then_block_result->span;

    if (match_ident("else")) {
        if (match_ident("if")) {
            auto nested_if_result = parse_if_expr(previous().span);
            if (!nested_if_result)
                return std::unexpected(std::move(nested_if_result.error()));
            end_span = (*nested_if_result)->span;
            else_expr = std::move(*nested_if_result);
        } else if (check(cstc::lexer::TokenKind::OpenBrace)) {
            auto else_block_result = parse_block();
            if (!else_block_result)
                return std::unexpected(std::move(else_block_result.error()));

            end_span = else_block_result->span;
            else_expr = make_expr(
                end_span, cstc::ast::BlockExpr{
                              .block = std::move(*else_block_result),
                          });
        } else {
            return fail<std::unique_ptr<cstc::ast::Expr>>("expected `if` or block after `else`");
        }
    }

    return make_expr(
        merge_spans(if_span, end_span), cstc::ast::IfExpr{
                                            .cond = std::move(*cond_result),
                                            .then_block = std::move(*then_block_result),
                                            .else_expr = std::move(else_expr),
                                        });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_match_expr(cstc::span::SourceSpan match_span) {
    auto scrutinee_result = parse_expr();
    if (!scrutinee_result)
        return std::unexpected(std::move(scrutinee_result.error()));

    auto open_result = expect(cstc::lexer::TokenKind::OpenBrace, "`{` to start match arms");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::MatchArm> arms;
    while (!check(cstc::lexer::TokenKind::CloseBrace)) {
        auto pattern_result = parse_pattern();
        if (!pattern_result)
            return std::unexpected(std::move(pattern_result.error()));

        if (!match_fat_arrow())
            return fail<std::unique_ptr<cstc::ast::Expr>>("expected `=>` in match arm");

        auto body_result = parse_expr();
        if (!body_result)
            return std::unexpected(std::move(body_result.error()));

        arms.push_back(
            cstc::ast::MatchArm{
                .span = merge_spans((*pattern_result)->span, (*body_result)->span),
                .pat = std::move(*pattern_result),
                .body = std::move(*body_result),
            });

        if (match(cstc::lexer::TokenKind::Comma))
            continue;

        if (check(cstc::lexer::TokenKind::CloseBrace))
            break;

        return fail<std::unique_ptr<cstc::ast::Expr>>("expected `,` or `}` after match arm");
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after match arms");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    return make_expr(
        merge_spans(match_span, close_result->span), cstc::ast::MatchExpr{
                                                         .scrutinee = std::move(*scrutinee_result),
                                                         .arms = std::move(arms),
                                                     });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_loop_expr(cstc::span::SourceSpan loop_span) {
    auto body_result = parse_block();
    if (!body_result)
        return std::unexpected(std::move(body_result.error()));

    return make_expr(
        merge_spans(loop_span, body_result->span),
        cstc::ast::LoopExpr{.body = std::move(*body_result)});
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_for_expr(cstc::span::SourceSpan for_span) {
    auto open_result = expect(cstc::lexer::TokenKind::OpenParen, "`(` after `for`");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::optional<std::unique_ptr<cstc::ast::Expr>> init;
    std::optional<std::unique_ptr<cstc::ast::Expr>> cond;
    std::optional<std::unique_ptr<cstc::ast::Expr>> step;

    if (!check(cstc::lexer::TokenKind::Semi)) {
        auto init_result = parse_expr();
        if (!init_result)
            return std::unexpected(std::move(init_result.error()));
        init = std::move(*init_result);
    }

    auto first_semi = expect(cstc::lexer::TokenKind::Semi, "`;` after for-loop initializer");
    if (!first_semi)
        return std::unexpected(std::move(first_semi.error()));

    if (!check(cstc::lexer::TokenKind::Semi)) {
        auto cond_result = parse_expr();
        if (!cond_result)
            return std::unexpected(std::move(cond_result.error()));
        cond = std::move(*cond_result);
    }

    auto second_semi = expect(cstc::lexer::TokenKind::Semi, "`;` after for-loop condition");
    if (!second_semi)
        return std::unexpected(std::move(second_semi.error()));

    if (!check(cstc::lexer::TokenKind::CloseParen)) {
        auto step_result = parse_expr();
        if (!step_result)
            return std::unexpected(std::move(step_result.error()));
        step = std::move(*step_result);
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after for-loop header");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    auto body_result = parse_block();
    if (!body_result)
        return std::unexpected(std::move(body_result.error()));

    return make_expr(
        merge_spans(for_span, body_result->span), cstc::ast::ForExpr{
                                                    .init = std::move(init),
                                                    .cond = std::move(cond),
                                                    .step = std::move(step),
                                                    .body = std::move(*body_result),
                                                });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_return_expr(cstc::span::SourceSpan return_span) {
    std::optional<std::unique_ptr<cstc::ast::Expr>> value;
    cstc::span::SourceSpan end_span = return_span;

    if (!check(cstc::lexer::TokenKind::Semi) && !check(cstc::lexer::TokenKind::Comma)
        && !check(cstc::lexer::TokenKind::CloseBrace) && !at_end()) {
        auto value_result = parse_expr();
        if (!value_result)
            return std::unexpected(std::move(value_result.error()));
        end_span = (*value_result)->span;
        value = std::move(*value_result);
    }

    return make_expr(
        merge_spans(return_span, end_span), cstc::ast::ReturnExpr{
                                                .value = std::move(value),
                                            });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_lambda_expr(cstc::span::SourceSpan lambda_span) {
    auto open_result = expect(cstc::lexer::TokenKind::OpenParen, "`(` after `lambda`");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    std::vector<cstc::ast::LambdaParam> params;
    if (!check(cstc::lexer::TokenKind::CloseParen)) {
        while (true) {
            auto param_name_result = parse_identifier("lambda parameter name");
            if (!param_name_result)
                return std::unexpected(std::move(param_name_result.error()));

            std::optional<std::unique_ptr<cstc::ast::TypeNode>> param_type;
            cstc::span::SourceSpan param_span = param_name_result->span;
            if (match(cstc::lexer::TokenKind::Colon)) {
                auto type_result = parse_type();
                if (!type_result)
                    return std::unexpected(std::move(type_result.error()));
                param_span = merge_spans(param_span, (*type_result)->span);
                param_type = std::move(*type_result);
            }

            params.push_back(
                cstc::ast::LambdaParam{
                    .span = param_span,
                    .name = param_name_result->symbol,
                    .ty = std::move(param_type),
                });

            if (!match(cstc::lexer::TokenKind::Comma))
                break;
            if (check(cstc::lexer::TokenKind::CloseParen))
                break;
        }
    }

    auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after lambda parameters");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    const auto lambda_scope_base = local_scopes_.size();
    push_local_scope();
    for (const auto& param : params)
        bind_local_symbol(param.name);
    lambda_scope_base_depths_.push_back(lambda_scope_base);

    auto body_result = parse_block();
    if (!body_result) {
        lambda_scope_base_depths_.pop_back();
        pop_local_scope();
        return std::unexpected(std::move(body_result.error()));
    }

    lambda_scope_base_depths_.pop_back();
    pop_local_scope();

    return make_expr(
        merge_spans(lambda_span, body_result->span), cstc::ast::LambdaExpr{
                                                         .params = std::move(params),
                                                         .body = std::move(*body_result),
                                                     });
}

ParseResult<std::unique_ptr<cstc::ast::Expr>>
    Parser::parse_decl_expr(cstc::span::SourceSpan decl_span) {
    auto open_result = expect(cstc::lexer::TokenKind::OpenParen, "`(` after `decl`");
    if (!open_result)
        return std::unexpected(std::move(open_result.error()));

    auto type_result = parse_type();
    if (!type_result)
        return std::unexpected(std::move(type_result.error()));

    auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after `decl` argument");
    if (!close_result)
        return std::unexpected(std::move(close_result.error()));

    return make_expr(
        merge_spans(decl_span, close_result->span), cstc::ast::DeclExpr{
                                                        .type_expr = std::move(*type_result),
                                                    });
}

ParseResult<std::unique_ptr<cstc::ast::Pat>> Parser::parse_pattern() { return parse_or_pattern(); }

ParseResult<std::unique_ptr<cstc::ast::Pat>> Parser::parse_or_pattern() {
    auto first_result = parse_as_pattern();
    if (!first_result)
        return std::unexpected(std::move(first_result.error()));

    if (!match_single_pipe())
        return std::move(*first_result);

    std::vector<std::unique_ptr<cstc::ast::Pat>> alternatives;
    const auto start_span = (*first_result)->span;
    alternatives.push_back(std::move(*first_result));

    do {
        auto alternative_result = parse_as_pattern();
        if (!alternative_result)
            return std::unexpected(std::move(alternative_result.error()));
        alternatives.push_back(std::move(*alternative_result));
    } while (match_single_pipe());

    return make_pat(
        merge_spans(start_span, alternatives.back()->span),
        cstc::ast::OrPat{
            .alternatives = std::move(alternatives),
        });
}

ParseResult<std::unique_ptr<cstc::ast::Pat>> Parser::parse_as_pattern() {
    if (check(cstc::lexer::TokenKind::Ident) && peek().text != "_"
        && check(cstc::lexer::TokenKind::At, 1)) {
        auto binder_result = parse_identifier("pattern binder");
        if (!binder_result)
            return std::unexpected(std::move(binder_result.error()));

        auto at_result = expect(cstc::lexer::TokenKind::At, "`@` in as-pattern");
        if (!at_result)
            return std::unexpected(std::move(at_result.error()));

        auto inner_result = parse_as_pattern();
        if (!inner_result)
            return std::unexpected(std::move(inner_result.error()));

        return make_pat(
            merge_spans(binder_result->span, (*inner_result)->span),
            cstc::ast::AsPat{
                .name = binder_result->symbol,
                .inner = std::move(*inner_result),
            });
    }

    return parse_pattern_atom();
}

ParseResult<std::unique_ptr<cstc::ast::Pat>> Parser::parse_pattern_atom() {
    if (check(cstc::lexer::TokenKind::Ident) && peek().text == "_") {
        const auto wildcard = advance();
        return make_pat(wildcard.span, cstc::ast::WildcardPat{});
    }

    if (check(cstc::lexer::TokenKind::LitInt) || check(cstc::lexer::TokenKind::LitFloat)
        || check(cstc::lexer::TokenKind::LitStr)
        || (check(cstc::lexer::TokenKind::Ident) && is_bool_literal(peek().text))) {
        auto literal_result = parse_literal();
        if (!literal_result)
            return std::unexpected(std::move(literal_result.error()));
        return make_pat(
            literal_result->span, cstc::ast::LitPat{
                                      .lit = std::move(*literal_result),
                                  });
    }

    if (match(cstc::lexer::TokenKind::OpenParen)) {
        const auto open_span = previous().span;

        if (match(cstc::lexer::TokenKind::CloseParen)) {
            return make_pat(
                merge_spans(open_span, previous().span), cstc::ast::TuplePat{.elements = {}});
        }

        auto first_result = parse_pattern();
        if (!first_result)
            return std::unexpected(std::move(first_result.error()));

        if (!match(cstc::lexer::TokenKind::Comma)) {
            auto close_result =
                expect(cstc::lexer::TokenKind::CloseParen, "`)` after grouped pattern");
            if (!close_result)
                return std::unexpected(std::move(close_result.error()));
            (*first_result)->span = merge_spans(open_span, close_result->span);
            return std::move(*first_result);
        }

        std::vector<std::unique_ptr<cstc::ast::Pat>> elements;
        elements.push_back(std::move(*first_result));

        if (!check(cstc::lexer::TokenKind::CloseParen)) {
            while (true) {
                auto element_result = parse_pattern();
                if (!element_result)
                    return std::unexpected(std::move(element_result.error()));
                elements.push_back(std::move(*element_result));

                if (!match(cstc::lexer::TokenKind::Comma))
                    break;
                if (check(cstc::lexer::TokenKind::CloseParen))
                    break;
            }
        }

        auto close_result = expect(cstc::lexer::TokenKind::CloseParen, "`)` after tuple pattern");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));

        return make_pat(
            merge_spans(open_span, close_result->span),
            cstc::ast::TuplePat{.elements = std::move(elements)});
    }

    auto path_result = parse_path();
    if (!path_result)
        return std::unexpected(std::move(path_result.error()));

    if (match(cstc::lexer::TokenKind::OpenBrace)) {
        std::vector<cstc::ast::ConstructorFieldsPat::Field> fields;
        if (!check(cstc::lexer::TokenKind::CloseBrace)) {
            while (true) {
                auto field_name_result = parse_identifier("pattern field name");
                if (!field_name_result)
                    return std::unexpected(std::move(field_name_result.error()));

                auto colon_result =
                    expect(cstc::lexer::TokenKind::Colon, "`:` after pattern field name");
                if (!colon_result)
                    return std::unexpected(std::move(colon_result.error()));

                auto field_pat_result = parse_pattern();
                if (!field_pat_result)
                    return std::unexpected(std::move(field_pat_result.error()));

                fields.push_back(
                    cstc::ast::ConstructorFieldsPat::Field{
                        .span = merge_spans(field_name_result->span, (*field_pat_result)->span),
                        .name = field_name_result->symbol,
                        .pat = std::move(*field_pat_result),
                    });

                if (!match(cstc::lexer::TokenKind::Comma))
                    break;
                if (check(cstc::lexer::TokenKind::CloseBrace))
                    break;
            }
        }

        auto close_result = expect(cstc::lexer::TokenKind::CloseBrace, "`}` after pattern fields");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));

        return make_pat(
            merge_spans(path_result->span, close_result->span),
            cstc::ast::ConstructorFieldsPat{
                .constructor = std::move(*path_result),
                .fields = std::move(fields),
            });
    }

    if (match(cstc::lexer::TokenKind::OpenParen)) {
        std::vector<std::unique_ptr<cstc::ast::Pat>> args;
        if (!check(cstc::lexer::TokenKind::CloseParen)) {
            while (true) {
                auto arg_result = parse_pattern();
                if (!arg_result)
                    return std::unexpected(std::move(arg_result.error()));
                args.push_back(std::move(*arg_result));

                if (!match(cstc::lexer::TokenKind::Comma))
                    break;
                if (check(cstc::lexer::TokenKind::CloseParen))
                    break;
            }
        }

        auto close_result =
            expect(cstc::lexer::TokenKind::CloseParen, "`)` after positional pattern");
        if (!close_result)
            return std::unexpected(std::move(close_result.error()));

        return make_pat(
            merge_spans(path_result->span, close_result->span),
            cstc::ast::ConstructorPositionalPat{
                .constructor = std::move(*path_result),
                .args = std::move(args),
            });
    }

    if (path_result->segments.size() == 1 && !is_constructor_path(*path_result)) {
        return make_pat(
            path_result->span, cstc::ast::BindingPat{
                                   .name = path_result->segments.front().name,
                               });
    }

    return make_pat(
        path_result->span, cstc::ast::ConstructorUnitPat{
                               .constructor = std::move(*path_result),
                           });
}

std::unique_ptr<cstc::ast::Expr> Parser::make_binary_expr(
    cstc::ast::BinaryOp op, std::unique_ptr<cstc::ast::Expr> lhs,
    std::unique_ptr<cstc::ast::Expr> rhs) {
    const auto span = merge_spans(lhs->span, rhs->span);
    return make_expr(
        span, cstc::ast::BinaryExpr{
                  .op = op,
                  .lhs = std::move(lhs),
                  .rhs = std::move(rhs),
              });
}

std::unique_ptr<cstc::ast::Expr> Parser::make_unary_expr(
    cstc::ast::UnaryOp op, cstc::span::SourceSpan op_span,
    std::unique_ptr<cstc::ast::Expr> operand) {
    const auto span = merge_spans(op_span, operand->span);
    return make_expr(
        span, cstc::ast::UnaryExpr{
                  .op = op,
                  .operand = std::move(operand),
              });
}

bool Parser::is_constructor_path(const cstc::ast::Path& path) const {
    if (path.segments.empty())
        return false;
    const auto name = symbols_.str(path.segments.back().name);
    if (name.empty())
        return false;

    return std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

ParseResult<cstc::ast::Crate> Parser::parse_crate() {
    std::vector<cstc::ast::Item> items;
    while (!at_end()) {
        auto item_result = parse_item();
        if (!item_result)
            return std::unexpected(std::move(item_result.error()));
        items.push_back(std::move(*item_result));
    }

    return cstc::ast::Crate{.items = std::move(items)};
}

} // namespace

inline std::vector<LexedToken> lex_source(std::string_view source, bool keep_trivia) {
    std::string owned_source(source);
    cstc::lexer::Cursor cursor(owned_source);

    std::vector<LexedToken> tokens;
    tokens.reserve(owned_source.size() / 2 + 1);

    std::size_t offset = 0;
    while (true) {
        const auto token = cursor.advance_token();
        const auto start = offset;
        const auto end = offset + token.len;
        offset = end;

        LexedToken lexed{
            .kind = token.kind,
            .span =
                {
                       .start = start,
                       .end = end,
                       },
            .text = token.len == 0 ? std::string{  }
                 : owned_source.substr(start, token.len),
        };

        if (keep_trivia || !is_trivia_token(lexed.kind)
            || lexed.kind == cstc::lexer::TokenKind::Eof)
            tokens.push_back(std::move(lexed));

        if (token.kind == cstc::lexer::TokenKind::Eof)
            break;
    }

    if (tokens.empty() || tokens.back().kind != cstc::lexer::TokenKind::Eof) {
        tokens.push_back(
            LexedToken{
                .kind = cstc::lexer::TokenKind::Eof,
                .span = {.start = offset, .end = offset},
                .text = {},
        });
    }

    return tokens;
}

inline std::expected<cstc::ast::Crate, ParseError>
    parse_tokens(std::span<const LexedToken> tokens, cstc::ast::SymbolTable& symbols) {
    Parser parser(tokens, symbols);
    return parser.parse_crate();
}

inline std::expected<cstc::ast::Crate, ParseError>
    parse_source(std::string_view source, cstc::ast::SymbolTable& symbols) {
    const auto tokens = lex_source(source, false);
    return parse_tokens(tokens, symbols);
}

} // namespace cstc::parser

#endif // CICEST_COMPILER_CSTC_PARSER_PARSER_IMPL_HPP
