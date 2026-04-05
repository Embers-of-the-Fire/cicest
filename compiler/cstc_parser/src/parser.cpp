#include <cstc_parser/parser.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_lexer/lexer.hpp>
#include <cstc_lexer/token.hpp>
#include <cstc_span/span.hpp>

namespace cstc::parser {

namespace {

using cstc::lexer::Token;
using cstc::lexer::TokenKind;

[[nodiscard]] cstc::span::SourceSpan
    merge_spans(const cstc::span::SourceSpan& lhs, const cstc::span::SourceSpan& rhs) {
    return cstc::span::merge(lhs, rhs);
}

[[nodiscard]] bool is_optional_expr_terminator(TokenKind kind) {
    return kind == TokenKind::Semicolon || kind == TokenKind::Comma || kind == TokenKind::RParen
        || kind == TokenKind::RBrace || kind == TokenKind::EndOfFile;
}

[[nodiscard]] cstc::symbol::Symbol string_contents_symbol(const Token& token) {
    const std::string_view raw = token.symbol.as_str();
    return cstc::symbol::Symbol::intern(raw.substr(1, raw.size() - 2));
}

class Parser {
public:
    explicit Parser(std::span<const Token> input_tokens)
        : tokens_(input_tokens.begin(), input_tokens.end()) {
        if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
            const std::size_t position = tokens_.empty() ? 0 : tokens_.back().span.end;
            tokens_.push_back(
                Token{
                    .kind = TokenKind::EndOfFile,
                    .span = {.start = position, .end = position},
                    .symbol = cstc::symbol::kInvalidSymbol,
            });
        }
    }

    [[nodiscard]] std::expected<ast::Program, ParseError> parse_program() {
        ast::Program program;

        while (!is_at_end()) {
            auto item = parse_item();
            if (!item.has_value())
                return std::unexpected(item.error());
            program.items.push_back(std::move(*item));
        }

        return program;
    }

private:
    struct ParsedPattern {
        bool discard = false;
        cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
        cstc::span::SourceSpan span;
    };

    [[nodiscard]] std::string_view token_text(const Token& token) const {
        return token.symbol.as_str();
    }

    [[nodiscard]] const Token& peek(std::size_t offset = 0) const {
        const std::size_t index = cursor_ + offset;
        if (index >= tokens_.size())
            return tokens_.back();
        return tokens_[index];
    }

    [[nodiscard]] const Token& previous() const { return tokens_[cursor_ - 1]; }

    [[nodiscard]] bool is_at_end() const { return peek().kind == TokenKind::EndOfFile; }

    [[nodiscard]] bool check(TokenKind kind) const { return peek().kind == kind; }

    [[nodiscard]] bool check_attribute_start() const {
        return check(TokenKind::LBracket) && peek(1).kind == TokenKind::LBracket;
    }

    [[nodiscard]] bool match(TokenKind kind) {
        if (!check(kind))
            return false;
        static_cast<void>(advance());
        return true;
    }

    [[nodiscard]] const Token& advance() {
        if (!is_at_end())
            ++cursor_;
        return previous();
    }

    [[nodiscard]] ParseError make_error_here(std::string message) const {
        return ParseError{
            .span = peek().span,
            .message = std::move(message),
        };
    }

    [[nodiscard]] ParseError make_error_token(const Token& token, std::string message) const {
        return ParseError{
            .span = token.span,
            .message = std::move(message),
        };
    }

    [[nodiscard]] cstc::span::SourceSpan
        item_lead_span(const std::vector<ast::Attribute>& attributes, const Token& fallback) const {
        if (!attributes.empty())
            return attributes.front().span;
        return fallback.span;
    }

    [[nodiscard]] bool looks_like_struct_initializer() const {
        if (!check(TokenKind::LBrace))
            return false;

        if (peek(1).kind == TokenKind::RBrace)
            return true;

        if (peek(1).kind != TokenKind::Identifier)
            return false;

        return peek(2).kind == TokenKind::Colon;
    }

    [[nodiscard]] bool is_postfixable(const ast::ExprPtr& expr) const {
        return std::holds_alternative<ast::PathExpr>(expr->node)
            || std::holds_alternative<ast::StructInitExpr>(expr->node)
            || std::holds_alternative<ast::GenericAppExpr>(expr->node)
            || std::holds_alternative<ast::FieldAccessExpr>(expr->node)
            || std::holds_alternative<ast::CallExpr>(expr->node);
    }

    [[nodiscard]] bool token_can_start_type(TokenKind kind) const {
        return kind == TokenKind::KwRuntime || kind == TokenKind::Amp || kind == TokenKind::KwUnit
            || kind == TokenKind::KwNum || kind == TokenKind::KwStr || kind == TokenKind::KwBool
            || kind == TokenKind::Bang || kind == TokenKind::Identifier;
    }

    [[nodiscard]] bool looks_like_turbofish() const {
        return check(TokenKind::ColonColon) && peek(1).kind == TokenKind::Lt;
    }

    [[nodiscard]] bool looks_like_generic_struct_initializer() const {
        if (!check(TokenKind::Lt))
            return false;

        std::size_t index = cursor_;
        std::size_t depth = 0;

        while (index < tokens_.size()) {
            const TokenKind kind = tokens_[index].kind;
            if (kind == TokenKind::Lt) {
                ++depth;
                ++index;
                continue;
            }
            if (kind == TokenKind::Gt) {
                if (depth == 0)
                    return false;
                --depth;
                ++index;
                if (depth == 0)
                    return index < tokens_.size() && tokens_[index].kind == TokenKind::LBrace;
                continue;
            }
            if (kind == TokenKind::Comma) {
                ++index;
                continue;
            }
            if (!token_can_start_type(kind))
                return false;
            ++index;
        }

        return false;
    }

    [[nodiscard]] bool is_semicolon_optional_stmt_expr(const ast::ExprPtr& expr) const {
        return std::holds_alternative<ast::BlockPtr>(expr->node)
            || std::holds_alternative<ast::IfExpr>(expr->node)
            || std::holds_alternative<ast::LoopExpr>(expr->node)
            || std::holds_alternative<ast::WhileExpr>(expr->node)
            || std::holds_alternative<ast::ForExpr>(expr->node);
    }

    [[nodiscard]] std::expected<Token, ParseError>
        consume(TokenKind kind, std::string_view message) {
        if (!check(kind)) {
            return std::unexpected(make_error_here(
                std::string(message) + ", got `" + std::string(token_text(peek())) + "`"));
        }
        return advance();
    }

    [[nodiscard]] std::expected<Token, ParseError> consume_identifier(std::string_view message) {
        if (!check(TokenKind::Identifier))
            return std::unexpected(make_error_here(std::string(message)));
        return advance();
    }

