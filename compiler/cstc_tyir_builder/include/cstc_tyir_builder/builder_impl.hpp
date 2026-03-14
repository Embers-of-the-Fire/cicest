#ifndef CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_IMPL_HPP
#define CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_IMPL_HPP

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <cstc_symbol/symbol.hpp>

namespace cstc::tyir_builder {

namespace detail {

// ─── Type environment ────────────────────────────────────────────────────────

/// Signature stored for each top-level function.
struct FnSignature {
    std::vector<tyir::Ty> param_types;
    tyir::Ty return_ty;
    cstc::span::SourceSpan span;
};

/// Global type and function environment built during the collection passes.
struct TypeEnv {
    /// Maps each struct name → its resolved field list.
    std::unordered_map<
        cstc::symbol::Symbol, std::vector<tyir::TyFieldDecl>, cstc::symbol::SymbolHash>
        struct_fields;

    /// Maps each enum name → its variant list.
    std::unordered_map<
        cstc::symbol::Symbol, std::vector<tyir::TyEnumVariant>, cstc::symbol::SymbolHash>
        enum_variants;

    /// Maps each function name → its signature.
    std::unordered_map<cstc::symbol::Symbol, FnSignature, cstc::symbol::SymbolHash> fn_signatures;

    [[nodiscard]] bool is_struct(cstc::symbol::Symbol name) const {
        return struct_fields.count(name) > 0;
    }
    [[nodiscard]] bool is_enum(cstc::symbol::Symbol name) const {
        return enum_variants.count(name) > 0;
    }
    [[nodiscard]] bool is_fn(cstc::symbol::Symbol name) const {
        return fn_signatures.count(name) > 0;
    }
    [[nodiscard]] bool is_named_type(cstc::symbol::Symbol name) const {
        return is_struct(name) || is_enum(name);
    }

    /// Returns the resolved type of `field_name` on struct `struct_name`, or
    /// `std::nullopt` if the field does not exist.
    [[nodiscard]] std::optional<tyir::Ty>
        field_ty(cstc::symbol::Symbol struct_name, cstc::symbol::Symbol field_name) const {
        const auto it = struct_fields.find(struct_name);
        if (it == struct_fields.end())
            return std::nullopt;
        for (const tyir::TyFieldDecl& f : it->second) {
            if (f.name == field_name)
                return f.ty;
        }
        return std::nullopt;
    }

    /// Returns true when `variant_name` is a declared variant of `enum_name`.
    [[nodiscard]] bool
        has_variant(cstc::symbol::Symbol enum_name, cstc::symbol::Symbol variant_name) const {
        const auto it = enum_variants.find(enum_name);
        if (it == enum_variants.end())
            return false;
        for (const tyir::TyEnumVariant& v : it->second) {
            if (v.name == variant_name)
                return true;
        }
        return false;
    }
};

// ─── Scope (local variable environment) ─────────────────────────────────────

/// Scoped stack of local variable type bindings.
///
/// A new stack frame is pushed on entry to every block or `for`-init scope and
/// popped on exit.
class Scope {
public:
    void push() { frames_.push_back({}); }
    void pop() {
        assert(!frames_.empty());
        frames_.pop_back();
    }

    void insert(cstc::symbol::Symbol name, tyir::Ty ty) {
        assert(!frames_.empty());
        frames_.back().emplace(name, std::move(ty));
    }

