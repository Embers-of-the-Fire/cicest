#include <cstc_hir_interpreter/interpreter.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cstc::hir::interpreter {

namespace {

struct RawNode;
using RawNodePtr = std::unique_ptr<RawNode>;

Value make_unit_value() { return Value{.kind = UnitValue{}}; }

[[nodiscard]] bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error)
            return true;
    }
    return false;
}

void push_diagnostic(std::vector<Diagnostic>& diagnostics, DiagnosticSeverity severity,
    std::string scope, std::string message) {
    diagnostics.push_back(Diagnostic{
        .severity = severity,
        .scope = std::move(scope),
        .message = std::move(message),
    });
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
        ++start;

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
        --end;

    return std::string{text.substr(start, end - start)};
}

[[nodiscard]] bool starts_with_keyword(std::string_view text, std::string_view keyword) {
    if (!text.starts_with(keyword))
        return false;
    if (text.size() == keyword.size())
        return true;

    const char next = text[keyword.size()];
    if (std::isspace(static_cast<unsigned char>(next)) != 0)
        return true;

    return next == '(' || next == '{' || next == '[';
}

[[nodiscard]] std::string join_segments(
    const std::vector<std::string>& segments, std::string_view separator = "::") {
    std::string out;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index != 0)
            out += separator;
        out += segments[index];
    }
    return out;
}

[[nodiscard]] std::unordered_set<std::string> builtin_type_names() {
    return {
        "bool",
        "i8",
        "i16",
        "i32",
        "i64",
        "u8",
        "u16",
        "u32",
        "u64",
        "isize",
        "usize",
        "f32",
        "f64",
        "str",
        "String",
        "unit",
        "void",
        "()",
    };
}

[[nodiscard]] bool is_integer_type_name(std::string_view name) {
    return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "u8"
        || name == "u16" || name == "u32" || name == "u64" || name == "isize"
        || name == "usize";
}

[[nodiscard]] bool is_builtin_type_name(std::string_view name) {
    return builtin_type_names().contains(std::string{name});
}

[[nodiscard]] std::optional<std::int64_t> builtin_sizeof_type(std::string_view name) {
    if (name == "bool" || name == "i8" || name == "u8")
        return 1;
    if (name == "i16" || name == "u16")
        return 2;
    if (name == "i32" || name == "u32" || name == "f32")
        return 4;
    if (name == "i64" || name == "u64" || name == "f64" || name == "isize" || name == "usize")
        return 8;
    return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> parse_i64(std::string_view text) {
    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end)
        return std::nullopt;
    return value;
}

[[nodiscard]] std::optional<Value> parse_literal_value(std::string_view text) {
    const std::string trimmed = trim_copy(text);
    if (trimmed.empty())
        return std::nullopt;

    if (trimmed == "true")
        return Value{.kind = true};
    if (trimmed == "false")
        return Value{.kind = false};

    if (const auto integer = parse_i64(trimmed); integer.has_value())
        return Value{.kind = *integer};

    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        std::string value;
        value.reserve(trimmed.size() - 2);
        for (std::size_t index = 1; index + 1 < trimmed.size(); ++index) {
            if (trimmed[index] == '\\' && index + 2 < trimmed.size()) {
                ++index;
                switch (trimmed[index]) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(trimmed[index]); break;
                }
            } else {
                value.push_back(trimmed[index]);
            }
        }
        return Value{.kind = std::move(value)};
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> value_as_int(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value.kind))
        return *integer;
    return std::nullopt;
}

[[nodiscard]] std::optional<bool> value_as_bool(const Value& value) {
    if (const auto* boolean = std::get_if<bool>(&value.kind))
        return *boolean;
    if (const auto* integer = std::get_if<std::int64_t>(&value.kind))
        return *integer != 0;
    return std::nullopt;
}

[[nodiscard]] std::string value_runtime_type_name(const Value& value) {
    if (std::holds_alternative<std::int64_t>(value.kind))
        return "i32";
    if (std::holds_alternative<bool>(value.kind))
        return "bool";
    if (std::holds_alternative<std::string>(value.kind))
        return "str";
    if (const auto* object = std::get_if<ObjectValue>(&value.kind); object != nullptr)
        return object->type_name.empty() ? std::string{"object"} : object->type_name;
    if (std::holds_alternative<LambdaValue>(value.kind))
        return "lambda";
    if (std::holds_alternative<FunctionRefValue>(value.kind))
        return "fn";
    return "unit";
}

[[nodiscard]] bool value_matches_named_type(const Value& value, std::string_view type_name) {
    if (std::holds_alternative<std::int64_t>(value.kind))
        return is_integer_type_name(type_name);
    if (std::holds_alternative<bool>(value.kind))
        return type_name == "bool";
    if (std::holds_alternative<std::string>(value.kind))
        return type_name == "str" || type_name == "String";
    if (const auto* object = std::get_if<ObjectValue>(&value.kind); object != nullptr)
        return object->type_name == type_name;
    if (std::holds_alternative<LambdaValue>(value.kind))
        return type_name == "fn";
    if (std::holds_alternative<FunctionRefValue>(value.kind))
        return type_name == "fn";
    if (std::holds_alternative<UnitValue>(value.kind))
        return type_name == "unit" || type_name == "void" || type_name == "()";
    return false;
}

[[nodiscard]] std::string format_type_inline(const cstc::hir::Type& type) {
    return std::visit(
        [](const auto& kind) -> std::string {
            using Kind = std::decay_t<decltype(kind)>;

            if constexpr (std::is_same_v<Kind, cstc::hir::PathType>) {
                std::string out = join_segments(kind.segments);
                if (out.empty())
                    out = "<empty-type>";

                if (!kind.args.empty()) {
                    out += '<';
                    for (std::size_t index = 0; index < kind.args.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        if (kind.args[index] == nullptr)
                            out += "<missing-type>";
                        else
                            out += format_type_inline(*kind.args[index]);
                    }
                    out += '>';
                }
                return out;
            } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractType>) {
                const std::string prefix =
                    kind.kind == cstc::hir::TypeContractKind::Runtime ? "runtime" : "!runtime";
                if (kind.inner == nullptr)
                    return prefix + " <missing-type>";
                return prefix + " " + format_type_inline(*kind.inner);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::RefType>) {
                if (kind.inner == nullptr)
                    return "&<missing-type>";
                return "&" + format_type_inline(*kind.inner);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::FnPointerType>) {
                std::string out = "fn(";
                for (std::size_t index = 0; index < kind.params.size(); ++index) {
                    if (index != 0)
                        out += ", ";
                    if (kind.params[index] == nullptr)
                        out += "<missing-type>";
                    else
                        out += format_type_inline(*kind.params[index]);
                }
                out += ") -> ";
                if (kind.result == nullptr)
                    out += "<missing-type>";
                else
                    out += format_type_inline(*kind.result);
                return out;
            } else if constexpr (std::is_same_v<Kind, cstc::hir::InferredType>) {
                return "_";
            }

            return "<unknown-type>";
        },
        type.kind);
}

enum class RawTokenKind {
    End,
    Identifier,
    Number,
    StringLiteral,
    TrueLiteral,
    FalseLiteral,
    LParen,
    RParen,
    Comma,
    Dot,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Amp,
    Pipe,
    Caret,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    EqualEqual,
    BangEqual,
    AndAnd,
    OrOr,
    ShiftLeft,
    ShiftRight,
    ColonColon,
};

struct RawToken {
    RawTokenKind kind = RawTokenKind::End;
    std::string text;
};

[[nodiscard]] bool is_identifier_head(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

[[nodiscard]] bool is_identifier_part(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

class RawLexer {
public:
    explicit RawLexer(std::string_view text)
        : text_(text) {}

    [[nodiscard]] bool lex(std::vector<RawToken>& tokens, std::string& error) {
        while (!at_end()) {
            const char ch = peek();

            if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
                advance();
                continue;
            }

            if (is_identifier_head(ch)) {
                lex_identifier(tokens);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                lex_number(tokens);
                continue;
            }

            if (ch == '"') {
                if (!lex_string(tokens, error))
                    return false;
                continue;
            }

            if (!lex_symbol(tokens, error))
                return false;
        }

        tokens.push_back(RawToken{.kind = RawTokenKind::End, .text = {}});
        return true;
    }

private:
    [[nodiscard]] bool at_end() const { return index_ >= text_.size(); }

    [[nodiscard]] char peek(std::size_t offset = 0) const {
        if (index_ + offset >= text_.size())
            return '\0';
        return text_[index_ + offset];
    }

    char advance() {
        const char ch = peek();
        if (!at_end())
            ++index_;
        return ch;
    }

    void lex_identifier(std::vector<RawToken>& tokens) {
        const std::size_t start = index_;
        advance();
        while (!at_end() && is_identifier_part(peek()))
            advance();

        std::string text{text_.substr(start, index_ - start)};
        RawTokenKind kind = RawTokenKind::Identifier;
        if (text == "true")
            kind = RawTokenKind::TrueLiteral;
        else if (text == "false")
            kind = RawTokenKind::FalseLiteral;

        tokens.push_back(RawToken{.kind = kind, .text = std::move(text)});
    }

    void lex_number(std::vector<RawToken>& tokens) {
        const std::size_t start = index_;
        advance();
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0)
            advance();

        tokens.push_back(RawToken{
            .kind = RawTokenKind::Number,
            .text = std::string{text_.substr(start, index_ - start)},
        });
    }

    [[nodiscard]] bool lex_string(std::vector<RawToken>& tokens, std::string& error) {
        advance();
        std::string value;

        while (!at_end()) {
            const char ch = advance();
            if (ch == '"') {
                tokens.push_back(RawToken{.kind = RawTokenKind::StringLiteral, .text = std::move(value)});
                return true;
            }

            if (ch == '\\') {
                if (at_end()) {
                    error = "unterminated escape sequence";
                    return false;
                }

                const char escaped = advance();
                switch (escaped) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(escaped); break;
                }
                continue;
            }

            value.push_back(ch);
        }

        error = "unterminated string literal";
        return false;
    }

    [[nodiscard]] bool lex_symbol(std::vector<RawToken>& tokens, std::string& error) {
        const char first = advance();
        const char second = peek();

        switch (first) {
        case '(': tokens.push_back(RawToken{.kind = RawTokenKind::LParen, .text = "("}); return true;
        case ')': tokens.push_back(RawToken{.kind = RawTokenKind::RParen, .text = ")"}); return true;
        case ',': tokens.push_back(RawToken{.kind = RawTokenKind::Comma, .text = ","}); return true;
        case '.': tokens.push_back(RawToken{.kind = RawTokenKind::Dot, .text = "."}); return true;
        case '+': tokens.push_back(RawToken{.kind = RawTokenKind::Plus, .text = "+"}); return true;
        case '-': tokens.push_back(RawToken{.kind = RawTokenKind::Minus, .text = "-"}); return true;
        case '*': tokens.push_back(RawToken{.kind = RawTokenKind::Star, .text = "*"}); return true;
        case '/': tokens.push_back(RawToken{.kind = RawTokenKind::Slash, .text = "/"}); return true;
        case '%': tokens.push_back(RawToken{.kind = RawTokenKind::Percent, .text = "%"}); return true;
        case '^': tokens.push_back(RawToken{.kind = RawTokenKind::Caret, .text = "^"}); return true;
        case '!':
            if (second == '=') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::BangEqual, .text = "!="});
            } else {
                tokens.push_back(RawToken{.kind = RawTokenKind::Bang, .text = "!"});
            }
            return true;
        case '&':
            if (second == '&') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::AndAnd, .text = "&&"});
            } else {
                tokens.push_back(RawToken{.kind = RawTokenKind::Amp, .text = "&"});
            }
            return true;
        case '|':
            if (second == '|') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::OrOr, .text = "||"});
            } else {
                tokens.push_back(RawToken{.kind = RawTokenKind::Pipe, .text = "|"});
            }
            return true;
        case '=':
            if (second == '=') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::EqualEqual, .text = "=="});
                return true;
            }
            break;
        case '<':
            if (second == '=') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::LessEqual, .text = "<="});
            } else if (second == '<') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::ShiftLeft, .text = "<<"});
            } else {
                tokens.push_back(RawToken{.kind = RawTokenKind::Less, .text = "<"});
            }
            return true;
        case '>':
            if (second == '=') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::GreaterEqual, .text = ">="});
            } else if (second == '>') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::ShiftRight, .text = ">>"});
            } else {
                tokens.push_back(RawToken{.kind = RawTokenKind::Greater, .text = ">"});
            }
            return true;
        case ':':
            if (second == ':') {
                advance();
                tokens.push_back(RawToken{.kind = RawTokenKind::ColonColon, .text = "::"});
                return true;
            }
            break;
        default: break;
        }

        error = "unexpected character '" + std::string{first} + "'";
        return false;
    }

    std::string_view text_;
    std::size_t index_ = 0;
};

struct RawLiteralNode {
    Value value;
};

struct RawPathNode {
    std::vector<std::string> segments;
};

struct RawUnaryNode {
    std::string op;
    RawNodePtr operand;
};

struct RawBinaryNode {
    std::string op;
    RawNodePtr lhs;
    RawNodePtr rhs;
};

