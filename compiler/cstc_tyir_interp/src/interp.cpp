#include <cstc_tyir_interp/detail.hpp>
#include <cstc_tyir_interp/interp.hpp>

#include <cstc_tyir/type_compat.hpp>

#include <cassert>
#include <unordered_map>
#include <unordered_set>

namespace cstc::tyir_interp::detail {

using tyir::common_type;
using tyir::compatible;
using tyir::matches_type_shape;

ValuePtr make_num(double value) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Num;
    out->num_value = value;
    return out;
}

ValuePtr make_bool(bool value) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Bool;
    out->bool_value = value;
    return out;
}

ValuePtr make_unit() { return std::make_shared<Value>(); }

ValuePtr make_string(std::string value, Value::StringOwnership ownership) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::String;
    out->string_value = std::move(value);
    out->string_ownership = ownership;
    return out;
}

ValuePtr make_ref(ValuePtr referent) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Ref;
    out->referent = std::move(referent);
    return out;
}

ValuePtr make_enum(Symbol type_name, Symbol variant_name) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Enum;
    out->type_name = type_name;
    out->variant_name = variant_name;
    return out;
}

ValuePtr make_struct(Symbol type_name, std::vector<ValueField> fields) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Struct;
    out->type_name = type_name;
    out->fields = std::move(fields);
    return out;
}

class ConstEnv {
public:
    struct LookupResult {
        bool found = false;
        std::optional<ValuePtr> value;
    };

    void push() { frames_.emplace_back(); }

    void pop() {
        assert(!frames_.empty());
        frames_.pop_back();
    }

    void bind_known(Symbol name, ValuePtr value) {
        assert(!frames_.empty());
        frames_.back().insert_or_assign(name, std::move(value));
    }

    void bind_unknown(Symbol name) {
        assert(!frames_.empty());
        frames_.back().insert_or_assign(name, std::nullopt);
    }

    [[nodiscard]] LookupResult lookup(Symbol name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            const auto found = it->find(name);
            if (found == it->end())
                continue;
            return {true, found->second};
        }
        return {};
    }

private:
    using Frame = std::unordered_map<Symbol, std::optional<ValuePtr>, SymbolHash>;
    std::vector<Frame> frames_;
};

struct EvalState {
    enum class BlockedReason {
        UnknownValue,
        RuntimeOnly,
    };

    enum class Kind {
        Value,
        Return,
        Break,
        Continue,
        Blocked,
    };

    Kind kind = Kind::Blocked;
    ValuePtr value;
    BlockedReason blocked_reason = BlockedReason::UnknownValue;

    [[nodiscard]] static EvalState from_value(ValuePtr value) {
        return EvalState{Kind::Value, std::move(value)};
    }

    [[nodiscard]] static EvalState from_return(ValuePtr value) {
        return EvalState{Kind::Return, std::move(value)};
    }

    [[nodiscard]] static EvalState from_break(ValuePtr value = nullptr) {
        return EvalState{Kind::Break, std::move(value)};
    }

    [[nodiscard]] static EvalState continue_() { return EvalState{Kind::Continue, nullptr}; }

    [[nodiscard]] static EvalState blocked(BlockedReason reason = BlockedReason::UnknownValue) {
        EvalState state;
        state.blocked_reason = reason;
        return state;
    }
};

[[nodiscard]] static std::unexpected<EvalError>
    make_error(const EvalContext& ctx, SourceSpan span, std::string message) {
    return std::unexpected(EvalError{span, std::move(message), ctx.stack, std::nullopt});
}

[[nodiscard]] static std::expected<void, EvalError>
    consume_step_budget(EvalContext& ctx, SourceSpan span, std::string_view activity) {
    if (ctx.remaining_steps == 0)
        return make_error(
            ctx, span, "const-eval step budget exhausted while " + std::string(activity));
    --ctx.remaining_steps;
    return {};
}

[[nodiscard]] static std::expected<void, EvalError>
    consume_call_depth(EvalContext& ctx, SourceSpan span, Symbol fn_name) {
    if (ctx.remaining_call_depth == 0) {
        const std::string callee = fn_name.is_valid() ? std::string(fn_name.as_str()) : "<unknown>";
        return make_error(
            ctx, span, "const-eval call depth exhausted while calling '" + callee + "'");
    }
    --ctx.remaining_call_depth;
    return {};
}

[[nodiscard]] static std::expected<void, EvalError> require_call_arity(
    const EvalContext& ctx, SourceSpan span, std::string_view callee_name, std::size_t expected,
    std::size_t actual) {
    if (expected == actual)
        return {};
    return make_error(
        ctx, span,
        "mismatched compile-time call arity for '" + std::string(callee_name) + "': expected "
            + std::to_string(expected) + " argument(s), got " + std::to_string(actual));
}

[[nodiscard]] static std::string strip_quotes(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return std::string(value.substr(1, value.size() - 2));
    return std::string(value);
}

[[nodiscard]] static std::string quote_string_contents(std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (const char ch : value) {
        switch (ch) {
        case '\\': quoted += "\\\\"; break;
        case '"': quoted += "\\\""; break;
        case '\n': quoted += "\\n"; break;
        case '\r': quoted += "\\r"; break;
        case '\t': quoted += "\\t"; break;
        default: quoted.push_back(ch); break;
        }
    }
    quoted.push_back('"');
    return quoted;
}

[[nodiscard]] static tyir::Ty
    apply_substitution(const tyir::Ty& ty, const TypeSubstitution& subst) {
    if (ty.kind == tyir::TyKind::Ref) {
        if (ty.pointee == nullptr)
            return ty;
        tyir::Ty rewritten = ty;
        rewritten.pointee = std::make_shared<tyir::Ty>(apply_substitution(*ty.pointee, subst));
        return rewritten;
    }
    if (ty.kind != tyir::TyKind::Named)
        return ty;

    if (ty.generic_args.empty()) {
        const auto it = subst.find(ty.name);
        if (it != subst.end()) {
            tyir::Ty rewritten = it->second;
            rewritten.is_runtime = rewritten.is_runtime || ty.is_runtime;
            if (!rewritten.display_name.is_valid())
                rewritten.display_name = ty.display_name;
            return rewritten;
        }
    }

    tyir::Ty rewritten = ty;
    rewritten.generic_args.clear();
    rewritten.generic_args.reserve(ty.generic_args.size());
    for (const tyir::Ty& arg : ty.generic_args)
        rewritten.generic_args.push_back(apply_substitution(arg, subst));
    return rewritten;
}

using GenericParamSet = std::unordered_set<Symbol, SymbolHash>;

[[nodiscard]] static bool
    type_is_generic_param(const tyir::Ty& ty, const GenericParamSet& generic_params) {
    return ty.kind == tyir::TyKind::Named && ty.generic_args.empty()
        && generic_params.contains(ty.name);
}