    [[nodiscard]] std::optional<tyir::Ty> lookup(cstc::symbol::Symbol name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }
        return std::nullopt;
    }

private:
    std::vector<std::unordered_map<cstc::symbol::Symbol, tyir::Ty, cstc::symbol::SymbolHash>>
        frames_;
};

// ─── Lowering context ────────────────────────────────────────────────────────

/// Per-function lowering state threaded through all expression / statement
/// lowering functions.
struct LowerCtx {
    const TypeEnv& env;
    Scope scope;
    /// Return type of the function currently being lowered.
    tyir::Ty current_return_ty;
};

// ─── Type compatibility ──────────────────────────────────────────────────────

/// Returns true when `actual` may appear where `expected` is required.
///
/// `Never` (bottom type) is compatible with any expected type.
[[nodiscard]] inline bool compatible(const tyir::Ty& actual, const tyir::Ty& expected) {
    if (actual.is_never())
        return true;
    return actual == expected;
}

// ─── Error helpers ────────────────────────────────────────────────────────────

[[nodiscard]] inline std::unexpected<LowerError>
    make_error(cstc::span::SourceSpan span, std::string msg) {
    return std::unexpected(LowerError{span, std::move(msg)});
}

// ─── Type resolution ─────────────────────────────────────────────────────────

/// Converts an AST `TypeRef` to a `tyir::Ty`, validating named types against
/// the type environment.
[[nodiscard]] inline std::expected<tyir::Ty, LowerError>
    lower_type(const ast::TypeRef& ref, const TypeEnv& env, cstc::span::SourceSpan span) {
    switch (ref.kind) {
    case ast::TypeKind::Unit: return tyir::ty::unit();
    case ast::TypeKind::Num: return tyir::ty::num();
    case ast::TypeKind::Str: return tyir::ty::str();
    case ast::TypeKind::Bool: return tyir::ty::bool_();
    case ast::TypeKind::Named:
        if (!ref.symbol.is_valid())
            return make_error(span, "invalid named type reference");
        if (!env.is_named_type(ref.symbol))
            return make_error(span, "undefined type '" + std::string(ref.symbol.as_str()) + "'");
        return tyir::ty::named(ref.symbol);
    }
    return tyir::ty::unit();
}

// ─── Forward declarations ────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyExprPtr, LowerError>
    lower_expr(const ast::ExprPtr& expr, LowerCtx& ctx);

[[nodiscard]] inline std::expected<tyir::TyBlock, LowerError>
    lower_block(const ast::BlockExpr& block, LowerCtx& ctx);

// ─── Function signature resolution ──────────────────────────────────────────

[[nodiscard]] inline std::expected<FnSignature, LowerError>
    resolve_fn_signature(const ast::FnDecl& fn, const TypeEnv& env) {
    FnSignature sig;
    sig.span = fn.span;
    if (fn.return_type.has_value()) {
        auto t = lower_type(*fn.return_type, env, fn.span);
        if (!t)
            return std::unexpected(std::move(t.error()));
        sig.return_ty = *t;
    } else {
        sig.return_ty = tyir::ty::unit();
    }
    for (const ast::Param& p : fn.params) {
        auto pt = lower_type(p.type, env, p.span);
        if (!pt)
            return std::unexpected(std::move(pt.error()));
        sig.param_types.push_back(*pt);
    }
    return sig;
}

// ─── Statement lowering ──────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyStmt, LowerError>
    lower_stmt(const ast::Stmt& stmt, LowerCtx& ctx) {
    return std::visit(
        [&](const auto& s) -> std::expected<tyir::TyStmt, LowerError> {
            using S = std::decay_t<decltype(s)>;

            if constexpr (std::is_same_v<S, ast::LetStmt>) {
                // Lower the initializer
                auto init = lower_expr(s.initializer, ctx);
                if (!init)
                    return std::unexpected(std::move(init.error()));

                // Resolve binding type (explicit annotation or infer from init)
                tyir::Ty binding_ty;
                if (s.type_annotation.has_value()) {
                    auto ann = lower_type(*s.type_annotation, ctx.env, s.span);
                    if (!ann)
                        return std::unexpected(std::move(ann.error()));
                    if (!compatible((*init)->ty, *ann))
                        return make_error(
                            s.span, "type mismatch in let binding: expected '" + ann->display()
                                        + "', found '" + (*init)->ty.display() + "'");
                    binding_ty = *ann;
                } else {
                    binding_ty = (*init)->ty;
                }

                // Register binding in scope (unless discard pattern)
                if (!s.discard && s.name.is_valid())
                    ctx.scope.insert(s.name, binding_ty);

                return tyir::TyLetStmt{s.discard, s.name, binding_ty, std::move(*init), s.span};

            } else { // ast::ExprStmt
                auto expr = lower_expr(s.expr, ctx);
                if (!expr)
                    return std::unexpected(std::move(expr.error()));
                return tyir::TyExprStmt{std::move(*expr), s.span};
            }
        },
        stmt);
}

// ─── Block lowering ──────────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyBlock, LowerError>
    lower_block(const ast::BlockExpr& block, LowerCtx& ctx) {
    ctx.scope.push();

    tyir::TyBlock result;
    result.span = block.span;

    for (const ast::Stmt& stmt : block.statements) {
        auto lowered = lower_stmt(stmt, ctx);
        if (!lowered) {
            ctx.scope.pop();
            return std::unexpected(std::move(lowered.error()));
        }
        result.stmts.push_back(std::move(*lowered));
    }

    if (block.tail.has_value()) {
        auto tail = lower_expr(*block.tail, ctx);
        if (!tail) {
            ctx.scope.pop();
            return std::unexpected(std::move(tail.error()));
        }
        result.ty = (*tail)->ty;
        result.tail = std::move(*tail);
    } else {
        result.ty = tyir::ty::unit();
    }

    ctx.scope.pop();
    return result;
}

// ─── Expression lowering ─────────────────────────────────────────────────────

[[nodiscard]] inline std::expected<tyir::TyExprPtr, LowerError>
    lower_expr(const ast::ExprPtr& expr, LowerCtx& ctx) {
    assert(expr != nullptr);

    return std::visit(
        [&](const auto& node) -> std::expected<tyir::TyExprPtr, LowerError> {
            using N = std::decay_t<decltype(node)>;

            // ── Literal ───────────────────────────────────────────────────
            if constexpr (std::is_same_v<N, ast::LiteralExpr>) {
                tyir::TyLiteral lit;
                tyir::Ty ty;
                switch (node.kind) {
                case ast::LiteralExpr::Kind::Num:
                    lit = {tyir::TyLiteral::Kind::Num, node.symbol, false};
                    ty = tyir::ty::num();
                    break;
                case ast::LiteralExpr::Kind::Str:
                    lit = {tyir::TyLiteral::Kind::Str, node.symbol, false};
                    ty = tyir::ty::str();
                    break;
                case ast::LiteralExpr::Kind::Bool:
                    lit = {
                        tyir::TyLiteral::Kind::Bool, cstc::symbol::kInvalidSymbol, node.bool_value};
                    ty = tyir::ty::bool_();
                    break;
                case ast::LiteralExpr::Kind::Unit:
                    lit = {tyir::TyLiteral::Kind::Unit, cstc::symbol::kInvalidSymbol, false};
                    ty = tyir::ty::unit();
                    break;
                }
                return tyir::make_ty_expr(expr->span, std::move(lit), std::move(ty));
            }

            // ── Path ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::PathExpr>) {
                if (node.tail.has_value()) {
                    // EnumName::Variant
                    const cstc::symbol::Symbol enum_name = node.head;
                    const cstc::symbol::Symbol variant_name = *node.tail;
                    if (!ctx.env.is_enum(enum_name))
                        return make_error(
                            expr->span,
                            "'" + std::string(enum_name.as_str()) + "' is not an enum type");
                    if (!ctx.env.has_variant(enum_name, variant_name))
                        return make_error(
                            expr->span, "no variant '" + std::string(variant_name.as_str())
                                            + "' in enum '" + std::string(enum_name.as_str())
                                            + "'");
                    return tyir::make_ty_expr(
                        expr->span, tyir::EnumVariantRef{enum_name, variant_name},
                        tyir::ty::named(enum_name));
                }

                // Local variable reference
                const auto local_ty = ctx.scope.lookup(node.head);
                if (local_ty.has_value())
                    return tyir::make_ty_expr(expr->span, tyir::LocalRef{node.head}, *local_ty);

                return make_error(
                    expr->span, "undefined variable '" + std::string(node.head.as_str()) + "'");
            }

            // ── Struct init ───────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::StructInitExpr>) {
                const cstc::symbol::Symbol type_name = node.type_name;
                if (!ctx.env.is_struct(type_name))
                    return make_error(
                        expr->span,
                        "'" + std::string(type_name.as_str()) + "' is not a struct type");

                const auto& expected_fields = ctx.env.struct_fields.at(type_name);

                // Validate: no extra fields, each field type matches
                std::vector<tyir::TyStructInitField> lowered_fields;
                lowered_fields.reserve(node.fields.size());

                for (const ast::StructInitField& field : node.fields) {
                    const auto expected_ty = ctx.env.field_ty(type_name, field.name);
                    if (!expected_ty)
                        return make_error(
                            field.span, "no field '" + std::string(field.name.as_str())
                                            + "' in struct '" + std::string(type_name.as_str())
                                            + "'");

                    auto val = lower_expr(field.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));

                    if (!compatible((*val)->ty, *expected_ty))
                        return make_error(
                            field.span, "field '" + std::string(field.name.as_str())
                                            + "': expected '" + expected_ty->display()
                                            + "', found '" + (*val)->ty.display() + "'");

                    lowered_fields.push_back(
                        tyir::TyStructInitField{field.name, std::move(*val), field.span});
                }

                // Validate that all struct fields are initialised
                if (lowered_fields.size() != expected_fields.size())
                    return make_error(
                        expr->span, "struct '" + std::string(type_name.as_str()) + "' expects "
                                        + std::to_string(expected_fields.size()) + " field(s), "
                                        + std::to_string(lowered_fields.size()) + " provided");

                return tyir::make_ty_expr(
                    expr->span, tyir::TyStructInit{type_name, std::move(lowered_fields)},
                    tyir::ty::named(type_name));
            }