struct RawCallNode {
    std::vector<std::string> callee;
    std::vector<RawNodePtr> args;
};

struct RawMemberAccessNode {
    RawNodePtr receiver;
    std::string member;
};

struct RawMemberCallNode {
    RawNodePtr receiver;
    std::string member;
    std::vector<RawNodePtr> args;
};

using RawNodeKind =
    std::variant<RawLiteralNode, RawPathNode, RawUnaryNode, RawBinaryNode, RawCallNode,
        RawMemberAccessNode, RawMemberCallNode>;

struct RawNode {
    RawNodeKind kind;
};

[[nodiscard]] std::string token_kind_text(RawTokenKind kind) {
    switch (kind) {
    case RawTokenKind::End: return "<end>";
    case RawTokenKind::Identifier: return "identifier";
    case RawTokenKind::Number: return "number";
    case RawTokenKind::StringLiteral: return "string";
    case RawTokenKind::TrueLiteral: return "true";
    case RawTokenKind::FalseLiteral: return "false";
    case RawTokenKind::LParen: return "(";
    case RawTokenKind::RParen: return ")";
    case RawTokenKind::Comma: return ",";
    case RawTokenKind::Dot: return ".";
    case RawTokenKind::Plus: return "+";
    case RawTokenKind::Minus: return "-";
    case RawTokenKind::Star: return "*";
    case RawTokenKind::Slash: return "/";
    case RawTokenKind::Percent: return "%";
    case RawTokenKind::Bang: return "!";
    case RawTokenKind::Amp: return "&";
    case RawTokenKind::Pipe: return "|";
    case RawTokenKind::Caret: return "^";
    case RawTokenKind::Less: return "<";
    case RawTokenKind::Greater: return ">";
    case RawTokenKind::LessEqual: return "<=";
    case RawTokenKind::GreaterEqual: return ">=";
    case RawTokenKind::EqualEqual: return "==";
    case RawTokenKind::BangEqual: return "!=";
    case RawTokenKind::AndAnd: return "&&";
    case RawTokenKind::OrOr: return "||";
    case RawTokenKind::ShiftLeft: return "<<";
    case RawTokenKind::ShiftRight: return ">>";
    case RawTokenKind::ColonColon: return "::";
    }
    return "<token>";
}

[[nodiscard]] int binary_precedence(RawTokenKind kind) {
    switch (kind) {
    case RawTokenKind::OrOr: return 1;
    case RawTokenKind::AndAnd: return 2;
    case RawTokenKind::Pipe: return 3;
    case RawTokenKind::Caret: return 4;
    case RawTokenKind::Amp: return 5;
    case RawTokenKind::EqualEqual:
    case RawTokenKind::BangEqual: return 6;
    case RawTokenKind::Less:
    case RawTokenKind::LessEqual:
    case RawTokenKind::Greater:
    case RawTokenKind::GreaterEqual: return 7;
    case RawTokenKind::ShiftLeft:
    case RawTokenKind::ShiftRight: return 8;
    case RawTokenKind::Plus:
    case RawTokenKind::Minus: return 9;
    case RawTokenKind::Star:
    case RawTokenKind::Slash:
    case RawTokenKind::Percent: return 10;
    default: return 0;
    }
}

class RawParser {
public:
    explicit RawParser(std::vector<RawToken> tokens)
        : tokens_(std::move(tokens)) {}

    [[nodiscard]] bool parse(RawNodePtr& node, std::string& error) {
        node = parse_binary_expression(1, error);
        if (node == nullptr)
            return false;

        if (current().kind != RawTokenKind::End) {
            error = "unexpected token: " + token_kind_text(current().kind);
            return false;
        }

        return true;
    }

private:
    [[nodiscard]] const RawToken& current() const { return tokens_[index_]; }

    [[nodiscard]] const RawToken& peek_token(std::size_t offset = 0) const {
        const std::size_t target = index_ + offset;
        if (target >= tokens_.size())
            return tokens_.back();
        return tokens_[target];
    }

    const RawToken& advance() {
        const RawToken& token = current();
        if (index_ + 1 < tokens_.size())
            ++index_;
        return token;
    }

    [[nodiscard]] bool match(RawTokenKind kind) {
        if (current().kind != kind)
            return false;
        advance();
        return true;
    }

    [[nodiscard]] bool parse_turbofish_suffix(std::string& member, std::string& error) {
        if (current().kind != RawTokenKind::ColonColon || peek_token(1).kind != RawTokenKind::Less)
            return true;

        member += advance().text;

        int depth = 0;
        while (true) {
            if (current().kind == RawTokenKind::End) {
                error = "unterminated turbofish generic arguments";
                return false;
            }

            const RawToken token = advance();
            member += token.text;

            if (token.kind == RawTokenKind::Less) {
                ++depth;
                continue;
            }

            if (token.kind == RawTokenKind::Greater) {
                --depth;
                if (depth == 0)
                    return true;
                if (depth < 0) {
                    error = "malformed turbofish generic arguments";
                    return false;
                }
                continue;
            }

            if (token.kind == RawTokenKind::ShiftRight) {
                depth -= 2;
                if (depth == 0)
                    return true;
                if (depth < 0) {
                    error = "malformed turbofish generic arguments";
                    return false;
                }
            }
        }
    }

    [[nodiscard]] RawNodePtr parse_binary_expression(int min_precedence, std::string& error) {
        RawNodePtr lhs = parse_unary_expression(error);
        if (lhs == nullptr)
            return nullptr;

        while (true) {
            const RawTokenKind op = current().kind;
            const int precedence = binary_precedence(op);
            if (precedence < min_precedence)
                break;

            const std::string op_text = advance().text;
            RawNodePtr rhs = parse_binary_expression(precedence + 1, error);
            if (rhs == nullptr)
                return nullptr;

            lhs = std::make_unique<RawNode>(RawNode{
                .kind = RawBinaryNode{
                    .op = op_text,
                    .lhs = std::move(lhs),
                    .rhs = std::move(rhs),
                },
            });
        }

        return lhs;
    }

    [[nodiscard]] RawNodePtr parse_unary_expression(std::string& error) {
        const RawTokenKind kind = current().kind;
        if (kind == RawTokenKind::Minus || kind == RawTokenKind::Bang || kind == RawTokenKind::Amp
            || kind == RawTokenKind::Star) {
            const std::string op_text = advance().text;
            RawNodePtr operand = parse_unary_expression(error);
            if (operand == nullptr)
                return nullptr;

            return std::make_unique<RawNode>(RawNode{
                .kind = RawUnaryNode{
                    .op = op_text,
                    .operand = std::move(operand),
                },
            });
        }

        return parse_postfix_expression(error);
    }

    [[nodiscard]] RawNodePtr parse_postfix_expression(std::string& error) {
        RawNodePtr node = parse_primary_expression_base(error);
        if (node == nullptr)
            return nullptr;

        while (match(RawTokenKind::Dot)) {
            if (current().kind != RawTokenKind::Identifier) {
                error = "expected member name after '.'";
                return nullptr;
            }

            std::string member = advance().text;
            if (!parse_turbofish_suffix(member, error))
                return nullptr;

            if (!match(RawTokenKind::LParen)) {
                node = std::make_unique<RawNode>(RawNode{
                    .kind = RawMemberAccessNode{
                        .receiver = std::move(node),
                        .member = std::move(member),
                    },
                });
                continue;
            }

            std::vector<RawNodePtr> args;
            if (!match(RawTokenKind::RParen)) {
                while (true) {
                    RawNodePtr arg = parse_binary_expression(1, error);
                    if (arg == nullptr)
                        return nullptr;
                    args.push_back(std::move(arg));

                    if (match(RawTokenKind::RParen))
                        break;

                    if (!match(RawTokenKind::Comma)) {
                        error = "expected ',' or ')' in member call arguments";
                        return nullptr;
                    }
                }
            }

            node = std::make_unique<RawNode>(RawNode{
                .kind = RawMemberCallNode{
                    .receiver = std::move(node),
                    .member = std::move(member),
                    .args = std::move(args),
                },
            });
        }

        return node;
    }

    [[nodiscard]] RawNodePtr parse_primary_expression_base(std::string& error) {
        const RawToken token = current();
        switch (token.kind) {
        case RawTokenKind::Number: {
            advance();
            const auto value = parse_i64(token.text);
            if (!value.has_value()) {
                error = "invalid integer literal: " + token.text;
                return nullptr;
            }

            return std::make_unique<RawNode>(RawNode{
                .kind = RawLiteralNode{.value = Value{.kind = *value}},
            });
        }
        case RawTokenKind::TrueLiteral:
            advance();
            return std::make_unique<RawNode>(
                RawNode{.kind = RawLiteralNode{.value = Value{.kind = true}}});
        case RawTokenKind::FalseLiteral:
            advance();
            return std::make_unique<RawNode>(
                RawNode{.kind = RawLiteralNode{.value = Value{.kind = false}}});
        case RawTokenKind::StringLiteral:
            advance();
            return std::make_unique<RawNode>(
                RawNode{.kind = RawLiteralNode{.value = Value{.kind = token.text}}});
        case RawTokenKind::Identifier:
            return parse_path_or_call(error);
        case RawTokenKind::LParen: {
            advance();
            RawNodePtr expr = parse_binary_expression(1, error);
            if (expr == nullptr)
                return nullptr;
            if (!match(RawTokenKind::RParen)) {
                error = "expected ')'";
                return nullptr;
            }
            return expr;
        }
        default:
            error = "unexpected token in expression: " + token_kind_text(token.kind);
            return nullptr;
        }
    }

    [[nodiscard]] RawNodePtr parse_path_or_call(std::string& error) {
        std::vector<std::string> segments;
        segments.push_back(advance().text);

        while (current().kind == RawTokenKind::ColonColon) {
            if (peek_token(1).kind == RawTokenKind::Less) {
                std::string ignored_turbofish;
                if (!parse_turbofish_suffix(ignored_turbofish, error))
                    return nullptr;
                break;
            }

            advance();
            if (current().kind != RawTokenKind::Identifier) {
                error = "expected identifier after '::'";
                return nullptr;
            }
            segments.push_back(advance().text);
        }

        if (!match(RawTokenKind::LParen)) {
            return std::make_unique<RawNode>(RawNode{.kind = RawPathNode{.segments = std::move(segments)}});
        }

        std::vector<RawNodePtr> args;
        if (!match(RawTokenKind::RParen)) {
            while (true) {
                RawNodePtr arg = parse_binary_expression(1, error);
                if (arg == nullptr)
                    return nullptr;
                args.push_back(std::move(arg));

                if (match(RawTokenKind::RParen))
                    break;

                if (!match(RawTokenKind::Comma)) {
                    error = "expected ',' or ')' in call arguments";
                    return nullptr;
                }
            }
        }

        return std::make_unique<RawNode>(RawNode{
            .kind = RawCallNode{
                .callee = std::move(segments),
                .args = std::move(args),
            },
        });
    }

    std::vector<RawToken> tokens_;
    std::size_t index_ = 0;
};