[[nodiscard]] static bool type_shape_depends_on_generic_substitution(
    const tyir::Ty& actual, const tyir::Ty& expected, const GenericParamSet& generic_params) {
    if (type_is_generic_param(actual, generic_params)
        || type_is_generic_param(expected, generic_params)) {
        return true;
    }
    if (actual.kind != expected.kind)
        return false;
    if (actual.kind == tyir::TyKind::Ref) {
        if (actual.pointee == nullptr || expected.pointee == nullptr)
            return actual.pointee == expected.pointee;
        return type_shape_depends_on_generic_substitution(
            *actual.pointee, *expected.pointee, generic_params);
    }
    if (actual.kind != tyir::TyKind::Named)
        return true;
    if (actual.name != expected.name || actual.generic_args.size() != expected.generic_args.size())
        return false;
    for (std::size_t index = 0; index < actual.generic_args.size(); ++index) {
        if (!type_shape_depends_on_generic_substitution(
                actual.generic_args[index], expected.generic_args[index], generic_params)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool types_may_be_compatible_after_substitution(
    const tyir::Ty& actual, const tyir::Ty& expected, const GenericParamSet& generic_params) {
    if (actual.is_never())
        return true;
    if (!type_shape_depends_on_generic_substitution(actual, expected, generic_params))
        return false;
    if (actual.is_runtime && !expected.is_runtime)
        return false;
    if (actual.kind == tyir::TyKind::Ref) {
        if (actual.pointee == nullptr || expected.pointee == nullptr)
            return actual.pointee == expected.pointee;
        return types_may_be_compatible_after_substitution(
            *actual.pointee, *expected.pointee, generic_params);
    }
    if (actual.kind != tyir::TyKind::Named)
        return true;
    for (std::size_t index = 0; index < actual.generic_args.size(); ++index) {
        if (!types_may_be_compatible_after_substitution(
                actual.generic_args[index], expected.generic_args[index], generic_params)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool types_may_have_common_type_after_substitution(
    const tyir::Ty& lhs, const tyir::Ty& rhs, const GenericParamSet& generic_params) {
    return lhs.is_never() || rhs.is_never()
        || type_shape_depends_on_generic_substitution(lhs, rhs, generic_params);
}

[[nodiscard]] static GenericParamSet
    make_generic_param_set(const std::vector<cstc::ast::GenericParam>& generic_params) {
    GenericParamSet params;
    params.reserve(generic_params.size());
    for (const cstc::ast::GenericParam& param : generic_params)
        params.insert(param.name);
    return params;
}

[[nodiscard]] static bool
    type_depends_on_generic_params(const tyir::Ty& ty, const GenericParamSet& generic_params) {
    if (ty.kind == tyir::TyKind::Ref)
        return ty.pointee != nullptr && type_depends_on_generic_params(*ty.pointee, generic_params);
    if (ty.kind != tyir::TyKind::Named)
        return false;
    if (ty.generic_args.empty() && generic_params.contains(ty.name))
        return true;
    for (const tyir::Ty& generic_arg : ty.generic_args) {
        if (type_depends_on_generic_params(generic_arg, generic_params))
            return true;
    }
    return false;
}

[[nodiscard]] static bool generic_args_depend_on_generic_params(
    const std::vector<tyir::Ty>& generic_args, const GenericParamSet& generic_params) {
    for (const tyir::Ty& generic_arg : generic_args) {
        if (type_depends_on_generic_params(generic_arg, generic_params))
            return true;
    }
    return false;
}

[[nodiscard]] static ConstraintEvalResult generic_substitution_dependency_result() {
    return {
        ConstraintEvalKind::NotConstEvaluable,
        "probed expression still depends on generic substitution",
        std::nullopt,
    };
}

class ProbeOwnershipScope {
public:
    struct LocalState {
        cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
        tyir::Ty ty;
        bool moved = false;
        std::size_t active_borrows = 0;
        std::size_t deferred_value_uses = 0;
        std::optional<std::size_t> borrowed_local;
    };

    ProbeOwnershipScope() { push(); }

    void push() { frames_.push_back(Frame{{}, locals_.size()}); }
    void pop() {
        assert(!frames_.empty());
        while (locals_.size() > frames_.back().start_local_count) {
            LocalState& local = locals_.back();
            if (local.borrowed_local.has_value()) {
                LocalState& owner = locals_.at(*local.borrowed_local);
                assert(owner.active_borrows > 0);
                owner.active_borrows -= 1;
            }
            locals_.pop_back();
        }
        frames_.pop_back();
    }

    [[nodiscard]] std::optional<std::size_t> lookup_local(cstc::symbol::Symbol name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            const auto found = it->bindings.find(name);
            if (found != it->bindings.end())
                return found->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t ensure_external_local(cstc::symbol::Symbol name, const tyir::Ty& ty) {
        if (const auto found = lookup_local(name); found.has_value())
            return *found;
        assert(!frames_.empty());
        const std::size_t index = locals_.size();
        locals_.push_back(LocalState{name, ty, false, 0, 0, std::nullopt});
        frames_.front().bindings.emplace(name, index);
        return index;
    }

    void insert(
        cstc::symbol::Symbol name, const tyir::Ty& ty,
        std::optional<std::size_t> borrowed_local = std::nullopt) {
        assert(!frames_.empty());
        if (borrowed_local.has_value())
            locals_.at(*borrowed_local).active_borrows += 1;
        const std::size_t index = locals_.size();
        locals_.push_back(LocalState{name, ty, false, 0, 0, borrowed_local});
        frames_.back().bindings.emplace(name, index);
    }

    [[nodiscard]] LocalState& local(std::size_t index) { return locals_.at(index); }
    [[nodiscard]] const LocalState& local(std::size_t index) const { return locals_.at(index); }

    void release_temp_borrows(const std::vector<std::size_t>& borrowed_locals) {
        for (const std::size_t index : borrowed_locals) {
            LocalState& owner = locals_.at(index);
            assert(owner.active_borrows > 0);
            owner.active_borrows -= 1;
        }
    }

    void merge_from(const ProbeOwnershipScope& other) {
        assert(locals_.size() == other.locals_.size());
        for (std::size_t index = 0; index < locals_.size(); ++index) {
            locals_[index].moved = locals_[index].moved || other.locals_[index].moved;
            locals_[index].active_borrows =
                std::max(locals_[index].active_borrows, other.locals_[index].active_borrows);
            locals_[index].deferred_value_uses = std::max(
                locals_[index].deferred_value_uses, other.locals_[index].deferred_value_uses);
        }
    }

private:
    struct Frame {
        std::unordered_map<cstc::symbol::Symbol, std::size_t, cstc::symbol::SymbolHash> bindings;
        std::size_t start_local_count = 0;
    };

    std::vector<Frame> frames_;
    std::vector<LocalState> locals_;
};

struct ProbeOwnershipCheck {
    ConstraintEvalResult status;
    std::vector<std::size_t> temp_borrows;
};

[[nodiscard]] static ProbeOwnershipCheck
    satisfied_probe_ownership(std::vector<std::size_t> temp_borrows = {}) {
    return {
        {ConstraintEvalKind::Satisfied, {}, std::nullopt},
        std::move(temp_borrows)
    };
}

[[nodiscard]] static ProbeOwnershipCheck failed_probe_ownership(ConstraintEvalResult status) {
    return {std::move(status), {}};
}

[[nodiscard]] static std::optional<std::size_t>
    borrowed_owner_local(const tyir::TyExprPtr& expr, ProbeOwnershipScope& scope) {
    if (expr == nullptr)
        return std::nullopt;
    return std::visit(
        [&](const auto& node) -> std::optional<std::size_t> {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, tyir::LocalRef>) {
                if (node.use_kind != tyir::ValueUseKind::Borrow)
                    return std::nullopt;
                return scope.ensure_external_local(node.name, expr->ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyBorrow>) {
                return borrowed_owner_local(node.rhs, scope);
            } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                if (node.use_kind != tyir::ValueUseKind::Borrow)
                    return std::nullopt;
                return borrowed_owner_local(node.base, scope);
            } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                if (node == nullptr || !node->tail.has_value())
                    return std::nullopt;
                return borrowed_owner_local(*node->tail, scope);
            } else {
                return std::nullopt;
            }
        },
        expr->node);
}

[[nodiscard]] static std::optional<std::size_t> captured_borrowed_local(
    const tyir::TyExprPtr& expr, const std::vector<std::size_t>& temp_borrows,
    ProbeOwnershipScope& scope) {
    if (const auto borrowed_local = borrowed_owner_local(expr, scope); borrowed_local.has_value())
        return borrowed_local;
    if (temp_borrows.empty())
        return std::nullopt;
    const std::size_t borrowed_local = temp_borrows.front();
    for (const std::size_t local : temp_borrows) {
        if (local != borrowed_local)
            return std::nullopt;
    }
    return borrowed_local;
}

static void release_uncaptured_temp_borrows(
    ProbeOwnershipScope& scope, const std::vector<std::size_t>& temp_borrows,
    std::optional<std::size_t> captured_borrowed_local) {
    bool released_captured_borrow = false;
    std::vector<std::size_t> remaining_borrows;
    remaining_borrows.reserve(temp_borrows.size());
    for (const std::size_t borrowed_local : temp_borrows) {
        if (captured_borrowed_local.has_value() && borrowed_local == *captured_borrowed_local
            && !released_captured_borrow) {
            released_captured_borrow = true;
            continue;
        }
        remaining_borrows.push_back(borrowed_local);
    }
    scope.release_temp_borrows(remaining_borrows);
}

[[nodiscard]] static ConstraintEvalResult validate_decl_probe_ownership(
    const tyir::TyExprPtr& expr, const GenericParamSet& generic_params) {
    ProbeOwnershipScope scope;
    const auto validate_expr = [&](const auto& self, const tyir::TyExprPtr& value,
                                   ProbeOwnershipScope& current) -> ProbeOwnershipCheck {
        if (value == nullptr)
            return satisfied_probe_ownership();

        return std::visit(
            [&](const auto& node) -> ProbeOwnershipCheck {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (
                    std::is_same_v<Node, tyir::TyLiteral>
                    || std::is_same_v<Node, tyir::EnumVariantRef>
                    || std::is_same_v<Node, tyir::TyDeferredGenericCall>
                    || std::is_same_v<Node, tyir::TyDeclProbe>
                    || std::is_same_v<Node, tyir::TyContinue>) {
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::LocalRef>) {
                    const std::size_t local_index =
                        current.ensure_external_local(node.name, value->ty);
                    auto& local = current.local(local_index);
                    if (local.moved) {
                        return failed_probe_ownership({
                            ConstraintEvalKind::Unsatisfied,
                            "use of moved value '" + std::string(node.name.as_str()) + "'",
                            std::nullopt,
                        });
                    }
                    if (node.use_kind == tyir::ValueUseKind::Borrow) {
                        local.active_borrows += 1;
                        return satisfied_probe_ownership({local_index});
                    }
                    if (type_depends_on_generic_params(value->ty, generic_params)) {
                        if (local.active_borrows > 0 || local.deferred_value_uses > 0) {
                            return failed_probe_ownership(generic_substitution_dependency_result());
                        }
                        local.deferred_value_uses += 1;
                        return satisfied_probe_ownership();
                    }
                    if (value->ty.is_move_only()) {
                        if (local.active_borrows > 0) {
                            return failed_probe_ownership({
                                ConstraintEvalKind::Unsatisfied,
                                "cannot move '" + std::string(node.name.as_str())
                                    + "' while it is borrowed",
                                std::nullopt,
                            });
                        }
                        local.moved = true;
                    }
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                    std::vector<std::size_t> temp_borrows;
                    for (const tyir::TyStructInitField& field : node.fields) {
                        auto nested = self(self, field.value, current);
                        if (nested.status.kind != ConstraintEvalKind::Satisfied) {
                            current.release_temp_borrows(temp_borrows);
                            return nested;
                        }
                        temp_borrows.insert(
                            temp_borrows.end(), nested.temp_borrows.begin(),
                            nested.temp_borrows.end());
                    }
                    current.release_temp_borrows(temp_borrows);
                    return satisfied_probe_ownership();
                } else if constexpr (
                    std::is_same_v<Node, tyir::TyBorrow> || std::is_same_v<Node, tyir::TyUnary>) {
                    auto nested = self(self, node.rhs, current);
                    if (nested.status.kind != ConstraintEvalKind::Satisfied)
                        return nested;
                    if constexpr (std::is_same_v<Node, tyir::TyBorrow>)
                        return nested;
                    current.release_temp_borrows(nested.temp_borrows);
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                    auto lhs = self(self, node.lhs, current);
                    if (lhs.status.kind != ConstraintEvalKind::Satisfied)
                        return lhs;
                    auto rhs = self(self, node.rhs, current);
                    current.release_temp_borrows(lhs.temp_borrows);
                    if (rhs.status.kind != ConstraintEvalKind::Satisfied)
                        return rhs;
                    current.release_temp_borrows(rhs.temp_borrows);
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                    auto base = self(self, node.base, current);
                    if (base.status.kind != ConstraintEvalKind::Satisfied)
                        return base;
                    if (node.use_kind == tyir::ValueUseKind::Borrow)
                        return base;
                    current.release_temp_borrows(base.temp_borrows);
                    if (type_depends_on_generic_params(value->ty, generic_params)) {
                        return failed_probe_ownership(generic_substitution_dependency_result());
                    }
                    if (value->ty.is_move_only()) {
                        return failed_probe_ownership({
                            ConstraintEvalKind::Unsatisfied,
                            "cannot move field '" + std::string(node.field.as_str()) + "' out of '"
                                + node.base->ty.display() + "'; borrow the field instead",
                            std::nullopt,
                        });
                    }
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                    std::vector<std::size_t> temp_borrows;
                    for (const tyir::TyExprPtr& arg : node.args) {
                        auto nested = self(self, arg, current);
                        if (nested.status.kind != ConstraintEvalKind::Satisfied) {
                            current.release_temp_borrows(temp_borrows);
                            return nested;
                        }
                        temp_borrows.insert(
                            temp_borrows.end(), nested.temp_borrows.begin(),
                            nested.temp_borrows.end());
                    }
                    current.release_temp_borrows(temp_borrows);
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                    if (node == nullptr)
                        return satisfied_probe_ownership();
                    current.push();
                    for (const tyir::TyStmt& stmt : node->stmts) {
                        auto nested = std::visit(
                            [&](const auto& stmt_node) -> ProbeOwnershipCheck {
                                using StmtNode = std::decay_t<decltype(stmt_node)>;
                                if constexpr (std::is_same_v<StmtNode, tyir::TyLetStmt>) {
                                    auto init = self(self, stmt_node.init, current);
                                    if (init.status.kind != ConstraintEvalKind::Satisfied)
                                        return init;
                                    std::optional<std::size_t> borrowed_local;
                                    if (stmt_node.ty.is_ref())
                                        borrowed_local = captured_borrowed_local(
                                            stmt_node.init, init.temp_borrows, current);
                                    if (!stmt_node.discard)
                                        current.insert(
                                            stmt_node.name, stmt_node.ty, borrowed_local);
                                    release_uncaptured_temp_borrows(
                                        current, init.temp_borrows, borrowed_local);
                                    return satisfied_probe_ownership();
                                } else {
                                    auto nested_expr = self(self, stmt_node.expr, current);
                                    if (nested_expr.status.kind != ConstraintEvalKind::Satisfied) {
                                        return nested_expr;
                                    }
                                    current.release_temp_borrows(nested_expr.temp_borrows);
                                    return satisfied_probe_ownership();
                                }
                            },
                            stmt);
                        if (nested.status.kind != ConstraintEvalKind::Satisfied) {
                            current.pop();
                            return nested;
                        }
                    }
                    ProbeOwnershipCheck tail = satisfied_probe_ownership();
                    if (node->tail.has_value())
                        tail = self(self, *node->tail, current);
                    if (tail.status.kind != ConstraintEvalKind::Satisfied) {
                        current.pop();
                        return tail;
                    }
                    current.pop();
                    return tail;
                } else if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                    auto condition = self(self, node.condition, current);
                    if (condition.status.kind != ConstraintEvalKind::Satisfied)
                        return condition;
                    current.release_temp_borrows(condition.temp_borrows);

                    ProbeOwnershipScope then_scope = current;
                    auto then_branch = self(
                        self, tyir::make_ty_expr(value->span, node.then_block, node.then_block->ty),
                        then_scope);
                    if (then_branch.status.kind != ConstraintEvalKind::Satisfied)
                        return then_branch;
                    then_scope.release_temp_borrows(then_branch.temp_borrows);

                    if (!node.else_branch.has_value()) {
                        current.merge_from(then_scope);
                        return satisfied_probe_ownership();
                    }

                    ProbeOwnershipScope else_scope = current;
                    auto else_branch = self(self, *node.else_branch, else_scope);
                    if (else_branch.status.kind != ConstraintEvalKind::Satisfied)
                        return else_branch;
                    else_scope.release_temp_borrows(else_branch.temp_borrows);
                    current.merge_from(then_scope);
                    current.merge_from(else_scope);
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                    auto nested = self(
                        self, tyir::make_ty_expr(value->span, node.body, node.body->ty), current);
                    if (nested.status.kind != ConstraintEvalKind::Satisfied)
                        return nested;
                    current.release_temp_borrows(nested.temp_borrows);
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                    auto condition = self(self, node.condition, current);
                    if (condition.status.kind != ConstraintEvalKind::Satisfied)
                        return condition;
                    current.release_temp_borrows(condition.temp_borrows);
                    auto body = self(
                        self, tyir::make_ty_expr(value->span, node.body, node.body->ty), current);
                    if (body.status.kind != ConstraintEvalKind::Satisfied)
                        return body;
                    current.release_temp_borrows(body.temp_borrows);
                    return satisfied_probe_ownership();
                } else if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                    current.push();
                    if (node.init.has_value()) {
                        auto init = self(self, node.init->init, current);
                        if (init.status.kind != ConstraintEvalKind::Satisfied) {
                            current.pop();
                            return init;
                        }
                        std::optional<std::size_t> borrowed_local;
                        if (node.init->ty.is_ref())
                            borrowed_local = captured_borrowed_local(
                                node.init->init, init.temp_borrows, current);
                        current.insert(node.init->name, node.init->ty, borrowed_local);
                        release_uncaptured_temp_borrows(current, init.temp_borrows, borrowed_local);
                    }
                    if (node.condition.has_value()) {
                        auto condition = self(self, *node.condition, current);
                        if (condition.status.kind != ConstraintEvalKind::Satisfied) {
                            current.pop();
                            return condition;
                        }
                        current.release_temp_borrows(condition.temp_borrows);
                    }
                    if (node.step.has_value()) {
                        auto step = self(self, *node.step, current);
                        if (step.status.kind != ConstraintEvalKind::Satisfied) {
                            current.pop();
                            return step;
                        }
                        current.release_temp_borrows(step.temp_borrows);
                    }
                    auto body = self(
                        self, tyir::make_ty_expr(value->span, node.body, node.body->ty), current);
                    if (body.status.kind != ConstraintEvalKind::Satisfied) {
                        current.pop();
                        return body;
                    }
                    current.release_temp_borrows(body.temp_borrows);
                    current.pop();
                    return satisfied_probe_ownership();
                } else {
                    static_assert(
                        std::is_same_v<Node, tyir::TyBreak>
                        || std::is_same_v<Node, tyir::TyReturn>);
                    if (!node.value.has_value())
                        return satisfied_probe_ownership();
                    auto nested = self(self, *node.value, current);
                    if (nested.status.kind != ConstraintEvalKind::Satisfied)
                        return nested;
                    current.release_temp_borrows(nested.temp_borrows);
                    return satisfied_probe_ownership();
                }
            },
            value->node);
    };

    return validate_expr(validate_expr, expr, scope).status;
}

[[nodiscard]] static std::optional<ConstraintEvalResult> unresolved_shape_check(
    const tyir::Ty& actual, const tyir::Ty& expected, const GenericParamSet& generic_params) {
    if (!matches_type_shape(actual, expected)
        && type_shape_depends_on_generic_substitution(actual, expected, generic_params)) {
        return generic_substitution_dependency_result();
    }
    return std::nullopt;
}

[[nodiscard]] static std::optional<ConstraintEvalResult> unresolved_compatibility_check(
    const tyir::Ty& actual, const tyir::Ty& expected, const GenericParamSet& generic_params) {
    if (!compatible(actual, expected)
        && types_may_be_compatible_after_substitution(actual, expected, generic_params)) {
        return generic_substitution_dependency_result();
    }
    return std::nullopt;
}

[[nodiscard]] static std::optional<ConstraintEvalResult> unresolved_common_type_check(
    const tyir::Ty& lhs, const tyir::Ty& rhs, const GenericParamSet& generic_params) {
    if (!common_type(lhs, rhs).has_value()
        && types_may_have_common_type_after_substitution(lhs, rhs, generic_params)) {
        return generic_substitution_dependency_result();
    }
    return std::nullopt;
}

[[nodiscard]] static bool expr_depends_on_generic_params(
    const tyir::TyExprPtr& expr, const GenericParamSet& generic_params);

[[nodiscard]] static bool block_depends_on_generic_params(
    const tyir::TyBlockPtr& block, const GenericParamSet& generic_params) {
    if (block == nullptr)
        return false;
    if (type_depends_on_generic_params(block->ty, generic_params))
        return true;
    for (const tyir::TyStmt& stmt : block->stmts) {
        const bool depends = std::visit(
            [&](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyLetStmt>) {
                    return type_depends_on_generic_params(node.ty, generic_params)
                        || expr_depends_on_generic_params(node.init, generic_params);
                } else {
                    return expr_depends_on_generic_params(node.expr, generic_params);
                }
            },
            stmt);
        if (depends)
            return true;
    }
    return block->tail.has_value() && expr_depends_on_generic_params(*block->tail, generic_params);
}

[[nodiscard]] static bool expr_depends_on_generic_params(
    const tyir::TyExprPtr& expr, const GenericParamSet& generic_params) {
    if (expr == nullptr)
        return false;
    if (type_depends_on_generic_params(expr->ty, generic_params))
        return true;
    return std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (
                std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                || std::is_same_v<Node, tyir::EnumVariantRef>
                || std::is_same_v<Node, tyir::TyContinue>) {
                return false;
            } else if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                for (const tyir::Ty& generic_arg : node.generic_args) {
                    if (type_depends_on_generic_params(generic_arg, generic_params))
                        return true;
                }
                for (const tyir::TyStructInitField& field : node.fields) {
                    if (expr_depends_on_generic_params(field.value, generic_params))
                        return true;
                }
                return false;
            } else if constexpr (
                std::is_same_v<Node, tyir::TyBorrow> || std::is_same_v<Node, tyir::TyUnary>) {
                return expr_depends_on_generic_params(node.rhs, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                return expr_depends_on_generic_params(node.lhs, generic_params)
                    || expr_depends_on_generic_params(node.rhs, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                return expr_depends_on_generic_params(node.base, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                for (const tyir::Ty& generic_arg : node.generic_args) {
                    if (type_depends_on_generic_params(generic_arg, generic_params))
                        return true;
                }
                for (const tyir::TyExprPtr& arg : node.args) {
                    if (expr_depends_on_generic_params(arg, generic_params))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Node, tyir::TyDeclProbe>) {
                return node.expr.has_value()
                    && expr_depends_on_generic_params(*node.expr, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                for (const std::optional<tyir::Ty>& generic_arg : node.generic_args) {
                    if (generic_arg.has_value()
                        && type_depends_on_generic_params(*generic_arg, generic_params)) {
                        return true;
                    }
                }
                for (const tyir::TyExprPtr& arg : node.args) {
                    if (expr_depends_on_generic_params(arg, generic_params))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                return block_depends_on_generic_params(node, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                if (expr_depends_on_generic_params(node.condition, generic_params)
                    || block_depends_on_generic_params(node.then_block, generic_params)) {
                    return true;
                }
                return node.else_branch.has_value()
                    && expr_depends_on_generic_params(*node.else_branch, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                return block_depends_on_generic_params(node.body, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                return expr_depends_on_generic_params(node.condition, generic_params)
                    || block_depends_on_generic_params(node.body, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                if (node.init.has_value()
                    && (type_depends_on_generic_params(node.init->ty, generic_params)
                        || expr_depends_on_generic_params(node.init->init, generic_params))) {
                    return true;
                }
                if (node.condition.has_value()
                    && expr_depends_on_generic_params(*node.condition, generic_params)) {
                    return true;
                }
                if (node.step.has_value()
                    && expr_depends_on_generic_params(*node.step, generic_params)) {
                    return true;
                }
                return block_depends_on_generic_params(node.body, generic_params);
            } else {
                static_assert(
                    std::is_same_v<Node, tyir::TyBreak> || std::is_same_v<Node, tyir::TyReturn>);
                return node.value.has_value()
                    && expr_depends_on_generic_params(*node.value, generic_params);
            }
        },
        expr->node);
}

[[nodiscard]] static tyir::TyExprPtr
    apply_substitution(const tyir::TyExprPtr& expr, const TypeSubstitution& subst);

[[nodiscard]] static tyir::TyBlockPtr
    apply_substitution(const tyir::TyBlockPtr& block, const TypeSubstitution& subst);

[[nodiscard]] static tyir::TyForInit
    apply_substitution(const tyir::TyForInit& init, const TypeSubstitution& subst) {
    tyir::TyForInit rewritten = init;
    rewritten.ty = apply_substitution(init.ty, subst);
    rewritten.init = apply_substitution(init.init, subst);
    return rewritten;
}

[[nodiscard]] static tyir::TyBlockPtr
    apply_substitution(const tyir::TyBlockPtr& block, const TypeSubstitution& subst) {
    if (block == nullptr)
        return nullptr;

    auto rewritten = std::make_shared<tyir::TyBlock>();
    rewritten->ty = apply_substitution(block->ty, subst);
    rewritten->span = block->span;
    rewritten->stmts.reserve(block->stmts.size());
    for (const tyir::TyStmt& stmt : block->stmts) {
        rewritten->stmts.push_back(
            std::visit(
                [&](const auto& node) -> tyir::TyStmt {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, tyir::TyLetStmt>) {
                        return tyir::TyLetStmt{
                            node.discard,
                            node.name,
                            apply_substitution(node.ty, subst),
                            apply_substitution(node.init, subst),
                            node.span,
                        };
                    } else {
                        return tyir::TyExprStmt{apply_substitution(node.expr, subst), node.span};
                    }
                },
                stmt));
    }
    if (block->tail.has_value())
        rewritten->tail = apply_substitution(*block->tail, subst);
    return rewritten;
}

[[nodiscard]] static tyir::TyExprPtr
    apply_substitution(const tyir::TyExprPtr& expr, const TypeSubstitution& subst) {
    if (expr == nullptr)
        return nullptr;

    const tyir::Ty rewritten_ty = apply_substitution(expr->ty, subst);
    return std::visit(
        [&](const auto& node) -> tyir::TyExprPtr {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (
                std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                || std::is_same_v<Node, tyir::EnumVariantRef>
                || std::is_same_v<Node, tyir::TyContinue>) {
                return tyir::make_ty_expr(expr->span, node, rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                tyir::TyStructInit rewritten = node;
                rewritten.generic_args.clear();
                rewritten.generic_args.reserve(node.generic_args.size());
                for (const tyir::Ty& generic_arg : node.generic_args)
                    rewritten.generic_args.push_back(apply_substitution(generic_arg, subst));
                for (tyir::TyStructInitField& field : rewritten.fields)
                    field.value = apply_substitution(field.value, subst);
                return tyir::make_ty_expr(expr->span, std::move(rewritten), rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyBorrow>) {
                return tyir::make_ty_expr(
                    expr->span, tyir::TyBorrow{apply_substitution(node.rhs, subst)}, rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyUnary>) {
                return tyir::make_ty_expr(
                    expr->span, tyir::TyUnary{node.op, apply_substitution(node.rhs, subst)},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyBinary{
                        node.op, apply_substitution(node.lhs, subst),
                        apply_substitution(node.rhs, subst)},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyFieldAccess{
                        apply_substitution(node.base, subst), node.field, node.use_kind},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                tyir::TyCall rewritten = node;
                rewritten.generic_args.clear();
                rewritten.generic_args.reserve(node.generic_args.size());
                for (const tyir::Ty& generic_arg : node.generic_args)
                    rewritten.generic_args.push_back(apply_substitution(generic_arg, subst));
                for (tyir::TyExprPtr& arg : rewritten.args)
                    arg = apply_substitution(arg, subst);
                return tyir::make_ty_expr(expr->span, std::move(rewritten), rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyDeclProbe>) {
                tyir::TyDeclProbe rewritten = node;
                if (rewritten.expr.has_value())
                    rewritten.expr = apply_substitution(*rewritten.expr, subst);
                return tyir::make_ty_expr(expr->span, std::move(rewritten), rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                tyir::TyDeferredGenericCall rewritten = node;
                rewritten.generic_args.clear();
                rewritten.generic_args.reserve(node.generic_args.size());
                bool fully_resolved = true;
                for (const std::optional<tyir::Ty>& generic_arg : node.generic_args) {
                    if (!generic_arg.has_value()) {
                        rewritten.generic_args.push_back(std::nullopt);
                        fully_resolved = false;
                        continue;
                    }
                    rewritten.generic_args.push_back(apply_substitution(*generic_arg, subst));
                }
                for (tyir::TyExprPtr& arg : rewritten.args)
                    arg = apply_substitution(arg, subst);
                if (!fully_resolved)
                    return tyir::make_ty_expr(expr->span, std::move(rewritten), rewritten_ty);

                std::vector<tyir::Ty> concrete_generic_args;
                concrete_generic_args.reserve(rewritten.generic_args.size());
                for (const std::optional<tyir::Ty>& generic_arg : rewritten.generic_args) {
                    assert(generic_arg.has_value());
                    concrete_generic_args.push_back(*generic_arg);
                }
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyCall{
                        rewritten.fn_name, std::move(concrete_generic_args),
                        std::move(rewritten.args)},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                return tyir::make_ty_expr(
                    expr->span, apply_substitution(node, subst), rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                std::optional<tyir::TyExprPtr> else_branch;
                if (node.else_branch.has_value())
                    else_branch = apply_substitution(*node.else_branch, subst);
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyIf{
                        apply_substitution(node.condition, subst),
                        apply_substitution(node.then_block, subst), std::move(else_branch)},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                return tyir::make_ty_expr(
                    expr->span, tyir::TyLoop{apply_substitution(node.body, subst)}, rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyWhile{
                        apply_substitution(node.condition, subst),
                        apply_substitution(node.body, subst)},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                std::optional<tyir::TyForInit> init;
                if (node.init.has_value())
                    init = apply_substitution(*node.init, subst);
                std::optional<tyir::TyExprPtr> condition;
                if (node.condition.has_value())
                    condition = apply_substitution(*node.condition, subst);
                std::optional<tyir::TyExprPtr> step;
                if (node.step.has_value())
                    step = apply_substitution(*node.step, subst);
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyFor{
                        std::move(init), std::move(condition), std::move(step),
                        apply_substitution(node.body, subst)},
                    rewritten_ty);
            } else if constexpr (std::is_same_v<Node, tyir::TyBreak>) {
                std::optional<tyir::TyExprPtr> value;
                if (node.value.has_value())
                    value = apply_substitution(*node.value, subst);
                return tyir::make_ty_expr(
                    expr->span, tyir::TyBreak{std::move(value)}, rewritten_ty);
            } else {
                std::optional<tyir::TyExprPtr> value;
                if (node.value.has_value())
                    value = apply_substitution(*node.value, subst);
                return tyir::make_ty_expr(
                    expr->span, tyir::TyReturn{std::move(value)}, rewritten_ty);
            }
        },
        expr->node);
}

[[nodiscard]] static std::string format_num(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    std::string rendered = out.str();
    if (rendered.find_first_of("eE") != std::string::npos) {
        out.str("");
        out.clear();
        out << std::fixed << std::setprecision(17) << value;
        rendered = out.str();
    }
    if (const auto dot = rendered.find('.'); dot != std::string::npos) {
        while (!rendered.empty() && rendered.back() == '0')
            rendered.pop_back();
        if (!rendered.empty() && rendered.back() == '.')
            rendered.pop_back();
    }
    if (rendered == "-0")
        return "0";
    if (rendered.empty())
        return "0";
    return rendered;
}

[[nodiscard]] static double parse_num(Symbol symbol) {
    const std::string text(symbol.as_str());
    return std::strtod(text.c_str(), nullptr);
}

[[nodiscard]] static const Value* deref_value(const ValuePtr& value) {
    const Value* current = value.get();
    while (current != nullptr && current->kind == Value::Kind::Ref && current->referent != nullptr)
        current = current->referent.get();
    return current;
}

[[nodiscard]] static ValuePtr find_struct_field_ptr(const Value& base, Symbol field_name) {
    for (const ValueField& field : base.fields) {
        if (field.name == field_name)
            return field.value;
    }
    return nullptr;
}

[[nodiscard]] static bool ordered_num_equal(double lhs, double rhs);

bool values_equal(const ValuePtr& lhs, const ValuePtr& rhs) {
    const Value* left = deref_value(lhs);
    const Value* right = deref_value(rhs);
    if (left == nullptr || right == nullptr)
        return left == right;
    if (left->kind != right->kind)
        return false;

    switch (left->kind) {
    case Value::Kind::Num: return ordered_num_equal(left->num_value, right->num_value);
    case Value::Kind::Bool: return left->bool_value == right->bool_value;
    case Value::Kind::Unit: return true;
    case Value::Kind::String: return left->string_value == right->string_value;
    case Value::Kind::Enum:
        return left->type_name == right->type_name && left->variant_name == right->variant_name;
    case Value::Kind::Struct:
        if (left->type_name != right->type_name || left->fields.size() != right->fields.size())
            return false;
        for (std::size_t index = 0; index < left->fields.size(); ++index) {
            if (left->fields[index].name != right->fields[index].name)
                return false;
            if (!values_equal(left->fields[index].value, right->fields[index].value))
                return false;
        }
        return true;
    case Value::Kind::Ref:
        assert(false && "deref_value should not leave reference wrappers behind");
        return false;
    }
    return false;
}

[[nodiscard]] static std::optional<bool> constant_bool(
    const tyir::TyExprPtr& expr, const ConstEnv& env, const ProgramView& program,
    std::vector<EvalStackFrame> stack, const GenericParamSet& generic_params);

[[nodiscard]] static ConstraintEvalResult validate_decl_probe(
    const tyir::TyDeclProbe& probe, const ProgramView& program,
    const GenericParamSet& generic_params,
    const std::shared_ptr<ConstraintEvalState>& constraint_state =
        std::make_shared<ConstraintEvalState>());

[[nodiscard]] static std::expected<EvalState, EvalError>
    eval_expr(const tyir::TyExprPtr& expr, ConstEnv& env, EvalContext& ctx);

[[nodiscard]] static std::expected<void, EvalError> enforce_constraints(
    const ProgramView& program, const std::vector<tyir::TyGenericConstraint>& constraints,
    const TypeSubstitution& substitution, std::string_view owner_kind, Symbol owner_name,
    const std::vector<tyir::Ty>& owner_generic_args, SourceSpan owner_span, SourceSpan use_span,
    const std::shared_ptr<ConstraintEvalState>& constraint_state);

[[nodiscard]] static std::expected<EvalState, EvalError> eval_block(
    const tyir::TyBlockPtr& block, ConstEnv& env, EvalContext& ctx, bool consume_budget = true);

[[nodiscard]] static std::unexpected<EvalError>
    unsupported_value_error(const EvalContext& ctx, SourceSpan span, std::string_view what) {
    return make_error(ctx, span, "unsupported compile-time value: " + std::string(what));
}

struct NumericOperands {
    double lhs = 0.0;
    double rhs = 0.0;
};

[[nodiscard]] static bool ordered_num_equal(double lhs, double rhs) {
    // Match runtime LLVM ordered float equality: NaN never compares equal, even to itself.
    return !std::isnan(lhs) && !std::isnan(rhs) && lhs == rhs;
}

[[nodiscard]] static std::expected<NumericOperands, EvalError> require_numeric_operands(
    const Value* lhs, const Value* rhs, const EvalContext& ctx, SourceSpan span,
    std::string_view what) {
    if (lhs == nullptr || rhs == nullptr || lhs->kind != Value::Kind::Num
        || rhs->kind != Value::Kind::Num) {
        return unsupported_value_error(ctx, span, what);
    }
    return NumericOperands{lhs->num_value, rhs->num_value};
}

[[nodiscard]] static std::string
    describe_constraint_owner(std::string_view kind, Symbol name, SourceSpan span) {
    return std::string(kind) + " '" + std::string(name.as_str()) + "' at "
         + std::to_string(span.start) + ":" + std::to_string(span.end);
}

[[nodiscard]] static TypeSubstitution build_substitution(
    const std::vector<cstc::ast::GenericParam>& generic_params,
    const std::vector<tyir::Ty>& generic_args) {
    TypeSubstitution substitution;
    substitution.reserve(generic_params.size());
    for (std::size_t index = 0; index < generic_params.size() && index < generic_args.size();
         ++index)
        substitution.emplace(generic_params[index].name, generic_args[index]);
    return substitution;
}

[[nodiscard]] static std::string
    constraint_instantiation_key(Symbol owner_name, const std::vector<tyir::Ty>& generic_args) {
    std::string key = std::string(owner_name.as_str()) + "<";
    for (std::size_t index = 0; index < generic_args.size(); ++index) {
        if (index > 0)
            key += ",";
        key += encode_type_for_constraint_key(generic_args[index]);
    }
    key += ">";
    return key;
}

[[nodiscard]] static cstc::tyir::InstantiationFrame make_instantiation_frame(
    Symbol owner_name, SourceSpan span, const std::vector<tyir::Ty>& generic_args) {
    cstc::tyir::InstantiationFrame frame;
    frame.item_name = owner_name;
    frame.span = span;
    frame.generic_args = generic_args;
    return frame;
}

ConstraintEvalResult evaluate_constraint(
    const tyir::TyExprPtr& expr, const TypeSubstitution& substitution, const ProgramView& program,
    std::vector<EvalStackFrame> stack,
    const std::shared_ptr<ConstraintEvalState>& constraint_state) {
    const tyir::TyExprPtr substituted = apply_substitution(expr, substitution);
    ConstEnv env;
    env.push();
    EvalContext ctx{
        program,
        std::move(stack),
        kDefaultEvalStepBudget,
        kDefaultEvalCallDepth,
        {},
        std::move(constraint_state),
    };
    auto value = eval_expr(substituted, env, ctx);
    env.pop();
    if (!value)
        return {
            ConstraintEvalKind::NotConstEvaluable,
            value.error().message,
            value.error().instantiation_limit,
        };
    if (value->kind == EvalState::Kind::Blocked) {
        return {
            value->blocked_reason == EvalState::BlockedReason::RuntimeOnly
                ? ConstraintEvalKind::RuntimeOnly
                : ConstraintEvalKind::NotConstEvaluable,
            value->blocked_reason == EvalState::BlockedReason::RuntimeOnly
                ? "constraint uses runtime-only behavior"
                : "constraint could not be const-evaluated",
            std::nullopt,
        };
    }
    if (value->kind != EvalState::Kind::Value)
        return {
            ConstraintEvalKind::NotConstEvaluable,
            "constraint did not produce a value",
            std::nullopt,
        };
    const Value* actual = deref_value(value->value);
    if (actual == nullptr || actual->kind != Value::Kind::Enum
        || actual->type_name != program.constraint_enum_name) {
        return {
            ConstraintEvalKind::InvalidType,
            "constraint expression must evaluate to 'Constraint'",
            std::nullopt,
        };
    }
    if (actual->variant_name == Symbol::intern("Valid"))
        return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
    if (actual->variant_name == Symbol::intern("Invalid"))
        return {ConstraintEvalKind::Unsatisfied, {}, std::nullopt};
    return {
        ConstraintEvalKind::InvalidType,
        "constraint must be Constraint::Valid or Constraint::Invalid",
        std::nullopt,
    };
}

[[nodiscard]] static ConstraintEvalResult validate_decl_probe(
    const tyir::TyDeclProbe& probe, const ProgramView& program,
    const GenericParamSet& generic_params,
    const std::shared_ptr<ConstraintEvalState>& constraint_state) {
    if (probe.is_invalid) {
        return {
            ConstraintEvalKind::Unsatisfied,
            probe.invalid_reason.value_or("probed expression is not type-valid"),
            std::nullopt,
        };
    }
    if (!probe.expr.has_value()) {
        return {
            ConstraintEvalKind::Unsatisfied,
            "probed expression is not type-valid",
            std::nullopt,
        };
    }

    ConstraintEvalResult ownership_status =
        validate_decl_probe_ownership(*probe.expr, generic_params);
    if (ownership_status.kind != ConstraintEvalKind::Satisfied)
        return ownership_status;

    const auto validate_expr = [&](const auto& self,
                                   const tyir::TyExprPtr& expr) -> ConstraintEvalResult {
        if (expr == nullptr) {
            return {
                ConstraintEvalKind::Unsatisfied, "probed expression is not type-valid",
                std::nullopt};
        }
        if (expr->ty.is_runtime) {
            return {
                ConstraintEvalKind::Unsatisfied,
                "probed expression uses runtime-only behavior",
                std::nullopt,
            };
        }

        return std::visit(
            [&](const auto& node) -> ConstraintEvalResult {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (
                    std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                    || std::is_same_v<Node, tyir::EnumVariantRef>) {
                    return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
                } else if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                    const auto decl_it = program.structs.find(node.type_name);
                    bool defer_where_clause = false;
                    if (decl_it != program.structs.end()) {
                        if (node.generic_args.size() != decl_it->second->generic_params.size()) {
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        const bool generic_args_depend = generic_args_depend_on_generic_params(
                            node.generic_args, generic_params);
                        const auto substitution =
                            build_substitution(decl_it->second->generic_params, node.generic_args);
                        if (!generic_args_depend) {
                            auto status = enforce_constraints(
                                program, decl_it->second->lowered_where_clause, substitution,
                                "type", decl_it->second->name, node.generic_args,
                                decl_it->second->span, expr->span, constraint_state);
                            if (!status) {
                                if (status.error().instantiation_limit.has_value()) {
                                    return {
                                        ConstraintEvalKind::NotConstEvaluable,
                                        status.error().message,
                                        status.error().instantiation_limit,
                                    };
                                }
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    status.error().message,
                                    status.error().instantiation_limit,
                                };
                            }
                        }

                        std::unordered_set<Symbol, SymbolHash> seen_fields;
                        seen_fields.reserve(node.fields.size());
                        for (const tyir::TyStructInitField& field : node.fields) {
                            if (!seen_fields.insert(field.name).second) {
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    "probed expression is not type-valid",
                                    std::nullopt,
                                };
                            }
                            const auto field_it = std::find_if(
                                decl_it->second->fields.begin(), decl_it->second->fields.end(),
                                [&](const tyir::TyFieldDecl& decl_field) {
                                    return decl_field.name == field.name;
                                });
                            if (field_it == decl_it->second->fields.end()) {
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    "probed expression is not type-valid",
                                    std::nullopt,
                                };
                            }

                            const tyir::Ty expected_ty =
                                apply_substitution(field_it->ty, substitution);
                            if (!compatible(field.value->ty, expected_ty)) {
                                if (auto unresolved = unresolved_compatibility_check(
                                        field.value->ty, expected_ty, generic_params);
                                    unresolved.has_value()) {
                                    return *unresolved;
                                }
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    "probed expression is not type-valid",
                                    std::nullopt,
                                };
                            }
                        }
                        for (const tyir::TyFieldDecl& decl_field : decl_it->second->fields) {
                            if (seen_fields.count(decl_field.name) == 0) {
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    "probed expression is not type-valid",
                                    std::nullopt,
                                };
                            }
                        }
                        defer_where_clause =
                            generic_args_depend && !decl_it->second->lowered_where_clause.empty();
                    }
                    for (const tyir::TyStructInitField& field : node.fields) {
                        auto nested = self(self, field.value);
                        if (nested.kind != ConstraintEvalKind::Satisfied)
                            return nested;
                    }
                    if (defer_where_clause)
                        return generic_substitution_dependency_result();
                    return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
                } else if constexpr (
                    std::is_same_v<Node, tyir::TyBorrow> || std::is_same_v<Node, tyir::TyUnary>) {
                    if constexpr (std::is_same_v<Node, tyir::TyUnary>) {
                        if (node.op == ast::UnaryOp::Negate
                            && !matches_type_shape(node.rhs->ty, tyir::ty::num())) {
                            if (auto unresolved = unresolved_shape_check(
                                    node.rhs->ty, tyir::ty::num(), generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        if (node.op == ast::UnaryOp::Not
                            && !matches_type_shape(node.rhs->ty, tyir::ty::bool_())) {
                            if (auto unresolved = unresolved_shape_check(
                                    node.rhs->ty, tyir::ty::bool_(), generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                    }
                    return self(self, node.rhs);
                } else if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                    const auto require_lhs_rhs =
                        [&](const tyir::Ty& expected) -> std::optional<ConstraintEvalResult> {
                        if (!matches_type_shape(node.lhs->ty, expected)) {
                            if (auto unresolved =
                                    unresolved_shape_check(node.lhs->ty, expected, generic_params);
                                unresolved.has_value()) {
                                return unresolved;
                            }
                            return ConstraintEvalResult{
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        if (!matches_type_shape(node.rhs->ty, expected)) {
                            if (auto unresolved =
                                    unresolved_shape_check(node.rhs->ty, expected, generic_params);
                                unresolved.has_value()) {
                                return unresolved;
                            }
                            return ConstraintEvalResult{
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        return std::nullopt;
                    };
                    switch (node.op) {
                    case ast::BinaryOp::Add:
                    case ast::BinaryOp::Sub:
                    case ast::BinaryOp::Mul:
                    case ast::BinaryOp::Div:
                    case ast::BinaryOp::Mod:
                    case ast::BinaryOp::Lt:
                    case ast::BinaryOp::Le:
                    case ast::BinaryOp::Gt:
                    case ast::BinaryOp::Ge:
                        if (auto invalid = require_lhs_rhs(tyir::ty::num()); invalid.has_value())
                            return *invalid;
                        break;
                    case ast::BinaryOp::And:
                    case ast::BinaryOp::Or:
                        if (auto invalid = require_lhs_rhs(tyir::ty::bool_()); invalid.has_value())
                            return *invalid;
                        break;
                    case ast::BinaryOp::Eq:
                    case ast::BinaryOp::Ne:
                        if (!compatible(node.lhs->ty, node.rhs->ty)
                            && !compatible(node.rhs->ty, node.lhs->ty)) {
                            if (auto unresolved = unresolved_compatibility_check(
                                    node.lhs->ty, node.rhs->ty, generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            if (auto unresolved = unresolved_compatibility_check(
                                    node.rhs->ty, node.lhs->ty, generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        break;
                    }
                    auto lhs = self(self, node.lhs);
                    if (lhs.kind != ConstraintEvalKind::Satisfied)
                        return lhs;
                    return self(self, node.rhs);
                } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                    return self(self, node.base);
                } else if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                    bool defer_where_clause = false;
                    if (const auto fn_it = program.fns.find(node.fn_name);
                        fn_it != program.fns.end()) {
                        const tyir::TyFnDecl& fn = *fn_it->second;
                        if (fn.return_ty.is_runtime || fn.is_runtime) {
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression uses runtime-only behavior",
                                std::nullopt,
                            };
                        }
                        const bool generic_args_depend = generic_args_depend_on_generic_params(
                            node.generic_args, generic_params);
                        if (node.generic_args.size() != fn.generic_params.size()) {
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        const auto substitution =
                            build_substitution(fn.generic_params, node.generic_args);
                        if (node.args.size() != fn.params.size()) {
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        for (std::size_t index = 0; index < node.args.size(); ++index) {
                            const tyir::Ty expected_ty =
                                apply_substitution(fn.params[index].ty, substitution);
                            if (!compatible(node.args[index]->ty, expected_ty)) {
                                if (auto unresolved = unresolved_compatibility_check(
                                        node.args[index]->ty, expected_ty, generic_params);
                                    unresolved.has_value()) {
                                    return *unresolved;
                                }
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    "probed expression is not type-valid",
                                    std::nullopt,
                                };
                            }
                        }
                        if (!generic_args_depend) {
                            auto status = enforce_constraints(
                                program, fn.lowered_where_clause, substitution, "function", fn.name,
                                node.generic_args, fn.span, expr->span, constraint_state);
                            if (!status) {
                                if (status.error().instantiation_limit.has_value()) {
                                    return {
                                        ConstraintEvalKind::NotConstEvaluable,
                                        status.error().message,
                                        status.error().instantiation_limit,
                                    };
                                }
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    status.error().message,
                                    status.error().instantiation_limit,
                                };
                            }
                        }
                        defer_where_clause =
                            generic_args_depend && !fn.lowered_where_clause.empty();
                    } else if (const auto ext_it = program.extern_fns.find(node.fn_name);
                               ext_it != program.extern_fns.end()) {
                        const tyir::TyExternFnDecl& decl = *ext_it->second;
                        if (decl.return_ty.is_runtime || decl.is_runtime) {
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression uses runtime-only behavior",
                                std::nullopt,
                            };
                        }
                        if (node.args.size() != decl.params.size()) {
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        for (std::size_t index = 0; index < node.args.size(); ++index) {
                            const tyir::Ty& expected_ty = decl.params[index].ty;
                            if (!compatible(node.args[index]->ty, expected_ty)) {
                                if (auto unresolved = unresolved_compatibility_check(
                                        node.args[index]->ty, expected_ty, generic_params);
                                    unresolved.has_value()) {
                                    return *unresolved;
                                }
                                return {
                                    ConstraintEvalKind::Unsatisfied,
                                    "probed expression is not type-valid",
                                    std::nullopt,
                                };
                            }
                        }
                    }
                    for (const tyir::TyExprPtr& arg : node.args) {
                        auto nested = self(self, arg);
                        if (nested.kind != ConstraintEvalKind::Satisfied)
                            return nested;
                    }
                    if (defer_where_clause)
                        return generic_substitution_dependency_result();
                    return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
                } else if constexpr (std::is_same_v<Node, tyir::TyDeclProbe>) {
                    return validate_decl_probe(node, program, generic_params, constraint_state);
                } else if constexpr (std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                    if (expr_depends_on_generic_params(expr, generic_params)) {
                        return {
                            ConstraintEvalKind::NotConstEvaluable,
                            "probed expression still depends on generic substitution",
                            std::nullopt,
                        };
                    }
                    return {
                        ConstraintEvalKind::Unsatisfied,
                        "probed expression requires unresolved generic inference",
                        std::nullopt,
                    };
                } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                    if (node == nullptr)
                        return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
                    for (const tyir::TyStmt& stmt : node->stmts) {
                        auto nested = std::visit(
                            [&](const auto& stmt_node) -> ConstraintEvalResult {
                                using StmtNode = std::decay_t<decltype(stmt_node)>;
                                if constexpr (std::is_same_v<StmtNode, tyir::TyLetStmt>) {
                                    if (!compatible(stmt_node.init->ty, stmt_node.ty)) {
                                        if (auto unresolved = unresolved_compatibility_check(
                                                stmt_node.init->ty, stmt_node.ty, generic_params);
                                            unresolved.has_value()) {
                                            return *unresolved;
                                        }
                                        return ConstraintEvalResult{
                                            ConstraintEvalKind::Unsatisfied,
                                            "probed expression is not type-valid",
                                            std::nullopt,
                                        };
                                    }
                                    return self(self, stmt_node.init);
                                } else
                                    return self(self, stmt_node.expr);
                            },
                            stmt);
                        if (nested.kind != ConstraintEvalKind::Satisfied)
                            return nested;
                    }
                    if (node->tail.has_value())
                        return self(self, *node->tail);
                    return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
                } else if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                    if (!matches_type_shape(node.condition->ty, tyir::ty::bool_())) {
                        if (auto unresolved = unresolved_shape_check(
                                node.condition->ty, tyir::ty::bool_(), generic_params);
                            unresolved.has_value()) {
                            return *unresolved;
                        }
                        return {
                            ConstraintEvalKind::Unsatisfied,
                            "probed expression is not type-valid",
                            std::nullopt,
                        };
                    }
                    auto condition = self(self, node.condition);
                    if (condition.kind != ConstraintEvalKind::Satisfied)
                        return condition;
                    auto then_branch = self(
                        self, tyir::make_ty_expr(expr->span, node.then_block, node.then_block->ty));
                    if (then_branch.kind != ConstraintEvalKind::Satisfied)
                        return then_branch;
                    if (node.else_branch.has_value()) {
                        if (!common_type(node.then_block->ty, (*node.else_branch)->ty)
                                 .has_value()) {
                            if (auto unresolved = unresolved_common_type_check(
                                    node.then_block->ty, (*node.else_branch)->ty, generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        return self(self, *node.else_branch);
                    }
                    return {ConstraintEvalKind::Satisfied, {}, std::nullopt};
                } else if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                    return self(self, tyir::make_ty_expr(expr->span, node.body, node.body->ty));
                } else if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                    if (!matches_type_shape(node.condition->ty, tyir::ty::bool_())) {
                        if (auto unresolved = unresolved_shape_check(
                                node.condition->ty, tyir::ty::bool_(), generic_params);
                            unresolved.has_value()) {
                            return *unresolved;
                        }
                        return {
                            ConstraintEvalKind::Unsatisfied,
                            "probed expression is not type-valid",
                            std::nullopt,
                        };
                    }
                    auto condition = self(self, node.condition);
                    if (condition.kind != ConstraintEvalKind::Satisfied)
                        return condition;
                    return self(self, tyir::make_ty_expr(expr->span, node.body, node.body->ty));
                } else if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                    if (node.init.has_value()) {
                        if (!compatible(node.init->init->ty, node.init->ty)) {
                            if (auto unresolved = unresolved_compatibility_check(
                                    node.init->init->ty, node.init->ty, generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        auto init = self(self, node.init->init);
                        if (init.kind != ConstraintEvalKind::Satisfied)
                            return init;
                    }
                    if (node.condition.has_value()) {
                        if (!matches_type_shape((*node.condition)->ty, tyir::ty::bool_())) {
                            if (auto unresolved = unresolved_shape_check(
                                    (*node.condition)->ty, tyir::ty::bool_(), generic_params);
                                unresolved.has_value()) {
                                return *unresolved;
                            }
                            return {
                                ConstraintEvalKind::Unsatisfied,
                                "probed expression is not type-valid",
                                std::nullopt,
                            };
                        }
                        auto condition = self(self, *node.condition);
                        if (condition.kind != ConstraintEvalKind::Satisfied)
                            return condition;
                    }
                    if (node.step.has_value()) {
                        auto step = self(self, *node.step);
                        if (step.kind != ConstraintEvalKind::Satisfied)
                            return step;
                    }
                    return self(self, tyir::make_ty_expr(expr->span, node.body, node.body->ty));
                } else {
                    return {
                        ConstraintEvalKind::Unsatisfied,
                        "probed expression uses non-local control flow",
                        std::nullopt,
                    };
                }
            },
            expr->node);
    };

    return validate_expr(validate_expr, *probe.expr);
}

[[nodiscard]] static std::expected<void, EvalError> enforce_constraints(
    const ProgramView& program, const std::vector<tyir::TyGenericConstraint>& constraints,
    const TypeSubstitution& substitution, std::string_view owner_kind, Symbol owner_name,
    const std::vector<tyir::Ty>& owner_generic_args, SourceSpan owner_span, SourceSpan use_span,
    const std::shared_ptr<ConstraintEvalState>& constraint_state =
        std::make_shared<ConstraintEvalState>()) {
    const std::string key = constraint_instantiation_key(owner_name, owner_generic_args);
    if (constraint_state->satisfied_keys.contains(key))
        return {};

    if (constraint_state->active_keys.contains(key)
        || constraint_state->instantiation_stack.size()
               >= cstc::tyir::kMaxGenericInstantiationDepth) {
        auto stack = constraint_state->instantiation_stack;
        stack.push_back(make_instantiation_frame(owner_name, owner_span, owner_generic_args));
        return std::unexpected(
            EvalError{
                use_span,
                "const-eval recursion limit reached while checking generic constraints for "
                    + describe_constraint_owner(owner_kind, owner_name, owner_span)
                    + "; active limit is "
                    + std::to_string(cstc::tyir::kMaxGenericInstantiationDepth)
                    + " and the program may contain non-productive recursion",
                {},
                cstc::tyir::InstantiationLimitDiagnostic{
                                                                          cstc::tyir::InstantiationPhase::ConstEval,
                                                                          cstc::tyir::kMaxGenericInstantiationDepth,
                                                                          std::move(stack),
                                                                          },
        });
    }

    constraint_state->active_keys.insert(key);
    constraint_state->instantiation_stack.push_back(
        make_instantiation_frame(owner_name, owner_span, owner_generic_args));

    for (const tyir::TyGenericConstraint& constraint : constraints) {
        const ConstraintEvalResult result =
            evaluate_constraint(constraint.expr, substitution, program, {}, constraint_state);
        switch (result.kind) {
        case ConstraintEvalKind::Satisfied: break;
        case ConstraintEvalKind::Unsatisfied:
            constraint_state->instantiation_stack.pop_back();
            constraint_state->active_keys.erase(key);
            return make_error(
                EvalContext{
                    program,
                    {},
                    kDefaultEvalStepBudget,
                    kDefaultEvalCallDepth,
                    {},
                    constraint_state,
                },
                use_span,
                "generic constraint failed for "
                    + describe_constraint_owner(owner_kind, owner_name, owner_span));
        case ConstraintEvalKind::RuntimeOnly:
            constraint_state->instantiation_stack.pop_back();
            constraint_state->active_keys.erase(key);
            return make_error(
                EvalContext{
                    program,
                    {},
                    kDefaultEvalStepBudget,
                    kDefaultEvalCallDepth,
                    {},
                    constraint_state,
                },
                use_span,
                "generic constraint for "
                    + describe_constraint_owner(owner_kind, owner_name, owner_span)
                    + " used runtime-only behavior");
        case ConstraintEvalKind::NotConstEvaluable:
            constraint_state->instantiation_stack.pop_back();
            constraint_state->active_keys.erase(key);
            if (result.instantiation_limit.has_value()) {
                return std::unexpected(
                    EvalError{
                        use_span,
                        "generic constraint for "
                            + describe_constraint_owner(owner_kind, owner_name, owner_span)
                            + " could not be const-evaluated: " + result.detail,
                        {},
                        result.instantiation_limit,
                    });
            }
            return make_error(
                EvalContext{
                    program,
                    {},
                    kDefaultEvalStepBudget,
                    kDefaultEvalCallDepth,
                    {},
                    constraint_state,
                },
                use_span,
                "generic constraint for "
                    + describe_constraint_owner(owner_kind, owner_name, owner_span)
                    + " could not be const-evaluated: " + result.detail);
        case ConstraintEvalKind::InvalidType:
            constraint_state->instantiation_stack.pop_back();
            constraint_state->active_keys.erase(key);
            return make_error(
                EvalContext{
                    program,
                    {},
                    kDefaultEvalStepBudget,
                    kDefaultEvalCallDepth,
                    {},
                    constraint_state,
                },
                use_span,
                "generic constraint for "
                    + describe_constraint_owner(owner_kind, owner_name, owner_span)
                    + " is invalid: " + result.detail);
        }
    }

    constraint_state->instantiation_stack.pop_back();
    constraint_state->active_keys.erase(key);
    constraint_state->satisfied_keys.insert(key);
    return {};
}

std::expected<ValuePtr, EvalError> eval_lang_intrinsic(
    const tyir::TyExternFnDecl& decl, const std::vector<ValuePtr>& args, EvalContext& ctx,
    SourceSpan span) {
    const Symbol resolved_name = decl.link_name.is_valid() ? decl.link_name : decl.name;
    const std::string_view name = resolved_name.as_str();
    const std::string_view display_name = decl.name.is_valid() ? decl.name.as_str() : name;
    const auto require_intrinsic_arity =
        [&](std::size_t expected) -> std::expected<void, EvalError> {
        return require_call_arity(ctx, span, display_name, expected, args.size());
    };
    auto declared_arity =
        require_call_arity(ctx, span, display_name, decl.params.size(), args.size());
    if (!declared_arity)
        return std::unexpected(std::move(declared_arity.error()));
    const auto unwrap_string = [&](std::size_t index) -> std::expected<const Value*, EvalError> {
        const Value* value = deref_value(args[index]);
        if (value == nullptr || value->kind != Value::Kind::String)
            return unsupported_value_error(ctx, span, "string argument");
        return value;
    };

    if (name == "to_str" || name == "cstc_std_to_str") {
        auto arity = require_intrinsic_arity(1);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        const Value* arg = deref_value(args[0]);
        if (arg == nullptr || arg->kind != Value::Kind::Num)
            return unsupported_value_error(ctx, span, "numeric argument");
        return make_string(format_num(arg->num_value), Value::StringOwnership::Owned);
    }

    if (name == "str_concat" || name == "cstc_std_str_concat") {
        auto arity = require_intrinsic_arity(2);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        auto lhs = unwrap_string(0);
        if (!lhs)
            return std::unexpected(std::move(lhs.error()));
        auto rhs = unwrap_string(1);
        if (!rhs)
            return std::unexpected(std::move(rhs.error()));
        return make_string(
            (*lhs)->string_value + (*rhs)->string_value, Value::StringOwnership::Owned);
    }

    if (name == "str_len" || name == "cstc_std_str_len") {
        auto arity = require_intrinsic_arity(1);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        auto value = unwrap_string(0);
        if (!value)
            return std::unexpected(std::move(value.error()));
        return make_num(static_cast<double>((*value)->string_value.size()));
    }

    if (name == "str_free" || name == "cstc_std_str_free") {
        auto arity = require_intrinsic_arity(1);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        auto value = unwrap_string(0);
        if (!value)
            return std::unexpected(std::move(value.error()));
        // Runtime str_free only frees owned buffers; borrowed literals are a no-op.
        return make_unit();
    }

    if (name == "assert" || name == "cstc_std_assert") {
        auto arity = require_intrinsic_arity(1);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        const Value* value = deref_value(args[0]);
        if (value == nullptr || value->kind != Value::Kind::Bool)
            return unsupported_value_error(ctx, span, "boolean assertion argument");
        if (!value->bool_value)
            return make_error(ctx, span, "compile-time assertion failed");
        return make_unit();
    }

    if (name == "assert_eq" || name == "cstc_std_assert_eq") {
        auto arity = require_intrinsic_arity(2);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        const Value* lhs = deref_value(args[0]);
        const Value* rhs = deref_value(args[1]);
        auto operands =
            require_numeric_operands(lhs, rhs, ctx, span, "numeric equality assertion arguments");
        if (!operands)
            return std::unexpected(std::move(operands.error()));
        if (!ordered_num_equal(operands->lhs, operands->rhs)) {
            return make_error(
                ctx, span,
                "compile-time assert_eq failed: left=" + format_num(operands->lhs)
                    + ", right=" + format_num(operands->rhs));
        }
        return make_unit();
    }

    if (name == "constraint" || name == "cstc_std_constraint") {
        auto arity = require_intrinsic_arity(1);
        if (!arity)
            return std::unexpected(std::move(arity.error()));
        const Value* value = deref_value(args[0]);
        if (value == nullptr || value->kind != Value::Kind::Bool)
            return unsupported_value_error(ctx, span, "boolean constraint argument");
        return make_enum(
            decl.return_ty.name,
            value->bool_value ? Symbol::intern("Valid") : Symbol::intern("Invalid"));
    }

    if (name == "print" || name == "println" || name == "cstc_std_print"
        || name == "cstc_std_println") {
        return std::unexpected(
            EvalError{
                span,
                "impure lang intrinsic '" + std::string(name)
                    + "' is not const-evaluable; mark it `runtime`",
                ctx.stack,
                std::nullopt,
            });
    }

    return make_error(
        ctx, span, "unsupported non-runtime lang intrinsic '" + std::string(name) + "'");
}

[[nodiscard]] static std::expected<EvalState, EvalError>
    eval_expr(const tyir::TyExprPtr& expr, ConstEnv& env, EvalContext& ctx) {
    if (expr->ty.is_runtime)
        return EvalState::blocked(EvalState::BlockedReason::RuntimeOnly);

    return std::visit(
        [&](const auto& node) -> std::expected<EvalState, EvalError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<Node, tyir::TyLiteral>) {
                switch (node.kind) {
                case tyir::TyLiteral::Kind::Num:
                    return EvalState::from_value(make_num(parse_num(node.symbol)));
                case tyir::TyLiteral::Kind::Str:
                    return EvalState::from_value(make_ref(make_string(
                        strip_quotes(node.symbol.as_str()),
                        Value::StringOwnership::BorrowedLiteral)));
                case tyir::TyLiteral::Kind::OwnedStr:
                    return EvalState::from_value(make_string(
                        strip_quotes(node.symbol.as_str()), Value::StringOwnership::Owned));
                case tyir::TyLiteral::Kind::Bool:
                    return EvalState::from_value(make_bool(node.bool_value));
                case tyir::TyLiteral::Kind::Unit: return EvalState::from_value(make_unit());
                }
                return EvalState::blocked();
            }

            if constexpr (std::is_same_v<Node, tyir::LocalRef>) {
                const auto found = env.lookup(node.name);
                if (!found.found || !found.value.has_value())
                    return EvalState::blocked();
                return EvalState::from_value(*found.value);
            }

            if constexpr (std::is_same_v<Node, tyir::EnumVariantRef>) {
                return EvalState::from_value(make_enum(node.enum_name, node.variant_name));
            }

            if constexpr (std::is_same_v<Node, tyir::TyDeclProbe>) {
                const auto result = validate_decl_probe(
                    node, ctx.program, ctx.generic_params,
                    ctx.constraint_state != nullptr ? ctx.constraint_state
                                                    : std::make_shared<ConstraintEvalState>());
                if (result.kind == ConstraintEvalKind::NotConstEvaluable
                    && result.instantiation_limit.has_value()) {
                    return std::unexpected(
                        EvalError{
                            expr->span,
                            result.detail.empty() ? "constraint could not be const-evaluated"
                                                  : result.detail,
                            ctx.stack,
                            result.instantiation_limit,
                        });
                }
                if (result.kind == ConstraintEvalKind::NotConstEvaluable)
                    return EvalState::blocked(EvalState::BlockedReason::UnknownValue);
                return EvalState::from_value(make_enum(
                    ctx.program.constraint_enum_name, result.kind == ConstraintEvalKind::Satisfied
                                                          ? Symbol::intern("Valid")
                                                          : Symbol::intern("Invalid")));
            }

            if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                const auto decl_it = ctx.program.structs.find(node.type_name);
                if (decl_it != ctx.program.structs.end()
                    && !generic_args_depend_on_generic_params(
                        node.generic_args, ctx.generic_params)) {
                    const auto constraint_status = enforce_constraints(
                        ctx.program, decl_it->second->lowered_where_clause,
                        build_substitution(decl_it->second->generic_params, node.generic_args),
                        "type", decl_it->second->name, node.generic_args, decl_it->second->span,
                        expr->span,
                        ctx.constraint_state != nullptr ? ctx.constraint_state
                                                        : std::make_shared<ConstraintEvalState>());
                    if (!constraint_status)
                        return std::unexpected(std::move(constraint_status.error()));
                }
                std::vector<ValueField> fields;
                fields.reserve(node.fields.size());
                for (const tyir::TyStructInitField& field : node.fields) {
                    auto value = eval_expr(field.value, env, ctx);
                    if (!value)
                        return std::unexpected(std::move(value.error()));
                    if (value->kind != EvalState::Kind::Value)
                        return *value;
                    fields.push_back({field.name, value->value});
                }
                return EvalState::from_value(make_struct(node.type_name, std::move(fields)));
            }

            if constexpr (std::is_same_v<Node, tyir::TyBorrow>) {
                auto value = eval_expr(node.rhs, env, ctx);
                if (!value)
                    return std::unexpected(std::move(value.error()));
                if (value->kind != EvalState::Kind::Value)
                    return *value;
                return EvalState::from_value(make_ref(value->value));
            }

            if constexpr (std::is_same_v<Node, tyir::TyUnary>) {
                auto rhs = eval_expr(node.rhs, env, ctx);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                if (rhs->kind != EvalState::Kind::Value)
                    return *rhs;
                const Value* value = deref_value(rhs->value);
                switch (node.op) {
                case cstc::ast::UnaryOp::Borrow: return EvalState::from_value(make_ref(rhs->value));
                case cstc::ast::UnaryOp::Negate:
                    if (value == nullptr || value->kind != Value::Kind::Num)
                        return unsupported_value_error(ctx, expr->span, "unary numeric operand");
                    return EvalState::from_value(make_num(-value->num_value));
                case cstc::ast::UnaryOp::Not:
                    if (value == nullptr || value->kind != Value::Kind::Bool)
                        return unsupported_value_error(ctx, expr->span, "unary boolean operand");
                    return EvalState::from_value(make_bool(!value->bool_value));
                }
                return EvalState::blocked();
            }

            if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                auto lhs = eval_expr(node.lhs, env, ctx);
                if (!lhs)
                    return std::unexpected(std::move(lhs.error()));
                if (lhs->kind != EvalState::Kind::Value)
                    return *lhs;
                const Value* left = deref_value(lhs->value);

                using Op = cstc::ast::BinaryOp;
                if (node.op == Op::And || node.op == Op::Or) {
                    if (left == nullptr || left->kind != Value::Kind::Bool)
                        return unsupported_value_error(ctx, expr->span, "boolean binary operands");

                    if (node.op == Op::And && !left->bool_value)
                        return EvalState::from_value(make_bool(false));
                    if (node.op == Op::Or && left->bool_value)
                        return EvalState::from_value(make_bool(true));
                }

                auto rhs = eval_expr(node.rhs, env, ctx);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                if (rhs->kind != EvalState::Kind::Value)
                    return *rhs;
                const Value* right = deref_value(rhs->value);

                switch (node.op) {
                case Op::Add:
                case Op::Sub:
                case Op::Mul:
                case Op::Div:
                case Op::Mod:
                case Op::Lt:
                case Op::Le:
                case Op::Gt:
                case Op::Ge: {
                    const bool comparison = node.op == Op::Lt || node.op == Op::Le
                                         || node.op == Op::Gt || node.op == Op::Ge;
                    auto operands = require_numeric_operands(
                        left, right, ctx, expr->span,
                        comparison ? "numeric comparison operands" : "numeric binary operands");
                    if (!operands)
                        return std::unexpected(std::move(operands.error()));
                    if ((node.op == Op::Div || node.op == Op::Mod) && operands->rhs == 0.0)
                        return make_error(ctx, expr->span, "division by zero during const-eval");

                    switch (node.op) {
                    case Op::Add:
                        return EvalState::from_value(make_num(operands->lhs + operands->rhs));
                    case Op::Sub:
                        return EvalState::from_value(make_num(operands->lhs - operands->rhs));
                    case Op::Mul:
                        return EvalState::from_value(make_num(operands->lhs * operands->rhs));
                    case Op::Div:
                        return EvalState::from_value(make_num(operands->lhs / operands->rhs));
                    case Op::Mod:
                        return EvalState::from_value(
                            make_num(std::fmod(operands->lhs, operands->rhs)));
                    case Op::Lt:
                        return EvalState::from_value(make_bool(operands->lhs < operands->rhs));
                    case Op::Le:
                        return EvalState::from_value(make_bool(operands->lhs <= operands->rhs));
                    case Op::Gt:
                        return EvalState::from_value(make_bool(operands->lhs > operands->rhs));
                    case Op::Ge:
                        return EvalState::from_value(make_bool(operands->lhs >= operands->rhs));
                    default: break;
                    }
                    break;
                }
                case Op::Eq:
                    return EvalState::from_value(make_bool(values_equal(lhs->value, rhs->value)));
                case Op::Ne:
                    return EvalState::from_value(make_bool(!values_equal(lhs->value, rhs->value)));
                case Op::And:
                case Op::Or:
                    if (right == nullptr || right->kind != Value::Kind::Bool) {
                        return unsupported_value_error(ctx, expr->span, "boolean binary operands");
                    }
                    return EvalState::from_value(make_bool(right->bool_value));
                }
                return unsupported_value_error(ctx, expr->span, "binary operator");
            }

            if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                auto base = eval_expr(node.base, env, ctx);
                if (!base)
                    return std::unexpected(std::move(base.error()));
                if (base->kind != EvalState::Kind::Value)
                    return *base;
                const Value* base_value = deref_value(base->value);
                if (base_value == nullptr || base_value->kind != Value::Kind::Struct)
                    return unsupported_value_error(ctx, expr->span, "struct field access");
                ValuePtr field = find_struct_field_ptr(*base_value, node.field);
                if (field == nullptr)
                    return make_error(
                        ctx, expr->span,
                        "missing field '" + std::string(node.field.as_str())
                            + "' during const-eval");
                if (node.use_kind == tyir::ValueUseKind::Borrow)
                    return EvalState::from_value(make_ref(field));
                return EvalState::from_value(field);
            }

            if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                const auto fn_it = ctx.program.fns.find(node.fn_name);
                if (fn_it != ctx.program.fns.end()) {
                    const tyir::TyFnDecl& fn = *fn_it->second;
                    if (fn.return_ty.is_runtime || fn.is_runtime)
                        return EvalState::blocked(EvalState::BlockedReason::RuntimeOnly);
                    if (!generic_args_depend_on_generic_params(
                            node.generic_args, ctx.generic_params)) {
                        const auto constraint_status = enforce_constraints(
                            ctx.program, fn.lowered_where_clause,
                            build_substitution(fn.generic_params, node.generic_args), "function",
                            fn.name, node.generic_args, fn.span, expr->span,
                            ctx.constraint_state != nullptr
                                ? ctx.constraint_state
                                : std::make_shared<ConstraintEvalState>());
                        if (!constraint_status)
                            return std::unexpected(std::move(constraint_status.error()));
                    }
                    auto arity = require_call_arity(
                        ctx, expr->span, fn.name.as_str(), fn.params.size(), node.args.size());
                    if (!arity)
                        return std::unexpected(std::move(arity.error()));

                    std::vector<ValuePtr> args;
                    args.reserve(node.args.size());
                    for (const tyir::TyExprPtr& arg : node.args) {
                        auto value = eval_expr(arg, env, ctx);
                        if (!value)
                            return std::unexpected(std::move(value.error()));
                        if (value->kind != EvalState::Kind::Value)
                            return *value;
                        args.push_back(value->value);
                    }

                    ConstEnv fn_env;
                    fn_env.push();
                    for (std::size_t index = 0; index < fn.params.size(); ++index)
                        fn_env.bind_known(fn.params[index].name, args[index]);

                    auto call_budget = consume_call_depth(ctx, expr->span, fn.name);
                    if (!call_budget)
                        return std::unexpected(std::move(call_budget.error()));
                    ctx.stack.push_back(EvalStackFrame{fn.name, expr->span});
                    auto result = eval_block(fn.body, fn_env, ctx);
                    ctx.stack.pop_back();
                    ++ctx.remaining_call_depth;
                    if (!result)
                        return std::unexpected(std::move(result.error()));

                    switch (result->kind) {
                    case EvalState::Kind::Value: return result;
                    case EvalState::Kind::Return:
                        return EvalState::from_value(
                            result->value != nullptr ? result->value : make_unit());
                    case EvalState::Kind::Blocked:
                        return EvalState::blocked(result->blocked_reason);
                    case EvalState::Kind::Break:
                    case EvalState::Kind::Continue:
                        return make_error(
                            ctx, expr->span, "loop control escaped function during const-eval");
                    }
                    return EvalState::blocked();
                }

                const auto ext_it = ctx.program.extern_fns.find(node.fn_name);
                if (ext_it == ctx.program.extern_fns.end())
                    return make_error(
                        ctx, expr->span,
                        "missing function body for '" + std::string(node.fn_name.as_str()) + "'");

                const tyir::TyExternFnDecl& decl = *ext_it->second;
                if (decl.return_ty.is_runtime || decl.is_runtime)
                    return EvalState::blocked(EvalState::BlockedReason::RuntimeOnly);
                if (decl.abi.as_str() != std::string_view{"lang"})
                    return make_error(
                        ctx, expr->span,
                        "reached non-runtime extern call with unsupported ABI '"
                            + std::string(decl.abi.as_str()) + "'");
                auto arity = require_call_arity(
                    ctx, expr->span, decl.name.as_str(), decl.params.size(), node.args.size());
                if (!arity)
                    return std::unexpected(std::move(arity.error()));

                std::vector<ValuePtr> args;
                args.reserve(node.args.size());
                for (const tyir::TyExprPtr& arg : node.args) {
                    auto value = eval_expr(arg, env, ctx);
                    if (!value)
                        return std::unexpected(std::move(value.error()));
                    if (value->kind != EvalState::Kind::Value)
                        return *value;
                    args.push_back(value->value);
                }

                auto value = eval_lang_intrinsic(decl, args, ctx, expr->span);
                if (!value)
                    return std::unexpected(std::move(value.error()));
                return EvalState::from_value(*value);
            }

            if constexpr (std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                return EvalState::blocked();
            }

            if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                return eval_block(node, env, ctx);
            }

            if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                auto cond = eval_expr(node.condition, env, ctx);
                if (!cond)
                    return std::unexpected(std::move(cond.error()));
                if (cond->kind != EvalState::Kind::Value)
                    return *cond;
                const Value* cond_value = deref_value(cond->value);
                if (cond_value == nullptr || cond_value->kind != Value::Kind::Bool)
                    return unsupported_value_error(ctx, expr->span, "if condition");
                if (cond_value->bool_value)
                    return eval_block(node.then_block, env, ctx);
                if (node.else_branch.has_value())
                    return eval_expr(*node.else_branch, env, ctx);
                return EvalState::from_value(make_unit());
            }

            if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                while (true) {
                    auto iteration_budget =
                        consume_step_budget(ctx, expr->span, "evaluating loop iteration");
                    if (!iteration_budget)
                        return std::unexpected(std::move(iteration_budget.error()));
                    auto body = eval_block(node.body, env, ctx, false);
                    if (!body)
                        return std::unexpected(std::move(body.error()));
                    switch (body->kind) {
                    case EvalState::Kind::Value: break;
                    case EvalState::Kind::Continue: continue;
                    case EvalState::Kind::Break:
                        return EvalState::from_value(
                            body->value != nullptr ? body->value : make_unit());
                    case EvalState::Kind::Return:
                    case EvalState::Kind::Blocked: return *body;
                    }
                }
            }

            if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                while (true) {
                    auto cond = eval_expr(node.condition, env, ctx);
                    if (!cond)
                        return std::unexpected(std::move(cond.error()));
                    if (cond->kind != EvalState::Kind::Value)
                        return *cond;
                    const Value* cond_value = deref_value(cond->value);
                    if (cond_value == nullptr || cond_value->kind != Value::Kind::Bool)
                        return unsupported_value_error(ctx, expr->span, "while condition");
                    if (!cond_value->bool_value)
                        return EvalState::from_value(make_unit());

                    auto iteration_budget =
                        consume_step_budget(ctx, expr->span, "evaluating while iteration");
                    if (!iteration_budget)
                        return std::unexpected(std::move(iteration_budget.error()));
                    auto body = eval_block(node.body, env, ctx, false);
                    if (!body)
                        return std::unexpected(std::move(body.error()));
                    switch (body->kind) {
                    case EvalState::Kind::Value:
                    case EvalState::Kind::Continue: continue;
                    case EvalState::Kind::Break:
                        return EvalState::from_value(
                            body->value != nullptr ? body->value : make_unit());
                    case EvalState::Kind::Return:
                    case EvalState::Kind::Blocked: return *body;
                    }
                }
            }

            if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                env.push();
                if (node.init.has_value()) {
                    auto init = eval_expr(node.init->init, env, ctx);
                    if (!init) {
                        env.pop();
                        return std::unexpected(std::move(init.error()));
                    }
                    if (init->kind != EvalState::Kind::Value) {
                        env.pop();
                        return *init;
                    }
                    if (!node.init->discard)
                        env.bind_known(node.init->name, init->value);
                }

                while (true) {
                    if (node.condition.has_value()) {
                        auto cond = eval_expr(*node.condition, env, ctx);
                        if (!cond) {
                            env.pop();
                            return std::unexpected(std::move(cond.error()));
                        }
                        if (cond->kind != EvalState::Kind::Value) {
                            env.pop();
                            return *cond;
                        }
                        const Value* cond_value = deref_value(cond->value);
                        if (cond_value == nullptr || cond_value->kind != Value::Kind::Bool) {
                            env.pop();
                            return unsupported_value_error(ctx, expr->span, "for condition");
                        }
                        if (!cond_value->bool_value)
                            break;
                    }

                    auto iteration_budget =
                        consume_step_budget(ctx, expr->span, "evaluating for iteration");
                    if (!iteration_budget) {
                        env.pop();
                        return std::unexpected(std::move(iteration_budget.error()));
                    }
                    auto body = eval_block(node.body, env, ctx, false);
                    if (!body) {
                        env.pop();
                        return std::unexpected(std::move(body.error()));
                    }
                    if (body->kind == EvalState::Kind::Return
                        || body->kind == EvalState::Kind::Blocked) {
                        env.pop();
                        return *body;
                    }
                    if (body->kind == EvalState::Kind::Break) {
                        if (body->value != nullptr) {
                            env.pop();
                            return make_error(
                                ctx, expr->span,
                                "'break' with a value is only allowed inside 'loop' during "
                                "const-eval");
                        }
                        break;
                    }

                    if (node.step.has_value()) {
                        auto step = eval_expr(*node.step, env, ctx);
                        if (!step) {
                            env.pop();
                            return std::unexpected(std::move(step.error()));
                        }
                        if (step->kind != EvalState::Kind::Value) {
                            env.pop();
                            return *step;
                        }
                    }
                }

                env.pop();
                return EvalState::from_value(make_unit());
            }

            if constexpr (std::is_same_v<Node, tyir::TyBreak>) {
                if (!node.value.has_value())
                    return EvalState::from_break();
                auto value = eval_expr(*node.value, env, ctx);
                if (!value)
                    return std::unexpected(std::move(value.error()));
                if (value->kind != EvalState::Kind::Value)
                    return *value;
                return EvalState::from_break(value->value);
            }

            if constexpr (std::is_same_v<Node, tyir::TyContinue>) {
                return EvalState::continue_();
            }

            if constexpr (std::is_same_v<Node, tyir::TyReturn>) {
                if (!node.value.has_value())
                    return EvalState::from_return(make_unit());
                auto value = eval_expr(*node.value, env, ctx);
                if (!value)
                    return std::unexpected(std::move(value.error()));
                if (value->kind != EvalState::Kind::Value)
                    return *value;
                return EvalState::from_return(value->value);
            }

            return EvalState::blocked();
        },
        expr->node);
}

[[nodiscard]] static std::expected<EvalState, EvalError> eval_block(
    const tyir::TyBlockPtr& block, ConstEnv& env, EvalContext& ctx, bool consume_budget) {
    if (consume_budget) {
        auto budget = consume_step_budget(ctx, block->span, "evaluating block");
        if (!budget)
            return std::unexpected(std::move(budget.error()));
    }
    env.push();
    for (const tyir::TyStmt& stmt : block->stmts) {
        auto stmt_result = std::visit(
            [&](const auto& node) -> std::expected<EvalState, EvalError> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyLetStmt>) {
                    auto value = eval_expr(node.init, env, ctx);
                    if (!value)
                        return std::unexpected(std::move(value.error()));
                    if (value->kind != EvalState::Kind::Value)
                        return *value;
                    if (!node.discard)
                        env.bind_known(node.name, value->value);
                    return EvalState::from_value(make_unit());
                } else {
                    return eval_expr(node.expr, env, ctx);
                }
            },
            stmt);
        if (!stmt_result) {
            env.pop();
            return std::unexpected(std::move(stmt_result.error()));
        }
        if (stmt_result->kind != EvalState::Kind::Value) {
            env.pop();
            return *stmt_result;
        }
    }

    if (!block->tail.has_value()) {
        env.pop();
        return EvalState::from_value(make_unit());
    }

    auto tail = eval_expr(*block->tail, env, ctx);
    env.pop();
    return tail;
}

[[nodiscard]] static std::optional<bool> constant_bool(
    const tyir::TyExprPtr& expr, const ConstEnv& env, const ProgramView& program,
    std::vector<EvalStackFrame> stack, const GenericParamSet& generic_params) {
    ConstEnv eval_env = env;
    EvalContext ctx{
        program,
        std::move(stack),
        kDefaultEvalStepBudget,
        kDefaultEvalCallDepth,
        {},
        std::make_shared<ConstraintEvalState>(),
    };
    ctx.generic_params = generic_params;
    const auto value = eval_expr(expr, eval_env, ctx);
    if (!value || value->kind != EvalState::Kind::Value)
        return std::nullopt;
    const Value* bool_value = deref_value(value->value);
    if (bool_value == nullptr || bool_value->kind != Value::Kind::Bool)
        return std::nullopt;
    return bool_value->bool_value;
}

[[nodiscard]] static std::unexpected<EvalError>
    materialization_error(SourceSpan span, std::string message) {
    return std::unexpected(EvalError{span, std::move(message), {}, std::nullopt});
}

[[nodiscard]] static bool can_fold_reference_expr(const tyir::Ty& ty, const ValuePtr& value) {
    if (!ty.is_ref() || ty.pointee == nullptr)
        return true;

    if (value == nullptr || value->kind != Value::Kind::Ref || value->referent == nullptr)
        return true;

    const Value* actual = deref_value(value);
    if (actual == nullptr)
        return true;

    return actual->kind == Value::Kind::String
        && actual->string_ownership == Value::StringOwnership::BorrowedLiteral;
}

std::expected<tyir::TyExprPtr, EvalError> value_to_expr(
    const ProgramView& program, const ValuePtr& value, const tyir::Ty& ty, SourceSpan span) {
    if (value == nullptr)
        return materialization_error(
            span, "missing compile-time value while materializing type '" + ty.display() + "'");

    if (ty.is_ref()) {
        if (value->kind != Value::Kind::Ref || value->referent == nullptr || ty.pointee == nullptr)
            return materialization_error(
                span, "mismatched compile-time reference shape while materializing type '"
                          + ty.display() + "'");
        const Value* referent = value->referent.get();
        if (referent->kind == Value::Kind::String
            && referent->string_ownership == Value::StringOwnership::BorrowedLiteral) {
            return tyir::make_ty_expr(
                span,
                tyir::TyLiteral{
                    tyir::TyLiteral::Kind::Str,
                    Symbol::intern(quote_string_contents(referent->string_value)),
                    false,
                },
                ty);
        }

        auto rhs = value_to_expr(program, value->referent, *ty.pointee, span);
        if (!rhs)
            return std::unexpected(std::move(rhs.error()));
        return tyir::make_ty_expr(span, tyir::TyBorrow{*rhs}, ty);
    }

    const Value* actual = deref_value(value);
    if (actual == nullptr)
        return materialization_error(
            span, "missing compile-time value while materializing type '" + ty.display() + "'");

    switch (actual->kind) {
    case Value::Kind::Num:
        return tyir::make_ty_expr(
            span,
            tyir::TyLiteral{
                tyir::TyLiteral::Kind::Num, Symbol::intern(format_num(actual->num_value)), false},
            ty);
    case Value::Kind::Bool:
        return tyir::make_ty_expr(
            span,
            tyir::TyLiteral{
                tyir::TyLiteral::Kind::Bool, cstc::symbol::kInvalidSymbol, actual->bool_value},
            ty);
    case Value::Kind::Unit:
        return tyir::make_ty_expr(
            span, tyir::TyLiteral{tyir::TyLiteral::Kind::Unit, cstc::symbol::kInvalidSymbol, false},
            ty);
    case Value::Kind::String: {
        const tyir::TyLiteral::Kind kind = actual->string_ownership == Value::StringOwnership::Owned
                                             ? tyir::TyLiteral::Kind::OwnedStr
                                             : tyir::TyLiteral::Kind::Str;
        return tyir::make_ty_expr(
            span,
            tyir::TyLiteral{
                kind, Symbol::intern(quote_string_contents(actual->string_value)), false},
            ty);
    }
    case Value::Kind::Enum:
        return tyir::make_ty_expr(
            span, tyir::EnumVariantRef{actual->type_name, actual->variant_name}, ty);
    case Value::Kind::Struct: {
        const auto decl_it = program.structs.find(actual->type_name);
        if (decl_it == program.structs.end())
            return std::unexpected(
                EvalError{
                    span,
                    "missing struct declaration while materializing compile-time constant '"
                        + std::string(actual->type_name.as_str()) + "'",
                    {},
                    std::nullopt,
                });

        TypeSubstitution substitution;
        const auto& generic_params = decl_it->second->generic_params;
        if (!generic_params.empty()) {
            assert(generic_params.size() == ty.generic_args.size());
            substitution.reserve(generic_params.size());
            for (std::size_t index = 0; index < generic_params.size(); ++index)
                substitution.emplace(generic_params[index].name, ty.generic_args[index]);
        }

        std::vector<tyir::TyStructInitField> fields;
        for (const tyir::TyFieldDecl& field_decl : decl_it->second->fields) {
            ValuePtr field_value = find_struct_field_ptr(*actual, field_decl.name);
            if (field_value == nullptr) {
                return std::unexpected(
                    EvalError{
                        span,
                        "missing struct field '" + std::string(field_decl.name.as_str())
                            + "' while materializing compile-time constant",
                        {},
                        std::nullopt,
                    });
            }
            tyir::Ty field_ty = field_decl.ty;
            if (!substitution.empty())
                field_ty = apply_substitution(field_ty, substitution);
            auto expr_value = value_to_expr(program, field_value, field_ty, span);
            if (!expr_value)
                return std::unexpected(std::move(expr_value.error()));
            fields.push_back({field_decl.name, *expr_value, span});
        }
        return tyir::make_ty_expr(
            span, tyir::TyStructInit{actual->type_name, ty.generic_args, std::move(fields)}, ty);
    }
    case Value::Kind::Ref:
        return std::unexpected(
            EvalError{
                span,
                "unexpected reference value during materialization",
                {},
                std::nullopt,
            });
    }
    return std::unexpected(
        EvalError{
            span,
            "unsupported compile-time value materialization",
            {},
            std::nullopt,
        });
}

[[nodiscard]] static std::expected<tyir::TyExprPtr, EvalError> fold_expr(
    const tyir::TyExprPtr& expr, ConstEnv& env, const ProgramView& program,
    const GenericParamSet& generic_params);

[[nodiscard]] static bool expr_can_fallthrough(const tyir::TyExpr& expr);

[[nodiscard]] static bool stmt_can_fallthrough(const tyir::TyStmt& stmt) {
    return std::visit(
        [](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, tyir::TyLetStmt>)
                return expr_can_fallthrough(*node.init);
            else
                return expr_can_fallthrough(*node.expr);
        },
        stmt);
}

[[nodiscard]] static bool block_can_fallthrough(const tyir::TyBlock& block);

[[nodiscard]] static bool expr_can_fallthrough(const tyir::TyExpr& expr) {
    if (expr.ty.is_never())
        return false;

    return std::visit(
        [](const auto& node) {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (
                std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                || std::is_same_v<Node, tyir::EnumVariantRef>) { // NOLINT(bugprone-branch-clone)
                return true;
            } else if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                for (const tyir::TyStructInitField& field : node.fields) {
                    if (!expr_can_fallthrough(*field.value))
                        return false;
                }
                return true;
            } else if constexpr (
                std::is_same_v<Node, tyir::TyBorrow> || std::is_same_v<Node, tyir::TyUnary>) {
                return expr_can_fallthrough(*node.rhs);
            } else if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                return expr_can_fallthrough(*node.lhs) && expr_can_fallthrough(*node.rhs);
            } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                return expr_can_fallthrough(*node.base);
            } else if constexpr (
                std::is_same_v<Node, tyir::TyCall>
                || std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                for (const tyir::TyExprPtr& arg : node.args) {
                    if (!expr_can_fallthrough(*arg))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<Node, tyir::TyDeclProbe>) {
                return true;
            } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                return block_can_fallthrough(*node);
            } else if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                if (!expr_can_fallthrough(*node.condition))
                    return false;
                if (!node.else_branch.has_value())
                    return true;
                return block_can_fallthrough(*node.then_block)
                    || expr_can_fallthrough(**node.else_branch);
            } else if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                return true;
            } else if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                return expr_can_fallthrough(*node.condition);
            } else if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                if (node.init.has_value() && !expr_can_fallthrough(*node.init->init))
                    return false;
                if (node.condition.has_value() && !expr_can_fallthrough(**node.condition))
                    return false;
                return true;
            } else if constexpr (
                std::is_same_v<Node, tyir::TyBreak> || std::is_same_v<Node, tyir::TyContinue>
                || std::is_same_v<Node, tyir::TyReturn>) {
                return false;
            } else {
                return true;
            }
        },
        expr.node);
}

[[nodiscard]] static bool block_can_fallthrough(const tyir::TyBlock& block) {
    for (const tyir::TyStmt& stmt : block.stmts) {
        if (!stmt_can_fallthrough(stmt))
            return false;
    }
    if (!block.tail.has_value())
        return true;
    return expr_can_fallthrough(**block.tail);
}

[[nodiscard]] static std::expected<tyir::TyBlock, EvalError> fold_block(
    const tyir::TyBlock& block, ConstEnv& env, const ProgramView& program,
    const GenericParamSet& generic_params) {
    env.push();
    tyir::TyBlock folded;
    folded.ty = block.ty;
    folded.span = block.span;

    for (std::size_t index = 0; index < block.stmts.size(); ++index) {
        const tyir::TyStmt& stmt = block.stmts[index];
        auto folded_stmt = std::visit(
            [&](const auto& node) -> std::expected<tyir::TyStmt, EvalError> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyLetStmt>) {
                    auto init = fold_expr(node.init, env, program, generic_params);
                    if (!init)
                        return std::unexpected(std::move(init.error()));

                    ConstEnv eval_env = env;
                    EvalContext ctx{
                        program,
                        {},
                        kDefaultEvalStepBudget,
                        kDefaultEvalCallDepth,
                        {},
                        std::make_shared<ConstraintEvalState>(),
                    };
                    ctx.generic_params = generic_params;
                    const auto value = eval_expr(*init, eval_env, ctx);
                    if (value && value->kind == EvalState::Kind::Value) {
                        if (!node.discard)
                            env.bind_known(node.name, value->value);
                    } else if (!node.discard) {
                        env.bind_unknown(node.name);
                    }

                    return tyir::TyLetStmt{node.discard, node.name, node.ty, *init, node.span};
                } else {
                    auto value = fold_expr(node.expr, env, program, generic_params);
                    if (!value)
                        return std::unexpected(std::move(value.error()));
                    return tyir::TyExprStmt{*value, node.span};
                }
            },
            stmt);
        if (!folded_stmt) {
            env.pop();
            return std::unexpected(std::move(folded_stmt.error()));
        }
        const bool reaches_next_stmt = stmt_can_fallthrough(*folded_stmt);
        folded.stmts.push_back(std::move(*folded_stmt));
        if (!reaches_next_stmt) {
            // Preserve the unreachable remainder without folding it.
            folded.stmts.insert(
                folded.stmts.end(), block.stmts.begin() + static_cast<std::ptrdiff_t>(index + 1),
                block.stmts.end());
            folded.tail = block.tail;
            env.pop();
            return folded;
        }
    }

    if (block.tail.has_value()) {
        auto tail = fold_expr(*block.tail, env, program, generic_params);
        if (!tail) {
            env.pop();
            return std::unexpected(std::move(tail.error()));
        }
        folded.tail = *tail;
    }

    env.pop();
    return folded;
}