            // ── Unary ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::UnaryExpr>) {
                auto rhs = lower_expr(node.rhs, ctx);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));

                tyir::Ty result_ty;
                switch (node.op) {
                case ast::UnaryOp::Negate:
                    if (!compatible((*rhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span,
                            "unary '-' requires 'num', found '" + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::num();
                    break;
                case ast::UnaryOp::Not:
                    if (!compatible((*rhs)->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span,
                            "unary '!' requires 'bool', found '" + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyUnary{node.op, std::move(*rhs)}, std::move(result_ty));
            }

            // ── Binary ────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BinaryExpr>) {
                auto lhs = lower_expr(node.lhs, ctx);
                if (!lhs)
                    return std::unexpected(std::move(lhs.error()));
                auto rhs = lower_expr(node.rhs, ctx);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));

                tyir::Ty result_ty;
                using Op = ast::BinaryOp;
                switch (node.op) {
                case Op::Add:
                case Op::Sub:
                case Op::Mul:
                case Op::Div:
                case Op::Mod:
                    if (!compatible((*lhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "arithmetic operator requires 'num' on left, found '"
                                            + (*lhs)->ty.display() + "'");
                    if (!compatible((*rhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "arithmetic operator requires 'num' on right, found '"
                                            + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::num();
                    break;

                case Op::Lt:
                case Op::Le:
                case Op::Gt:
                case Op::Ge:
                    if (!compatible((*lhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "comparison operator requires 'num' on left, found '"
                                            + (*lhs)->ty.display() + "'");
                    if (!compatible((*rhs)->ty, tyir::ty::num()))
                        return make_error(
                            expr->span, "comparison operator requires 'num' on right, found '"
                                            + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;

                case Op::Eq:
                case Op::Ne: {
                    // Both operands must have the same type (Never unifies)
                    const tyir::Ty& lty = (*lhs)->ty;
                    const tyir::Ty& rty = (*rhs)->ty;
                    if (!lty.is_never() && !rty.is_never() && lty != rty)
                        return make_error(
                            expr->span,
                            "equality operator requires same types on both sides, found '"
                                + lty.display() + "' and '" + rty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;
                }

                case Op::And:
                case Op::Or:
                    if (!compatible((*lhs)->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span, "logical operator requires 'bool' on left, found '"
                                            + (*lhs)->ty.display() + "'");
                    if (!compatible((*rhs)->ty, tyir::ty::bool_()))
                        return make_error(
                            expr->span, "logical operator requires 'bool' on right, found '"
                                            + (*rhs)->ty.display() + "'");
                    result_ty = tyir::ty::bool_();
                    break;
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyBinary{node.op, std::move(*lhs), std::move(*rhs)},
                    std::move(result_ty));
            }

            // ── Field access ──────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::FieldAccessExpr>) {
                auto base = lower_expr(node.base, ctx);
                if (!base)
                    return std::unexpected(std::move(base.error()));

                if ((*base)->ty.is_never())
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyFieldAccess{std::move(*base), node.field},
                        tyir::ty::never());

                if (!(*base)->ty.is_named())
                    return make_error(
                        expr->span,
                        "field access on non-struct type '" + (*base)->ty.display() + "'");

                const auto field_ty = ctx.env.field_ty((*base)->ty.name, node.field);
                if (!field_ty)
                    return make_error(
                        expr->span, "no field '" + std::string(node.field.as_str())
                                        + "' in struct '" + std::string((*base)->ty.name.as_str())
                                        + "'");

                return tyir::make_ty_expr(
                    expr->span, tyir::TyFieldAccess{std::move(*base), node.field}, *field_ty);
            }

            // ── Call ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::CallExpr>) {
                // Cicest has no first-class functions; callee must be a plain
                // PathExpr resolving to a top-level function name.
                const ast::PathExpr* callee_path = std::get_if<ast::PathExpr>(&node.callee->node);
                if (callee_path == nullptr || callee_path->tail.has_value())
                    return make_error(expr->span, "call callee must be a function name");

                const cstc::symbol::Symbol fn_name = callee_path->head;
                if (!ctx.env.is_fn(fn_name))
                    return make_error(
                        expr->span, "undefined function '" + std::string(fn_name.as_str()) + "'");

                const FnSignature& sig = ctx.env.fn_signatures.at(fn_name);

                if (node.args.size() != sig.param_types.size())
                    return make_error(
                        expr->span, "function '" + std::string(fn_name.as_str()) + "' expects "
                                        + std::to_string(sig.param_types.size()) + " argument(s), "
                                        + std::to_string(node.args.size()) + " provided");

                std::vector<tyir::TyExprPtr> lowered_args;
                lowered_args.reserve(node.args.size());

                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    auto arg = lower_expr(node.args[i], ctx);
                    if (!arg)
                        return std::unexpected(std::move(arg.error()));
                    if (!compatible((*arg)->ty, sig.param_types[i]))
                        return make_error(
                            node.args[i]->span, "argument " + std::to_string(i + 1) + " of '"
                                                    + std::string(fn_name.as_str())
                                                    + "': expected '" + sig.param_types[i].display()
                                                    + "', found '" + (*arg)->ty.display() + "'");
                    lowered_args.push_back(std::move(*arg));
                }

                return tyir::make_ty_expr(
                    expr->span, tyir::TyCall{fn_name, std::move(lowered_args)}, sig.return_ty);
            }

            // ── Block expression ──────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BlockPtr>) {
                auto block = lower_block(*node, ctx);
                if (!block)
                    return std::unexpected(std::move(block.error()));
                tyir::Ty block_ty = block->ty;
                auto block_ptr = std::make_shared<tyir::TyBlock>(std::move(*block));
                return tyir::make_ty_expr(expr->span, std::move(block_ptr), block_ty);
            }

            // ── If ────────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::IfExpr>) {
                auto cond = lower_expr(node.condition, ctx);
                if (!cond)
                    return std::unexpected(std::move(cond.error()));
                if (!compatible((*cond)->ty, tyir::ty::bool_()))
                    return make_error(
                        node.condition->span, "'if' condition must have type 'bool', found '"
                                                  + (*cond)->ty.display() + "'");

                auto then_block_val = lower_block(*node.then_block, ctx);
                if (!then_block_val)
                    return std::unexpected(std::move(then_block_val.error()));

                auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block_val));

                tyir::Ty result_ty = then_ptr->ty;

                std::optional<tyir::TyExprPtr> else_branch;
                if (node.else_branch.has_value()) {
                    auto else_val = lower_expr(*node.else_branch, ctx);
                    if (!else_val)
                        return std::unexpected(std::move(else_val.error()));

                    // Unify then / else types (Never unifies with anything)
                    const tyir::Ty& else_ty = (*else_val)->ty;
                    if (!compatible(then_ptr->ty, else_ty) && !compatible(else_ty, then_ptr->ty))
                        return make_error(
                            expr->span, "'if' then-branch has type '" + then_ptr->ty.display()
                                            + "' but else-branch has type '" + else_ty.display()
                                            + "'");

                    if (then_ptr->ty.is_never())
                        result_ty = else_ty;

                    else_branch = std::move(*else_val);
                } else {
                    // No else branch: result type is Unit
                    result_ty = tyir::ty::unit();
                }

                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyIf{std::move(*cond), std::move(then_ptr), std::move(else_branch)},
                    result_ty);
            }

            // ── Loop ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::LoopExpr>) {
                auto body = lower_block(*node.body, ctx);
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return tyir::make_ty_expr(
                    expr->span, tyir::TyLoop{std::move(body_ptr)}, tyir::ty::unit());
            }

            // ── While ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::WhileExpr>) {
                auto cond = lower_expr(node.condition, ctx);
                if (!cond)
                    return std::unexpected(std::move(cond.error()));
                if (!compatible((*cond)->ty, tyir::ty::bool_()))
                    return make_error(
                        node.condition->span, "'while' condition must have type 'bool', found '"
                                                  + (*cond)->ty.display() + "'");

                auto body = lower_block(*node.body, ctx);
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

                return tyir::make_ty_expr(
                    expr->span, tyir::TyWhile{std::move(*cond), std::move(body_ptr)},
                    tyir::ty::unit());
            }

            // ── For ───────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ForExpr>) {
                // The for-init introduces a new scope that outlives the header
                ctx.scope.push();

                std::optional<tyir::TyForInit> lowered_init;

                if (node.init.has_value()) {
                    const auto& init_var = *node.init;
                    if (const auto* init_let = std::get_if<ast::ForInitLet>(&init_var)) {
                        auto init_expr = lower_expr(init_let->initializer, ctx);
                        if (!init_expr) {
                            ctx.scope.pop();
                            return std::unexpected(std::move(init_expr.error()));
                        }
                        tyir::Ty init_ty;
                        if (init_let->type_annotation.has_value()) {
                            auto ann =
                                lower_type(*init_let->type_annotation, ctx.env, init_let->span);
                            if (!ann) {
                                ctx.scope.pop();
                                return std::unexpected(std::move(ann.error()));
                            }
                            if (!compatible((*init_expr)->ty, *ann)) {
                                ctx.scope.pop();
                                return make_error(
                                    init_let->span, "for-init type mismatch: expected '"
                                                        + ann->display() + "', found '"
                                                        + (*init_expr)->ty.display() + "'");
                            }
                            init_ty = *ann;
                        } else {
                            init_ty = (*init_expr)->ty;
                        }
                        if (!init_let->discard && init_let->name.is_valid())
                            ctx.scope.insert(init_let->name, init_ty);
                        lowered_init = tyir::TyForInit{
                            init_let->discard, init_let->name, init_ty, std::move(*init_expr),
                            init_let->span};
                    } else {
                        // Expression initializer
                        const auto& init_expr_ptr = std::get<ast::ExprPtr>(init_var);
                        auto init_expr = lower_expr(init_expr_ptr, ctx);
                        if (!init_expr) {
                            ctx.scope.pop();
                            return std::unexpected(std::move(init_expr.error()));
                        }
                        // Treat as a discard init — wrap in TyForInit with discard=true
                        lowered_init = tyir::TyForInit{
                            true, cstc::symbol::kInvalidSymbol, (*init_expr)->ty,
                            std::move(*init_expr), init_expr_ptr->span};
                    }
                }

                std::optional<tyir::TyExprPtr> lowered_cond;
                if (node.condition.has_value()) {
                    auto cond = lower_expr(*node.condition, ctx);
                    if (!cond) {
                        ctx.scope.pop();
                        return std::unexpected(std::move(cond.error()));
                    }
                    if (!compatible((*cond)->ty, tyir::ty::bool_())) {
                        ctx.scope.pop();
                        return make_error(
                            (*node.condition)->span,
                            "'for' condition must have type 'bool', found '" + (*cond)->ty.display()
                                + "'");
                    }
                    lowered_cond = std::move(*cond);
                }

                std::optional<tyir::TyExprPtr> lowered_step;
                if (node.step.has_value()) {
                    auto step = lower_expr(*node.step, ctx);
                    if (!step) {
                        ctx.scope.pop();
                        return std::unexpected(std::move(step.error()));
                    }
                    lowered_step = std::move(*step);
                }

                auto body = lower_block(*node.body, ctx);
                if (!body) {
                    ctx.scope.pop();
                    return std::unexpected(std::move(body.error()));
                }
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

                ctx.scope.pop();

                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyFor{
                        std::move(lowered_init), std::move(lowered_cond), std::move(lowered_step),
                        std::move(body_ptr)},
                    tyir::ty::unit());
            }

            // ── Break ─────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::BreakExpr>) {
                std::optional<tyir::TyExprPtr> lowered_val;
                if (node.value.has_value()) {
                    auto val = lower_expr(*node.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));
                    lowered_val = std::move(*val);
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyBreak{std::move(lowered_val)}, tyir::ty::never());
            }

            // ── Continue ──────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ContinueExpr>) {
                return tyir::make_ty_expr(expr->span, tyir::TyContinue{}, tyir::ty::never());
            }

            // ── Return ────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, ast::ReturnExpr>) {
                std::optional<tyir::TyExprPtr> lowered_val;
                if (node.value.has_value()) {
                    auto val = lower_expr(*node.value, ctx);
                    if (!val)
                        return std::unexpected(std::move(val.error()));
                    if (!compatible((*val)->ty, ctx.current_return_ty))
                        return make_error(
                            expr->span, "return type mismatch: expected '"
                                            + ctx.current_return_ty.display() + "', found '"
                                            + (*val)->ty.display() + "'");
                    lowered_val = std::move(*val);
                } else {
                    // Bare `return` — valid only in functions returning Unit
                    if (!compatible(tyir::ty::unit(), ctx.current_return_ty))
                        return make_error(
                            expr->span, "bare 'return' in function returning '"
                                            + ctx.current_return_ty.display() + "'");
                }
                return tyir::make_ty_expr(
                    expr->span, tyir::TyReturn{std::move(lowered_val)}, tyir::ty::never());
            }

            // Should be exhaustive — but keep the compiler happy
            return make_error(expr->span, "unsupported expression kind");
        },
        expr->node);
}