[[nodiscard]] bool type_contains_not_runtime(const cstc::hir::Type& type) {
    return std::visit(
        [](const auto& kind) -> bool {
            using Kind = std::decay_t<decltype(kind)>;

            if constexpr (std::is_same_v<Kind, cstc::hir::ContractType>) {
                if (kind.kind == cstc::hir::TypeContractKind::NotRuntime)
                    return true;
                if (kind.inner == nullptr)
                    return false;
                return type_contains_not_runtime(*kind.inner);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::RefType>) {
                if (kind.inner == nullptr)
                    return false;
                return type_contains_not_runtime(*kind.inner);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::FnPointerType>) {
                if (kind.result != nullptr && type_contains_not_runtime(*kind.result))
                    return true;

                for (const auto& param : kind.params) {
                    if (param != nullptr && type_contains_not_runtime(*param))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Kind, cstc::hir::PathType>) {
                for (const auto& arg : kind.args) {
                    if (arg != nullptr && type_contains_not_runtime(*arg))
                        return true;
                }
                return false;
            }

            return false;
        },
        type.kind);
}

[[nodiscard]] const cstc::hir::Type* strip_not_runtime_contract(const cstc::hir::Type* type) {
    const cstc::hir::Type* current = type;
    while (current != nullptr) {
        const auto* contract = std::get_if<cstc::hir::ContractType>(&current->kind);
        if (contract == nullptr || contract->kind != cstc::hir::TypeContractKind::NotRuntime)
            break;
        current = contract->inner.get();
    }
    return current;
}

[[nodiscard]] bool type_is_runtime(const cstc::hir::Type& type) {
    const cstc::hir::Type* stripped = strip_not_runtime_contract(&type);
    if (stripped == nullptr)
        return false;

    const auto* contract = std::get_if<cstc::hir::ContractType>(&stripped->kind);
    return contract != nullptr && contract->kind == cstc::hir::TypeContractKind::Runtime;
}

void materialize_type_contracts(cstc::hir::Type& type) {
    if (auto* path = std::get_if<cstc::hir::PathType>(&type.kind); path != nullptr) {
        for (auto& arg : path->args) {
            if (arg != nullptr)
                materialize_type_contracts(*arg);
        }
        return;
    }

    if (auto* contract = std::get_if<cstc::hir::ContractType>(&type.kind); contract != nullptr) {
        if (contract->inner == nullptr) {
            type.kind = cstc::hir::InferredType{};
            return;
        }

        materialize_type_contracts(*contract->inner);

        if (contract->kind == cstc::hir::TypeContractKind::NotRuntime) {
            cstc::hir::Type inner = std::move(*contract->inner);
            type = std::move(inner);
            materialize_type_contracts(type);
            return;
        }

        while (contract->inner != nullptr) {
            auto* nested = std::get_if<cstc::hir::ContractType>(&contract->inner->kind);
            if (nested == nullptr || nested->kind != cstc::hir::TypeContractKind::Runtime
                || nested->inner == nullptr) {
                break;
            }
            contract->inner = std::move(nested->inner);
        }
        return;
    }

    if (auto* ref = std::get_if<cstc::hir::RefType>(&type.kind); ref != nullptr) {
        if (ref->inner == nullptr) {
            type.kind = cstc::hir::InferredType{};
            return;
        }
        materialize_type_contracts(*ref->inner);
        return;
    }

    if (auto* fn = std::get_if<cstc::hir::FnPointerType>(&type.kind); fn != nullptr) {
        for (auto& param : fn->params) {
            if (param != nullptr)
                materialize_type_contracts(*param);
        }

        if (fn->result != nullptr)
            materialize_type_contracts(*fn->result);
    }
}

void materialize_expression_types(cstc::hir::Expr& expr) {
    std::visit(
        [](auto& kind) {
            using Kind = std::decay_t<decltype(kind)>;

            if constexpr (std::is_same_v<Kind, cstc::hir::BinaryExpr>) {
                if (kind.lhs != nullptr)
                    materialize_expression_types(*kind.lhs);
                if (kind.rhs != nullptr)
                    materialize_expression_types(*kind.rhs);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::CallExpr>) {
                if (kind.callee != nullptr)
                    materialize_expression_types(*kind.callee);
                for (auto& arg : kind.args) {
                    if (arg != nullptr)
                        materialize_expression_types(*arg);
                }
            } else if constexpr (std::is_same_v<Kind, cstc::hir::MemberAccessExpr>) {
                if (kind.receiver != nullptr)
                    materialize_expression_types(*kind.receiver);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::MemberCallExpr>) {
                if (kind.receiver != nullptr)
                    materialize_expression_types(*kind.receiver);
                for (auto& arg : kind.args) {
                    if (arg != nullptr)
                        materialize_expression_types(*arg);
                }
            } else if constexpr (std::is_same_v<Kind, cstc::hir::StaticMemberAccessExpr>) {
                materialize_type_contracts(kind.receiver_type);
                if (kind.receiver != nullptr)
                    materialize_expression_types(*kind.receiver);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::StaticMemberCallExpr>) {
                materialize_type_contracts(kind.receiver_type);
                if (kind.receiver != nullptr)
                    materialize_expression_types(*kind.receiver);
                for (auto& arg : kind.args) {
                    if (arg != nullptr)
                        materialize_expression_types(*arg);
                }
            } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractBlockExpr>) {
                for (auto& body_expr : kind.body) {
                    if (body_expr != nullptr)
                        materialize_expression_types(*body_expr);
                }
            } else if constexpr (std::is_same_v<Kind, cstc::hir::LiftedConstantExpr>) {
                materialize_type_contracts(kind.type);
            } else if constexpr (std::is_same_v<Kind, cstc::hir::DeclConstraintExpr>) {
                materialize_type_contracts(kind.checked_type);
            }
        },
        expr.kind);
}

[[nodiscard]] std::optional<std::string> extract_constraint_type_head(std::string_view text) {
    std::string normalized = trim_copy(text);
    while (true) {
        if (normalized.starts_with("runtime ")) {
            normalized.erase(0, std::string{"runtime "}.size());
            normalized = trim_copy(normalized);
            continue;
        }
        if (normalized.starts_with("!runtime ")) {
            normalized.erase(0, std::string{"!runtime "}.size());
            normalized = trim_copy(normalized);
            continue;
        }
        if (normalized.starts_with("const ")) {
            normalized.erase(0, std::string{"const "}.size());
            normalized = trim_copy(normalized);
            continue;
        }
        break;
    }

    while (!normalized.empty() && normalized.front() == '&') {
        normalized.erase(normalized.begin());
        normalized = trim_copy(normalized);
    }

    if (normalized.starts_with("fn("))
        return std::string{"fn"};

    const std::size_t generic_pos = normalized.find('<');
    const std::size_t cut_pos = generic_pos == std::string::npos ? normalized.size() : generic_pos;
    std::string head = trim_copy(normalized.substr(0, cut_pos));
    if (head.empty())
        return std::nullopt;
    return head;
}

void collect_sizeof_arguments(std::string_view text, std::vector<std::string>& args, bool& malformed) {
    std::size_t search_from = 0;
    malformed = false;

    while (search_from < text.size()) {
        const std::size_t found = text.find("sizeof(", search_from);
        if (found == std::string::npos)
            break;

        std::size_t cursor = found + std::string{"sizeof("}.size();
        int depth = 1;
        while (cursor < text.size() && depth > 0) {
            if (text[cursor] == '(')
                ++depth;
            else if (text[cursor] == ')')
                --depth;
            ++cursor;
        }

        if (depth != 0 || cursor == 0) {
            malformed = true;
            return;
        }

        const std::size_t begin = found + std::string{"sizeof("}.size();
        const std::size_t end = cursor - 1;
        args.push_back(trim_copy(text.substr(begin, end - begin)));
        search_from = cursor;
    }
}

[[nodiscard]] std::optional<std::size_t> find_matching_delimiter(
    std::string_view text, std::size_t open_pos, char open_ch, char close_ch) {
    if (open_pos >= text.size() || text[open_pos] != open_ch)
        return std::nullopt;

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = open_pos; index < text.size(); ++index) {
        const char ch = text[index];

        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                escaped = true;
                continue;
            }

            if (ch == '"')
                in_string = false;

            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == open_ch) {
            ++depth;
            continue;
        }

        if (ch == close_ch) {
            --depth;
            if (depth == 0)
                return index;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<std::string> split_top_level(std::string_view text, char delimiter) {
    std::vector<std::string> out;
    std::size_t begin = 0;

    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];

        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                escaped = true;
                continue;
            }

            if (ch == '"')
                in_string = false;

            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '(')
            ++paren_depth;
        else if (ch == ')')
            --paren_depth;
        else if (ch == '{')
            ++brace_depth;
        else if (ch == '}')
            --brace_depth;
        else if (ch == '[')
            ++bracket_depth;
        else if (ch == ']')
            --bracket_depth;

        if (ch == delimiter && paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            out.push_back(trim_copy(text.substr(begin, index - begin)));
            begin = index + 1;
        }
    }

    out.push_back(trim_copy(text.substr(begin)));
    return out;
}

[[nodiscard]] std::vector<std::string> split_block_statements(std::string_view block_body) {
    std::vector<std::string> statements;
    std::size_t begin = 0;

    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t index = 0; index < block_body.size(); ++index) {
        const char ch = block_body[index];

        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                escaped = true;
                continue;
            }

            if (ch == '"')
                in_string = false;

            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '(')
            ++paren_depth;
        else if (ch == ')')
            --paren_depth;
        else if (ch == '{')
            ++brace_depth;
        else if (ch == '}')
            --brace_depth;
        else if (ch == '[')
            ++bracket_depth;
        else if (ch == ']')
            --bracket_depth;

        if (ch == ';' && paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            statements.push_back(trim_copy(block_body.substr(begin, index - begin + 1)));
            begin = index + 1;
        }
    }

    const std::string tail = trim_copy(block_body.substr(begin));
    if (!tail.empty())
        statements.push_back(tail);

    return statements;
}

[[nodiscard]] std::optional<std::size_t> find_top_level_token(
    std::string_view text, std::string_view token, std::size_t start = 0) {
    if (token.empty() || start >= text.size())
        return std::nullopt;

    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t index = start; index < text.size(); ++index) {
        const char ch = text[index];

        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                escaped = true;
                continue;
            }

            if (ch == '"')
                in_string = false;

            continue;
        }

        if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0
            && text.substr(index).starts_with(token)) {
            return index;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '(')
            ++paren_depth;
        else if (ch == ')' && paren_depth > 0)
            --paren_depth;
        else if (ch == '{')
            ++brace_depth;
        else if (ch == '}' && brace_depth > 0)
            --brace_depth;
        else if (ch == '[')
            ++bracket_depth;
        else if (ch == ']' && bracket_depth > 0)
            --bracket_depth;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> find_top_level_char(
    std::string_view text, char target, std::size_t start = 0) {
    const auto found = find_top_level_token(text, std::string_view{&target, 1}, start);
    if (!found.has_value())
        return std::nullopt;
    return *found;
}

[[nodiscard]] std::string remove_turbofish_suffix(std::string_view text) {
    const std::string trimmed = trim_copy(text);
    const std::size_t marker = trimmed.find("::<");
    if (marker == std::string::npos)
        return trimmed;

    const auto close = find_matching_delimiter(trimmed, marker + 2, '<', '>');
    if (!close.has_value())
        return trimmed;

    std::string out = std::string{trimmed.substr(0, marker)};
    out += trimmed.substr(*close + 1);
    return trim_copy(out);
}

[[nodiscard]] std::vector<std::string> split_path_segments(std::string_view path_text) {
    std::vector<std::string> segments;
    std::string current;

    const std::string normalized = remove_turbofish_suffix(path_text);
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        if (index + 1 < normalized.size() && normalized[index] == ':' && normalized[index + 1] == ':') {
            if (!current.empty()) {
                segments.push_back(trim_copy(current));
                current.clear();
            }
            ++index;
            continue;
        }

        current.push_back(normalized[index]);
    }

    if (!current.empty())
        segments.push_back(trim_copy(current));
    return segments;
}

[[nodiscard]] bool looks_like_path_text(std::string_view text) {
    const std::string normalized = remove_turbofish_suffix(text);
    if (normalized.empty())
        return false;

    for (std::size_t index = 0; index < normalized.size(); ++index) {
        const char ch = normalized[index];
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':')
            continue;
        return false;
    }

    return true;
}

} // namespace

[[nodiscard]] std::string format_value(const Value& value) {
    return std::visit(
        [](const auto& kind) -> std::string {
            using Kind = std::decay_t<decltype(kind)>;

            if constexpr (std::is_same_v<Kind, UnitValue>) {
                return "unit";
            } else if constexpr (std::is_same_v<Kind, std::int64_t>) {
                return std::to_string(kind);
            } else if constexpr (std::is_same_v<Kind, bool>) {
                return kind ? "true" : "false";
            } else if constexpr (std::is_same_v<Kind, std::string>) {
                return kind;
            } else if constexpr (std::is_same_v<Kind, ObjectValue>) {
                std::string out = kind.type_name;
                out += " { ";
                bool first = true;
                for (const auto& [field, value_ptr] : kind.fields) {
                    if (!first)
                        out += ", ";
                    first = false;
                    out += field + ": "
                        + (value_ptr == nullptr ? std::string{"<null>"} : format_value(*value_ptr));
                }
                out += " }";
                return out;
            } else if constexpr (std::is_same_v<Kind, LambdaValue>) {
                std::string out = "lambda(";
                for (std::size_t index = 0; index < kind.params.size(); ++index) {
                    if (index != 0)
                        out += ", ";
                    out += kind.params[index];
                }
                out += ")";
                return out;
            } else if constexpr (std::is_same_v<Kind, FunctionRefValue>) {
                return "fn " + kind.name;
            }

            return "<value>";
        },
        value.kind);
}

struct HirInterpreter::Impl {
    struct FunctionEntry {
        const cstc::hir::Declaration* declaration = nullptr;
        const cstc::hir::FunctionDecl* header = nullptr;
    };

    struct EvalContext {
        std::unordered_map<std::string, Value> locals;
        std::unordered_map<std::string, std::string> generic_bindings;
        std::unordered_set<std::string> generic_params;
        ExecutionMode mode = ExecutionMode::Runtime;
        std::size_t call_depth = 0;
        std::size_t call_depth_limit = 256;
        std::size_t loop_iteration_limit = 1024;
        bool allow_type_symbols = false;
        std::string scope;
    };

    struct EvalOutcome {
        bool ok = true;
        bool returned = false;
        bool has_value = false;
        Value value = make_unit_value();
    };

    explicit Impl(cstc::hir::Module& module)
        : module_(module) {}

    [[nodiscard]] ValidationResult validate() {
        ValidationResult result;
        index_module(result.diagnostics);

        for (const auto& declaration : module_.declarations)
            validate_declaration(declaration, result.diagnostics);

        result.ok = !has_errors(result.diagnostics);
        return result;
    }

