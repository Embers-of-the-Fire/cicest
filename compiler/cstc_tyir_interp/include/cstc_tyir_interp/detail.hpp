#ifndef CICEST_COMPILER_CSTC_TYIR_INTERP_DETAIL_HPP
#define CICEST_COMPILER_CSTC_TYIR_INTERP_DETAIL_HPP

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cstc_tyir_interp/interp.hpp>

namespace cstc::tyir_interp::detail {

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

[[nodiscard]] ValuePtr make_num(double value);
[[nodiscard]] ValuePtr make_bool(bool value);
[[nodiscard]] ValuePtr make_unit();
[[nodiscard]] ValuePtr make_string(std::string value, Value::StringOwnership ownership);
[[nodiscard]] ValuePtr make_ref(ValuePtr referent);
[[nodiscard]] ValuePtr make_enum(Symbol type_name, Symbol variant_name);
[[nodiscard]] ValuePtr make_struct(Symbol type_name, std::vector<ValueField> fields);

struct ProgramView {
    std::unordered_map<Symbol, const tyir::TyStructDecl*, SymbolHash> structs;
    std::unordered_map<Symbol, const tyir::TyEnumDecl*, SymbolHash> enums;
    std::unordered_map<Symbol, const tyir::TyFnDecl*, SymbolHash> fns;
    std::unordered_map<Symbol, const tyir::TyExternFnDecl*, SymbolHash> extern_fns;
    Symbol constraint_enum_name = cstc::symbol::kInvalidSymbol;
};

inline constexpr std::size_t kDefaultEvalStepBudget = 4096;
inline constexpr std::size_t kDefaultEvalCallDepth = 256;

struct EvalContext {
    const ProgramView& program;
    std::vector<EvalStackFrame> stack;
    std::size_t remaining_steps = kDefaultEvalStepBudget;
    std::size_t remaining_call_depth = kDefaultEvalCallDepth;
    std::unordered_set<Symbol, SymbolHash> generic_params;
};

using TypeSubstitution = std::unordered_map<Symbol, tyir::Ty, SymbolHash>;

[[nodiscard]] bool values_equal(const ValuePtr& lhs, const ValuePtr& rhs);
[[nodiscard]] std::expected<ValuePtr, EvalError> eval_lang_intrinsic(
    const tyir::TyExternFnDecl& decl, const std::vector<ValuePtr>& args, EvalContext& ctx,
    SourceSpan span);
[[nodiscard]] std::expected<tyir::TyExprPtr, EvalError> value_to_expr(
    const ProgramView& program, const ValuePtr& value, const tyir::Ty& ty, SourceSpan span);
[[nodiscard]] ConstraintEvalResult evaluate_constraint(
    const tyir::TyExprPtr& expr, const TypeSubstitution& substitution, const ProgramView& program,
    std::vector<EvalStackFrame> stack = {});

} // namespace cstc::tyir_interp::detail

#endif // CICEST_COMPILER_CSTC_TYIR_INTERP_DETAIL_HPP
