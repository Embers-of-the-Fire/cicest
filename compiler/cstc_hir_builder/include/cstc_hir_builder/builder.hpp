#ifndef CICEST_COMPILER_CSTC_HIR_BUILDER_BUILDER_HPP
#define CICEST_COMPILER_CSTC_HIR_BUILDER_BUILDER_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_hir/hir.hpp>

namespace cstc::hir::builder {

namespace detail {

class AstToHirLowerer {
public:
    explicit AstToHirLowerer(const cstc::ast::SymbolTable* symbols)
        : symbols_(symbols) {}

    [[nodiscard]] cstc::hir::Module lower_crate(const cstc::ast::Crate& crate) const {
        cstc::hir::Module module;
        for (const auto& item : crate.items)
            lower_item(item, module.declarations);
        return module;
    }

private:
    [[nodiscard]] std::string symbol_text(cstc::ast::Symbol symbol) const {
        if (symbols_ != nullptr) {
            const auto symbol_name = symbols_->str(symbol);
            if (!symbol_name.empty())
                return std::string{symbol_name};
        }
        return "$" + std::to_string(symbol.symbol_id);
    }

    [[nodiscard]] static std::string keyword_text(cstc::ast::KeywordKind kind) {
        switch (kind) {
        case cstc::ast::KeywordKind::Runtime: return "runtime";
        case cstc::ast::KeywordKind::NotRuntime: return "const";
        }

        return "<unknown-keyword>";
    }

    [[nodiscard]] static cstc::hir::TypeContractKind
        map_type_contract_kind(cstc::ast::KeywordKind kind) {
        switch (kind) {
        case cstc::ast::KeywordKind::Runtime: return cstc::hir::TypeContractKind::Runtime;
        case cstc::ast::KeywordKind::NotRuntime: return cstc::hir::TypeContractKind::NotRuntime;
        }

        return cstc::hir::TypeContractKind::Runtime;
    }

    [[nodiscard]] static std::optional<cstc::hir::ContractBlockKind>
        map_contract_block_kind(cstc::ast::KeywordKind kind) {
        switch (kind) {
        case cstc::ast::KeywordKind::Runtime: return cstc::hir::ContractBlockKind::Runtime;
        case cstc::ast::KeywordKind::NotRuntime: return cstc::hir::ContractBlockKind::Const;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string format_path(const cstc::ast::Path& path) const {
        if (path.segments.empty())
            return "<empty-path>";

        std::string out;
        for (std::size_t index = 0; index < path.segments.size(); ++index) {
            if (index != 0)
                out += "::";
            out += symbol_text(path.segments[index].name);
        }
        return out;
    }

    [[nodiscard]] std::string
        format_generic_args(const std::optional<cstc::ast::GenericArgs>& args) const {
        if (!args.has_value())
            return {};

        std::string out = "<";
        for (std::size_t index = 0; index < args->args.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += format_type(*args->args[index]);
        }
        out += ">";
        return out;
    }

    [[nodiscard]] std::string format_generic_args(const cstc::ast::GenericArgs& args) const {
        std::string out = "<";
        for (std::size_t index = 0; index < args.args.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += format_type(*args.args[index]);
        }
        out += ">";
        return out;
    }

    [[nodiscard]] std::string format_type(const cstc::ast::TypeNode& type) const {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::PathType>) {
                    return format_path(kind.path) + format_generic_args(kind.args);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::KeywordType>) {
                    std::string out;
                    for (const auto& keyword : kind.keywords) {
                        if (!out.empty())
                            out += ' ';
                        out += keyword_text(keyword.kind);
                    }

                    if (!out.empty())
                        out += ' ';
                    out += format_type(*kind.inner);
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::RefType>) {
                    return "&" + format_type(*kind.inner);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::FnPointerType>) {
                    std::string out = "fn(";
                    for (std::size_t index = 0; index < kind.params.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_type(*kind.params[index]);
                    }
                    out += ") -> ";
                    out += format_type(*kind.ret);
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::InferredType>) {
                    return "_";
                }

                return "<unknown-type>";
            },
            type.kind);
    }

    [[nodiscard]] std::string format_pat(const cstc::ast::Pat& pat) const {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::WildcardPat>) {
                    return "_";
                } else if constexpr (std::is_same_v<Kind, cstc::ast::BindingPat>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::LitPat>) {
                    return kind.lit.value;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorFieldsPat>) {
                    std::string out = format_path(kind.constructor) + " { ";
                    for (std::size_t index = 0; index < kind.fields.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += symbol_text(kind.fields[index].name);
                        out += ": ";
                        out += format_pat(*kind.fields[index].pat);
                    }
                    out += " }";
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorPositionalPat>) {
                    std::string out = format_path(kind.constructor) + "(";
                    for (std::size_t index = 0; index < kind.args.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_pat(*kind.args[index]);
                    }
                    out += ')';
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorUnitPat>) {
                    return format_path(kind.constructor);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TuplePat>) {
                    std::string out = "(";
                    for (std::size_t index = 0; index < kind.elements.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_pat(*kind.elements[index]);
                    }
                    out += ')';
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::OrPat>) {
                    std::string out;
                    for (std::size_t index = 0; index < kind.alternatives.size(); ++index) {
                        if (index != 0)
                            out += " | ";
                        out += format_pat(*kind.alternatives[index]);
                    }
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::AsPat>) {
                    return symbol_text(kind.name) + " @ " + format_pat(*kind.inner);
                }