    [[nodiscard]] MaterializationResult materialize_const_types() {
        ValidationResult validation = validate();

        MaterializationResult result;
        result.diagnostics = std::move(validation.diagnostics);
        if (!validation.ok) {
            result.ok = false;
            return result;
        }

        std::unordered_set<std::string> const_candidates;
        for (const auto& declaration : module_.declarations) {
            const auto* function = std::get_if<cstc::hir::FunctionDecl>(&declaration.header);
            if (function == nullptr)
                continue;

            if (type_contains_not_runtime(function->return_type) && function->generic_params.empty()
                && function->params.empty()) {
                const_candidates.insert(function->name);
            }
        }

        for (auto& declaration : module_.declarations) {
            if (auto* function = std::get_if<cstc::hir::FunctionDecl>(&declaration.header); function != nullptr) {
                for (auto& param : function->params)
                    materialize_type_contracts(param.type);
                materialize_type_contracts(function->return_type);
            }

            for (auto& body_expr : declaration.body) {
                if (body_expr != nullptr)
                    materialize_expression_types(*body_expr);
            }

            for (auto& constraint_expr : declaration.constraints) {
                if (constraint_expr != nullptr)
                    materialize_expression_types(*constraint_expr);
            }
        }

        index_module(result.diagnostics);
        lifted_constants_.clear();

        for (const std::string& fn_name : const_candidates) {
            const auto value = execute_function(
                fn_name, {}, ExecutionMode::ConstEval, 0, 256, result.diagnostics, "materialize");
            if (!value.has_value())
                continue;
            lifted_constants_[fn_name] = *value;
        }

        result.ok = !has_errors(result.diagnostics);
        result.lifted_constants = lifted_constants_;
        return result;
    }

    [[nodiscard]] ExecutionResult run(const RunOptions& options) {
        ExecutionResult result;

        ValidationResult validation = validate();
        result.diagnostics = std::move(validation.diagnostics);
        if (!validation.ok) {
            result.ok = false;
            return result;
        }

        const auto value = execute_function(options.entry, {}, options.mode, 0,
            options.call_depth_limit, result.diagnostics, options.entry);
        if (!value.has_value()) {
            result.ok = false;
            return result;
        }

        result.value = *value;
        result.ok = !has_errors(result.diagnostics);
        return result;
    }

    [[nodiscard]] ExecutionResult eval_repl_line(std::string_view line) {
        ExecutionResult result;

        ValidationResult validation = validate();
        result.diagnostics = std::move(validation.diagnostics);
        if (!validation.ok) {
            result.ok = false;
            return result;
        }

        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            result.ok = true;
            return result;
        }

        EvalContext context;
        context.locals = repl_locals_;
        context.mode = ExecutionMode::Runtime;
        context.call_depth = 0;
        context.call_depth_limit = 256;
        context.allow_type_symbols = true;
        context.scope = "repl";

        EvalOutcome outcome = evaluate_raw_statement(trimmed, context, result.diagnostics);
        if (!outcome.ok) {
            result.ok = false;
            return result;
        }

        repl_locals_ = std::move(context.locals);
        if (outcome.has_value)
            result.value = outcome.value;