// ─── Item lowering ────────────────────────────────────────────────────────────

/// Lowers a function declaration using the fully-built `TypeEnv`.
[[nodiscard]] inline std::expected<tyir::TyFnDecl, LowerError>
    lower_fn(const ast::FnDecl& fn, const TypeEnv& env) {
    const FnSignature& sig = env.fn_signatures.at(fn.name);

    // Build typed param list
    std::vector<tyir::TyParam> ty_params;
    ty_params.reserve(fn.params.size());
    for (std::size_t i = 0; i < fn.params.size(); ++i)
        ty_params.push_back(
            tyir::TyParam{fn.params[i].name, sig.param_types[i], fn.params[i].span});

    // Set up lowering context with params in scope
    LowerCtx ctx{env, {}, sig.return_ty};
    ctx.scope.push();
    for (const tyir::TyParam& p : ty_params)
        ctx.scope.insert(p.name, p.ty);

    auto body = lower_block(*fn.body, ctx);
    ctx.scope.pop();
    if (!body)
        return std::unexpected(std::move(body.error()));

    // Check that body type matches declared return type (when a tail is present)
    if (body->tail.has_value() && !compatible(body->ty, sig.return_ty))
        return make_error(
            fn.body->span, "function '" + std::string(fn.name.as_str()) + "' body has type '"
                               + body->ty.display() + "' but return type is '"
                               + sig.return_ty.display() + "'");

    auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));

    return tyir::TyFnDecl{
        fn.name, std::move(ty_params), sig.return_ty, std::move(body_ptr), fn.span};
}

} // namespace detail