                return "<unknown-pat>";
            },
            pat.kind);
    }

    [[nodiscard]] static std::string unary_op_text(cstc::ast::UnaryOp op) {
        switch (op) {
        case cstc::ast::UnaryOp::Neg: return "-";
        case cstc::ast::UnaryOp::Not: return "!";
        case cstc::ast::UnaryOp::Borrow: return "&";
        case cstc::ast::UnaryOp::Deref: return "*";
        }

        return "<unknown-unary-op>";
    }

    [[nodiscard]] static std::string binary_op_text(cstc::ast::BinaryOp op) {
        switch (op) {
        case cstc::ast::BinaryOp::Add: return "+";
        case cstc::ast::BinaryOp::Sub: return "-";
        case cstc::ast::BinaryOp::Mul: return "*";
        case cstc::ast::BinaryOp::Div: return "/";
        case cstc::ast::BinaryOp::Mod: return "%";
        case cstc::ast::BinaryOp::BitAnd: return "&";
        case cstc::ast::BinaryOp::BitOr: return "|";
        case cstc::ast::BinaryOp::BitXor: return "^";
        case cstc::ast::BinaryOp::Shl: return "<<";
        case cstc::ast::BinaryOp::Shr: return ">>";
        case cstc::ast::BinaryOp::Eq: return "==";
        case cstc::ast::BinaryOp::Ne: return "!=";
        case cstc::ast::BinaryOp::Lt: return "<";
        case cstc::ast::BinaryOp::Gt: return ">";
        case cstc::ast::BinaryOp::Le: return "<=";
        case cstc::ast::BinaryOp::Ge: return ">=";
        case cstc::ast::BinaryOp::And: return "&&";
        case cstc::ast::BinaryOp::Or: return "||";
        }

        return "<unknown-binary-op>";
    }

    [[nodiscard]] std::string format_stmt_inline(const cstc::ast::Stmt& stmt) const {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::LetStmt>) {
                    std::string text = "let " + format_pat(*kind.pat);
                    if (kind.ty.has_value())
                        text += ": " + format_type(*kind.ty->get());
                    if (kind.init.has_value())
                        text += " = " + format_expr(*kind.init->get());
                    text += ';';
                    return text;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ExprStmt>) {
                    std::string text = format_expr(*kind.expr);
                    if (kind.has_semi)
                        text += ';';
                    return text;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ItemStmt>) {
                    return "item " + item_name(*kind.item);
                }

                return "<unknown-stmt>";
            },
            stmt.kind);
    }

    [[nodiscard]] std::string format_block_inline(const cstc::ast::Block& block) const {
        if (block.stmts.empty())
            return "{}";

        std::string out = "{ ";
        for (std::size_t index = 0; index < block.stmts.size(); ++index) {
            if (index != 0)
                out += ' ';
            out += format_stmt_inline(block.stmts[index]);
        }
        out += " }";
        return out;
    }

    [[nodiscard]] std::string format_expr(const cstc::ast::Expr& expr) const {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::LitExpr>) {
                    return kind.lit.value;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::PathExpr>) {
                    return format_path(kind.path);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::BlockExpr>) {
                    return format_block_inline(kind.block);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::GroupedExpr>) {
                    return "(" + format_expr(*kind.inner) + ")";
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TupleExpr>) {
                    std::string out = "(";
                    for (std::size_t index = 0; index < kind.elements.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_expr(*kind.elements[index]);
                    }
                    out += ')';
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::UnaryExpr>) {
                    return unary_op_text(kind.op) + format_expr(*kind.operand);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::BinaryExpr>) {
                    return format_expr(*kind.lhs) + " " + binary_op_text(kind.op) + " "
                        + format_expr(*kind.rhs);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::CallExpr>) {
                    std::string out = format_expr(*kind.callee) + "(";
                    for (std::size_t index = 0; index < kind.args.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_expr(*kind.args[index]);
                    }
                    out += ')';
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::MethodCallExpr>) {
                    std::string out = format_expr(*kind.receiver) + "." + symbol_text(kind.method.name);
                    if (kind.turbofish.has_value())
                        out += "::" + format_generic_args(kind.turbofish);

                    out += "(";
                    for (std::size_t index = 0; index < kind.args.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_expr(*kind.args[index]);
                    }
                    out += ')';
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::FieldExpr>) {
                    return format_expr(*kind.object) + "." + symbol_text(kind.field);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorFieldsExpr>) {
                    std::string out = format_path(kind.constructor) + " { ";
                    for (std::size_t index = 0; index < kind.fields.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += symbol_text(kind.fields[index].name);
                        out += ": ";
                        out += format_expr(*kind.fields[index].value);
                    }
                    out += " }";
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorPositionalExpr>) {
                    std::string out = format_path(kind.constructor) + "(";
                    for (std::size_t index = 0; index < kind.args.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_expr(*kind.args[index]);
                    }
                    out += ')';
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::LambdaExpr>) {
                    std::string out = "lambda(";
                    for (std::size_t index = 0; index < kind.params.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += symbol_text(kind.params[index].name);
                        if (kind.params[index].ty.has_value()) {
                            out += ": ";
                            out += format_type(*kind.params[index].ty->get());
                        }
                    }
                    out += ") ";
                    out += format_block_inline(kind.body);
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::MatchExpr>) {
                    std::string out = "match " + format_expr(*kind.scrutinee) + " { ";
                    for (std::size_t index = 0; index < kind.arms.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_pat(*kind.arms[index].pat);
                        out += " => ";
                        out += format_expr(*kind.arms[index].body);
                    }
                    out += " }";
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::IfExpr>) {
                    std::string out =
                        "if " + format_expr(*kind.cond) + " " + format_block_inline(kind.then_block);
                    if (kind.else_expr.has_value()) {
                        out += " else ";
                        out += format_expr(*kind.else_expr->get());
                    }
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::LoopExpr>) {
                    return "loop " + format_block_inline(kind.body);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ForExpr>) {
                    const std::string init =
                        kind.init.has_value() ? format_expr(*kind.init->get()) : std::string{};
                    const std::string cond =
                        kind.cond.has_value() ? format_expr(*kind.cond->get()) : std::string{};
                    const std::string step =
                        kind.step.has_value() ? format_expr(*kind.step->get()) : std::string{};
                    return "for (" + init + "; " + cond + "; " + step + ") "
                        + format_block_inline(kind.body);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ReturnExpr>) {
                    if (!kind.value.has_value())
                        return "return";
                    return "return " + format_expr(*kind.value->get());
                } else if constexpr (std::is_same_v<Kind, cstc::ast::KeywordBlockExpr>) {
                    if (kind.keywords.empty())
                        return format_block_inline(kind.body);

                    std::string out;
                    for (std::size_t index = 0; index < kind.keywords.size(); ++index) {
                        if (index != 0)
                            out += ' ';
                        out += keyword_text(kind.keywords[index].kind);
                    }
                    out += " ";
                    out += format_block_inline(kind.body);
                    return out;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TurbofishExpr>) {
                    return format_expr(*kind.base) + "::" + format_generic_args(kind.args);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::DeclExpr>) {
                    return "decl(" + format_type(*kind.type_expr) + ")";
                }

                return "<unknown-expr>";
            },
            expr.kind);
    }

    [[nodiscard]] cstc::hir::Type lower_type(const cstc::ast::TypeNode& type) const {
        return std::visit(
            [this](const auto& kind) -> cstc::hir::Type {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::PathType>) {
                    cstc::hir::PathType result;
                    result.segments.reserve(kind.path.segments.size());
                    for (const auto& segment : kind.path.segments)
                        result.segments.push_back(symbol_text(segment.name));

                    if (kind.args.has_value()) {
                        result.args.reserve(kind.args->args.size());
                        for (const auto& arg : kind.args->args)
                            result.args.push_back(cstc::hir::make_type(lower_type(*arg).kind));
                    }

                    return cstc::hir::Type{.kind = std::move(result)};
                } else if constexpr (std::is_same_v<Kind, cstc::ast::KeywordType>) {
                    cstc::hir::Type lowered = lower_type(*kind.inner);
                    for (auto it = kind.keywords.rbegin(); it != kind.keywords.rend(); ++it) {
                        lowered = cstc::hir::Type{
                            .kind =
                                cstc::hir::ContractType{
                                    .kind = map_type_contract_kind(it->kind),
                                    .inner = cstc::hir::make_type(std::move(lowered.kind)),
                                },
                        };
                    }
                    return lowered;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::RefType>) {
                    return cstc::hir::Type{
                        .kind =
                            cstc::hir::RefType{
                                .inner = cstc::hir::make_type(lower_type(*kind.inner).kind),
                            },
                    };
                } else if constexpr (std::is_same_v<Kind, cstc::ast::FnPointerType>) {
                    cstc::hir::FnPointerType fn;
                    fn.params.reserve(kind.params.size());
                    for (const auto& param : kind.params)
                        fn.params.push_back(cstc::hir::make_type(lower_type(*param).kind));
                    fn.result = cstc::hir::make_type(lower_type(*kind.ret).kind);

                    return cstc::hir::Type{.kind = std::move(fn)};
                } else if constexpr (std::is_same_v<Kind, cstc::ast::InferredType>) {
                    return cstc::hir::Type{.kind = cstc::hir::InferredType{}};
                }

                return cstc::hir::Type{.kind = cstc::hir::InferredType{}};
            },
            type.kind);
    }

    [[nodiscard]] cstc::hir::Type apply_fn_keywords(
        cstc::hir::Type return_type, const std::vector<cstc::ast::KeywordModifier>& keywords) const {
        for (auto it = keywords.rbegin(); it != keywords.rend(); ++it) {
            return_type = cstc::hir::Type{
                .kind =
                    cstc::hir::ContractType{
                        .kind = map_type_contract_kind(it->kind),
                        .inner = cstc::hir::make_type(std::move(return_type.kind)),
                    },
            };
        }

        return return_type;
    }

    [[nodiscard]] std::vector<cstc::hir::FnParam> lower_fn_params(const cstc::ast::FnSig& sig) const {
        std::vector<cstc::hir::FnParam> params;

        if (sig.self_param.has_value()) {
            cstc::hir::Type self_type{
                .kind =
                    cstc::hir::PathType{
                        .segments = {"Self"},
                        .args = {},
                    },
            };

            const auto& self = *sig.self_param;
            if (self.explicit_ty.has_value()) {
                self_type = lower_type(*self.explicit_ty->get());
            } else if (self.is_ref) {
                self_type = cstc::hir::Type{
                    .kind =
                        cstc::hir::RefType{
                            .inner = cstc::hir::make_type(std::move(self_type.kind)),
                        },
                };
            }

            params.push_back(cstc::hir::FnParam{
                .name = "self",
                .type = std::move(self_type),
            });
        }

        params.reserve(params.size() + sig.params.size());
        for (const auto& param : sig.params) {
            params.push_back(cstc::hir::FnParam{
                .name = symbol_text(param.name),
                .type = lower_type(*param.ty),
            });
        }

        return params;
    }

    [[nodiscard]] std::vector<std::string>
        lower_generic_params(const std::optional<cstc::ast::GenericParams>& params) const {
        std::vector<std::string> names;
        if (!params.has_value())
            return names;

        names.reserve(params->params.size());
        for (const auto& param : params->params)
            names.push_back(symbol_text(param.name));
        return names;
    }

    [[nodiscard]] cstc::hir::ExprPtr lower_expr(const cstc::ast::Expr& expr) const {
        return std::visit(
            [this](const auto& kind) -> cstc::hir::ExprPtr {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::LitExpr>) {
                    return cstc::hir::make_expr(cstc::hir::LiteralExpr{.text = kind.lit.value});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::PathExpr>) {
                    std::vector<std::string> segments;
                    segments.reserve(kind.path.segments.size());
                    for (const auto& segment : kind.path.segments)
                        segments.push_back(symbol_text(segment.name));
                    return cstc::hir::make_expr(cstc::hir::PathExpr{.segments = std::move(segments)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::BlockExpr>) {
                    return cstc::hir::make_expr(cstc::hir::RawExpr{.text = format_block_inline(kind.block)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::GroupedExpr>) {
                    return cstc::hir::make_expr(
                        cstc::hir::RawExpr{.text = "(" + format_expr(*kind.inner) + ")"});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TupleExpr>) {
                    std::string out = "(";
                    for (std::size_t index = 0; index < kind.elements.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_expr(*kind.elements[index]);
                    }
                    out += ')';
                    return cstc::hir::make_expr(cstc::hir::RawExpr{.text = std::move(out)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::UnaryExpr>) {
                    return cstc::hir::make_expr(cstc::hir::RawExpr{
                        .text = unary_op_text(kind.op) + format_expr(*kind.operand),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::BinaryExpr>) {
                    return cstc::hir::make_expr(cstc::hir::BinaryExpr{
                        .op = binary_op_text(kind.op),
                        .lhs = lower_expr(*kind.lhs),
                        .rhs = lower_expr(*kind.rhs),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::CallExpr>) {
                    std::vector<cstc::hir::ExprPtr> args;
                    args.reserve(kind.args.size());
                    for (const auto& arg : kind.args)
                        args.push_back(lower_expr(*arg));

                    return cstc::hir::make_expr(cstc::hir::CallExpr{
                        .callee = lower_expr(*kind.callee),
                        .args = std::move(args),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::MethodCallExpr>) {
                    std::string member = symbol_text(kind.method.name);
                    if (kind.turbofish.has_value())
                        member += "::" + format_generic_args(kind.turbofish);

                    std::vector<cstc::hir::ExprPtr> args;
                    args.reserve(kind.args.size());
                    for (const auto& arg : kind.args)
                        args.push_back(lower_expr(*arg));

                    return cstc::hir::make_expr(cstc::hir::MemberCallExpr{
                        .receiver = lower_expr(*kind.receiver),
                        .member = std::move(member),
                        .args = std::move(args),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::FieldExpr>) {
                    return cstc::hir::make_expr(cstc::hir::MemberAccessExpr{
                        .receiver = lower_expr(*kind.object),
                        .member = symbol_text(kind.field),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::KeywordBlockExpr>) {
                    if (kind.keywords.empty())
                        return cstc::hir::make_expr(
                            cstc::hir::RawExpr{.text = format_block_inline(kind.body)});

                    std::vector<cstc::hir::ExprPtr> body = lower_block(kind.body);
                    cstc::hir::ExprPtr current;

                    for (auto it = kind.keywords.rbegin(); it != kind.keywords.rend(); ++it) {
                        const auto block_kind = map_contract_block_kind(it->kind);
                        if (!block_kind.has_value())
                            return cstc::hir::make_expr(cstc::hir::RawExpr{.text = format_expr_from_keyword_block(kind)});

                        if (current == nullptr) {
                            current = cstc::hir::make_expr(cstc::hir::ContractBlockExpr{
                                .kind = *block_kind,
                                .body = std::move(body),
                            });
                        } else {
                            std::vector<cstc::hir::ExprPtr> nested;
                            nested.push_back(std::move(current));
                            current = cstc::hir::make_expr(cstc::hir::ContractBlockExpr{
                                .kind = *block_kind,
                                .body = std::move(nested),
                            });
                        }
                    }

                    return current;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorPositionalExpr>) {
                    std::vector<std::string> segments;
                    segments.reserve(kind.constructor.segments.size());
                    for (const auto& segment : kind.constructor.segments)
                        segments.push_back(symbol_text(segment.name));

                    std::vector<cstc::hir::ExprPtr> args;
                    args.reserve(kind.args.size());
                    for (const auto& arg : kind.args)
                        args.push_back(lower_expr(*arg));

                    return cstc::hir::make_expr(cstc::hir::CallExpr{
                        .callee = cstc::hir::make_expr(cstc::hir::PathExpr{.segments = std::move(segments)}),
                        .args = std::move(args),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConstructorFieldsExpr>) {
                    std::string out = format_path(kind.constructor) + " { ";
                    for (std::size_t index = 0; index < kind.fields.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += symbol_text(kind.fields[index].name);
                        out += ": ";
                        out += format_expr(*kind.fields[index].value);
                    }
                    out += " }";
                    return cstc::hir::make_expr(cstc::hir::RawExpr{.text = std::move(out)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::LambdaExpr>) {
                    std::string out = "lambda(";
                    for (std::size_t index = 0; index < kind.params.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += symbol_text(kind.params[index].name);
                        if (kind.params[index].ty.has_value()) {
                            out += ": ";
                            out += format_type(*kind.params[index].ty->get());
                        }
                    }
                    out += ") ";
                    out += format_block_inline(kind.body);
                    return cstc::hir::make_expr(cstc::hir::RawExpr{.text = std::move(out)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::MatchExpr>) {
                    std::string out = "match " + format_expr(*kind.scrutinee) + " { ";
                    for (std::size_t index = 0; index < kind.arms.size(); ++index) {
                        if (index != 0)
                            out += ", ";
                        out += format_pat(*kind.arms[index].pat);
                        out += " => ";
                        out += format_expr(*kind.arms[index].body);
                    }
                    out += " }";
                    return cstc::hir::make_expr(cstc::hir::RawExpr{.text = std::move(out)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::IfExpr>) {
                    std::string out =
                        "if " + format_expr(*kind.cond) + " " + format_block_inline(kind.then_block);
                    if (kind.else_expr.has_value()) {
                        out += " else ";
                        out += format_expr(*kind.else_expr->get());
                    }
                    return cstc::hir::make_expr(cstc::hir::RawExpr{.text = std::move(out)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::LoopExpr>) {
                    return cstc::hir::make_expr(
                        cstc::hir::RawExpr{.text = "loop " + format_block_inline(kind.body)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ForExpr>) {
                    const std::string init =
                        kind.init.has_value() ? format_expr(*kind.init->get()) : std::string{};
                    const std::string cond =
                        kind.cond.has_value() ? format_expr(*kind.cond->get()) : std::string{};
                    const std::string step =
                        kind.step.has_value() ? format_expr(*kind.step->get()) : std::string{};
                    return cstc::hir::make_expr(cstc::hir::RawExpr{
                        .text = "for (" + init + "; " + cond + "; " + step + ") "
                            + format_block_inline(kind.body),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ReturnExpr>) {
                    return cstc::hir::make_expr(
                        cstc::hir::RawExpr{.text = kind.value.has_value()
                                ? "return " + format_expr(*kind.value->get())
                                : "return"});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TurbofishExpr>) {
                    return cstc::hir::make_expr(
                        cstc::hir::RawExpr{.text = format_expr(*kind.base) + "::" + format_generic_args(kind.args)});
                } else if constexpr (std::is_same_v<Kind, cstc::ast::DeclExpr>) {
                    return cstc::hir::make_expr(
                        cstc::hir::RawExpr{.text = "decl(" + format_type(*kind.type_expr) + ")"});
                }

                return cstc::hir::make_expr(cstc::hir::RawExpr{.text = "<unknown-expr>"});
            },
            expr.kind);
    }

    [[nodiscard]] std::string
        format_expr_from_keyword_block(const cstc::ast::KeywordBlockExpr& expr) const {
        if (expr.keywords.empty())
            return format_block_inline(expr.body);

        std::string out;
        for (std::size_t index = 0; index < expr.keywords.size(); ++index) {
            if (index != 0)
                out += ' ';
            out += keyword_text(expr.keywords[index].kind);
        }
        out += " ";
        out += format_block_inline(expr.body);
        return out;
    }

    [[nodiscard]] std::vector<cstc::hir::ExprPtr> lower_block(const cstc::ast::Block& block) const {
        std::vector<cstc::hir::ExprPtr> body;
        for (const auto& stmt : block.stmts)
            lower_stmt(stmt, body);
        return body;
    }

    void lower_stmt(
        const cstc::ast::Stmt& stmt, std::vector<cstc::hir::ExprPtr>& body) const {
        std::visit(
            [this, &body](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::LetStmt>) {
                    std::string text = "let " + format_pat(*kind.pat);
                    if (kind.ty.has_value())
                        text += ": " + format_type(*kind.ty->get());
                    if (kind.init.has_value())
                        text += " = " + format_expr(*kind.init->get());
                    body.push_back(cstc::hir::make_expr(cstc::hir::RawExpr{.text = std::move(text)}));
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ExprStmt>) {
                    if (kind.has_semi) {
                        body.push_back(cstc::hir::make_expr(
                            cstc::hir::RawExpr{.text = format_expr(*kind.expr) + ";"}));
                    } else {
                        body.push_back(lower_expr(*kind.expr));
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ItemStmt>) {
                    body.push_back(
                        cstc::hir::make_expr(cstc::hir::RawExpr{.text = "item " + item_name(*kind.item)}));
                }
            },
            stmt.kind);
    }

    void append_constraints(
        std::vector<cstc::hir::ExprPtr>& constraints,
        const std::optional<cstc::ast::WhereClause>& where_clause) const {
        if (!where_clause.has_value())
            return;

        for (const auto& predicate : where_clause->predicates) {
            if (const auto* decl_expr = std::get_if<cstc::ast::DeclExpr>(&predicate.expr->kind);
                decl_expr != nullptr) {
                constraints.push_back(cstc::hir::make_expr(cstc::hir::DeclConstraintExpr{
                    .checked_type = lower_type(*decl_expr->type_expr),
                }));
                continue;
            }

            constraints.push_back(lower_expr(*predicate.expr));
        }
    }

    [[nodiscard]] std::string item_name(const cstc::ast::Item& item) const {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::FnItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ImportItem>) {
                    return "import<" + kind.source + ">";
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ExternFnItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::MarkerStructItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::NamedStructItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TupleStructItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::EnumItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TypeAliasItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConceptItem>) {
                    return symbol_text(kind.name);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::WithItem>) {
                    return "with<" + format_type(*kind.target_ty) + ">";
                }

                return "<unknown-item>";
            },
            item.kind);
    }

    [[nodiscard]] std::string
        format_generic_params(const std::optional<cstc::ast::GenericParams>& params) const {
        if (!params.has_value() || params->params.empty())
            return {};

        std::string out = "<";
        for (std::size_t index = 0; index < params->params.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += symbol_text(params->params[index].name);
        }
        out += '>';
        return out;
    }

    [[nodiscard]] std::string
        format_fn_signature(const cstc::ast::FnSig& sig, bool include_self) const {
        std::string out = "(";
        bool first = true;

        if (include_self && sig.self_param.has_value()) {
            const auto& self = *sig.self_param;
            if (self.is_ref) {
                out += "&self";
            } else {
                out += "self";
                if (self.explicit_ty.has_value())
                    out += ": " + format_type(*self.explicit_ty->get());
            }
            first = false;
        }

        for (const auto& param : sig.params) {
            if (!first)
                out += ", ";
            out += symbol_text(param.name);
            out += ": ";
            out += format_type(*param.ty);
            first = false;
        }

        out += ") -> ";
        out += format_type(*sig.ret_ty);
        return out;
    }

    [[nodiscard]] std::string
        format_where_clause(const std::optional<cstc::ast::WhereClause>& where_clause) const {
        if (!where_clause.has_value() || where_clause->predicates.empty())
            return {};

        std::string out = " where ";
        for (std::size_t index = 0; index < where_clause->predicates.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += format_expr(*where_clause->predicates[index].expr);
        }
        return out;
    }

    [[nodiscard]] std::string
        format_struct_fields(const std::vector<cstc::ast::StructField>& fields) const {
        if (fields.empty())
            return "{}";

        std::string out = "{ ";
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += symbol_text(fields[index].name);
            out += ": ";
            out += format_type(*fields[index].ty);
        }
        out += " }";
        return out;
    }

    [[nodiscard]] std::string
        format_tuple_fields(const std::vector<std::unique_ptr<cstc::ast::TypeNode>>& fields) const {
        std::string out = "(";
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (index != 0)
                out += ", ";
            out += format_type(*fields[index]);
        }
        out += ')';
        return out;
    }

    [[nodiscard]] std::string format_enum_variant(const cstc::ast::EnumVariant& variant) const {
        return std::visit(
            [this, &variant](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;
                const std::string name = symbol_text(variant.name);

                if constexpr (std::is_same_v<Kind, cstc::ast::UnitVariant>) {
                    return name;
                } else if constexpr (std::is_same_v<Kind, cstc::ast::FieldsVariant>) {
                    return name + " " + format_struct_fields(kind.fields);
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TupleVariant>) {
                    return name + format_tuple_fields(kind.types);
                }

                return name;
            },
            variant.kind);
    }

    [[nodiscard]] std::string
        format_keyword_modifiers(const std::vector<cstc::ast::KeywordModifier>& keywords) const {
        std::string out;
        for (std::size_t index = 0; index < keywords.size(); ++index) {
            if (index != 0)
                out += ' ';
            out += keyword_text(keywords[index].kind);
        }
        return out;
    }

    [[nodiscard]] std::string format_concept_method(const cstc::ast::ConceptMethod& method) const {
        std::string out = format_keyword_modifiers(method.keywords);
        if (!out.empty())
            out += ' ';

        out += "fn ";
        out += symbol_text(method.name);
        out += format_generic_params(method.generics.params);
        out += format_fn_signature(method.sig, true);
        out += format_where_clause(method.generics.where_clause);
        out += ';';
        return out;
    }

    [[nodiscard]] cstc::hir::Declaration lower_fn_item(
        std::string name, bool is_exported, const std::vector<cstc::ast::KeywordModifier>& keywords,
        const std::vector<std::string>& generic_prefix, const cstc::ast::Generics& generics,
        const cstc::ast::FnSig& sig, const cstc::ast::Block& body,
        const std::optional<cstc::ast::WhereClause>& extra_where_clause) const {
        auto generic_params = generic_prefix;
        const auto method_generics = lower_generic_params(generics.params);
        generic_params.insert(generic_params.end(), method_generics.begin(), method_generics.end());

        std::vector<cstc::hir::ExprPtr> constraints;
        append_constraints(constraints, extra_where_clause);
        append_constraints(constraints, generics.where_clause);

        return cstc::hir::Declaration{
            .header =
                cstc::hir::FunctionDecl{
                    .name = std::move(name),
                    .generic_params = std::move(generic_params),
                    .params = lower_fn_params(sig),
                    .return_type = apply_fn_keywords(lower_type(*sig.ret_ty), keywords),
                    .is_exported = is_exported,
                },
            .body = lower_block(body),
            .constraints = std::move(constraints),
        };
    }

    void lower_item(
        const cstc::ast::Item& item, std::vector<cstc::hir::Declaration>& declarations) const {
        std::visit(
            [this, &declarations](const auto& kind) {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, cstc::ast::ImportItem>) {
                    std::vector<cstc::hir::ImportSpecifier> specifiers;
                    specifiers.reserve(kind.specifiers.size());

                    for (const auto& specifier : kind.specifiers) {
                        const auto imported_name = symbol_text(specifier.imported_name);
                        const bool has_alias = specifier.local_name.has_value();

                        specifiers.push_back(cstc::hir::ImportSpecifier{
                            .imported_name = imported_name,
                            .local_name = has_alias ? symbol_text(*specifier.local_name)
                                                    : imported_name,
                            .has_alias = has_alias,
                        });
                    }

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::ImportDecl{
                                .source = kind.source,
                                .specifiers = std::move(specifiers),
                            },
                        .body = {},
                        .constraints = {},
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::FnItem>) {
                    declarations.push_back(lower_fn_item(symbol_text(kind.name), kind.is_exported,
                        kind.keywords, {}, kind.generics, kind.sig, kind.body, std::nullopt));
                } else if constexpr (std::is_same_v<Kind, cstc::ast::WithItem>) {
                    const auto with_generics = lower_generic_params(kind.generic_params);
                    const std::string target_name = format_type(*kind.target_ty);

                    if (kind.methods.empty()) {
                        declarations.push_back(cstc::hir::Declaration{
                            .header =
                                cstc::hir::RawDecl{
                                    .name = "with<" + target_name + ">",
                                    .text = "with" + format_generic_params(kind.generic_params) + " "
                                        + target_name + format_where_clause(kind.where_clause),
                                },
                            .body = {},
                            .constraints = {},
                        });
                    } else {
                        for (const auto& method : kind.methods) {
                            declarations.push_back(lower_fn_item(target_name + "::"
                                        + symbol_text(method.name),
                                false, method.keywords, with_generics, method.generics, method.sig,
                                method.body,
                                kind.where_clause));
                        }
                    }
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ExternFnItem>) {
                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "extern fn " + symbol_text(kind.name)
                                    + format_fn_signature(kind.sig, true),
                            },
                        .body = {},
                        .constraints = {},
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::MarkerStructItem>) {
                    std::vector<cstc::hir::ExprPtr> constraints;
                    append_constraints(constraints, kind.generics.where_clause);

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "struct " + symbol_text(kind.name)
                                    + format_generic_params(kind.generics.params) + ";",
                            },
                        .body = {},
                        .constraints = std::move(constraints),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::NamedStructItem>) {
                    std::vector<cstc::hir::ExprPtr> constraints;
                    append_constraints(constraints, kind.generics.where_clause);

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "struct " + symbol_text(kind.name)
                                    + format_generic_params(kind.generics.params) + " "
                                    + format_struct_fields(kind.fields),
                            },
                        .body = {},
                        .constraints = std::move(constraints),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TupleStructItem>) {
                    std::vector<cstc::hir::ExprPtr> constraints;
                    append_constraints(constraints, kind.generics.where_clause);

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "struct " + symbol_text(kind.name)
                                    + format_generic_params(kind.generics.params)
                                    + format_tuple_fields(kind.fields) + ";",
                            },
                        .body = {},
                        .constraints = std::move(constraints),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::EnumItem>) {
                    std::vector<cstc::hir::ExprPtr> constraints;
                    append_constraints(constraints, kind.generics.where_clause);

                    std::string enum_body = "{ ";
                    for (std::size_t index = 0; index < kind.variants.size(); ++index) {
                        if (index != 0)
                            enum_body += ", ";
                        enum_body += format_enum_variant(kind.variants[index]);
                    }
                    enum_body += " }";

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "enum " + symbol_text(kind.name)
                                    + format_generic_params(kind.generics.params) + " " + enum_body,
                            },
                        .body = {},
                        .constraints = std::move(constraints),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::TypeAliasItem>) {
                    std::vector<cstc::hir::ExprPtr> constraints;
                    append_constraints(constraints, kind.generics.where_clause);

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "type " + symbol_text(kind.name)
                                    + format_generic_params(kind.generics.params) + " = "
                                    + format_type(*kind.ty) + ";",
                            },
                        .body = {},
                        .constraints = std::move(constraints),
                    });
                } else if constexpr (std::is_same_v<Kind, cstc::ast::ConceptItem>) {
                    std::vector<cstc::hir::ExprPtr> constraints;
                    append_constraints(constraints, kind.generics.where_clause);

                    std::string concept_body = "{}";
                    if (!kind.methods.empty()) {
                        concept_body = "{ ";
                        for (std::size_t index = 0; index < kind.methods.size(); ++index) {
                            if (index != 0)
                                concept_body += " ";
                            concept_body += format_concept_method(kind.methods[index]);
                        }
                        concept_body += " }";
                    }

                    declarations.push_back(cstc::hir::Declaration{
                        .header =
                            cstc::hir::RawDecl{
                                .name = symbol_text(kind.name),
                                .text = "concept " + symbol_text(kind.name)
                                    + format_generic_params(kind.generics.params)
                                    + format_where_clause(kind.generics.where_clause) + " " + concept_body,
                            },
                        .body = {},
                        .constraints = std::move(constraints),
                    });
                }
            },
            item.kind);
    }

    const cstc::ast::SymbolTable* symbols_;
};

} // namespace detail

class HirBuilder {
public:
    explicit HirBuilder(const cstc::ast::SymbolTable* symbols = nullptr)
        : symbols_(symbols) {}

    [[nodiscard]] cstc::hir::Module lower(const cstc::ast::Crate& crate) const {
        return detail::AstToHirLowerer{symbols_}.lower_crate(crate);
    }

private:
    const cstc::ast::SymbolTable* symbols_;
};

[[nodiscard]] inline cstc::hir::Module
    lower_ast_to_hir(const cstc::ast::Crate& crate, const cstc::ast::SymbolTable* symbols = nullptr) {
    return HirBuilder{symbols}.lower(crate);
}

} // namespace cstc::hir::builder

#endif // CICEST_COMPILER_CSTC_HIR_BUILDER_BUILDER_HPP
