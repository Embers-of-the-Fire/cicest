#ifndef CICEST_COMPILER_CSTC_TYIR_INTERP_INTERP_HPP
#define CICEST_COMPILER_CSTC_TYIR_INTERP_INTERP_HPP

/// @file interp.hpp
/// @brief TyIR interpreter and const-folding pass.

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir_interp {

struct EvalStackFrame {
    cstc::symbol::Symbol fn_name = cstc::symbol::kInvalidSymbol;
    cstc::span::SourceSpan span;
};

struct EvalError {
    cstc::span::SourceSpan span;
    std::string message;
    std::vector<EvalStackFrame> stack;
};

[[nodiscard]] inline std::expected<tyir::TyProgram, EvalError>
    fold_program(const tyir::TyProgram& program);

} // namespace cstc::tyir_interp

#include <cassert>

namespace cstc::tyir_interp {

namespace detail {

using Symbol = cstc::symbol::Symbol;
using SymbolHash = cstc::symbol::SymbolHash;
using SourceSpan = cstc::span::SourceSpan;

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct ValueField {
    Symbol name = cstc::symbol::kInvalidSymbol;
    ValuePtr value;
};

struct Value {
    enum class Kind {
        Num,
        Bool,
        Unit,
        String,
        Struct,
        Enum,
        Ref,
    };

    enum class StringOwnership {
        BorrowedLiteral,
        Owned,
    };

    Kind kind = Kind::Unit;
    double num_value = 0.0;
    bool bool_value = false;
    std::string string_value;
    StringOwnership string_ownership = StringOwnership::BorrowedLiteral;
    Symbol type_name = cstc::symbol::kInvalidSymbol;
    Symbol variant_name = cstc::symbol::kInvalidSymbol;
    std::vector<ValueField> fields;
    ValuePtr referent;
};

[[nodiscard]] inline ValuePtr make_num(double value) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Num;
    out->num_value = value;
    return out;
}

[[nodiscard]] inline ValuePtr make_bool(bool value) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Bool;
    out->bool_value = value;
    return out;
}

[[nodiscard]] inline ValuePtr make_unit() { return std::make_shared<Value>(); }

[[nodiscard]] inline ValuePtr make_string(std::string value, Value::StringOwnership ownership) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::String;
    out->string_value = std::move(value);
    out->string_ownership = ownership;
    return out;
}

[[nodiscard]] inline ValuePtr make_ref(ValuePtr referent) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Ref;
    out->referent = std::move(referent);
    return out;
}

[[nodiscard]] inline ValuePtr make_enum(Symbol type_name, Symbol variant_name) {
    auto out = std::make_shared<Value>();
    out->kind = Value::Kind::Enum;
    out->type_name = type_name;
    out->variant_name = variant_name;
    return out;
}