// ─── Public API ──────────────────────────────────────────────────────────────

inline std::expected<tyir::TyProgram, LowerError> lower_program(const ast::Program& program) {
    detail::TypeEnv env;

    // ── Phase 1: collect named-type names (placeholders) ─────────────────
    for (const ast::Item& item : program.items) {
        std::visit(
            [&](const auto& decl) {
                using T = std::decay_t<decltype(decl)>;
                if constexpr (std::is_same_v<T, ast::StructDecl>)
                    env.struct_fields.emplace(decl.name, std::vector<tyir::TyFieldDecl>{});
                else if constexpr (std::is_same_v<T, ast::EnumDecl>)
                    env.enum_variants.emplace(decl.name, std::vector<tyir::TyEnumVariant>{});
            },
            item);
    }

    // ── Phase 2: resolve struct fields and enum variants ──────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* s = std::get_if<ast::StructDecl>(&item)) {
            auto& fields = env.struct_fields.at(s->name);
            for (const ast::FieldDecl& f : s->fields) {
                auto ty = detail::lower_type(f.type, env, f.span);
                if (!ty)
                    return std::unexpected(std::move(ty.error()));
                fields.push_back(tyir::TyFieldDecl{f.name, *ty, f.span});
            }
        } else if (const auto* e = std::get_if<ast::EnumDecl>(&item)) {
            auto& variants = env.enum_variants.at(e->name);
            for (const ast::EnumVariant& v : e->variants)
                variants.push_back(tyir::TyEnumVariant{v.name, v.discriminant, v.span});
        }
    }

    // ── Phase 3: resolve function signatures ─────────────────────────────
    for (const ast::Item& item : program.items) {
        if (const auto* fn = std::get_if<ast::FnDecl>(&item)) {
            auto sig = detail::resolve_fn_signature(*fn, env);
            if (!sig)
                return std::unexpected(std::move(sig.error()));
            env.fn_signatures.emplace(fn->name, std::move(*sig));
        }
    }

    // ── Phase 4: lower all items in source order ──────────────────────────
    tyir::TyProgram result;
    for (const ast::Item& item : program.items) {
        if (const auto* s = std::get_if<ast::StructDecl>(&item)) {
            tyir::TyStructDecl ty_decl;
            ty_decl.name = s->name;
            ty_decl.is_zst = s->is_zst;
            ty_decl.span = s->span;
            ty_decl.fields = env.struct_fields.at(s->name);
            result.items.push_back(std::move(ty_decl));
        } else if (const auto* e = std::get_if<ast::EnumDecl>(&item)) {
            tyir::TyEnumDecl ty_decl;
            ty_decl.name = e->name;
            ty_decl.span = e->span;
            ty_decl.variants = env.enum_variants.at(e->name);
            result.items.push_back(std::move(ty_decl));
        } else if (const auto* fn = std::get_if<ast::FnDecl>(&item)) {
            auto fn_result = detail::lower_fn(*fn, env);
            if (!fn_result)
                return std::unexpected(std::move(fn_result.error()));
            result.items.push_back(std::move(*fn_result));
        }
    }

    return result;
}

} // namespace cstc::tyir_builder

#endif // CICEST_COMPILER_CSTC_TYIR_BUILDER_BUILDER_IMPL_HPP