[[nodiscard]] static std::expected<tyir::TyExprPtr, EvalError> maybe_fold_constant(
    const tyir::TyExprPtr& expr, const ProgramView& program, const ConstEnv& env,
    const GenericParamSet& generic_params) {
    ConstEnv eval_env = env;
    EvalContext ctx{
        program,
        {},
        kDefaultEvalStepBudget,
        kDefaultEvalCallDepth,
        {},
        std::make_shared<ConstraintEvalState>(),
    };
    ctx.generic_params = generic_params;
    auto value = eval_expr(expr, eval_env, ctx);
    if (!value)
        return std::unexpected(std::move(value.error()));
    if (value->kind != EvalState::Kind::Value)
        return expr;
    if (!can_fold_reference_expr(expr->ty, value->value))
        return expr;
    auto folded = value_to_expr(program, value->value, expr->ty, expr->span);
    if (!folded)
        return std::unexpected(std::move(folded.error()));
    return *folded;
}

[[nodiscard]] static std::expected<tyir::TyExprPtr, EvalError> fold_expr(
    const tyir::TyExprPtr& expr, ConstEnv& env, const ProgramView& program,
    const GenericParamSet& generic_params) {
    return std::visit(
        [&](const auto& node) -> std::expected<tyir::TyExprPtr, EvalError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (
                std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                || std::is_same_v<Node, tyir::EnumVariantRef>) {
                return maybe_fold_constant(expr, program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                tyir::TyStructInit init;
                init.type_name = node.type_name;
                init.generic_args = node.generic_args;
                for (const tyir::TyStructInitField& field : node.fields) {
                    auto value = fold_expr(field.value, env, program, generic_params);
                    if (!value)
                        return std::unexpected(std::move(value.error()));
                    init.fields.push_back({field.name, *value, field.span});
                }
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, std::move(init), expr->ty), program, env,
                    generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBorrow>) {
                auto rhs = fold_expr(node.rhs, env, program, generic_params);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyBorrow{*rhs}, expr->ty), program, env,
                    generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyUnary>) {
                auto rhs = fold_expr(node.rhs, env, program, generic_params);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyUnary{node.op, *rhs}, expr->ty), program,
                    env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                auto lhs = fold_expr(node.lhs, env, program, generic_params);
                if (!lhs)
                    return std::unexpected(std::move(lhs.error()));

                if (node.op == cstc::ast::BinaryOp::And || node.op == cstc::ast::BinaryOp::Or) {
                    const auto lhs_value = constant_bool(*lhs, env, program, {}, generic_params);
                    if (lhs_value.has_value()) {
                        const bool short_circuits =
                            (node.op == cstc::ast::BinaryOp::And && !*lhs_value)
                            || (node.op == cstc::ast::BinaryOp::Or && *lhs_value);
                        if (short_circuits) {
                            return maybe_fold_constant(
                                tyir::make_ty_expr(
                                    expr->span, tyir::TyBinary{node.op, *lhs, node.rhs}, expr->ty),
                                program, env, generic_params);
                        }
                    }
                }

                auto rhs = fold_expr(node.rhs, env, program, generic_params);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyBinary{node.op, *lhs, *rhs}, expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                auto base = fold_expr(node.base, env, program, generic_params);
                if (!base)
                    return std::unexpected(std::move(base.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span, tyir::TyFieldAccess{*base, node.field, node.use_kind},
                        expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                std::vector<tyir::TyExprPtr> args;
                args.reserve(node.args.size());
                for (const tyir::TyExprPtr& arg : node.args) {
                    auto folded_arg = fold_expr(arg, env, program, generic_params);
                    if (!folded_arg)
                        return std::unexpected(std::move(folded_arg.error()));
                    args.push_back(*folded_arg);
                }
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span, tyir::TyCall{node.fn_name, node.generic_args, std::move(args)},
                        expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyDeclProbe>) {
                tyir::TyDeclProbe rewritten = node;
                const auto status = validate_decl_probe(rewritten, program, generic_params);
                if (status.kind == ConstraintEvalKind::NotConstEvaluable)
                    return tyir::make_ty_expr(expr->span, std::move(rewritten), expr->ty);

                return tyir::make_ty_expr(
                    expr->span,
                    tyir::EnumVariantRef{
                        expr->ty.name, status.kind == ConstraintEvalKind::Satisfied
                                           ? Symbol::intern("Valid")
                                           : Symbol::intern("Invalid")},
                    expr->ty);
            }

            if constexpr (std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                std::vector<tyir::TyExprPtr> args;
                args.reserve(node.args.size());
                for (const tyir::TyExprPtr& arg : node.args) {
                    auto folded_arg = fold_expr(arg, env, program, generic_params);
                    if (!folded_arg)
                        return std::unexpected(std::move(folded_arg.error()));
                    args.push_back(*folded_arg);
                }
                return tyir::make_ty_expr(
                    expr->span,
                    tyir::TyDeferredGenericCall{node.fn_name, node.generic_args, std::move(args)},
                    expr->ty);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                auto block = fold_block(*node, env, program, generic_params);
                if (!block)
                    return std::unexpected(std::move(block.error()));
                auto block_ptr = std::make_shared<tyir::TyBlock>(std::move(*block));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, block_ptr, expr->ty), program, env,
                    generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                auto condition = fold_expr(node.condition, env, program, generic_params);
                if (!condition)
                    return std::unexpected(std::move(condition.error()));

                ConstEnv branch_env = env;
                const auto cond_value =
                    constant_bool(*condition, branch_env, program, {}, generic_params);
                if (cond_value.has_value()) {
                    if (*cond_value) {
                        auto then_block =
                            fold_block(*node.then_block, branch_env, program, generic_params);
                        if (!then_block)
                            return std::unexpected(std::move(then_block.error()));
                        auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block));
                        return maybe_fold_constant(
                            tyir::make_ty_expr(expr->span, then_ptr, expr->ty), program, env,
                            generic_params);
                    }
                    if (node.else_branch.has_value())
                        return fold_expr(*node.else_branch, branch_env, program, generic_params);
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyLiteral{
                            tyir::TyLiteral::Kind::Unit, cstc::symbol::kInvalidSymbol, false},
                        expr->ty);
                }

                auto then_block = fold_block(*node.then_block, branch_env, program, generic_params);
                if (!then_block)
                    return std::unexpected(std::move(then_block.error()));
                auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block));

                std::optional<tyir::TyExprPtr> else_branch;
                if (node.else_branch.has_value()) {
                    auto else_expr =
                        fold_expr(*node.else_branch, branch_env, program, generic_params);
                    if (!else_expr)
                        return std::unexpected(std::move(else_expr.error()));
                    else_branch = *else_expr;
                }

                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyIf{*condition, std::move(then_ptr), std::move(else_branch)},
                        expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                auto body = fold_block(*node.body, env, program, generic_params);
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyLoop{std::move(body_ptr)}, expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                auto condition = fold_expr(node.condition, env, program, generic_params);
                if (!condition)
                    return std::unexpected(std::move(condition.error()));
                ConstEnv loop_env = env;
                const auto cond_value =
                    constant_bool(*condition, loop_env, program, {}, generic_params);
                if (cond_value.has_value() && !*cond_value) {
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyLiteral{
                            tyir::TyLiteral::Kind::Unit, cstc::symbol::kInvalidSymbol, false},
                        expr->ty);
                }
                auto body = fold_block(*node.body, env, program, generic_params);
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span, tyir::TyWhile{*condition, std::move(body_ptr)}, expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                ConstEnv loop_env = env;
                loop_env.push();

                std::optional<tyir::TyForInit> init;
                if (node.init.has_value()) {
                    auto init_expr = fold_expr(node.init->init, loop_env, program, generic_params);
                    if (!init_expr)
                        return std::unexpected(std::move(init_expr.error()));
                    init = tyir::TyForInit{
                        node.init->discard, node.init->name, node.init->ty, *init_expr,
                        node.init->span};

                    ConstEnv eval_env = loop_env;
                    EvalContext ctx{
                        program,
                        {},
                        kDefaultEvalStepBudget,
                        kDefaultEvalCallDepth,
                        {},
                        std::make_shared<ConstraintEvalState>(),
                    };
                    ctx.generic_params = generic_params;
                    const auto init_value = eval_expr(*init_expr, eval_env, ctx);
                    if (init_value && init_value->kind == EvalState::Kind::Value) {
                        if (!node.init->discard)
                            loop_env.bind_known(node.init->name, init_value->value);
                    } else if (!node.init->discard) {
                        loop_env.bind_unknown(node.init->name);
                    }
                }

                std::optional<tyir::TyExprPtr> condition;
                if (node.condition.has_value()) {
                    auto cond = fold_expr(*node.condition, loop_env, program, generic_params);
                    if (!cond)
                        return std::unexpected(std::move(cond.error()));
                    condition = *cond;
                }

                if (condition.has_value()) {
                    ConstEnv cond_env = loop_env;
                    const auto cond_value =
                        constant_bool(*condition, cond_env, program, {}, generic_params);
                    if (cond_value.has_value() && !*cond_value) {
                        loop_env.pop();
                        return maybe_fold_constant(
                            tyir::make_ty_expr(
                                expr->span,
                                tyir::TyFor{
                                    std::move(init), std::move(condition), node.step, node.body},
                                expr->ty),
                            program, env, generic_params);
                    }
                }

                std::optional<tyir::TyExprPtr> step;
                if (node.step.has_value()) {
                    auto folded_step = fold_expr(*node.step, loop_env, program, generic_params);
                    if (!folded_step)
                        return std::unexpected(std::move(folded_step.error()));
                    step = *folded_step;
                }

                auto body = fold_block(*node.body, loop_env, program, generic_params);
                loop_env.pop();
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyFor{
                            std::move(init), std::move(condition), std::move(step),
                            std::move(body_ptr)},
                        expr->ty),
                    program, env, generic_params);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBreak>) {
                std::optional<tyir::TyExprPtr> value;
                if (node.value.has_value()) {
                    auto folded = fold_expr(*node.value, env, program, generic_params);
                    if (!folded)
                        return std::unexpected(std::move(folded.error()));
                    value = *folded;
                }
                return tyir::make_ty_expr(expr->span, tyir::TyBreak{std::move(value)}, expr->ty);
            }

            if constexpr (std::is_same_v<Node, tyir::TyContinue>) {
                return expr;
            }

            if constexpr (std::is_same_v<Node, tyir::TyReturn>) {
                std::optional<tyir::TyExprPtr> value;
                if (node.value.has_value()) {
                    auto folded = fold_expr(*node.value, env, program, generic_params);
                    if (!folded)
                        return std::unexpected(std::move(folded.error()));
                    value = *folded;
                }
                return tyir::make_ty_expr(expr->span, tyir::TyReturn{std::move(value)}, expr->ty);
            }

            return expr;
        },
        expr->node);
}