    [[nodiscard]] std::expected<ast::Item, ParseError> parse_item() {
        auto parsed_attributes = parse_attributes();
        if (!parsed_attributes.has_value())
            return std::unexpected(parsed_attributes.error());
        std::vector<ast::Attribute> attributes = std::move(*parsed_attributes);

        std::optional<Token> visibility_keyword;
        const bool is_public = match(TokenKind::KwPub);
        if (is_public)
            visibility_keyword = previous();

        std::optional<Token> runtime_keyword;
        const bool is_runtime = match(TokenKind::KwRuntime);
        if (is_runtime)
            runtime_keyword = previous();

        if (is_runtime && !check(TokenKind::KwFn) && !check(TokenKind::KwExtern)) {
            return std::unexpected(make_error_here("expected `fn` or `extern` after `runtime`"));
        }

        if (match(TokenKind::KwStruct)) {
            const Token struct_keyword = previous();
            const Token* lead = &struct_keyword;
            if (visibility_keyword.has_value())
                lead = &*visibility_keyword;
            auto decl = parse_struct_decl(*lead, std::move(attributes), is_public);
            if (!decl.has_value())
                return std::unexpected(decl.error());
            return ast::Item{std::move(*decl)};
        }

        if (match(TokenKind::KwEnum)) {
            const Token enum_keyword = previous();
            const Token* lead = &enum_keyword;
            if (visibility_keyword.has_value())
                lead = &*visibility_keyword;
            auto decl = parse_enum_decl(*lead, std::move(attributes), is_public);
            if (!decl.has_value())
                return std::unexpected(decl.error());
            return ast::Item{std::move(*decl)};
        }

        if (match(TokenKind::KwFn)) {
            const Token fn_keyword = previous();
            const Token* lead = &fn_keyword;
            if (runtime_keyword.has_value())
                lead = &*runtime_keyword;
            if (visibility_keyword.has_value())
                lead = &*visibility_keyword;
            auto decl = parse_fn_decl(*lead, std::move(attributes), is_public, is_runtime);
            if (!decl.has_value())
                return std::unexpected(decl.error());
            return ast::Item{std::move(*decl)};
        }

        if (check(TokenKind::KwExtern)) {
            const Token* lead = &peek();
            if (runtime_keyword.has_value())
                lead = &*runtime_keyword;
            if (visibility_keyword.has_value())
                lead = &*visibility_keyword;
            return parse_extern_decl(*lead, std::move(attributes), is_public, is_runtime);
        }

        if (match(TokenKind::KwImport)) {
            const Token import_keyword = previous();
            if (!attributes.empty()) {
                return std::unexpected(make_error_token(
                    import_keyword, "attributes are not supported on import declarations"));
            }
            const Token* lead = &import_keyword;
            if (visibility_keyword.has_value())
                lead = &*visibility_keyword;
            auto decl = parse_import_decl(*lead, is_public);
            if (!decl.has_value())
                return std::unexpected(decl.error());
            return ast::Item{std::move(*decl)};
        }

        if (!attributes.empty()) {
            return std::unexpected(make_error_here(
                "expected item after attributes (`struct`, `enum`, `fn`, `extern`, `runtime`)"));
        }

        if (is_public) {
            return std::unexpected(make_error_here(
                "expected item after `pub` (`struct`, `enum`, `fn`, `extern`, `runtime`, "
                "`import`)"));
        }

        return std::unexpected(make_error_here(
            "expected item (`struct`, `enum`, `fn`, `extern`, `runtime`, "
            "`import`)"));
    }

    [[nodiscard]] std::expected<std::vector<ast::GenericParam>, ParseError>
        parse_generic_param_list() {
        auto open = consume(TokenKind::Lt, "expected `<` to start generic parameter list");
        if (!open.has_value())
            return std::unexpected(open.error());

        std::vector<ast::GenericParam> params;
        std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> seen_params;

        while (true) {
            auto name_token = consume_identifier("expected generic parameter name");
            if (!name_token.has_value())
                return std::unexpected(name_token.error());

            if (!seen_params.insert(name_token->symbol).second) {
                const std::string param_name_text = std::string(token_text(*name_token));
                return std::unexpected(make_error_token(
                    *name_token, "duplicate generic parameter `" + param_name_text + "`"));
            }

            params.push_back(
                ast::GenericParam{.name = name_token->symbol, .span = name_token->span});

            if (match(TokenKind::Comma)) {
                if (check(TokenKind::Gt))
                    break;
                continue;
            }
            break;
        }

        auto close = consume(TokenKind::Gt, "expected `>` to close generic parameter list");
        if (!close.has_value())
            return std::unexpected(close.error());

        return params;
    }

    [[nodiscard]] std::expected<std::vector<ast::TypeRef>, ParseError>
        parse_generic_argument_list() {
        auto open = consume(TokenKind::Lt, "expected `<` to start generic argument list");
        if (!open.has_value())
            return std::unexpected(open.error());

        std::vector<ast::TypeRef> args;

        while (true) {
            auto arg = parse_type();
            if (!arg.has_value())
                return std::unexpected(arg.error());
            args.push_back(std::move(*arg));

            if (match(TokenKind::Comma)) {
                if (check(TokenKind::Gt))
                    break;
                continue;
            }
            break;
        }

        auto close = consume(TokenKind::Gt, "expected `>` to close generic argument list");
        if (!close.has_value())
            return std::unexpected(close.error());

        return args;
    }

    [[nodiscard]] std::expected<std::vector<ast::GenericConstraint>, ParseError>
        parse_where_clause() {
        std::vector<ast::GenericConstraint> constraints;
        if (!match(TokenKind::KwWhere))
            return constraints;

        while (true) {
            if (check(TokenKind::LBrace) || check(TokenKind::Semicolon)) {
                return std::unexpected(
                    make_error_here("expected constraint expression after `where`"));
            }

            auto expr = parse_condition_expression();
            if (!expr.has_value())
                return std::unexpected(expr.error());

            constraints.push_back(ast::GenericConstraint{.expr = *expr, .span = (*expr)->span});

            if (match(TokenKind::Comma)) {
                if (check(TokenKind::LBrace) || check(TokenKind::Semicolon)) {
                    return std::unexpected(
                        make_error_here("expected constraint expression after `,`"));
                }
                continue;
            }
            break;
        }

        return constraints;
    }