        result.ok = !has_errors(result.diagnostics);
        return result;
    }

    void index_module(std::vector<Diagnostic>& diagnostics) {
        known_types_ = builtin_type_names();
        functions_.clear();

        for (const auto& declaration : module_.declarations) {
            if (const auto* function = std::get_if<cstc::hir::FunctionDecl>(&declaration.header);
                function != nullptr) {
                if (function->name.empty()) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, "module",
                        "function declaration has an empty name");
                    continue;
                }

                if (functions_.contains(function->name)) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, function->name,
                        "duplicate function declaration");
                    continue;
                }

                functions_.insert(
                    {function->name, FunctionEntry{.declaration = &declaration, .header = function}});
                continue;
            }

            if (const auto* raw = std::get_if<cstc::hir::RawDecl>(&declaration.header);
                raw != nullptr && !raw->name.empty()) {
                known_types_.insert(raw->name);
            }
        }
    }

    void validate_declaration(
        const cstc::hir::Declaration& declaration, std::vector<Diagnostic>& diagnostics) const {
        if (const auto* function = std::get_if<cstc::hir::FunctionDecl>(&declaration.header);
            function != nullptr) {
            std::unordered_set<std::string> generic_params;
            for (const std::string& generic_param : function->generic_params) {
                if (generic_param.empty()) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, function->name,
                        "generic parameter name cannot be empty");
                    continue;
                }

                if (!generic_params.insert(generic_param).second) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, function->name,
                        "duplicate generic parameter '" + generic_param + "'");
                }
            }

            std::unordered_set<std::string> param_names;
            for (const auto& param : function->params) {
                if (param.name.empty()) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, function->name,
                        "parameter name cannot be empty");
                    continue;
                }

                if (!param_names.insert(param.name).second) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, function->name,
                        "duplicate parameter name '" + param.name + "'");
                }

                (void)validate_type(param.type, generic_params, function->name, diagnostics, false);
            }

            (void)validate_type(function->return_type, generic_params, function->name, diagnostics, false);

            for (const auto& body_expr : declaration.body) {
                if (body_expr == nullptr) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, function->name,
                        "function body contains a missing expression node");
                    continue;
                }
                validate_expression(*body_expr, generic_params, function->name, diagnostics, false);
            }

            for (const auto& constraint_expr : declaration.constraints) {
                if (constraint_expr == nullptr) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error,
                        function->name + "::constraint", "constraint list contains a missing expression node");
                    continue;
                }
                validate_expression(*constraint_expr, generic_params, function->name, diagnostics, true);
            }
            return;
        }

        if (const auto* raw = std::get_if<cstc::hir::RawDecl>(&declaration.header);
            raw != nullptr && raw->name.empty()) {
            push_diagnostic(
                diagnostics, DiagnosticSeverity::Warning, "module", "raw declaration has an empty name");
        }
    }

    [[nodiscard]] bool is_known_path_head(const std::string& head,
        const std::unordered_set<std::string>& generic_params,
        const std::unordered_map<std::string, std::string>* bindings) const {
        if (generic_params.contains(head))
            return true;

        if (bindings != nullptr && bindings->contains(head))
            return true;

        return known_types_.contains(head) || is_builtin_type_name(head);
    }

    [[nodiscard]] bool validate_type(const cstc::hir::Type& type,
        const std::unordered_set<std::string>& generic_params, const std::string& scope,
        std::vector<Diagnostic>& diagnostics, bool allow_inferred) const {
        return std::visit(
            [this, &generic_params, &scope, &diagnostics,
                allow_inferred](const auto& kind) -> bool {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::hir::PathType>) {
                    if (kind.segments.empty()) {
                        push_diagnostic(
                            diagnostics, DiagnosticSeverity::Error, scope, "path type has no segments");
                        return false;
                    }

                    const std::string joined_name = join_segments(kind.segments);
                    const std::string& head = kind.segments.front();

                    if (!is_known_path_head(head, generic_params, nullptr)
                        && !known_types_.contains(joined_name)) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "unknown type name '" + joined_name + "'");
                    }

                    bool ok = true;
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "generic type argument is missing");
                            ok = false;
                            continue;
                        }
                        ok = validate_type(*arg, generic_params, scope, diagnostics, allow_inferred)
                            && ok;
                    }
                    return ok;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractType>) {
                    if (kind.inner == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "contract type missing inner type");
                        return false;
                    }

                    return validate_type(*kind.inner, generic_params, scope, diagnostics, allow_inferred);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::RefType>) {
                    if (kind.inner == nullptr) {
                        push_diagnostic(
                            diagnostics, DiagnosticSeverity::Error, scope, "reference type missing inner type");
                        return false;
                    }

                    return validate_type(*kind.inner, generic_params, scope, diagnostics, allow_inferred);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::FnPointerType>) {
                    bool ok = true;
                    for (const auto& param : kind.params) {
                        if (param == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "function pointer parameter type missing");
                            ok = false;
                            continue;
                        }
                        ok = validate_type(*param, generic_params, scope, diagnostics, allow_inferred)
                            && ok;
                    }

                    if (kind.result == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "function pointer result type missing");
                        return false;
                    }

                    return validate_type(*kind.result, generic_params, scope, diagnostics, allow_inferred)
                        && ok;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::InferredType>) {
                    if (!allow_inferred) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "inferred type '_' is not allowed at HIR interpreter stage");
                        return false;
                    }
                    return true;
                }

                return true;
            },
            type.kind);
    }

    void validate_constraint_text(std::string_view text,
        const std::unordered_set<std::string>& generic_params, const std::string& scope,
        std::vector<Diagnostic>& diagnostics) const {
        const std::string trimmed = trim_copy(text);
        if (trimmed.empty()) {
            push_diagnostic(
                diagnostics, DiagnosticSeverity::Error, scope, "constraint expression cannot be empty");
            return;
        }

        std::vector<std::string> sizeof_args;
        bool malformed_sizeof = false;
        collect_sizeof_arguments(trimmed, sizeof_args, malformed_sizeof);
        if (malformed_sizeof) {
            push_diagnostic(
                diagnostics, DiagnosticSeverity::Error, scope, "malformed sizeof(...) constraint expression");
            return;
        }

        for (const std::string& argument : sizeof_args) {
            const auto head = extract_constraint_type_head(argument);
            if (!head.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "failed to parse type in sizeof(" + argument + ")");
                continue;
            }

            const std::string& type_head = *head;
            const std::string first_segment =
                type_head.substr(0, type_head.find("::"));

            if (generic_params.contains(first_segment))
                continue;

            if (!known_types_.contains(type_head) && !known_types_.contains(first_segment)
                && !is_builtin_type_name(first_segment)) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "unknown type in sizeof constraint: '" + type_head + "'");
            }
        }

        std::vector<RawToken> tokens;
        std::string lexer_error;
        RawLexer lexer{trimmed};
        if (!lexer.lex(tokens, lexer_error)) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                "failed to lex constraint expression: " + lexer_error);
            return;
        }

        RawNodePtr parsed_expr;
        std::string parser_error;
        RawParser parser{std::move(tokens)};
        if (!parser.parse(parsed_expr, parser_error)) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                "failed to parse constraint expression: " + parser_error);
        }
    }

    void validate_expression(const cstc::hir::Expr& expr,
        const std::unordered_set<std::string>& generic_params, const std::string& scope,
        std::vector<Diagnostic>& diagnostics, bool is_constraint) const {
        std::visit(
            [this, &generic_params, &scope, &diagnostics, is_constraint](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::hir::RawExpr>) {
                    if (is_constraint)
                        validate_constraint_text(kind.text, generic_params, scope + "::constraint", diagnostics);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::LiteralExpr>) {
                    if (kind.text.empty()) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "literal expression has empty text");
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::hir::PathExpr>) {
                    if (kind.segments.empty()) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "path expression has no segments");
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::hir::BinaryExpr>) {
                    if (kind.lhs == nullptr || kind.rhs == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "binary expression has a missing operand");
                        return;
                    }
                    validate_expression(*kind.lhs, generic_params, scope, diagnostics, is_constraint);
                    validate_expression(*kind.rhs, generic_params, scope, diagnostics, is_constraint);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::CallExpr>) {
                    if (kind.callee == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "call expression is missing callee");
                        return;
                    }
                    validate_expression(*kind.callee, generic_params, scope, diagnostics, is_constraint);
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "call expression has missing argument");
                            continue;
                        }
                        validate_expression(*arg, generic_params, scope, diagnostics, is_constraint);
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::hir::MemberAccessExpr>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "member access expression missing receiver");
                        return;
                    }
                    validate_expression(*kind.receiver, generic_params, scope, diagnostics, is_constraint);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::MemberCallExpr>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "member call expression missing receiver");
                        return;
                    }
                    validate_expression(*kind.receiver, generic_params, scope, diagnostics, is_constraint);
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "member call expression has missing argument");
                            continue;
                        }
                        validate_expression(*arg, generic_params, scope, diagnostics, is_constraint);
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::hir::StaticMemberAccessExpr>) {
                    (void)validate_type(kind.receiver_type, generic_params, scope, diagnostics, true);
                    if (kind.receiver != nullptr)
                        validate_expression(*kind.receiver, generic_params, scope, diagnostics, is_constraint);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::StaticMemberCallExpr>) {
                    (void)validate_type(kind.receiver_type, generic_params, scope, diagnostics, true);
                    if (kind.receiver != nullptr)
                        validate_expression(*kind.receiver, generic_params, scope, diagnostics, is_constraint);
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "static member call has missing argument");
                            continue;
                        }
                        validate_expression(*arg, generic_params, scope, diagnostics, is_constraint);
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractBlockExpr>) {
                    for (const auto& body_expr : kind.body) {
                        if (body_expr == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "contract block contains missing expression");
                            continue;
                        }
                        validate_expression(*body_expr, generic_params, scope, diagnostics, is_constraint);
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::hir::LiftedConstantExpr>) {
                    (void)validate_type(kind.type, generic_params, scope, diagnostics, true);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::DeclConstraintExpr>) {
                    (void)validate_type(
                        kind.checked_type, generic_params, scope + "::constraint", diagnostics, true);
                }
            },
            expr.kind);
    }

    [[nodiscard]] std::optional<Value> resolve_path_value(
        const std::vector<std::string>& segments, const EvalContext& context,
        std::vector<Diagnostic>& diagnostics, const std::string& scope) const {
        if (segments.empty()) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                "cannot resolve value for empty path expression");
            return std::nullopt;
        }

        const std::string joined = join_segments(segments);
        if (segments.size() == 1) {
            const auto local = context.locals.find(segments[0]);
            if (local != context.locals.end())
                return local->second;

            const auto lifted = lifted_constants_.find(segments[0]);
            if (lifted != lifted_constants_.end())
                return lifted->second;

            const auto generic_binding = context.generic_bindings.find(segments[0]);
            if (context.allow_type_symbols && generic_binding != context.generic_bindings.end())
                return Value{.kind = generic_binding->second};

            if (context.allow_type_symbols && context.generic_params.contains(segments[0]))
                return Value{.kind = segments[0]};

            if (functions_.contains(segments[0]))
                return Value{.kind = FunctionRefValue{.name = segments[0]}};
        }

        const auto lifted_joined = lifted_constants_.find(joined);
        if (lifted_joined != lifted_constants_.end())
            return lifted_joined->second;

        if (context.allow_type_symbols) {
            if (known_types_.contains(joined) || known_types_.contains(segments.front())
                || is_builtin_type_name(segments.front())) {
                return Value{.kind = joined};
            }
        }

        if (functions_.contains(joined))
            return Value{.kind = FunctionRefValue{.name = joined}};

        push_diagnostic(
            diagnostics, DiagnosticSeverity::Error, scope, "unknown symbol '" + joined + "'");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<bool> to_truthy_boolean(
        const Value& value, std::vector<Diagnostic>& diagnostics, const std::string& scope) const {
        const auto boolean = value_as_bool(value);
        if (boolean.has_value())
            return boolean;

        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
            "expected boolean-compatible value, got '" + format_value(value) + "'");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> apply_unary_operator(const std::string& op, const Value& operand,
        std::vector<Diagnostic>& diagnostics, const std::string& scope) const {
        if (op == "-") {
            const auto integer = value_as_int(operand);
            if (!integer.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "unary '-' expects integer operand");
                return std::nullopt;
            }
            return Value{.kind = -*integer};
        }

        if (op == "!") {
            const auto boolean = value_as_bool(operand);
            if (!boolean.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "unary '!' expects boolean-compatible operand");
                return std::nullopt;
            }
            return Value{.kind = !*boolean};
        }

        if (op == "&" || op == "*")
            return operand;

        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
            "unsupported unary operator '" + op + "'");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> apply_binary_operator(const std::string& op, const Value& lhs,
        const Value& rhs, std::vector<Diagnostic>& diagnostics, const std::string& scope) const {
        if (op == "+") {
            if (const auto left_int = value_as_int(lhs); left_int.has_value()) {
                const auto right_int = value_as_int(rhs);
                if (!right_int.has_value()) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                        "operator '+' expects two integers");
                    return std::nullopt;
                }
                return Value{.kind = *left_int + *right_int};
            }

            if (const auto* left_string = std::get_if<std::string>(&lhs.kind);
                left_string != nullptr) {
                if (const auto* right_string = std::get_if<std::string>(&rhs.kind);
                    right_string != nullptr) {
                    return Value{.kind = *left_string + *right_string};
                }
            }

            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                "operator '+' expects integer or string operands");
            return std::nullopt;
        }

        auto int_op = [&](auto fn) -> std::optional<Value> {
            const auto left = value_as_int(lhs);
            const auto right = value_as_int(rhs);
            if (!left.has_value() || !right.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "operator '" + op + "' expects integer operands");
                return std::nullopt;
            }
            return Value{.kind = fn(*left, *right)};
        };

        if (op == "-")
            return int_op([](std::int64_t left, std::int64_t right) { return left - right; });
        if (op == "*")
            return int_op([](std::int64_t left, std::int64_t right) { return left * right; });
        if (op == "/") {
            const auto left = value_as_int(lhs);
            const auto right = value_as_int(rhs);
            if (!left.has_value() || !right.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "operator '/' expects integer operands");
                return std::nullopt;
            }
            if (*right == 0) {
                push_diagnostic(
                    diagnostics, DiagnosticSeverity::Error, scope, "division by zero");
                return std::nullopt;
            }
            return Value{.kind = *left / *right};
        }
        if (op == "%") {
            const auto left = value_as_int(lhs);
            const auto right = value_as_int(rhs);
            if (!left.has_value() || !right.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "operator '%' expects integer operands");
                return std::nullopt;
            }
            if (*right == 0) {
                push_diagnostic(
                    diagnostics, DiagnosticSeverity::Error, scope, "modulo by zero");
                return std::nullopt;
            }
            return Value{.kind = *left % *right};
        }
        if (op == "<<")
            return int_op([](std::int64_t left, std::int64_t right) { return left << right; });
        if (op == ">>")
            return int_op([](std::int64_t left, std::int64_t right) { return left >> right; });
        if (op == "&")
            return int_op([](std::int64_t left, std::int64_t right) { return left & right; });
        if (op == "|")
            return int_op([](std::int64_t left, std::int64_t right) { return left | right; });
        if (op == "^")
            return int_op([](std::int64_t left, std::int64_t right) { return left ^ right; });

        if (op == "==" || op == "!=") {
            bool equal = false;
            if (lhs.kind.index() == rhs.kind.index()) {
                if (const auto* left_int = std::get_if<std::int64_t>(&lhs.kind);
                    left_int != nullptr) {
                    equal = *left_int == std::get<std::int64_t>(rhs.kind);
                } else if (const auto* left_bool = std::get_if<bool>(&lhs.kind);
                    left_bool != nullptr) {
                    equal = *left_bool == std::get<bool>(rhs.kind);
                } else if (const auto* left_string = std::get_if<std::string>(&lhs.kind);
                    left_string != nullptr) {
                    equal = *left_string == std::get<std::string>(rhs.kind);
                } else if (const auto* left_fn = std::get_if<FunctionRefValue>(&lhs.kind);
                    left_fn != nullptr) {
                    equal = left_fn->name == std::get<FunctionRefValue>(rhs.kind).name;
                } else {
                    equal = std::holds_alternative<UnitValue>(lhs.kind);
                }
            } else {
                const auto left_int = value_as_int(lhs);
                const auto right_int = value_as_int(rhs);
                if (left_int.has_value() && right_int.has_value())
                    equal = *left_int == *right_int;
            }

            return Value{.kind = op == "==" ? equal : !equal};
        }

        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            const auto left = value_as_int(lhs);
            const auto right = value_as_int(rhs);
            if (!left.has_value() || !right.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "comparison operator '" + op + "' expects integer operands");
                return std::nullopt;
            }

            bool value = false;
            if (op == "<")
                value = *left < *right;
            else if (op == "<=")
                value = *left <= *right;
            else if (op == ">")
                value = *left > *right;
            else
                value = *left >= *right;

            return Value{.kind = value};
        }

        if (op == "&&" || op == "||") {
            const auto left = value_as_bool(lhs);
            const auto right = value_as_bool(rhs);
            if (!left.has_value() || !right.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                    "logical operator '" + op + "' expects boolean-compatible operands");
                return std::nullopt;
            }

            return Value{.kind = op == "&&" ? (*left && *right) : (*left || *right)};
        }

        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
            "unsupported binary operator '" + op + "'");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> resolve_type_name_from_raw_node(
        const RawNode& node, const EvalContext& context) const {
        if (const auto* path = std::get_if<RawPathNode>(&node.kind); path != nullptr) {
            if (path->segments.empty())
                return std::nullopt;

            if (path->segments.size() == 1) {
                const auto binding = context.generic_bindings.find(path->segments[0]);
                if (binding != context.generic_bindings.end())
                    return binding->second;
            }

            return join_segments(path->segments);
        }

        if (const auto* literal = std::get_if<RawLiteralNode>(&node.kind); literal != nullptr) {
            if (const auto* value = std::get_if<std::string>(&literal->value.kind); value != nullptr)
                return *value;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool is_type_resolvable(const cstc::hir::Type& type,
        const std::unordered_map<std::string, std::string>& generic_bindings,
        const std::unordered_set<std::string>& generic_params, bool allow_unbound_generic,
        std::vector<Diagnostic>& diagnostics, const std::string& scope) const {
        return std::visit(
            [this, &generic_bindings, &generic_params, allow_unbound_generic, &diagnostics,
                &scope](const auto& kind) -> bool {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::hir::PathType>) {
                    if (kind.segments.empty()) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "constraint type has empty path");
                        return false;
                    }

                    const std::string joined_name = join_segments(kind.segments);
                    const std::string& head = kind.segments.front();
                    bool head_known = false;

                    if (kind.segments.size() == 1 && generic_bindings.contains(head)) {
                        head_known = true;
                    } else if (generic_params.contains(head)) {
                        head_known = allow_unbound_generic;
                        if (!allow_unbound_generic) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "generic type '" + head + "' is not bound in constraint evaluation");
                        }
                    } else if (known_types_.contains(joined_name) || known_types_.contains(head)
                        || is_builtin_type_name(head)) {
                        head_known = true;
                    }

                    if (!head_known) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "constraint references unknown type '" + joined_name + "'");
                        return false;
                    }

                    bool ok = true;
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "constraint type has missing generic argument");
                            ok = false;
                            continue;
                        }
                        ok = is_type_resolvable(*arg, generic_bindings, generic_params,
                                  allow_unbound_generic, diagnostics, scope)
                            && ok;
                    }
                    return ok;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractType>) {
                    if (kind.inner == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "constraint contract type missing inner type");
                        return false;
                    }
                    return is_type_resolvable(*kind.inner, generic_bindings, generic_params,
                        allow_unbound_generic, diagnostics, scope);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::RefType>) {
                    if (kind.inner == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "constraint reference type missing inner type");
                        return false;
                    }
                    return is_type_resolvable(*kind.inner, generic_bindings, generic_params,
                        allow_unbound_generic, diagnostics, scope);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::FnPointerType>) {
                    if (kind.result == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "constraint function pointer missing result type");
                        return false;
                    }

                    bool ok = is_type_resolvable(*kind.result, generic_bindings, generic_params,
                        allow_unbound_generic, diagnostics, scope);
                    for (const auto& param : kind.params) {
                        if (param == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "constraint function pointer missing parameter type");
                            ok = false;
                            continue;
                        }
                        ok = is_type_resolvable(*param, generic_bindings, generic_params,
                                  allow_unbound_generic, diagnostics, scope)
                            && ok;
                    }
                    return ok;
                }

                return true;
            },
            type.kind);
    }

    [[nodiscard]] bool value_matches_type(const Value& value, const cstc::hir::Type& type,
        const std::unordered_set<std::string>& generic_params,
        std::unordered_map<std::string, std::string>& generic_bindings,
        std::vector<Diagnostic>& diagnostics, const std::string& scope) const {
        return std::visit(
            [this, &value, &generic_params, &generic_bindings, &diagnostics,
                &scope](const auto& kind) -> bool {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::hir::PathType>) {
                    if (kind.segments.empty()) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "cannot match value against empty path type");
                        return false;
                    }

                    const std::string& head = kind.segments.front();
                    if (kind.segments.size() == 1 && generic_params.contains(head)) {
                        const std::string actual_type = value_runtime_type_name(value);
                        const auto existing = generic_bindings.find(head);
                        if (existing == generic_bindings.end()) {
                            generic_bindings[head] = actual_type;
                            return true;
                        }

                        if (existing->second != actual_type) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "conflicting generic binding for '" + head + "': '"
                                    + existing->second + "' vs '" + actual_type + "'");
                            return false;
                        }

                        return true;
                    }

                    const std::string joined_name = join_segments(kind.segments);
                    if (known_types_.contains(joined_name) || known_types_.contains(head)) {
                        const std::string normalized =
                            known_types_.contains(joined_name) ? joined_name : head;
                        if (is_builtin_type_name(normalized)
                            && !value_matches_named_type(value, normalized)) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                                "value type mismatch: expected '" + normalized + "', got '"
                                    + value_runtime_type_name(value) + "'");
                            return false;
                        }
                    }

                    if (is_builtin_type_name(head) && !value_matches_named_type(value, head)) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "value type mismatch: expected '" + head + "', got '"
                                + value_runtime_type_name(value) + "'");
                        return false;
                    }

                    return true;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractType>) {
                    if (kind.inner == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "contract type has missing inner type");
                        return false;
                    }

                    return value_matches_type(
                        value, *kind.inner, generic_params, generic_bindings, diagnostics, scope);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::RefType>) {
                    if (kind.inner == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                            "reference type has missing inner type");
                        return false;
                    }

                    return value_matches_type(
                        value, *kind.inner, generic_params, generic_bindings, diagnostics, scope);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::FnPointerType>) {
                    if (std::holds_alternative<LambdaValue>(value.kind)
                        || std::holds_alternative<FunctionRefValue>(value.kind)) {
                        return true;
                    }

                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, scope,
                        "value type mismatch: expected function pointer-compatible value, got '"
                            + value_runtime_type_name(value) + "'");
                    return false;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::InferredType>) {
                    return true;
                }

                return false;
            },
            type.kind);
    }

    [[nodiscard]] std::optional<Value> execute_lambda(const LambdaValue& lambda,
        const std::vector<Value>& args, EvalContext& caller_context,
        std::vector<Diagnostic>& diagnostics) {
        if (lambda.params.size() != args.size()) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, caller_context.scope,
                "lambda expected " + std::to_string(lambda.params.size()) + " arguments but got "
                    + std::to_string(args.size()));
            return std::nullopt;
        }

        EvalContext lambda_context;
        lambda_context.mode = caller_context.mode;
        lambda_context.call_depth = caller_context.call_depth + 1;
        lambda_context.call_depth_limit = caller_context.call_depth_limit;
        lambda_context.loop_iteration_limit = caller_context.loop_iteration_limit;
        lambda_context.allow_type_symbols = true;
        lambda_context.generic_bindings = caller_context.generic_bindings;
        lambda_context.generic_params = caller_context.generic_params;
        lambda_context.scope = caller_context.scope + "::lambda";

        for (std::size_t index = 0; index < lambda.params.size(); ++index)
            lambda_context.locals[lambda.params[index]] = args[index];

        EvalOutcome result = evaluate_raw_expression_outcome(lambda.body, lambda_context, diagnostics);
        if (!result.ok)
            return std::nullopt;

        if (result.returned)
            return result.value;

        if (result.has_value)
            return result.value;

        return make_unit_value();
    }

    [[nodiscard]] std::optional<Value> execute_callable(
        const Value& callee, const std::vector<Value>& args, EvalContext& context,
        std::vector<Diagnostic>& diagnostics) {
        if (const auto* function = std::get_if<FunctionRefValue>(&callee.kind); function != nullptr) {
            return execute_function(function->name, args, context.mode, context.call_depth + 1,
                context.call_depth_limit, diagnostics, context.scope);
        }

        if (const auto* lambda = std::get_if<LambdaValue>(&callee.kind); lambda != nullptr)
            return execute_lambda(*lambda, args, context, diagnostics);

        if (const auto* name = std::get_if<std::string>(&callee.kind);
            name != nullptr && functions_.contains(*name)) {
            return execute_function(*name, args, context.mode, context.call_depth + 1,
                context.call_depth_limit, diagnostics, context.scope);
        }

        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
            "value is not callable: '" + format_value(callee) + "'");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_raw_node(
        const RawNode& node, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        return std::visit(
            [this, &context, &diagnostics](const auto& kind) -> std::optional<Value> {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, RawLiteralNode>) {
                    return kind.value;
                } else if constexpr (std::is_same_v<Kind, RawPathNode>) {
                    return resolve_path_value(kind.segments, context, diagnostics, context.scope);
                } else if constexpr (std::is_same_v<Kind, RawUnaryNode>) {
                    if (kind.operand == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "unary expression has missing operand");
                        return std::nullopt;
                    }

                    const auto operand = evaluate_raw_node(*kind.operand, context, diagnostics);
                    if (!operand.has_value())
                        return std::nullopt;
                    return apply_unary_operator(kind.op, *operand, diagnostics, context.scope);
                } else if constexpr (std::is_same_v<Kind, RawBinaryNode>) {
                    if (kind.lhs == nullptr || kind.rhs == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "binary expression has missing operand");
                        return std::nullopt;
                    }

                    const auto lhs = evaluate_raw_node(*kind.lhs, context, diagnostics);
                    if (!lhs.has_value())
                        return std::nullopt;

                    const auto rhs = evaluate_raw_node(*kind.rhs, context, diagnostics);
                    if (!rhs.has_value())
                        return std::nullopt;

                    return apply_binary_operator(kind.op, *lhs, *rhs, diagnostics, context.scope);
                } else if constexpr (std::is_same_v<Kind, RawCallNode>) {
                    const std::string callee = join_segments(kind.callee);

                    if (callee == "sizeof") {
                        if (kind.args.size() != 1) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "sizeof(...) expects exactly one argument");
                            return std::nullopt;
                        }

                        if (kind.args[0] == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "sizeof(...) received missing argument");
                            return std::nullopt;
                        }

                        auto type_name = resolve_type_name_from_raw_node(*kind.args[0], context);
                        if (!type_name.has_value()) {
                            const auto value = evaluate_raw_node(*kind.args[0], context, diagnostics);
                            if (!value.has_value())
                                return std::nullopt;

                            if (const auto* type_text = std::get_if<std::string>(&value->kind);
                                type_text != nullptr) {
                                type_name = *type_text;
                            }
                        }

                        if (!type_name.has_value()) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "sizeof(...) argument must resolve to a type symbol");
                            return std::nullopt;
                        }

                        const auto byte_size = builtin_sizeof_type(*type_name);
                        if (!byte_size.has_value()) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "sizeof(...) unsupported for type '" + *type_name + "'");
                            return std::nullopt;
                        }

                        return Value{.kind = *byte_size};
                    }

                    if (callee == "decl") {
                        if (kind.args.size() != 1) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "decl(...) expects exactly one argument");
                            return std::nullopt;
                        }

                        if (kind.args[0] == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "decl(...) received missing argument");
                            return std::nullopt;
                        }

                        const auto type_name = resolve_type_name_from_raw_node(*kind.args[0], context);
                        if (!type_name.has_value()) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "decl(...) argument must be a type symbol");
                            return std::nullopt;
                        }

                        const bool known = known_types_.contains(*type_name)
                            || is_builtin_type_name(*type_name);
                        return Value{.kind = known};
                    }

                    std::vector<Value> args;
                    args.reserve(kind.args.size());
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "call expression contains missing argument");
                            return std::nullopt;
                        }

                        const auto value = evaluate_raw_node(*arg, context, diagnostics);
                        if (!value.has_value())
                            return std::nullopt;
                        args.push_back(*value);
                    }

                    const auto callee_value = resolve_path_value(kind.callee, context, diagnostics, context.scope);
                    if (!callee_value.has_value())
                        return std::nullopt;

                    return execute_callable(*callee_value, args, context, diagnostics);
                } else if constexpr (std::is_same_v<Kind, RawMemberAccessNode>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "member access expression missing receiver");
                        return std::nullopt;
                    }

                    const auto receiver = evaluate_raw_node(*kind.receiver, context, diagnostics);
                    if (!receiver.has_value())
                        return std::nullopt;

                    if (const auto* object = std::get_if<ObjectValue>(&receiver->kind); object != nullptr) {
                        const auto field = object->fields.find(kind.member);
                        if (field == object->fields.end() || field->second == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "unknown field '" + kind.member + "' on type '" + object->type_name
                                    + "'");
                            return std::nullopt;
                        }

                        return *field->second;
                    }

                    if (const auto* text = std::get_if<std::string>(&receiver->kind);
                        text != nullptr && kind.member == "len") {
                        return Value{.kind = static_cast<std::int64_t>(text->size())};
                    }

                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                        "member access is not supported on value type '"
                            + value_runtime_type_name(*receiver) + "'");
                    return std::nullopt;
                } else if constexpr (std::is_same_v<Kind, RawMemberCallNode>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "member call expression missing receiver");
                        return std::nullopt;
                    }

                    const auto receiver = evaluate_raw_node(*kind.receiver, context, diagnostics);
                    if (!receiver.has_value())
                        return std::nullopt;

                    std::vector<Value> args;
                    args.push_back(*receiver);
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "member call argument is missing");
                            return std::nullopt;
                        }

                        const auto value = evaluate_raw_node(*arg, context, diagnostics);
                        if (!value.has_value())
                            return std::nullopt;
                        args.push_back(*value);
                    }

                    const std::string method_name = value_runtime_type_name(*receiver) + "::"
                        + remove_turbofish_suffix(kind.member);
                    return execute_function(method_name, args, context.mode, context.call_depth + 1,
                        context.call_depth_limit, diagnostics, context.scope);
                }

                return std::nullopt;
            },
            node.kind);
    }

    [[nodiscard]] std::optional<Value> evaluate_raw_leaf_expression(
        std::string_view text, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        std::vector<RawToken> tokens;
        std::string lexer_error;
        RawLexer lexer{text};
        if (!lexer.lex(tokens, lexer_error)) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "failed to lex expression: " + lexer_error + " [" + std::string{text} + "]");
            return std::nullopt;
        }

        RawNodePtr node;
        std::string parser_error;
        RawParser parser{std::move(tokens)};
        if (!parser.parse(node, parser_error)) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "failed to parse expression: " + parser_error + " [" + std::string{text} + "]");
            return std::nullopt;
        }

        if (node == nullptr) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "failed to parse expression node");
            return std::nullopt;
        }

        return evaluate_raw_node(*node, context, diagnostics);
    }

    [[nodiscard]] EvalOutcome evaluate_raw_expression_outcome(
        std::string_view text, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        EvalOutcome outcome;
        const std::string trimmed = trim_copy(text);
        if (trimmed.empty())
            return outcome;

        if (trimmed.front() == '{') {
            const auto close = find_matching_delimiter(trimmed, 0, '{', '}');
            if (close.has_value() && *close + 1 == trimmed.size()) {
                const auto statements = split_block_statements(trimmed.substr(1, *close - 1));
                Value last_value = make_unit_value();
                bool has_last_value = false;

                for (const auto& statement : statements) {
                    EvalOutcome statement_result = evaluate_raw_statement(statement, context, diagnostics);
                    if (!statement_result.ok)
                        return statement_result;
                    if (statement_result.returned)
                        return statement_result;

                    if (statement_result.has_value) {
                        has_last_value = true;
                        last_value = statement_result.value;
                    }
                }

                outcome.has_value = has_last_value;
                if (has_last_value)
                    outcome.value = std::move(last_value);
                return outcome;
            }
        }

        if (starts_with_keyword(trimmed, "if")) {
            const std::string rest = trim_copy(trimmed.substr(2));
            const auto then_open = find_top_level_char(rest, '{');
            if (!then_open.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "if expression missing then block");
                outcome.ok = false;
                return outcome;
            }

            const std::string condition_text = trim_copy(rest.substr(0, *then_open));
            const auto then_close = find_matching_delimiter(rest, *then_open, '{', '}');
            if (!then_close.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "if expression has unbalanced then block");
                outcome.ok = false;
                return outcome;
            }

            EvalOutcome condition_outcome =
                evaluate_raw_expression_outcome(condition_text, context, diagnostics);
            if (!condition_outcome.ok)
                return condition_outcome;
            if (condition_outcome.returned) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "if condition cannot contain return");
                outcome.ok = false;
                return outcome;
            }

            const auto condition =
                to_truthy_boolean(condition_outcome.value, diagnostics, context.scope);
            if (!condition.has_value()) {
                outcome.ok = false;
                return outcome;
            }

            const std::string then_expr = rest.substr(*then_open, *then_close - *then_open + 1);
            const std::string suffix = trim_copy(rest.substr(*then_close + 1));

            if (*condition)
                return evaluate_raw_expression_outcome(then_expr, context, diagnostics);

            if (suffix.empty())
                return outcome;

            if (!starts_with_keyword(suffix, "else")) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "if expression has unexpected trailing text: " + suffix);
                outcome.ok = false;
                return outcome;
            }

            const std::string else_expr = trim_copy(suffix.substr(4));
            if (else_expr.empty())
                return outcome;

            return evaluate_raw_expression_outcome(else_expr, context, diagnostics);
        }

        if (starts_with_keyword(trimmed, "loop")) {
            const std::string body = trim_copy(trimmed.substr(4));
            for (std::size_t iteration = 0; iteration < context.loop_iteration_limit; ++iteration) {
                EvalOutcome body_result = evaluate_raw_expression_outcome(body, context, diagnostics);
                if (!body_result.ok)
                    return body_result;
                if (body_result.returned)
                    return body_result;
                if (body_result.has_value) {
                    outcome.has_value = true;
                    outcome.value = body_result.value;
                }
            }

            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "loop iteration limit exceeded (" + std::to_string(context.loop_iteration_limit)
                    + ") without return");
            outcome.ok = false;
            return outcome;
        }

        if (starts_with_keyword(trimmed, "for")) {
            const auto open = find_top_level_char(trimmed, '(');
            if (!open.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "for expression missing '(' header");
                outcome.ok = false;
                return outcome;
            }

            const auto close = find_matching_delimiter(trimmed, *open, '(', ')');
            if (!close.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "for expression has unbalanced header");
                outcome.ok = false;
                return outcome;
            }

            const auto header_parts = split_top_level(trimmed.substr(*open + 1, *close - *open - 1), ';');
            if (header_parts.size() != 3) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "for expression header expects 3 sections");
                outcome.ok = false;
                return outcome;
            }

            const std::string init = header_parts[0];
            const std::string cond = header_parts[1];
            const std::string step = header_parts[2];
            const std::string body = trim_copy(trimmed.substr(*close + 1));

            if (!init.empty()) {
                EvalOutcome init_result = evaluate_raw_statement(init + ";", context, diagnostics);
                if (!init_result.ok)
                    return init_result;
                if (init_result.returned)
                    return init_result;
            }

            for (std::size_t iteration = 0; iteration < context.loop_iteration_limit; ++iteration) {
                if (!cond.empty()) {
                    EvalOutcome cond_result = evaluate_raw_expression_outcome(cond, context, diagnostics);
                    if (!cond_result.ok)
                        return cond_result;
                    if (cond_result.returned) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "for condition cannot return");
                        cond_result.ok = false;
                        return cond_result;
                    }

                    const auto truthy =
                        to_truthy_boolean(cond_result.value, diagnostics, context.scope);
                    if (!truthy.has_value()) {
                        cond_result.ok = false;
                        return cond_result;
                    }

                    if (!*truthy)
                        return outcome;
                }

                EvalOutcome body_result = evaluate_raw_expression_outcome(body, context, diagnostics);
                if (!body_result.ok)
                    return body_result;
                if (body_result.returned)
                    return body_result;
                if (body_result.has_value) {
                    outcome.has_value = true;
                    outcome.value = body_result.value;
                }

                if (!step.empty()) {
                    EvalOutcome step_result = evaluate_raw_statement(step + ";", context, diagnostics);
                    if (!step_result.ok)
                        return step_result;
                    if (step_result.returned)
                        return step_result;
                }
            }

            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "for iteration limit exceeded (" + std::to_string(context.loop_iteration_limit)
                    + ")");
            outcome.ok = false;
            return outcome;
        }

        if (starts_with_keyword(trimmed, "lambda")) {
            const auto open = find_top_level_char(trimmed, '(');
            if (!open.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "lambda expression missing parameter list");
                outcome.ok = false;
                return outcome;
            }

            const auto close = find_matching_delimiter(trimmed, *open, '(', ')');
            if (!close.has_value()) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "lambda parameter list is unbalanced");
                outcome.ok = false;
                return outcome;
            }

            std::vector<std::string> params;
            const auto param_text = trim_copy(trimmed.substr(*open + 1, *close - *open - 1));
            if (!param_text.empty()) {
                const auto entries = split_top_level(param_text, ',');
                for (const auto& entry : entries) {
                    if (entry.empty())
                        continue;

                    const auto colon = find_top_level_char(entry, ':');
                    const std::string name = colon.has_value() ? trim_copy(entry.substr(0, *colon))
                                                                : trim_copy(entry);
                    if (name.empty()) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "lambda parameter name cannot be empty");
                        outcome.ok = false;
                        return outcome;
                    }
                    params.push_back(name);
                }
            }

            const std::string body = trim_copy(trimmed.substr(*close + 1));
            outcome.has_value = true;
            outcome.value = Value{
                .kind = LambdaValue{
                    .params = std::move(params),
                    .body = body,
                },
            };
            return outcome;
        }

        const auto brace_pos = find_top_level_char(trimmed, '{');
        if (brace_pos.has_value() && *brace_pos > 0 && !starts_with_keyword(trimmed, "if")
            && !starts_with_keyword(trimmed, "for") && !starts_with_keyword(trimmed, "loop")
            && !starts_with_keyword(trimmed, "lambda")) {
            const auto close = find_matching_delimiter(trimmed, *brace_pos, '{', '}');
            if (close.has_value() && *close + 1 == trimmed.size()) {
                const std::string type_name = trim_copy(trimmed.substr(0, *brace_pos));
                if (looks_like_path_text(type_name)) {
                    ObjectValue object;
                    object.type_name = type_name;

                    const std::string fields_text =
                        trim_copy(trimmed.substr(*brace_pos + 1, *close - *brace_pos - 1));
                    if (!fields_text.empty()) {
                        const auto fields = split_top_level(fields_text, ',');
                        for (const auto& field_entry : fields) {
                            if (field_entry.empty())
                                continue;

                            const auto colon = find_top_level_char(field_entry, ':');
                            if (!colon.has_value()) {
                                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                    "constructor field missing ':' in '" + field_entry + "'");
                                outcome.ok = false;
                                return outcome;
                            }

                            const std::string field_name =
                                trim_copy(field_entry.substr(0, *colon));
                            const std::string expr_text =
                                trim_copy(field_entry.substr(*colon + 1));
                            if (field_name.empty() || expr_text.empty()) {
                                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                    "constructor field is malformed: '" + field_entry + "'");
                                outcome.ok = false;
                                return outcome;
                            }

                            EvalOutcome field_value =
                                evaluate_raw_expression_outcome(expr_text, context, diagnostics);
                            if (!field_value.ok)
                                return field_value;
                            if (field_value.returned)
                                return field_value;

                            object.fields[field_name] =
                                std::make_shared<Value>(field_value.has_value
                                        ? field_value.value
                                        : make_unit_value());
                        }
                    }

                    outcome.has_value = true;
                    outcome.value = Value{.kind = std::move(object)};
                    return outcome;
                }
            }
        }

        if (looks_like_path_text(trimmed) && trimmed.find("::<") != std::string::npos) {
            const auto callee = split_path_segments(trimmed);
            const auto value = resolve_path_value(callee, context, diagnostics, context.scope);
            if (!value.has_value()) {
                outcome.ok = false;
                return outcome;
            }

            outcome.has_value = true;
            outcome.value = *value;
            return outcome;
        }

        const auto value = evaluate_raw_leaf_expression(trimmed, context, diagnostics);
        if (!value.has_value()) {
            outcome.ok = false;
            return outcome;
        }

        outcome.has_value = true;
        outcome.value = *value;
        return outcome;
    }

    [[nodiscard]] std::optional<Value> evaluate_raw_expression(
        std::string_view text, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        EvalOutcome outcome = evaluate_raw_expression_outcome(text, context, diagnostics);
        if (!outcome.ok)
            return std::nullopt;

        if (outcome.returned) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "return is not allowed in this expression context");
            return std::nullopt;
        }

        if (!outcome.has_value)
            return make_unit_value();
        return outcome.value;
    }

    [[nodiscard]] EvalOutcome evaluate_raw_statement(
        std::string_view text, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        EvalOutcome outcome;

        const std::string trimmed = trim_copy(text);
        if (trimmed.empty())
            return outcome;

        if (starts_with_keyword(trimmed, "let")) {
            std::string binding_text = trim_copy(trimmed.substr(3));
            if (!binding_text.empty() && binding_text.back() == ';')
                binding_text = trim_copy(binding_text.substr(0, binding_text.size() - 1));
            const auto equal_pos = find_top_level_char(binding_text, '=');

            std::string lhs =
                !equal_pos.has_value() ? trim_copy(binding_text)
                                       : trim_copy(binding_text.substr(0, *equal_pos));
            std::string rhs =
                !equal_pos.has_value() ? std::string{} : trim_copy(binding_text.substr(*equal_pos + 1));

            std::string name = lhs;
            std::string declared_type;
            const auto colon_pos = find_top_level_char(lhs, ':');
            if (colon_pos.has_value()) {
                name = trim_copy(lhs.substr(0, *colon_pos));
                declared_type = trim_copy(lhs.substr(*colon_pos + 1));
            }

            if (name.empty()) {
                push_diagnostic(
                    diagnostics, DiagnosticSeverity::Error, context.scope, "let binding has empty name");
                outcome.ok = false;
                return outcome;
            }

            Value value = make_unit_value();
            if (!rhs.empty()) {
                EvalOutcome rhs_result = evaluate_raw_expression_outcome(rhs, context, diagnostics);
                if (!rhs_result.ok)
                    return rhs_result;
                if (rhs_result.returned) {
                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                        "let initializer cannot return");
                    rhs_result.ok = false;
                    return rhs_result;
                }
                if (rhs_result.has_value)
                    value = rhs_result.value;
            }

            if (!declared_type.empty()) {
                const auto type_head = extract_constraint_type_head(declared_type);
                if (type_head.has_value()) {
                    std::string normalized_type = *type_head;
                    if (context.generic_bindings.contains(normalized_type))
                        normalized_type = context.generic_bindings.at(normalized_type);

                    if (is_builtin_type_name(normalized_type)
                        && !value_matches_named_type(value, normalized_type)) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "let binding type mismatch: expected '" + normalized_type + "', got '"
                                + value_runtime_type_name(value) + "'");
                        outcome.ok = false;
                        return outcome;
                    }
                }
            }

            if (name != "_")
                context.locals[name] = value;

            return outcome;
        }

        if (starts_with_keyword(trimmed, "return")) {
            std::string expr = trim_copy(trimmed.substr(6));
            if (!expr.empty() && expr.back() == ';')
                expr = trim_copy(expr.substr(0, expr.size() - 1));
            Value value = make_unit_value();

            if (!expr.empty()) {
                EvalOutcome expr_result = evaluate_raw_expression_outcome(expr, context, diagnostics);
                if (!expr_result.ok)
                    return expr_result;
                if (expr_result.returned)
                    return expr_result;
                if (expr_result.has_value)
                    value = expr_result.value;
            }

            outcome.returned = true;
            outcome.has_value = true;
            outcome.value = std::move(value);
            return outcome;
        }

        if (trimmed.starts_with("item "))
            return outcome;

        if (!trimmed.empty() && trimmed.back() == ';') {
            const std::string expr = trim_copy(trimmed.substr(0, trimmed.size() - 1));
            if (!expr.empty()) {
                EvalOutcome expression_result = evaluate_raw_expression_outcome(expr, context, diagnostics);
                if (!expression_result.ok)
                    return expression_result;
                if (expression_result.returned)
                    return expression_result;
            }
            return outcome;
        }

        EvalOutcome expression_result = evaluate_raw_expression_outcome(trimmed, context, diagnostics);
        if (!expression_result.ok)
            return expression_result;
        if (expression_result.returned)
            return expression_result;

        outcome.has_value = expression_result.has_value;
        if (expression_result.has_value)
            outcome.value = expression_result.value;
        return outcome;
    }

    [[nodiscard]] std::optional<std::string> call_expr_callee_name(const cstc::hir::Expr& callee) {
        if (const auto* path = std::get_if<cstc::hir::PathExpr>(&callee.kind); path != nullptr)
            return join_segments(path->segments);

        if (const auto* raw = std::get_if<cstc::hir::RawExpr>(&callee.kind); raw != nullptr) {
            const std::string normalized = remove_turbofish_suffix(raw->text);
            if (looks_like_path_text(normalized))
                return join_segments(split_path_segments(normalized));

            std::vector<RawToken> tokens;
            std::string lexer_error;
            RawLexer lexer{raw->text};
            if (!lexer.lex(tokens, lexer_error)) {
                return std::nullopt;
            }

            RawNodePtr node;
            std::string parser_error;
            RawParser parser{std::move(tokens)};
            if (!parser.parse(node, parser_error) || node == nullptr) {
                return std::nullopt;
            }

            if (const auto* path_node = std::get_if<RawPathNode>(&node->kind); path_node != nullptr)
                return join_segments(path_node->segments);
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Value> evaluate_hir_expr_as_value(
        const cstc::hir::Expr& expr, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        EvalOutcome outcome = evaluate_hir_expr(expr, context, diagnostics);
        if (!outcome.ok)
            return std::nullopt;

        if (outcome.returned) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "return statement is not valid in this expression context");
            return std::nullopt;
        }

        if (!outcome.has_value) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                "expression did not produce a value");
            return std::nullopt;
        }

        return outcome.value;
    }

    [[nodiscard]] EvalOutcome evaluate_hir_expr(
        const cstc::hir::Expr& expr, EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        return std::visit(
            [this, &context, &diagnostics](const auto& kind) -> EvalOutcome {
                using Kind = std::decay_t<decltype(kind)>;

                EvalOutcome outcome;

                if constexpr (std::is_same_v<Kind, cstc::hir::RawExpr>) {
                    return evaluate_raw_statement(kind.text, context, diagnostics);
                } else if constexpr (std::is_same_v<Kind, cstc::hir::LiteralExpr>) {
                    const auto literal = parse_literal_value(kind.text);
                    if (!literal.has_value()) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "unsupported literal text '" + kind.text + "'");
                        outcome.ok = false;
                        return outcome;
                    }
                    outcome.has_value = true;
                    outcome.value = *literal;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::PathExpr>) {
                    const auto value = resolve_path_value(kind.segments, context, diagnostics, context.scope);
                    if (!value.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }
                    outcome.has_value = true;
                    outcome.value = *value;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::BinaryExpr>) {
                    if (kind.lhs == nullptr || kind.rhs == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "binary expression has missing operand");
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto lhs = evaluate_hir_expr_as_value(*kind.lhs, context, diagnostics);
                    if (!lhs.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto rhs = evaluate_hir_expr_as_value(*kind.rhs, context, diagnostics);
                    if (!rhs.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto applied =
                        apply_binary_operator(kind.op, *lhs, *rhs, diagnostics, context.scope);
                    if (!applied.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    outcome.has_value = true;
                    outcome.value = *applied;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::CallExpr>) {
                    if (kind.callee == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "call expression missing callee");
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto callee_name = call_expr_callee_name(*kind.callee);
                    if (callee_name.has_value() && *callee_name == "sizeof") {
                        if (kind.args.size() != 1 || kind.args[0] == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "sizeof(...) expects one valid argument");
                            outcome.ok = false;
                            return outcome;
                        }

                        const auto arg_value = evaluate_hir_expr_as_value(*kind.args[0], context, diagnostics);
                        if (!arg_value.has_value()) {
                            outcome.ok = false;
                            return outcome;
                        }

                        if (const auto* type_name = std::get_if<std::string>(&arg_value->kind);
                            type_name != nullptr) {
                            const auto byte_size = builtin_sizeof_type(*type_name);
                            if (!byte_size.has_value()) {
                                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                    "sizeof(...) unsupported for type '" + *type_name + "'");
                                outcome.ok = false;
                                return outcome;
                            }

                            outcome.has_value = true;
                            outcome.value = Value{.kind = *byte_size};
                            return outcome;
                        }

                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "sizeof(...) argument must resolve to a type name");
                        outcome.ok = false;
                        return outcome;
                    }

                    if (callee_name.has_value() && *callee_name == "decl") {
                        if (kind.args.size() != 1 || kind.args[0] == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "decl(...) expects one valid argument");
                            outcome.ok = false;
                            return outcome;
                        }

                        const auto arg_value = evaluate_hir_expr_as_value(*kind.args[0], context, diagnostics);
                        if (!arg_value.has_value()) {
                            outcome.ok = false;
                            return outcome;
                        }

                        if (const auto* type_name = std::get_if<std::string>(&arg_value->kind);
                            type_name != nullptr) {
                            const bool known =
                                known_types_.contains(*type_name) || is_builtin_type_name(*type_name);
                            outcome.has_value = true;
                            outcome.value = Value{.kind = known};
                            return outcome;
                        }

                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "decl(...) argument must resolve to a type name");
                        outcome.ok = false;
                        return outcome;
                    }

                    std::vector<Value> args;
                    args.reserve(kind.args.size());
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "call expression has missing argument");
                            outcome.ok = false;
                            return outcome;
                        }

                        const auto value = evaluate_hir_expr_as_value(*arg, context, diagnostics);
                        if (!value.has_value()) {
                            outcome.ok = false;
                            return outcome;
                        }
                        args.push_back(*value);
                    }

                    std::optional<Value> callee_value;
                    if (const auto* callee_path = std::get_if<cstc::hir::PathExpr>(&kind.callee->kind);
                        callee_path != nullptr) {
                        callee_value =
                            resolve_path_value(callee_path->segments, context, diagnostics, context.scope);
                    } else if (callee_name.has_value()) {
                        callee_value =
                            resolve_path_value(split_path_segments(*callee_name), context, diagnostics,
                                context.scope);
                    }

                    if (!callee_value.has_value()) {
                        const auto evaluated =
                            evaluate_hir_expr_as_value(*kind.callee, context, diagnostics);
                        if (!evaluated.has_value()) {
                            outcome.ok = false;
                            return outcome;
                        }
                        callee_value = *evaluated;
                    }

                    const auto value = execute_callable(*callee_value, args, context, diagnostics);
                    if (!value.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    outcome.has_value = true;
                    outcome.value = *value;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::MemberAccessExpr>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "member access expression missing receiver");
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto receiver = evaluate_hir_expr_as_value(*kind.receiver, context, diagnostics);
                    if (!receiver.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    if (const auto* object = std::get_if<ObjectValue>(&receiver->kind); object != nullptr) {
                        const auto field = object->fields.find(kind.member);
                        if (field == object->fields.end() || field->second == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "unknown field '" + kind.member + "' on type '" + object->type_name
                                    + "'");
                            outcome.ok = false;
                            return outcome;
                        }

                        outcome.has_value = true;
                        outcome.value = *field->second;
                        return outcome;
                    }

                    if (const auto* text = std::get_if<std::string>(&receiver->kind);
                        text != nullptr && kind.member == "len") {
                        outcome.has_value = true;
                        outcome.value = Value{
                            .kind = static_cast<std::int64_t>(text->size()),
                        };
                        return outcome;
                    }

                    push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                        "member access is not supported on value type '"
                            + value_runtime_type_name(*receiver) + "'");
                    outcome.ok = false;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::MemberCallExpr>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "member call expression missing receiver");
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto receiver = evaluate_hir_expr_as_value(*kind.receiver, context, diagnostics);
                    if (!receiver.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    std::vector<Value> args;
                    args.push_back(*receiver);
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "member call argument is missing");
                            outcome.ok = false;
                            return outcome;
                        }

                        const auto value = evaluate_hir_expr_as_value(*arg, context, diagnostics);
                        if (!value.has_value()) {
                            outcome.ok = false;
                            return outcome;
                        }
                        args.push_back(*value);
                    }

                    const std::string method_name =
                        value_runtime_type_name(*receiver) + "::"
                        + remove_turbofish_suffix(kind.member);
                    const auto value = execute_function(method_name, args, context.mode,
                        context.call_depth + 1, context.call_depth_limit, diagnostics, context.scope);
                    if (!value.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    outcome.has_value = true;
                    outcome.value = *value;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::StaticMemberAccessExpr>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "static member access expression missing receiver");
                        outcome.ok = false;
                        return outcome;
                    }

                    const auto receiver = evaluate_hir_expr_as_value(*kind.receiver, context, diagnostics);
                    if (!receiver.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    const std::string method_name = format_type_inline(kind.receiver_type) + "::"
                        + remove_turbofish_suffix(kind.member);
                    const auto value = execute_function(method_name, {*receiver}, context.mode,
                        context.call_depth + 1, context.call_depth_limit, diagnostics, context.scope);
                    if (!value.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    outcome.has_value = true;
                    outcome.value = *value;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::StaticMemberCallExpr>) {
                    if (kind.receiver == nullptr) {
                        push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                            "static member call expression missing receiver");
                        outcome.ok = false;
                        return outcome;
                    }

                    std::vector<Value> args;
                    const auto receiver = evaluate_hir_expr_as_value(*kind.receiver, context, diagnostics);
                    if (!receiver.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    args.push_back(*receiver);
                    for (const auto& arg : kind.args) {
                        if (arg == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "static member call argument is missing");
                            outcome.ok = false;
                            return outcome;
                        }

                        const auto value = evaluate_hir_expr_as_value(*arg, context, diagnostics);
                        if (!value.has_value()) {
                            outcome.ok = false;
                            return outcome;
                        }
                        args.push_back(*value);
                    }

                    const std::string method_name = format_type_inline(kind.receiver_type) + "::"
                        + remove_turbofish_suffix(kind.member);
                    const auto value = execute_function(method_name, args, context.mode,
                        context.call_depth + 1, context.call_depth_limit, diagnostics, context.scope);
                    if (!value.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    outcome.has_value = true;
                    outcome.value = *value;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::ContractBlockExpr>) {
                    if (kind.kind == cstc::hir::ContractBlockKind::Runtime
                        && context.mode == ExecutionMode::ConstEval) {
                        return outcome;
                    }

                    Value last_value = make_unit_value();
                    bool has_last_value = false;

                    for (const auto& body_expr : kind.body) {
                        if (body_expr == nullptr) {
                            push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                                "contract block has missing expression");
                            outcome.ok = false;
                            return outcome;
                        }

                        EvalOutcome body_result = evaluate_hir_expr(*body_expr, context, diagnostics);
                        if (!body_result.ok) {
                            outcome.ok = false;
                            return outcome;
                        }

                        if (body_result.returned)
                            return body_result;

                        if (body_result.has_value) {
                            has_last_value = true;
                            last_value = body_result.value;
                        }
                    }

                    outcome.has_value = has_last_value;
                    if (has_last_value)
                        outcome.value = std::move(last_value);
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::LiftedConstantExpr>) {
                    EvalContext eval_context = context;
                    const auto value = evaluate_raw_expression(kind.value, eval_context, diagnostics);
                    if (!value.has_value()) {
                        outcome.ok = false;
                        return outcome;
                    }

                    context.locals[kind.name] = *value;
                    lifted_constants_[kind.name] = *value;
                    return outcome;
                } else if constexpr (std::is_same_v<Kind, cstc::hir::DeclConstraintExpr>) {
                    const bool valid = is_type_resolvable(kind.checked_type, context.generic_bindings,
                        context.generic_params, false, diagnostics, context.scope + "::constraint");
                    outcome.has_value = true;
                    outcome.value = Value{.kind = valid};
                    if (!valid)
                        outcome.ok = false;
                    return outcome;
                }

                return outcome;
            },
            expr.kind);
    }

    [[nodiscard]] bool evaluate_constraints(const cstc::hir::Declaration& declaration,
        EvalContext& context, std::vector<Diagnostic>& diagnostics) {
        for (const auto& constraint_expr : declaration.constraints) {
            if (constraint_expr == nullptr) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "constraint list has missing expression");
                return false;
            }

            EvalOutcome outcome = evaluate_hir_expr(*constraint_expr, context, diagnostics);
            if (!outcome.ok)
                return false;

            if (!outcome.has_value) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "constraint expression does not produce a boolean value");
                return false;
            }

            const auto satisfied = to_truthy_boolean(outcome.value, diagnostics, context.scope);
            if (!satisfied.has_value())
                return false;

            if (!*satisfied) {
                push_diagnostic(
                    diagnostics, DiagnosticSeverity::Error, context.scope, "constraint evaluated to false");
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::optional<Value> execute_function(std::string_view name,
        const std::vector<Value>& args, ExecutionMode mode, std::size_t call_depth,
        std::size_t call_depth_limit, std::vector<Diagnostic>& diagnostics,
        const std::string& call_scope) {
        const auto fn_it = functions_.find(std::string{name});
        if (fn_it == functions_.end()) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, call_scope,
                "unknown function '" + std::string{name} + "'");
            return std::nullopt;
        }

        const FunctionEntry& entry = fn_it->second;
        if (entry.header == nullptr || entry.declaration == nullptr) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, call_scope,
                "function '" + std::string{name} + "' has invalid declaration linkage");
            return std::nullopt;
        }

        if (call_depth > call_depth_limit) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, call_scope,
                "call depth limit exceeded while executing '" + std::string{name} + "'");
            return std::nullopt;
        }

        if (mode == ExecutionMode::ConstEval && type_is_runtime(entry.header->return_type)) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, call_scope,
                "const-eval cannot call runtime-valued function '" + std::string{name} + "'");
            return std::nullopt;
        }

        if (entry.header->params.size() != args.size()) {
            push_diagnostic(diagnostics, DiagnosticSeverity::Error, call_scope,
                "function '" + std::string{name} + "' expected "
                    + std::to_string(entry.header->params.size()) + " arguments but got "
                    + std::to_string(args.size()));
            return std::nullopt;
        }

        EvalContext context;
        context.mode = mode;
        context.call_depth = call_depth;
        context.call_depth_limit = call_depth_limit;
        context.allow_type_symbols = true;
        context.scope = std::string{name};

        std::unordered_set<std::string> generic_params;
        for (const std::string& generic_param : entry.header->generic_params)
            generic_params.insert(generic_param);
        context.generic_params = generic_params;

        for (std::size_t index = 0; index < args.size(); ++index) {
            const auto& param = entry.header->params[index];
            context.locals[param.name] = args[index];

            if (!value_matches_type(args[index], param.type, generic_params, context.generic_bindings,
                    diagnostics, context.scope)) {
                return std::nullopt;
            }
        }

        if (!evaluate_constraints(*entry.declaration, context, diagnostics))
            return std::nullopt;

        Value last_value = make_unit_value();
        bool has_last_value = false;

        for (const auto& body_expr : entry.declaration->body) {
            if (body_expr == nullptr) {
                push_diagnostic(diagnostics, DiagnosticSeverity::Error, context.scope,
                    "function body contains a missing expression");
                return std::nullopt;
            }

            EvalOutcome expression_result = evaluate_hir_expr(*body_expr, context, diagnostics);
            if (!expression_result.ok)
                return std::nullopt;

            if (expression_result.returned) {
                if (!value_matches_type(expression_result.value, entry.header->return_type,
                        generic_params, context.generic_bindings, diagnostics, context.scope)) {
                    return std::nullopt;
                }
                return expression_result.value;
            }

            if (expression_result.has_value) {
                has_last_value = true;
                last_value = expression_result.value;
            }
        }

        if (!has_last_value)
            last_value = make_unit_value();

        if (!value_matches_type(last_value, entry.header->return_type, generic_params,
                context.generic_bindings, diagnostics, context.scope)) {
            return std::nullopt;
        }

        return last_value;
    }

    cstc::hir::Module& module_;
    std::unordered_set<std::string> known_types_;
    std::unordered_map<std::string, FunctionEntry> functions_;
    std::unordered_map<std::string, Value> lifted_constants_;
    std::unordered_map<std::string, Value> repl_locals_;
};

HirInterpreter::HirInterpreter(cstc::hir::Module& module)
    : impl_(std::make_unique<Impl>(module)) {}

HirInterpreter::~HirInterpreter() = default;

HirInterpreter::HirInterpreter(HirInterpreter&&) noexcept = default;

HirInterpreter& HirInterpreter::operator=(HirInterpreter&&) noexcept = default;

ValidationResult HirInterpreter::validate() { return impl_->validate(); }

MaterializationResult HirInterpreter::materialize_const_types() {
    return impl_->materialize_const_types();
}

ExecutionResult HirInterpreter::run(const RunOptions& options) { return impl_->run(options); }

ExecutionResult HirInterpreter::eval_repl_line(std::string_view line) {
    return impl_->eval_repl_line(line);
}

const std::unordered_map<std::string, Value>& HirInterpreter::lifted_constants() const {
    return impl_->lifted_constants_;
}

} // namespace cstc::hir::interpreter