[[nodiscard]] static std::expected<void, EvalError> validate_constraints_in_expr(
    const tyir::TyExprPtr& expr, const ProgramView& program, const GenericParamSet& generic_params);

[[nodiscard]] static std::expected<void, EvalError> validate_constraints_in_block(
    const tyir::TyBlockPtr& block, const ProgramView& program,
    const GenericParamSet& generic_params) {
    if (block == nullptr)
        return {};
    for (const tyir::TyStmt& stmt : block->stmts) {
        auto result = std::visit(
            [&](const auto& node) -> std::expected<void, EvalError> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyLetStmt>)
                    return validate_constraints_in_expr(node.init, program, generic_params);
                else
                    return validate_constraints_in_expr(node.expr, program, generic_params);
            },
            stmt);
        if (!result)
            return result;
    }
    if (block->tail.has_value())
        return validate_constraints_in_expr(*block->tail, program, generic_params);
    return {};
}

[[nodiscard]] static std::expected<void, EvalError> validate_constraints_in_expr(
    const tyir::TyExprPtr& expr, const ProgramView& program,
    const GenericParamSet& generic_params) {
    return std::visit(
        [&](const auto& node) -> std::expected<void, EvalError> {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (
                std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                || std::is_same_v<Node, tyir::EnumVariantRef>
                || std::is_same_v<Node, tyir::TyDeclProbe>) {
                return {};
            } else if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                const auto decl_it = program.structs.find(node.type_name);
                if (decl_it != program.structs.end()
                    && !generic_args_depend_on_generic_params(node.generic_args, generic_params)) {
                    auto status = enforce_constraints(
                        program, decl_it->second->lowered_where_clause,
                        build_substitution(decl_it->second->generic_params, node.generic_args),
                        "type", decl_it->second->name, node.generic_args, decl_it->second->span,
                        expr->span);
                    if (!status)
                        return status;
                }
                for (const tyir::TyStructInitField& field : node.fields) {
                    auto nested =
                        validate_constraints_in_expr(field.value, program, generic_params);
                    if (!nested)
                        return nested;
                }
                return {};
            } else if constexpr (
                std::is_same_v<Node, tyir::TyBorrow> || std::is_same_v<Node, tyir::TyUnary>) {
                return validate_constraints_in_expr(node.rhs, program, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                auto lhs = validate_constraints_in_expr(node.lhs, program, generic_params);
                if (!lhs)
                    return lhs;
                return validate_constraints_in_expr(node.rhs, program, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                return validate_constraints_in_expr(node.base, program, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                const auto fn_it = program.fns.find(node.fn_name);
                if (fn_it != program.fns.end()
                    && !generic_args_depend_on_generic_params(node.generic_args, generic_params)) {
                    auto status = enforce_constraints(
                        program, fn_it->second->lowered_where_clause,
                        build_substitution(fn_it->second->generic_params, node.generic_args),
                        "function", fn_it->second->name, node.generic_args, fn_it->second->span,
                        expr->span);
                    if (!status)
                        return status;
                }
                for (const tyir::TyExprPtr& arg : node.args) {
                    auto nested = validate_constraints_in_expr(arg, program, generic_params);
                    if (!nested)
                        return nested;
                }
                return {};
            } else if constexpr (std::is_same_v<Node, tyir::TyDeferredGenericCall>) {
                for (const tyir::TyExprPtr& arg : node.args) {
                    auto nested = validate_constraints_in_expr(arg, program, generic_params);
                    if (!nested)
                        return nested;
                }
                return {};
            } else if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                return validate_constraints_in_block(node, program, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                auto cond = validate_constraints_in_expr(node.condition, program, generic_params);
                if (!cond)
                    return cond;
                auto then_block =
                    validate_constraints_in_block(node.then_block, program, generic_params);
                if (!then_block)
                    return then_block;
                if (node.else_branch.has_value())
                    return validate_constraints_in_expr(*node.else_branch, program, generic_params);
                return {};
            } else if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                return validate_constraints_in_block(node.body, program, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                auto cond = validate_constraints_in_expr(node.condition, program, generic_params);
                if (!cond)
                    return cond;
                return validate_constraints_in_block(node.body, program, generic_params);
            } else if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                if (node.init.has_value()) {
                    auto init =
                        validate_constraints_in_expr(node.init->init, program, generic_params);
                    if (!init)
                        return init;
                }
                if (node.condition.has_value()) {
                    auto cond =
                        validate_constraints_in_expr(*node.condition, program, generic_params);
                    if (!cond)
                        return cond;
                }
                if (node.step.has_value()) {
                    auto step = validate_constraints_in_expr(*node.step, program, generic_params);
                    if (!step)
                        return step;
                }
                return validate_constraints_in_block(node.body, program, generic_params);
            } else if constexpr (
                std::is_same_v<Node, tyir::TyBreak> || std::is_same_v<Node, tyir::TyReturn>) {
                if (node.value.has_value())
                    return validate_constraints_in_expr(*node.value, program, generic_params);
                return {};
            } else {
                assert((std::is_same_v<Node, tyir::TyContinue>));
                return {};
            }
        },
        expr->node);
}

[[nodiscard]] static std::expected<void, EvalError>
    validate_program_constraints(const tyir::TyProgram& program, const ProgramView& view) {
    for (const tyir::TyItem& item : program.items) {
        auto result = std::visit(
            [&](const auto& node) -> std::expected<void, EvalError> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (
                    std::is_same_v<Node, tyir::TyStructDecl>
                    || std::is_same_v<Node, tyir::TyEnumDecl>) {
                    const GenericParamSet generic_params =
                        make_generic_param_set(node.generic_params);
                    for (const tyir::TyGenericConstraint& constraint : node.lowered_where_clause) {
                        if (expr_depends_on_generic_params(constraint.expr, generic_params))
                            continue;
                        auto status = evaluate_constraint(
                            constraint.expr, {}, view, {}, std::make_shared<ConstraintEvalState>());
                        if (status.kind == ConstraintEvalKind::Satisfied)
                            continue;
                        if (status.kind == ConstraintEvalKind::Unsatisfied) {
                            return make_error(
                                EvalContext{
                                    view,
                                    {},
                                    kDefaultEvalStepBudget,
                                    kDefaultEvalCallDepth,
                                    {},
                                    std::make_shared<ConstraintEvalState>(),
                                },
                                node.span,
                                "generic constraint failed for "
                                    + describe_constraint_owner(
                                        std::is_same_v<Node, tyir::TyStructDecl> ? "type" : "enum",
                                        node.name, constraint.span));
                        }
                        if (status.kind == ConstraintEvalKind::RuntimeOnly) {
                            return make_error(
                                EvalContext{
                                    view,
                                    {},
                                    kDefaultEvalStepBudget,
                                    kDefaultEvalCallDepth,
                                    {},
                                    std::make_shared<ConstraintEvalState>(),
                                },
                                node.span,
                                "generic constraint for "
                                    + describe_constraint_owner(
                                        std::is_same_v<Node, tyir::TyStructDecl> ? "type" : "enum",
                                        node.name, constraint.span)
                                    + " used runtime-only behavior");
                        }
                        const std::string message =
                            "generic constraint for "
                            + describe_constraint_owner(
                                std::is_same_v<Node, tyir::TyStructDecl> ? "type" : "enum",
                                node.name, constraint.span)
                            + (status.kind == ConstraintEvalKind::InvalidType
                                   ? " is invalid: "
                                   : " could not be const-evaluated: ")
                            + status.detail;
                        if (status.instantiation_limit.has_value()) {
                            return std::unexpected(
                                EvalError{
                                    node.span,
                                    std::move(message),
                                    {},
                                    status.instantiation_limit,
                                });
                        }
                        return make_error(
                            EvalContext{
                                view,
                                {},
                                kDefaultEvalStepBudget,
                                kDefaultEvalCallDepth,
                                {},
                                std::make_shared<ConstraintEvalState>(),
                            },
                            node.span, message);
                    }
                    return {};
                } else if constexpr (std::is_same_v<Node, tyir::TyFnDecl>) {
                    const GenericParamSet generic_params =
                        make_generic_param_set(node.generic_params);
                    for (const tyir::TyGenericConstraint& constraint : node.lowered_where_clause) {
                        if (expr_depends_on_generic_params(constraint.expr, generic_params))
                            continue;
                        auto status = evaluate_constraint(
                            constraint.expr, {}, view, {}, std::make_shared<ConstraintEvalState>());
                        if (status.kind == ConstraintEvalKind::Satisfied)
                            continue;
                        if (status.kind == ConstraintEvalKind::Unsatisfied) {
                            return make_error(
                                EvalContext{
                                    view,
                                    {},
                                    kDefaultEvalStepBudget,
                                    kDefaultEvalCallDepth,
                                    {},
                                    std::make_shared<ConstraintEvalState>(),
                                },
                                node.span,
                                "generic constraint failed for "
                                    + describe_constraint_owner(
                                        "function", node.name, constraint.span));
                        }
                        if (status.kind == ConstraintEvalKind::RuntimeOnly) {
                            return make_error(
                                EvalContext{
                                    view,
                                    {},
                                    kDefaultEvalStepBudget,
                                    kDefaultEvalCallDepth,
                                    {},
                                    std::make_shared<ConstraintEvalState>(),
                                },
                                node.span,
                                "generic constraint for "
                                    + describe_constraint_owner(
                                        "function", node.name, constraint.span)
                                    + " used runtime-only behavior");
                        }
                        const std::string message =
                            "generic constraint for "
                            + describe_constraint_owner("function", node.name, constraint.span)
                            + (status.kind == ConstraintEvalKind::InvalidType
                                   ? " is invalid: "
                                   : " could not be const-evaluated: ")
                            + status.detail;
                        if (status.instantiation_limit.has_value()) {
                            return std::unexpected(
                                EvalError{
                                    node.span,
                                    std::move(message),
                                    {},
                                    status.instantiation_limit,
                                });
                        }
                        return make_error(
                            EvalContext{
                                view,
                                {},
                                kDefaultEvalStepBudget,
                                kDefaultEvalCallDepth,
                                {},
                                std::make_shared<ConstraintEvalState>(),
                            },
                            node.span, message);
                    }
                    return validate_constraints_in_block(node.body, view, generic_params);
                } else {
                    return {};
                }
            },
            item);
        if (!result)
            return result;
    }
    return {};
}