[[nodiscard]] inline ValuePtr make_struct(Symbol type_name, std::vector<ValueField> fields) {
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

struct ProgramView {
    std::unordered_map<Symbol, const tyir::TyStructDecl*, SymbolHash> structs;
    std::unordered_map<Symbol, const tyir::TyEnumDecl*, SymbolHash> enums;
    std::unordered_map<Symbol, const tyir::TyFnDecl*, SymbolHash> fns;
    std::unordered_map<Symbol, const tyir::TyExternFnDecl*, SymbolHash> extern_fns;
};

struct EvalState {
    enum class Kind {
        Value,
        Return,
        Break,
        Continue,
        Blocked,
    };

    Kind kind = Kind::Blocked;
    ValuePtr value;

    [[nodiscard]] static EvalState from_value(ValuePtr value) {
        return EvalState{Kind::Value, std::move(value)};
    }

    [[nodiscard]] static EvalState from_return(ValuePtr value) {
        return EvalState{Kind::Return, std::move(value)};
    }

    [[nodiscard]] static EvalState from_break(ValuePtr value) {
        return EvalState{Kind::Break, std::move(value)};
    }

    [[nodiscard]] static EvalState continue_() { return EvalState{Kind::Continue, nullptr}; }

    [[nodiscard]] static EvalState blocked() { return EvalState{}; }
};

struct EvalContext {
    const ProgramView& program;
    std::vector<EvalStackFrame> stack;
};

[[nodiscard]] inline std::unexpected<EvalError>
    make_error(const EvalContext& ctx, SourceSpan span, std::string message) {
    return std::unexpected(EvalError{span, std::move(message), ctx.stack});
}

[[nodiscard]] inline std::string strip_quotes(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return std::string(value.substr(1, value.size() - 2));
    return std::string(value);
}

[[nodiscard]] inline std::string quote_string_contents(std::string_view value) {
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

[[nodiscard]] inline std::string format_num(double value) {
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

[[nodiscard]] inline double parse_num(Symbol symbol) {
    const std::string text(symbol.as_str());
    return std::strtod(text.c_str(), nullptr);
}

[[nodiscard]] inline const Value* deref_value(const ValuePtr& value) {
    const Value* current = value.get();
    while (current != nullptr && current->kind == Value::Kind::Ref && current->referent != nullptr)
        current = current->referent.get();
    return current;
}

[[nodiscard]] inline const Value* find_struct_field(const Value& base, Symbol field_name) {
    for (const ValueField& field : base.fields) {
        if (field.name == field_name)
            return field.value.get();
    }
    return nullptr;
}

[[nodiscard]] inline ValuePtr find_struct_field_ptr(const Value& base, Symbol field_name) {
    for (const ValueField& field : base.fields) {
        if (field.name == field_name)
            return field.value;
    }
    return nullptr;
}

[[nodiscard]] inline bool values_equal(const ValuePtr& lhs, const ValuePtr& rhs) {
    const Value* left = deref_value(lhs);
    const Value* right = deref_value(rhs);
    if (left == nullptr || right == nullptr)
        return left == right;
    if (left->kind != right->kind)
        return false;

    switch (left->kind) {
    case Value::Kind::Num: return left->num_value == right->num_value;
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
        if (left->referent == nullptr || right->referent == nullptr)
            return left->referent == right->referent;
        return values_equal(left->referent, right->referent);
    }
    return false;
}

[[nodiscard]] inline std::optional<bool> constant_bool(
    const tyir::TyExprPtr& expr, const ConstEnv& env, const ProgramView& program,
    std::vector<EvalStackFrame> stack = {});

[[nodiscard]] inline std::expected<EvalState, EvalError>
    eval_expr(const tyir::TyExprPtr& expr, ConstEnv& env, EvalContext& ctx);

[[nodiscard]] inline std::expected<EvalState, EvalError>
    eval_block(const tyir::TyBlockPtr& block, ConstEnv& env, EvalContext& ctx);

[[nodiscard]] inline std::unexpected<EvalError>
    unsupported_value_error(const EvalContext& ctx, SourceSpan span, std::string_view what) {
    return make_error(ctx, span, "unsupported compile-time value: " + std::string(what));
}

[[nodiscard]] inline std::expected<ValuePtr, EvalError> eval_lang_intrinsic(
    const tyir::TyExternFnDecl& decl, const std::vector<ValuePtr>& args, EvalContext& ctx,
    SourceSpan span) {
    const Symbol resolved_name = decl.link_name.is_valid() ? decl.link_name : decl.name;
    const std::string_view name = resolved_name.as_str();
    const auto unwrap_string = [&](std::size_t index) -> std::expected<const Value*, EvalError> {
        const Value* value = deref_value(args[index]);
        if (value == nullptr || value->kind != Value::Kind::String)
            return unsupported_value_error(ctx, span, "string argument");
        return value;
    };

    if (name == "to_str" || name == "cstc_std_to_str") {
        const Value* arg = deref_value(args[0]);
        if (arg == nullptr || arg->kind != Value::Kind::Num)
            return unsupported_value_error(ctx, span, "numeric argument");
        return make_string(format_num(arg->num_value), Value::StringOwnership::Owned);
    }

    if (name == "str_concat" || name == "cstc_std_str_concat") {
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
        auto value = unwrap_string(0);
        if (!value)
            return std::unexpected(std::move(value.error()));
        return make_num(static_cast<double>((*value)->string_value.size()));
    }

    if (name == "str_free" || name == "cstc_std_str_free") {
        const Value* value = deref_value(args[0]);
        if (value == nullptr || value->kind != Value::Kind::String
            || value->string_ownership != Value::StringOwnership::Owned) {
            return make_error(ctx, span, "compile-time 'str_free' requires an owned string");
        }
        return make_unit();
    }

    if (name == "assert" || name == "cstc_std_assert") {
        const Value* value = deref_value(args[0]);
        if (value == nullptr || value->kind != Value::Kind::Bool)
            return unsupported_value_error(ctx, span, "boolean assertion argument");
        if (!value->bool_value)
            return make_error(ctx, span, "compile-time assertion failed");
        return make_unit();
    }

    if (name == "assert_eq" || name == "cstc_std_assert_eq") {
        const Value* lhs = deref_value(args[0]);
        const Value* rhs = deref_value(args[1]);
        if (lhs == nullptr || rhs == nullptr || lhs->kind != Value::Kind::Num
            || rhs->kind != Value::Kind::Num) {
            return unsupported_value_error(ctx, span, "numeric equality assertion arguments");
        }
        constexpr double epsilon = 1e-9;
        if (std::fabs(lhs->num_value - rhs->num_value) > epsilon) {
            return make_error(
                ctx, span,
                "compile-time assert_eq failed: left=" + format_num(lhs->num_value)
                    + ", right=" + format_num(rhs->num_value));
        }
        return make_unit();
    }

    if (name == "print" || name == "println" || name == "cstc_std_print"
        || name == "cstc_std_println") {
        return std::unexpected(
            EvalError{
                span,
                "impure lang intrinsic '" + std::string(name)
                    + "' is not const-evaluable; mark it `runtime`",
                ctx.stack,
            });
    }

    return make_error(
        ctx, span, "unsupported non-runtime lang intrinsic '" + std::string(name) + "'");
}

[[nodiscard]] inline std::expected<EvalState, EvalError>
    eval_expr(const tyir::TyExprPtr& expr, ConstEnv& env, EvalContext& ctx) {
    if (expr->ty.is_runtime)
        return EvalState::blocked();

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

            if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
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
                    if (left == nullptr || right == nullptr || left->kind != Value::Kind::Num
                        || right->kind != Value::Kind::Num) {
                        return unsupported_value_error(ctx, expr->span, "numeric binary operands");
                    }
                    if ((node.op == Op::Div || node.op == Op::Mod) && right->num_value == 0.0)
                        return make_error(ctx, expr->span, "division by zero during const-eval");
                    switch (node.op) {
                    case Op::Add:
                        return EvalState::from_value(make_num(left->num_value + right->num_value));
                    case Op::Sub:
                        return EvalState::from_value(make_num(left->num_value - right->num_value));
                    case Op::Mul:
                        return EvalState::from_value(make_num(left->num_value * right->num_value));
                    case Op::Div:
                        return EvalState::from_value(make_num(left->num_value / right->num_value));
                    case Op::Mod:
                        return EvalState::from_value(
                            make_num(std::fmod(left->num_value, right->num_value)));
                    default: break;
                    }
                    break;
                case Op::Lt:
                    return EvalState::from_value(make_bool(left->num_value < right->num_value));
                case Op::Le:
                    return EvalState::from_value(make_bool(left->num_value <= right->num_value));
                case Op::Gt:
                    return EvalState::from_value(make_bool(left->num_value > right->num_value));
                case Op::Ge:
                    return EvalState::from_value(make_bool(left->num_value >= right->num_value));
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
                        return EvalState::blocked();

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

                    ctx.stack.push_back(EvalStackFrame{fn.name, expr->span});
                    auto result = eval_block(fn.body, fn_env, ctx);
                    ctx.stack.pop_back();
                    if (!result)
                        return std::unexpected(std::move(result.error()));

                    switch (result->kind) {
                    case EvalState::Kind::Value: return result;
                    case EvalState::Kind::Return:
                        return EvalState::from_value(
                            result->value != nullptr ? result->value : make_unit());
                    case EvalState::Kind::Blocked: return EvalState::blocked();
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
                    return EvalState::blocked();
                if (decl.abi.as_str() != std::string_view{"lang"})
                    return make_error(
                        ctx, expr->span,
                        "reached non-runtime extern call with unsupported ABI '"
                            + std::string(decl.abi.as_str()) + "'");

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
                    auto body = eval_block(node.body, env, ctx);
                    if (!body)
                        return std::unexpected(std::move(body.error()));
                    switch (body->kind) {
                    case EvalState::Kind::Value: break;
                    case EvalState::Kind::Continue: continue;
                    case EvalState::Kind::Break:
                        return EvalState::from_value(
                            body->value != nullptr ? body->value : make_unit());
                    case EvalState::Kind::Return: return *body;
                    case EvalState::Kind::Blocked: return EvalState::blocked();
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

                    auto body = eval_block(node.body, env, ctx);
                    if (!body)
                        return std::unexpected(std::move(body.error()));
                    switch (body->kind) {
                    case EvalState::Kind::Value:
                    case EvalState::Kind::Continue: continue;
                    case EvalState::Kind::Break:
                        return EvalState::from_value(
                            body->value != nullptr ? body->value : make_unit());
                    case EvalState::Kind::Return: return *body;
                    case EvalState::Kind::Blocked: return EvalState::blocked();
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

                    auto body = eval_block(node.body, env, ctx);
                    if (!body) {
                        env.pop();
                        return std::unexpected(std::move(body.error()));
                    }
                    if (body->kind == EvalState::Kind::Return
                        || body->kind == EvalState::Kind::Blocked) {
                        env.pop();
                        return *body;
                    }
                    if (body->kind == EvalState::Kind::Break)
                        break;

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
                    return EvalState::from_break(make_unit());
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

[[nodiscard]] inline std::expected<EvalState, EvalError>
    eval_block(const tyir::TyBlockPtr& block, ConstEnv& env, EvalContext& ctx) {
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

[[nodiscard]] inline std::optional<bool> constant_bool(
    const tyir::TyExprPtr& expr, const ConstEnv& env, const ProgramView& program,
    std::vector<EvalStackFrame> stack) {
    ConstEnv eval_env = env;
    EvalContext ctx{program, std::move(stack)};
    const auto value = eval_expr(expr, eval_env, ctx);
    if (!value || value->kind != EvalState::Kind::Value)
        return std::nullopt;
    const Value* bool_value = deref_value(value->value);
    if (bool_value == nullptr || bool_value->kind != Value::Kind::Bool)
        return std::nullopt;
    return bool_value->bool_value;
}

[[nodiscard]] inline std::expected<tyir::TyExprPtr, EvalError> value_to_expr(
    const ProgramView& program, const ValuePtr& value, const tyir::Ty& ty, SourceSpan span);

[[nodiscard]] inline std::expected<tyir::TyExprPtr, EvalError> value_to_expr(
    const ProgramView& program, const ValuePtr& value, const tyir::Ty& ty, SourceSpan span) {
    if (value == nullptr)
        return tyir::make_ty_expr(span, tyir::TyLiteral{}, tyir::ty::unit());

    if (ty.is_ref()) {
        if (value->kind != Value::Kind::Ref || value->referent == nullptr || ty.pointee == nullptr)
            return tyir::make_ty_expr(span, tyir::TyLiteral{}, tyir::ty::unit());
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
        return tyir::make_ty_expr(span, tyir::TyLiteral{}, tyir::ty::unit());

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
                });

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
                    });
            }
            auto expr_value = value_to_expr(program, field_value, field_decl.ty, span);
            if (!expr_value)
                return std::unexpected(std::move(expr_value.error()));
            fields.push_back({field_decl.name, *expr_value, span});
        }
        return tyir::make_ty_expr(
            span, tyir::TyStructInit{actual->type_name, std::move(fields)}, ty);
    }
    case Value::Kind::Ref:
        return std::unexpected(
            EvalError{span, "unexpected reference value during materialization", {}});
    }
    return std::unexpected(EvalError{span, "unsupported compile-time value materialization", {}});
}

[[nodiscard]] inline std::expected<tyir::TyExprPtr, EvalError>
    fold_expr(const tyir::TyExprPtr& expr, ConstEnv& env, const ProgramView& program);

[[nodiscard]] inline std::expected<tyir::TyBlock, EvalError>
    fold_block(const tyir::TyBlock& block, ConstEnv& env, const ProgramView& program) {
    env.push();
    tyir::TyBlock folded;
    folded.ty = block.ty;
    folded.span = block.span;

    for (const tyir::TyStmt& stmt : block.stmts) {
        auto folded_stmt = std::visit(
            [&](const auto& node) -> std::expected<tyir::TyStmt, EvalError> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyLetStmt>) {
                    auto init = fold_expr(node.init, env, program);
                    if (!init)
                        return std::unexpected(std::move(init.error()));

                    ConstEnv eval_env = env;
                    EvalContext ctx{program, {}};
                    const auto value = eval_expr(*init, eval_env, ctx);
                    if (value && value->kind == EvalState::Kind::Value) {
                        if (!node.discard)
                            env.bind_known(node.name, value->value);
                    } else if (!node.discard) {
                        env.bind_unknown(node.name);
                    }

                    return tyir::TyLetStmt{node.discard, node.name, node.ty, *init, node.span};
                } else {
                    auto value = fold_expr(node.expr, env, program);
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
        folded.stmts.push_back(std::move(*folded_stmt));
    }

    if (block.tail.has_value()) {
        auto tail = fold_expr(*block.tail, env, program);
        if (!tail) {
            env.pop();
            return std::unexpected(std::move(tail.error()));
        }
        folded.tail = *tail;
    }

    env.pop();
    return folded;
}

[[nodiscard]] inline std::expected<tyir::TyExprPtr, EvalError> maybe_fold_constant(
    const tyir::TyExprPtr& expr, const ProgramView& program, const ConstEnv& env) {
    ConstEnv eval_env = env;
    EvalContext ctx{program, {}};
    auto value = eval_expr(expr, eval_env, ctx);
    if (!value)
        return std::unexpected(std::move(value.error()));
    if (value->kind != EvalState::Kind::Value)
        return expr;
    auto folded = value_to_expr(program, value->value, expr->ty, expr->span);
    if (!folded)
        return std::unexpected(std::move(folded.error()));
    return *folded;
}

[[nodiscard]] inline std::expected<tyir::TyExprPtr, EvalError>
    fold_expr(const tyir::TyExprPtr& expr, ConstEnv& env, const ProgramView& program) {
    return std::visit(
        [&](const auto& node) -> std::expected<tyir::TyExprPtr, EvalError> {
            using Node = std::decay_t<decltype(node)>;

            if constexpr (
                std::is_same_v<Node, tyir::TyLiteral> || std::is_same_v<Node, tyir::LocalRef>
                || std::is_same_v<Node, tyir::EnumVariantRef>) {
                return maybe_fold_constant(expr, program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyStructInit>) {
                tyir::TyStructInit init;
                init.type_name = node.type_name;
                for (const tyir::TyStructInitField& field : node.fields) {
                    auto value = fold_expr(field.value, env, program);
                    if (!value)
                        return std::unexpected(std::move(value.error()));
                    init.fields.push_back({field.name, *value, field.span});
                }
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, std::move(init), expr->ty), program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBorrow>) {
                auto rhs = fold_expr(node.rhs, env, program);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyBorrow{*rhs}, expr->ty), program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyUnary>) {
                auto rhs = fold_expr(node.rhs, env, program);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyUnary{node.op, *rhs}, expr->ty), program,
                    env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBinary>) {
                auto lhs = fold_expr(node.lhs, env, program);
                if (!lhs)
                    return std::unexpected(std::move(lhs.error()));

                if (node.op == cstc::ast::BinaryOp::And || node.op == cstc::ast::BinaryOp::Or) {
                    const auto lhs_value = constant_bool(*lhs, env, program);
                    if (lhs_value.has_value()) {
                        const bool short_circuits =
                            (node.op == cstc::ast::BinaryOp::And && !*lhs_value)
                            || (node.op == cstc::ast::BinaryOp::Or && *lhs_value);
                        if (short_circuits) {
                            return maybe_fold_constant(
                                tyir::make_ty_expr(
                                    expr->span, tyir::TyBinary{node.op, *lhs, node.rhs}, expr->ty),
                                program, env);
                        }
                    }
                }

                auto rhs = fold_expr(node.rhs, env, program);
                if (!rhs)
                    return std::unexpected(std::move(rhs.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyBinary{node.op, *lhs, *rhs}, expr->ty),
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyFieldAccess>) {
                auto base = fold_expr(node.base, env, program);
                if (!base)
                    return std::unexpected(std::move(base.error()));
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span, tyir::TyFieldAccess{*base, node.field, node.use_kind},
                        expr->ty),
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyCall>) {
                std::vector<tyir::TyExprPtr> args;
                args.reserve(node.args.size());
                for (const tyir::TyExprPtr& arg : node.args) {
                    auto folded_arg = fold_expr(arg, env, program);
                    if (!folded_arg)
                        return std::unexpected(std::move(folded_arg.error()));
                    args.push_back(*folded_arg);
                }
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span, tyir::TyCall{node.fn_name, std::move(args)}, expr->ty),
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBlockPtr>) {
                auto block = fold_block(*node, env, program);
                if (!block)
                    return std::unexpected(std::move(block.error()));
                auto block_ptr = std::make_shared<tyir::TyBlock>(std::move(*block));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, block_ptr, expr->ty), program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyIf>) {
                auto condition = fold_expr(node.condition, env, program);
                if (!condition)
                    return std::unexpected(std::move(condition.error()));

                ConstEnv branch_env = env;
                const auto cond_value = constant_bool(*condition, branch_env, program);
                if (cond_value.has_value() && node.else_branch.has_value()) {
                    if (*cond_value) {
                        auto then_block = fold_block(*node.then_block, branch_env, program);
                        if (!then_block)
                            return std::unexpected(std::move(then_block.error()));
                        auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block));
                        return maybe_fold_constant(
                            tyir::make_ty_expr(expr->span, then_ptr, expr->ty), program, env);
                    }
                    return fold_expr(*node.else_branch, branch_env, program);
                }

                auto then_block = fold_block(*node.then_block, branch_env, program);
                if (!then_block)
                    return std::unexpected(std::move(then_block.error()));
                auto then_ptr = std::make_shared<tyir::TyBlock>(std::move(*then_block));

                std::optional<tyir::TyExprPtr> else_branch;
                if (node.else_branch.has_value()) {
                    auto else_expr = fold_expr(*node.else_branch, branch_env, program);
                    if (!else_expr)
                        return std::unexpected(std::move(else_expr.error()));
                    else_branch = *else_expr;
                }

                if (cond_value.has_value() && !node.else_branch.has_value() && !*cond_value) {
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyLiteral{
                            tyir::TyLiteral::Kind::Unit, cstc::symbol::kInvalidSymbol, false},
                        expr->ty);
                }

                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span,
                        tyir::TyIf{*condition, std::move(then_ptr), std::move(else_branch)},
                        expr->ty),
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyLoop>) {
                auto body = fold_block(*node.body, env, program);
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return maybe_fold_constant(
                    tyir::make_ty_expr(expr->span, tyir::TyLoop{std::move(body_ptr)}, expr->ty),
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyWhile>) {
                auto condition = fold_expr(node.condition, env, program);
                if (!condition)
                    return std::unexpected(std::move(condition.error()));
                auto body = fold_block(*node.body, env, program);
                if (!body)
                    return std::unexpected(std::move(body.error()));
                auto body_ptr = std::make_shared<tyir::TyBlock>(std::move(*body));
                return maybe_fold_constant(
                    tyir::make_ty_expr(
                        expr->span, tyir::TyWhile{*condition, std::move(body_ptr)}, expr->ty),
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyFor>) {
                ConstEnv loop_env = env;
                loop_env.push();

                std::optional<tyir::TyForInit> init;
                if (node.init.has_value()) {
                    auto init_expr = fold_expr(node.init->init, loop_env, program);
                    if (!init_expr)
                        return std::unexpected(std::move(init_expr.error()));
                    init = tyir::TyForInit{
                        node.init->discard, node.init->name, node.init->ty, *init_expr,
                        node.init->span};

                    ConstEnv eval_env = loop_env;
                    EvalContext ctx{program, {}};
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
                    auto cond = fold_expr(*node.condition, loop_env, program);
                    if (!cond)
                        return std::unexpected(std::move(cond.error()));
                    condition = *cond;
                }

                std::optional<tyir::TyExprPtr> step;
                if (node.step.has_value()) {
                    auto folded_step = fold_expr(*node.step, loop_env, program);
                    if (!folded_step)
                        return std::unexpected(std::move(folded_step.error()));
                    step = *folded_step;
                }

                auto body = fold_block(*node.body, loop_env, program);
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
                    program, env);
            }

            if constexpr (std::is_same_v<Node, tyir::TyBreak>) {
                std::optional<tyir::TyExprPtr> value;
                if (node.value.has_value()) {
                    auto folded = fold_expr(*node.value, env, program);
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
                    auto folded = fold_expr(*node.value, env, program);
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

[[nodiscard]] inline ProgramView build_program_view(const tyir::TyProgram& program) {
    ProgramView view;
    for (const tyir::TyItem& item : program.items) {
        std::visit(
            [&](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, tyir::TyStructDecl>) {
                    view.structs.emplace(node.name, &node);
                } else if constexpr (std::is_same_v<Node, tyir::TyEnumDecl>) {
                    view.enums.emplace(node.name, &node);
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

} // namespace detail

inline std::expected<tyir::TyProgram, EvalError> fold_program(const tyir::TyProgram& program) {
    const detail::ProgramView view = detail::build_program_view(program);

    tyir::TyProgram folded = program;
    for (tyir::TyItem& item : folded.items) {
        if (auto* fn = std::get_if<tyir::TyFnDecl>(&item)) {
            detail::ConstEnv env;
            env.push();
            for (const tyir::TyParam& param : fn->params)
                env.bind_unknown(param.name);

            auto body = detail::fold_block(*fn->body, env, view);
            env.pop();
            if (!body)
                return std::unexpected(std::move(body.error()));
            fn->body = std::make_shared<tyir::TyBlock>(std::move(*body));
        }
    }

    return folded;
}

} // namespace cstc::tyir_interp

#endif // CICEST_COMPILER_CSTC_TYIR_INTERP_INTERP_HPP