    [[nodiscard]] std::expected<ast::StructDecl, ParseError> parse_struct_decl(
        const Token& lead_token, std::vector<ast::Attribute> attributes, bool is_public) {
        auto name_token = consume_identifier("expected struct name");
        if (!name_token.has_value())
            return std::unexpected(name_token.error());

        ast::StructDecl decl;
        decl.is_public = is_public;
        decl.name = name_token->symbol;
        decl.attributes = std::move(attributes);

        if (check(TokenKind::Lt)) {
            auto generic_params = parse_generic_param_list();
            if (!generic_params.has_value())
                return std::unexpected(generic_params.error());
            decl.generic_params = std::move(*generic_params);
        }

        auto where_clause = parse_where_clause();
        if (!where_clause.has_value())
            return std::unexpected(where_clause.error());
        decl.where_clause = std::move(*where_clause);

        if (match(TokenKind::Semicolon)) {
            decl.is_zst = true;
            decl.span = merge_spans(item_lead_span(decl.attributes, lead_token), previous().span);
            return decl;
        }

        auto open_brace = consume(TokenKind::LBrace, "expected `{` or `;` after struct name");
        if (!open_brace.has_value())
            return std::unexpected(open_brace.error());

        std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> seen_fields;

        if (!check(TokenKind::RBrace)) {
            while (true) {
                auto field_name = consume_identifier("expected field name");
                if (!field_name.has_value())
                    return std::unexpected(field_name.error());

                if (!seen_fields.insert(field_name->symbol).second) {
                    const std::string field_name_text = std::string(token_text(*field_name));
                    return std::unexpected(make_error_token(
                        *field_name, "duplicate struct field `" + field_name_text + "`"));
                }

                auto colon = consume(TokenKind::Colon, "expected `:` after field name");
                if (!colon.has_value())
                    return std::unexpected(colon.error());

                auto field_type = parse_type();
                if (!field_type.has_value())
                    return std::unexpected(field_type.error());

                decl.fields.push_back(
                    ast::FieldDecl{
                        .name = field_name->symbol,
                        .type = std::move(*field_type),
                        .span = merge_spans(field_name->span, previous().span),
                    });

                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RBrace))
                        break;
                    continue;
                }
                break;
            }
        }

        auto close_brace = consume(TokenKind::RBrace, "expected `}` to close struct declaration");
        if (!close_brace.has_value())
            return std::unexpected(close_brace.error());

        decl.span = merge_spans(item_lead_span(decl.attributes, lead_token), close_brace->span);
        return decl;
    }

    [[nodiscard]] std::expected<ast::EnumDecl, ParseError> parse_enum_decl(
        const Token& lead_token, std::vector<ast::Attribute> attributes, bool is_public) {
        auto name_token = consume_identifier("expected enum name");
        if (!name_token.has_value())
            return std::unexpected(name_token.error());

        ast::EnumDecl decl;
        decl.is_public = is_public;
        decl.name = name_token->symbol;
        decl.attributes = std::move(attributes);

        if (check(TokenKind::Lt)) {
            auto generic_params = parse_generic_param_list();
            if (!generic_params.has_value())
                return std::unexpected(generic_params.error());
            decl.generic_params = std::move(*generic_params);
        }

        auto where_clause = parse_where_clause();
        if (!where_clause.has_value())
            return std::unexpected(where_clause.error());
        decl.where_clause = std::move(*where_clause);

        auto open_brace = consume(TokenKind::LBrace, "expected `{` after enum name");
        if (!open_brace.has_value())
            return std::unexpected(open_brace.error());

        std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> seen_variants;

        if (!check(TokenKind::RBrace)) {
            while (true) {
                auto variant_name = consume_identifier("expected enum variant name");
                if (!variant_name.has_value())
                    return std::unexpected(variant_name.error());

                if (!seen_variants.insert(variant_name->symbol).second) {
                    const std::string variant_name_text = std::string(token_text(*variant_name));
                    return std::unexpected(make_error_token(
                        *variant_name, "duplicate enum variant `" + variant_name_text + "`"));
                }

                ast::EnumVariant variant;
                variant.name = variant_name->symbol;
                variant.span = variant_name->span;

                if (match(TokenKind::Assign)) {
                    auto number_token =
                        consume(TokenKind::Number, "expected numeric discriminant after `=`");
                    if (!number_token.has_value())
                        return std::unexpected(number_token.error());
                    variant.discriminant = number_token->symbol;
                    variant.span = merge_spans(variant.span, number_token->span);
                }

                decl.variants.push_back(std::move(variant));

                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RBrace))
                        break;
                    continue;
                }
                break;
            }
        }

        auto close_brace = consume(TokenKind::RBrace, "expected `}` to close enum declaration");
        if (!close_brace.has_value())
            return std::unexpected(close_brace.error());

        decl.span = merge_spans(item_lead_span(decl.attributes, lead_token), close_brace->span);
        return decl;
    }

    [[nodiscard]] std::expected<ast::FnDecl, ParseError> parse_fn_decl(
        const Token& lead_token, std::vector<ast::Attribute> attributes, bool is_public,
        bool is_runtime) {
        auto name_token = consume_identifier("expected function name");
        if (!name_token.has_value())
            return std::unexpected(name_token.error());

        std::vector<ast::GenericParam> generic_params;
        if (check(TokenKind::Lt)) {
            auto parsed_generic_params = parse_generic_param_list();
            if (!parsed_generic_params.has_value())
                return std::unexpected(parsed_generic_params.error());
            generic_params = std::move(*parsed_generic_params);
        }

        auto open_paren = consume(TokenKind::LParen, "expected `(` after function name");
        if (!open_paren.has_value())
            return std::unexpected(open_paren.error());

        std::vector<ast::Param> params;
        std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> seen_params;

        if (!check(TokenKind::RParen)) {
            while (true) {
                auto param_name = consume_identifier("expected parameter name");
                if (!param_name.has_value())
                    return std::unexpected(param_name.error());

                if (!seen_params.insert(param_name->symbol).second) {
                    const std::string param_name_text = std::string(token_text(*param_name));
                    return std::unexpected(make_error_token(
                        *param_name, "duplicate parameter `" + param_name_text + "`"));
                }

                auto colon = consume(TokenKind::Colon, "expected `:` after parameter name");
                if (!colon.has_value())
                    return std::unexpected(colon.error());

                auto param_type = parse_type();
                if (!param_type.has_value())
                    return std::unexpected(param_type.error());

                params.push_back(
                    ast::Param{
                        .name = param_name->symbol,
                        .type = std::move(*param_type),
                        .span = merge_spans(param_name->span, previous().span),
                    });

                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RParen))
                        break;
                    continue;
                }
                break;
            }
        }

        auto close_paren = consume(TokenKind::RParen, "expected `)` after function parameters");
        if (!close_paren.has_value())
            return std::unexpected(close_paren.error());

        std::optional<ast::TypeRef> return_type;
        if (match(TokenKind::Arrow)) {
            auto parsed_return_type = parse_type();
            if (!parsed_return_type.has_value())
                return std::unexpected(parsed_return_type.error());
            return_type = std::move(*parsed_return_type);
        }

        auto where_clause = parse_where_clause();
        if (!where_clause.has_value())
            return std::unexpected(where_clause.error());

        auto body = parse_block_expr();
        if (!body.has_value())
            return std::unexpected(body.error());

        ast::FnDecl decl;
        decl.is_public = is_public;
        decl.name = name_token->symbol;
        decl.generic_params = std::move(generic_params);
        decl.params = std::move(params);
        decl.return_type = std::move(return_type);
        decl.where_clause = std::move(*where_clause);
        decl.body = *body;
        decl.attributes = std::move(attributes);
        decl.is_runtime = is_runtime;
        decl.span = merge_spans(item_lead_span(decl.attributes, lead_token), (*body)->span);

        return decl;
    }

    [[nodiscard]] std::expected<ast::Item, ParseError> parse_extern_decl(
        const Token& lead_token, std::vector<ast::Attribute> attributes, bool is_public,
        bool is_runtime) {
        static_cast<void>(advance()); // consume `extern`

        if (!check(TokenKind::String)) {
            return std::unexpected(
                make_error_here("expected ABI string after `extern` (e.g. `\"lang\"`)"));
        }
        const Token abi_token = advance();
        const cstc::symbol::Symbol abi = string_contents_symbol(abi_token);

        if (match(TokenKind::KwFn)) {
            auto decl =
                parse_extern_fn_decl(lead_token, abi, std::move(attributes), is_public, is_runtime);
            if (!decl.has_value())
                return std::unexpected(decl.error());
            return ast::Item{std::move(*decl)};
        }

        if (match(TokenKind::KwStruct)) {
            const Token struct_keyword = previous();
            if (is_runtime) {
                return std::unexpected(make_error_token(
                    struct_keyword, "expected `fn` after `runtime extern` ABI string"));
            }
            auto decl = parse_extern_struct_decl(lead_token, abi, std::move(attributes), is_public);
            if (!decl.has_value())
                return std::unexpected(decl.error());
            return ast::Item{std::move(*decl)};
        }

        if (is_runtime) {
            return std::unexpected(
                make_error_here("expected `fn` after `runtime extern` ABI string"));
        }
        return std::unexpected(
            make_error_here("expected `fn` or `struct` after extern ABI string"));
    }

    [[nodiscard]] std::expected<ast::ExternFnDecl, ParseError> parse_extern_fn_decl(
        const Token& lead_token, cstc::symbol::Symbol abi, std::vector<ast::Attribute> attributes,
        bool is_public, bool is_runtime) {
        auto name_token = consume_identifier("expected function name");
        if (!name_token.has_value())
            return std::unexpected(name_token.error());

        auto open_paren = consume(TokenKind::LParen, "expected `(` after function name");
        if (!open_paren.has_value())
            return std::unexpected(open_paren.error());

        std::vector<ast::Param> params;
        std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> seen_params;

        if (!check(TokenKind::RParen)) {
            while (true) {
                auto param_name = consume_identifier("expected parameter name");
                if (!param_name.has_value())
                    return std::unexpected(param_name.error());

                if (!seen_params.insert(param_name->symbol).second) {
                    const std::string param_name_text = std::string(token_text(*param_name));
                    return std::unexpected(make_error_token(
                        *param_name, "duplicate parameter `" + param_name_text + "`"));
                }

                auto colon = consume(TokenKind::Colon, "expected `:` after parameter name");
                if (!colon.has_value())
                    return std::unexpected(colon.error());

                auto param_type = parse_type();
                if (!param_type.has_value())
                    return std::unexpected(param_type.error());

                params.push_back(
                    ast::Param{
                        .name = param_name->symbol,
                        .type = std::move(*param_type),
                        .span = merge_spans(param_name->span, previous().span),
                    });

                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RParen))
                        break;
                    continue;
                }
                break;
            }
        }

        auto close_paren = consume(TokenKind::RParen, "expected `)` after function parameters");
        if (!close_paren.has_value())
            return std::unexpected(close_paren.error());

        std::optional<ast::TypeRef> return_type;
        if (match(TokenKind::Arrow)) {
            auto parsed_return_type = parse_type();
            if (!parsed_return_type.has_value())
                return std::unexpected(parsed_return_type.error());
            return_type = std::move(*parsed_return_type);
        }

        auto semi = consume(TokenKind::Semicolon, "expected `;` after extern fn declaration");
        if (!semi.has_value())
            return std::unexpected(semi.error());

        ast::ExternFnDecl decl;
        decl.is_public = is_public;
        decl.abi = abi;
        decl.name = name_token->symbol;
        decl.params = std::move(params);
        decl.return_type = std::move(return_type);
        decl.attributes = std::move(attributes);
        decl.is_runtime = is_runtime;
        decl.span = merge_spans(item_lead_span(decl.attributes, lead_token), semi->span);

        return decl;
    }

    [[nodiscard]] std::expected<ast::ExternStructDecl, ParseError> parse_extern_struct_decl(
        const Token& lead_token, cstc::symbol::Symbol abi, std::vector<ast::Attribute> attributes,
        bool is_public) {
        auto name_token = consume_identifier("expected struct name");
        if (!name_token.has_value())
            return std::unexpected(name_token.error());

        auto semi = consume(TokenKind::Semicolon, "expected `;` after extern struct declaration");
        if (!semi.has_value())
            return std::unexpected(semi.error());

        ast::ExternStructDecl decl;
        decl.is_public = is_public;
        decl.abi = abi;
        decl.name = name_token->symbol;
        decl.attributes = std::move(attributes);
        decl.span = merge_spans(item_lead_span(decl.attributes, lead_token), semi->span);

        return decl;
    }

    [[nodiscard]] std::expected<ast::ImportDecl, ParseError>
        parse_import_decl(const Token& lead_token, bool is_public) {
        auto open_brace = consume(TokenKind::LBrace, "expected `{` after `import`");
        if (!open_brace.has_value())
            return std::unexpected(open_brace.error());

        if (check(TokenKind::Star))
            return std::unexpected(make_error_here("`import *` is not supported"));

        ast::ImportDecl decl;
        decl.is_public = is_public;

        if (!check(TokenKind::RBrace)) {
            while (true) {
                auto item_name = consume_identifier("expected imported item name");
                if (!item_name.has_value())
                    return std::unexpected(item_name.error());

                ast::ImportItem item;
                item.name = item_name->symbol;
                item.span = item_name->span;

                if (match(TokenKind::KwAs)) {
                    auto alias = consume_identifier("expected alias name after `as`");
                    if (!alias.has_value())
                        return std::unexpected(alias.error());
                    item.alias = alias->symbol;
                    item.span = merge_spans(item.span, alias->span);
                }

                decl.items.push_back(std::move(item));

                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RBrace))
                        break;
                    continue;
                }
                break;
            }
        }

        auto close_brace = consume(TokenKind::RBrace, "expected `}` after import item list");
        if (!close_brace.has_value())
            return std::unexpected(close_brace.error());

        auto from_kw = consume(TokenKind::KwFrom, "expected `from` after import item list");
        if (!from_kw.has_value())
            return std::unexpected(from_kw.error());

        auto path_token = consume(TokenKind::String, "expected import path string after `from`");
        if (!path_token.has_value())
            return std::unexpected(path_token.error());

        auto semi = consume(TokenKind::Semicolon, "expected `;` after import declaration");
        if (!semi.has_value())
            return std::unexpected(semi.error());

        decl.path = string_contents_symbol(*path_token);
        decl.span = merge_spans(item_lead_span({}, lead_token), semi->span);
        return decl;
    }

    [[nodiscard]] std::expected<std::vector<ast::Attribute>, ParseError> parse_attributes() {
        std::vector<ast::Attribute> attributes;
        while (check(TokenKind::LBracket)) {
            if (!check_attribute_start()) {
                return std::unexpected(make_error_here("expected second `[` to start attribute"));
            }
            auto attribute = parse_attribute();
            if (!attribute.has_value())
                return std::unexpected(attribute.error());
            attributes.push_back(std::move(*attribute));
        }
        return attributes;
    }

    [[nodiscard]] std::expected<ast::Attribute, ParseError> parse_attribute() {
        auto open_0 = consume(TokenKind::LBracket, "expected `[[` to start attribute");
        if (!open_0.has_value())
            return std::unexpected(open_0.error());

        auto open_1 = consume(TokenKind::LBracket, "expected `[[` to start attribute");
        if (!open_1.has_value())
            return std::unexpected(open_1.error());

        auto name_token = consume_identifier("expected attribute name");
        if (!name_token.has_value())
            return std::unexpected(name_token.error());

        std::optional<cstc::symbol::Symbol> value;
        if (match(TokenKind::Assign)) {
            auto value_token =
                consume(TokenKind::String, "expected string literal after `=` in attribute");
            if (!value_token.has_value())
                return std::unexpected(value_token.error());
            value = string_contents_symbol(*value_token);
        }

        auto close_0 = consume(TokenKind::RBracket, "expected `]]` to close attribute");
        if (!close_0.has_value())
            return std::unexpected(close_0.error());

        auto close_1 = consume(TokenKind::RBracket, "expected `]]` to close attribute");
        if (!close_1.has_value())
            return std::unexpected(close_1.error());

        return ast::Attribute{
            .name = name_token->symbol,
            .value = std::move(value),
            .span = merge_spans(open_0->span, close_1->span),
        };
    }

    [[nodiscard]] std::expected<ast::TypeRef, ParseError> parse_type() {
        if (match(TokenKind::KwRuntime)) {
            if (check(TokenKind::KwRuntime)) {
                return std::unexpected(make_error_here("duplicate `runtime` type qualifier"));
            }
            auto inner = parse_type();
            if (!inner.has_value())
                return std::unexpected(inner.error());

            inner->is_runtime = true;
            return std::move(*inner);
        }

        if (match(TokenKind::Amp)) {
            auto inner = parse_type();
            if (!inner.has_value())
                return std::unexpected(inner.error());

            return ast::TypeRef{
                .kind = ast::TypeKind::Ref,
                .symbol = cstc::symbol::kInvalidSymbol,
                .display_name = cstc::symbol::kInvalidSymbol,
                .pointee = std::make_shared<ast::TypeRef>(std::move(*inner)),
                .generic_args = {},
            };
        }

        if (match(TokenKind::KwUnit))
            return ast::TypeRef{
                .kind = ast::TypeKind::Unit,
                .symbol = previous().symbol,
                .display_name = previous().symbol,
                .pointee = nullptr,
                .generic_args = {},
            };
        if (match(TokenKind::KwNum))
            return ast::TypeRef{
                .kind = ast::TypeKind::Num,
                .symbol = previous().symbol,
                .display_name = previous().symbol,
                .pointee = nullptr,
                .generic_args = {},
            };
        if (match(TokenKind::KwStr))
            return ast::TypeRef{
                .kind = ast::TypeKind::Str,
                .symbol = previous().symbol,
                .display_name = previous().symbol,
                .pointee = nullptr,
                .generic_args = {},
            };
        if (match(TokenKind::KwBool))
            return ast::TypeRef{
                .kind = ast::TypeKind::Bool,
                .symbol = previous().symbol,
                .display_name = previous().symbol,
                .pointee = nullptr,
                .generic_args = {},
            };
        if (match(TokenKind::Bang))
            return ast::TypeRef{
                .kind = ast::TypeKind::Never,
                .symbol = previous().symbol,
                .display_name = previous().symbol,
                .pointee = nullptr,
                .generic_args = {},
            };

        auto identifier = consume_identifier("expected type name");
        if (!identifier.has_value())
            return std::unexpected(identifier.error());

        std::vector<ast::TypeRef> generic_args;
        if (check(TokenKind::Lt)) {
            auto parsed_generic_args = parse_generic_argument_list();
            if (!parsed_generic_args.has_value())
                return std::unexpected(parsed_generic_args.error());
            generic_args = std::move(*parsed_generic_args);
        }

        return ast::TypeRef{
            .kind = ast::TypeKind::Named,
            .symbol = identifier->symbol,
            .display_name = identifier->symbol,
            .pointee = nullptr,
            .generic_args = std::move(generic_args),
        };
    }

    [[nodiscard]] std::expected<ParsedPattern, ParseError> parse_let_pattern() {
        auto identifier = consume_identifier("expected binding name or `_`");
        if (!identifier.has_value())
            return std::unexpected(identifier.error());

        ParsedPattern pattern;
        pattern.span = identifier->span;
        if (token_text(*identifier) == "_") {
            pattern.discard = true;
            return pattern;
        }

        pattern.discard = false;
        pattern.name = identifier->symbol;
        return pattern;
    }

    [[nodiscard]] std::expected<ast::LetStmt, ParseError>
        parse_let_stmt_no_semicolon(const Token& let_kw) {
        auto pattern = parse_let_pattern();
        if (!pattern.has_value())
            return std::unexpected(pattern.error());

        std::optional<ast::TypeRef> type_annotation;
        if (match(TokenKind::Colon)) {
            auto parsed_type = parse_type();
            if (!parsed_type.has_value())
                return std::unexpected(parsed_type.error());
            type_annotation = std::move(*parsed_type);
        }

        auto assign = consume(TokenKind::Assign, "expected `=` in let binding");
        if (!assign.has_value())
            return std::unexpected(assign.error());

        auto initializer = parse_expression();
        if (!initializer.has_value())
            return std::unexpected(initializer.error());

        return ast::LetStmt{
            .discard = pattern->discard,
            .name = pattern->name,
            .type_annotation = std::move(type_annotation),
            .initializer = *initializer,
            .span = merge_spans(let_kw.span, (*initializer)->span),
        };
    }

    [[nodiscard]] std::expected<ast::BlockPtr, ParseError> parse_block_expr() {
        auto open = consume(TokenKind::LBrace, "expected `{` to start block");
        if (!open.has_value())
            return std::unexpected(open.error());

        auto block = std::make_shared<ast::BlockExpr>();
        block->span = open->span;

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            if (match(TokenKind::KwLet)) {
                const Token let_kw = previous();
                auto let_stmt = parse_let_stmt_no_semicolon(let_kw);
                if (!let_stmt.has_value())
                    return std::unexpected(let_stmt.error());

                auto semi = consume(TokenKind::Semicolon, "expected `;` after let statement");
                if (!semi.has_value())
                    return std::unexpected(semi.error());

                let_stmt->span = merge_spans(let_stmt->span, semi->span);
                block->statements.emplace_back(std::move(*let_stmt));
                continue;
            }

            auto expr = parse_expression();
            if (!expr.has_value())
                return std::unexpected(expr.error());

            if (match(TokenKind::Semicolon)) {
                block->statements.emplace_back(
                    ast::ExprStmt{
                        .expr = *expr,
                        .span = merge_spans((*expr)->span, previous().span),
                    });
                continue;
            }

            if (is_semicolon_optional_stmt_expr(*expr) && !check(TokenKind::RBrace)) {
                block->statements.emplace_back(
                    ast::ExprStmt{
                        .expr = *expr,
                        .span = (*expr)->span,
                    });
                continue;
            }

            if (!check(TokenKind::RBrace)) {
                return std::unexpected(make_error_here("expected `;` or `}` after expression"));
            }

            block->tail = *expr;
            break;
        }

        auto close = consume(TokenKind::RBrace, "expected `}` to close block");
        if (!close.has_value())
            return std::unexpected(close.error());

        block->span = merge_spans(open->span, close->span);
        return block;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_expression() {
        return parse_logical_or();
    }

    [[nodiscard]] bool condition_starts_with_struct_init_ambiguity() const {
        if (!check(TokenKind::Identifier) || peek(1).kind != TokenKind::LBrace)
            return false;

        if (peek(2).kind == TokenKind::RBrace)
            return true;

        if (peek(2).kind != TokenKind::Identifier)
            return false;

        return peek(3).kind == TokenKind::Colon;
    }

    /// Parses an `if`/`while` condition, resolving only the leading `ident { }`
    /// ambiguity in favor of the following block.
    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_condition_expression() {
        if (!condition_starts_with_struct_init_ambiguity())
            return parse_expression();

        auto identifier = consume_identifier("expected condition expression");
        if (!identifier.has_value())
            return std::unexpected(identifier.error());

        return ast::make_expr(
            identifier->span, ast::PathExpr{
                                  .head = identifier->symbol,
                                  .tail = std::nullopt,
                                  .display_head = identifier->symbol,
                                  .generic_args = {},
                              });
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_logical_or() {
        auto lhs = parse_logical_and();
        if (!lhs.has_value())
            return std::unexpected(lhs.error());

        while (match(TokenKind::OrOr)) {
            auto rhs = parse_logical_and();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            *lhs = ast::make_expr(
                merge_spans((*lhs)->span, (*rhs)->span),
                ast::BinaryExpr{.op = ast::BinaryOp::Or, .lhs = *lhs, .rhs = *rhs});
        }

        return lhs;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_logical_and() {
        auto lhs = parse_equality();
        if (!lhs.has_value())
            return std::unexpected(lhs.error());

        while (match(TokenKind::AndAnd)) {
            auto rhs = parse_equality();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            *lhs = ast::make_expr(
                merge_spans((*lhs)->span, (*rhs)->span),
                ast::BinaryExpr{.op = ast::BinaryOp::And, .lhs = *lhs, .rhs = *rhs});
        }

        return lhs;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_equality() {
        auto lhs = parse_relational();
        if (!lhs.has_value())
            return std::unexpected(lhs.error());

        while (check(TokenKind::EqEq) || check(TokenKind::NotEq)) {
            const Token op = advance();
            auto rhs = parse_relational();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            const ast::BinaryOp ast_op =
                op.kind == TokenKind::EqEq ? ast::BinaryOp::Eq : ast::BinaryOp::Ne;

            *lhs = ast::make_expr(
                merge_spans((*lhs)->span, (*rhs)->span),
                ast::BinaryExpr{.op = ast_op, .lhs = *lhs, .rhs = *rhs});
        }

        return lhs;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_relational() {
        auto lhs = parse_additive();
        if (!lhs.has_value())
            return std::unexpected(lhs.error());

        while (check(TokenKind::Lt) || check(TokenKind::LtEq) || check(TokenKind::Gt)
               || check(TokenKind::GtEq)) {
            const Token op = advance();
            auto rhs = parse_additive();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            ast::BinaryOp ast_op = ast::BinaryOp::Lt;
            switch (op.kind) {
            case TokenKind::Lt: ast_op = ast::BinaryOp::Lt; break;
            case TokenKind::LtEq: ast_op = ast::BinaryOp::Le; break;
            case TokenKind::Gt: ast_op = ast::BinaryOp::Gt; break;
            case TokenKind::GtEq: ast_op = ast::BinaryOp::Ge; break;
            default: break;
            }

            *lhs = ast::make_expr(
                merge_spans((*lhs)->span, (*rhs)->span),
                ast::BinaryExpr{.op = ast_op, .lhs = *lhs, .rhs = *rhs});
        }

        return lhs;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_additive() {
        auto lhs = parse_multiplicative();
        if (!lhs.has_value())
            return std::unexpected(lhs.error());

        while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
            const Token op = advance();
            auto rhs = parse_multiplicative();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            const ast::BinaryOp ast_op =
                op.kind == TokenKind::Plus ? ast::BinaryOp::Add : ast::BinaryOp::Sub;

            *lhs = ast::make_expr(
                merge_spans((*lhs)->span, (*rhs)->span),
                ast::BinaryExpr{.op = ast_op, .lhs = *lhs, .rhs = *rhs});
        }

        return lhs;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_multiplicative() {
        auto lhs = parse_unary();
        if (!lhs.has_value())
            return std::unexpected(lhs.error());

        while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
            const Token op = advance();
            auto rhs = parse_unary();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            ast::BinaryOp ast_op = ast::BinaryOp::Mul;
            switch (op.kind) {
            case TokenKind::Star: ast_op = ast::BinaryOp::Mul; break;
            case TokenKind::Slash: ast_op = ast::BinaryOp::Div; break;
            case TokenKind::Percent: ast_op = ast::BinaryOp::Mod; break;
            default: break;
            }

            *lhs = ast::make_expr(
                merge_spans((*lhs)->span, (*rhs)->span),
                ast::BinaryExpr{.op = ast_op, .lhs = *lhs, .rhs = *rhs});
        }

        return lhs;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_unary() {
        if (match(TokenKind::Amp)) {
            const Token op = previous();
            auto rhs = parse_unary();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            return ast::make_expr(
                merge_spans(op.span, (*rhs)->span),
                ast::UnaryExpr{.op = ast::UnaryOp::Borrow, .rhs = *rhs});
        }

        if (match(TokenKind::Bang)) {
            const Token op = previous();
            auto rhs = parse_unary();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            return ast::make_expr(
                merge_spans(op.span, (*rhs)->span),
                ast::UnaryExpr{.op = ast::UnaryOp::Not, .rhs = *rhs});
        }

        if (match(TokenKind::Minus)) {
            const Token op = previous();
            auto rhs = parse_unary();
            if (!rhs.has_value())
                return std::unexpected(rhs.error());

            return ast::make_expr(
                merge_spans(op.span, (*rhs)->span),
                ast::UnaryExpr{.op = ast::UnaryOp::Negate, .rhs = *rhs});
        }

        return parse_postfix();
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_postfix() {
        auto expr = parse_primary();
        if (!expr.has_value())
            return std::unexpected(expr.error());

        while (true) {
            if (!is_postfixable(*expr))
                break;

            if (looks_like_turbofish()) {
                const Token turbofish = advance();
                static_cast<void>(turbofish);
                auto generic_args = parse_generic_argument_list();
                if (!generic_args.has_value())
                    return std::unexpected(generic_args.error());

                const cstc::span::SourceSpan end_span = previous().span;
                *expr = ast::make_expr(
                    merge_spans((*expr)->span, end_span),
                    ast::GenericAppExpr{.callee = *expr, .args = std::move(*generic_args)});
                continue;
            }

            if (match(TokenKind::Dot)) {
                auto field = consume_identifier("expected field name after `.`");
                if (!field.has_value())
                    return std::unexpected(field.error());

                *expr = ast::make_expr(
                    merge_spans((*expr)->span, field->span),
                    ast::FieldAccessExpr{.base = *expr, .field = field->symbol});
                continue;
            }

            if (match(TokenKind::LParen)) {
                std::vector<ast::ExprPtr> args;

                if (!check(TokenKind::RParen)) {
                    while (true) {
                        auto arg = parse_expression();
                        if (!arg.has_value())
                            return std::unexpected(arg.error());
                        args.push_back(*arg);

                        if (match(TokenKind::Comma)) {
                            if (check(TokenKind::RParen))
                                break;
                            continue;
                        }
                        break;
                    }
                }

                auto close = consume(TokenKind::RParen, "expected `)` after call arguments");
                if (!close.has_value())
                    return std::unexpected(close.error());

                if (const auto* callee_path = std::get_if<ast::PathExpr>(&(*expr)->node);
                    callee_path != nullptr && !callee_path->tail.has_value()
                    && callee_path->head == cstc::symbol::Symbol::intern("decl")
                    && callee_path->generic_args.empty()) {
                    if (args.size() != 1) {
                        return std::unexpected(
                            make_error_token(*close, "`decl` expects exactly 1 argument"));
                    }

                    *expr = ast::make_expr(
                        merge_spans((*expr)->span, close->span),
                        ast::DeclExpr{.expr = std::move(args.front())});
                } else {
                    *expr = ast::make_expr(
                        merge_spans((*expr)->span, close->span),
                        ast::CallExpr{.callee = *expr, .args = std::move(args)});
                }
                continue;
            }

            break;
        }

        return expr;
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_if_expr(const Token& if_kw) {
        auto condition = parse_condition_expression();
        if (!condition.has_value())
            return std::unexpected(condition.error());

        auto then_block = parse_block_expr();
        if (!then_block.has_value())
            return std::unexpected(then_block.error());

        std::optional<ast::ExprPtr> else_branch;
        cstc::span::SourceSpan end_span = (*then_block)->span;

        if (match(TokenKind::KwElse)) {
            if (match(TokenKind::KwIf)) {
                auto nested_if = parse_if_expr(previous());
                if (!nested_if.has_value())
                    return std::unexpected(nested_if.error());
                end_span = (*nested_if)->span;
                else_branch = *nested_if;
            } else {
                auto else_block = parse_block_expr();
                if (!else_block.has_value())
                    return std::unexpected(else_block.error());
                end_span = (*else_block)->span;
                else_branch = ast::make_expr((*else_block)->span, *else_block);
            }
        }

        return ast::make_expr(
            merge_spans(if_kw.span, end_span), ast::IfExpr{
                                                   .condition = *condition,
                                                   .then_block = *then_block,
                                                   .else_branch = std::move(else_branch),
                                               });
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_for_expr(const Token& for_kw) {
        auto open = consume(TokenKind::LParen, "expected `(` after `for`");
        if (!open.has_value())
            return std::unexpected(open.error());

        ast::ForExpr for_node;

        if (!check(TokenKind::Semicolon)) {
            if (match(TokenKind::KwLet)) {
                const Token let_kw = previous();
                auto let_stmt = parse_let_stmt_no_semicolon(let_kw);
                if (!let_stmt.has_value())
                    return std::unexpected(let_stmt.error());

                for_node.init = ast::ForInitLet{
                    .discard = let_stmt->discard,
                    .name = let_stmt->name,
                    .type_annotation = std::move(let_stmt->type_annotation),
                    .initializer = let_stmt->initializer,
                    .span = let_stmt->span,
                };
            } else {
                auto init_expr = parse_expression();
                if (!init_expr.has_value())
                    return std::unexpected(init_expr.error());
                for_node.init = *init_expr;
            }
        }

        auto semi_0 = consume(TokenKind::Semicolon, "expected `;` after for-loop initializer");
        if (!semi_0.has_value())
            return std::unexpected(semi_0.error());

        if (!check(TokenKind::Semicolon)) {
            auto condition = parse_expression();
            if (!condition.has_value())
                return std::unexpected(condition.error());
            for_node.condition = *condition;
        }

        auto semi_1 = consume(TokenKind::Semicolon, "expected second `;` in for-loop header");
        if (!semi_1.has_value())
            return std::unexpected(semi_1.error());

        if (!check(TokenKind::RParen)) {
            auto step = parse_expression();
            if (!step.has_value())
                return std::unexpected(step.error());
            for_node.step = *step;
        }

        auto close = consume(TokenKind::RParen, "expected `)` after for-loop header");
        if (!close.has_value())
            return std::unexpected(close.error());

        auto body = parse_block_expr();
        if (!body.has_value())
            return std::unexpected(body.error());
        for_node.body = *body;

        return ast::make_expr(merge_spans(for_kw.span, (*body)->span), std::move(for_node));
    }

    [[nodiscard]] std::expected<ast::ExprPtr, ParseError> parse_primary() {
        if (match(TokenKind::Number)) {
            const Token token = previous();
            return ast::make_expr(
                token.span, ast::LiteralExpr{
                                .kind = ast::LiteralExpr::Kind::Num,
                                .symbol = token.symbol,
                            });
        }

        if (match(TokenKind::String)) {
            const Token token = previous();
            return ast::make_expr(
                token.span, ast::LiteralExpr{
                                .kind = ast::LiteralExpr::Kind::Str,
                                .symbol = token.symbol,
                            });
        }

        if (match(TokenKind::KwTrue)) {
            const Token token = previous();
            return ast::make_expr(
                token.span, ast::LiteralExpr{
                                .kind = ast::LiteralExpr::Kind::Bool,
                                .symbol = token.symbol,
                                .bool_value = true,
                            });
        }

        if (match(TokenKind::KwFalse)) {
            const Token token = previous();
            return ast::make_expr(
                token.span, ast::LiteralExpr{
                                .kind = ast::LiteralExpr::Kind::Bool,
                                .symbol = token.symbol,
                                .bool_value = false,
                            });
        }

        if (match(TokenKind::LParen)) {
            const Token open = previous();
            if (match(TokenKind::RParen)) {
                return ast::make_expr(
                    merge_spans(open.span, previous().span),
                    ast::LiteralExpr{
                        .kind = ast::LiteralExpr::Kind::Unit,
                        .symbol = cstc::symbol::kw::UnitLit,
                    });
            }

            auto inner = parse_expression();
            if (!inner.has_value())
                return std::unexpected(inner.error());

            auto close =
                consume(TokenKind::RParen, "expected `)` to close parenthesized expression");
            if (!close.has_value())
                return std::unexpected(close.error());

            return *inner;
        }

        if (check(TokenKind::LBrace)) {
            auto block = parse_block_expr();
            if (!block.has_value())
                return std::unexpected(block.error());
            return ast::make_expr((*block)->span, *block);
        }

        if (match(TokenKind::KwIf))
            return parse_if_expr(previous());

        if (match(TokenKind::KwLoop)) {
            const Token loop_kw = previous();
            auto body = parse_block_expr();
            if (!body.has_value())
                return std::unexpected(body.error());
            return ast::make_expr(
                merge_spans(loop_kw.span, (*body)->span), ast::LoopExpr{.body = *body});
        }

        if (match(TokenKind::KwWhile)) {
            const Token while_kw = previous();
            auto condition = parse_condition_expression();
            if (!condition.has_value())
                return std::unexpected(condition.error());

            auto body = parse_block_expr();
            if (!body.has_value())
                return std::unexpected(body.error());

            return ast::make_expr(
                merge_spans(while_kw.span, (*body)->span),
                ast::WhileExpr{.condition = *condition, .body = *body});
        }

        if (match(TokenKind::KwFor))
            return parse_for_expr(previous());

        if (match(TokenKind::KwBreak)) {
            const Token break_kw = previous();
            std::optional<ast::ExprPtr> value;

            if (!is_optional_expr_terminator(peek().kind)) {
                auto expr = parse_expression();
                if (!expr.has_value())
                    return std::unexpected(expr.error());
                value = *expr;
            }

            const cstc::span::SourceSpan span =
                value.has_value() ? merge_spans(break_kw.span, (*value)->span) : break_kw.span;
            return ast::make_expr(span, ast::BreakExpr{.value = std::move(value)});
        }

        if (match(TokenKind::KwContinue)) {
            const Token continue_kw = previous();
            return ast::make_expr(continue_kw.span, ast::ContinueExpr{});
        }

        if (match(TokenKind::KwReturn)) {
            const Token return_kw = previous();
            std::optional<ast::ExprPtr> value;

            if (!is_optional_expr_terminator(peek().kind)) {
                auto expr = parse_expression();
                if (!expr.has_value())
                    return std::unexpected(expr.error());
                value = *expr;
            }

            const cstc::span::SourceSpan span =
                value.has_value() ? merge_spans(return_kw.span, (*value)->span) : return_kw.span;
            return ast::make_expr(span, ast::ReturnExpr{.value = std::move(value)});
        }

        if (match(TokenKind::Identifier)) {
            const Token identifier = previous();

            std::vector<ast::TypeRef> direct_generic_args;
            if (check(TokenKind::Lt) && looks_like_generic_struct_initializer()) {
                auto parsed_generic_args = parse_generic_argument_list();
                if (!parsed_generic_args.has_value())
                    return std::unexpected(parsed_generic_args.error());
                direct_generic_args = std::move(*parsed_generic_args);
            }

            if (check(TokenKind::ColonColon) && peek(1).kind != TokenKind::Lt) {
                static_cast<void>(advance());
                auto variant = consume_identifier("expected enum variant name after `::`");
                if (!variant.has_value())
                    return std::unexpected(variant.error());

                return ast::make_expr(
                    merge_spans(identifier.span, variant->span),
                    ast::PathExpr{
                        .head = identifier.symbol,
                        .tail = variant->symbol,
                        .display_head = identifier.symbol,
                        .generic_args = std::move(direct_generic_args),
                    });
            }

            if (looks_like_struct_initializer()) {
                auto open = consume(TokenKind::LBrace, "expected `{` in struct initializer");
                if (!open.has_value())
                    return std::unexpected(open.error());

                ast::StructInitExpr init_expr;
                init_expr.type_name = identifier.symbol;
                init_expr.display_name = identifier.symbol;
                init_expr.generic_args = std::move(direct_generic_args);

                std::unordered_set<cstc::symbol::Symbol, cstc::symbol::SymbolHash> seen_fields;

                if (!check(TokenKind::RBrace)) {
                    while (true) {
                        auto field_name = consume_identifier("expected struct field name");
                        if (!field_name.has_value())
                            return std::unexpected(field_name.error());

                        if (!seen_fields.insert(field_name->symbol).second) {
                            const std::string field_name_text =
                                std::string(token_text(*field_name));
                            return std::unexpected(make_error_token(
                                *field_name,
                                "duplicate struct field `" + field_name_text + "` in initializer"));
                        }

                        auto colon = consume(TokenKind::Colon, "expected `:` after field name");
                        if (!colon.has_value())
                            return std::unexpected(colon.error());

                        auto field_expr = parse_expression();
                        if (!field_expr.has_value())
                            return std::unexpected(field_expr.error());

                        init_expr.fields.push_back(
                            ast::StructInitField{
                                .name = field_name->symbol,
                                .value = *field_expr,
                                .span = merge_spans(field_name->span, (*field_expr)->span),
                            });

                        if (match(TokenKind::Comma)) {
                            if (check(TokenKind::RBrace))
                                break;
                            continue;
                        }
                        break;
                    }
                }

                auto close = consume(TokenKind::RBrace, "expected `}` to close struct initializer");
                if (!close.has_value())
                    return std::unexpected(close.error());

                return ast::make_expr(
                    merge_spans(identifier.span, close->span), std::move(init_expr));
            }

            return ast::make_expr(
                identifier.span, ast::PathExpr{
                                     .head = identifier.symbol,
                                     .tail = std::nullopt,
                                     .display_head = identifier.symbol,
                                     .generic_args = std::move(direct_generic_args),
                                 });
        }

        return std::unexpected(make_error_here("expected expression"));
    }

    std::vector<Token> tokens_;
    std::size_t cursor_ = 0;
};

} // namespace

std::expected<ast::Program, ParseError> parse_tokens(std::span<const lexer::Token> tokens) {
    std::vector<lexer::Token> filtered;
    filtered.reserve(tokens.size());

    for (const lexer::Token& token : tokens) {
        if (!lexer::is_trivia(token.kind))
            filtered.push_back(token);
    }

    Parser parser(filtered);
    return parser.parse_program();
}

std::expected<ast::Program, ParseError>
    parse_source_at(std::string_view source, cstc::span::BytePos base_pos) {
    const std::vector<lexer::Token> tokens = lexer::lex_source_at(source, base_pos, false);
    return parse_tokens(tokens);
}

std::expected<ast::Program, ParseError> parse_source(std::string_view source) {
    return parse_source_at(source, 0);
}

} // namespace cstc::parser