[[nodiscard]] static ProgramView build_program_view(const tyir::TyProgram& program) {
    ProgramView view;
    for (const tyir::TyItem& item : program.items) {
        std::visit(
            [&](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyStructDecl>) {
                    view.structs.emplace(node.name, &node);
                } else if constexpr (std::is_same_v<Node, tyir::TyEnumDecl>) {
                    view.enums.emplace(node.name, &node);
                    if (node.lang_name == Symbol::intern("cstc_constraint"))
                        view.constraint_enum_name = node.name;
                } else if constexpr (std::is_same_v<Node, tyir::TyFnDecl>) {
                    view.fns.emplace(node.name, &node);
                } else if constexpr (std::is_same_v<Node, tyir::TyExternFnDecl>) {
                    view.extern_fns.emplace(node.name, &node);
                }
            },
            item);
    }
    return view;
}

} // namespace cstc::tyir_interp::detail

namespace cstc::tyir_interp {

std::expected<tyir::TyProgram, EvalError> fold_program(const tyir::TyProgram& program) {
    const detail::ProgramView view = detail::build_program_view(program);
    if (auto constraints = detail::validate_program_constraints(program, view); !constraints)
        return std::unexpected(std::move(constraints.error()));

    tyir::TyProgram folded = program;
    for (tyir::TyItem& item : folded.items) {
        if (auto* fn = std::get_if<tyir::TyFnDecl>(&item)) {
            detail::ConstEnv env;
            env.push();
            for (const tyir::TyParam& param : fn->params)
                env.bind_unknown(param.name);

            const detail::GenericParamSet generic_params =
                detail::make_generic_param_set(fn->generic_params);
            auto body = detail::fold_block(*fn->body, env, view, generic_params);
            env.pop();
            if (!body)
                return std::unexpected(std::move(body.error()));
            fn->body = std::make_shared<tyir::TyBlock>(std::move(*body));
        }
    }

    return folded;
}

} // namespace cstc::tyir_interp
