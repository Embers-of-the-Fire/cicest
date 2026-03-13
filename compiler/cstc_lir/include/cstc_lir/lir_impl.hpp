#ifndef CICEST_COMPILER_CSTC_LIR_LIR_IMPL_HPP
#define CICEST_COMPILER_CSTC_LIR_LIR_IMPL_HPP

/// @file lir_impl.hpp
/// @brief Inline implementations for the LIR factory methods and helpers.
///
/// Included at the bottom of `lir.hpp`; not intended to be included directly.

#include <sstream>
#include <string>

namespace cstc::lir {

// ─── LirConst ────────────────────────────────────────────────────────────────

inline LirConst LirConst::num(cstc::symbol::Symbol sym) {
    return LirConst{Kind::Num, sym, false};
}

inline LirConst LirConst::str(cstc::symbol::Symbol sym) {
    return LirConst{Kind::Str, sym, false};
}

inline LirConst LirConst::bool_(bool value) {
    return LirConst{Kind::Bool, cstc::symbol::kInvalidSymbol, value};
}

inline LirConst LirConst::unit() {
    return LirConst{Kind::Unit, cstc::symbol::kInvalidSymbol, false};
}

inline Ty LirConst::ty() const {
    switch (kind) {
        case Kind::Num:  return tyir::ty::num();
        case Kind::Str:  return tyir::ty::str();
        case Kind::Bool: return tyir::ty::bool_();
        case Kind::Unit: return tyir::ty::unit();
    }
    return tyir::ty::unit();
}

inline std::string LirConst::display() const {
    switch (kind) {
        case Kind::Num:
            return symbol.is_valid() ? std::string(symbol.as_str()) : "<num>";
        case Kind::Str: {
            std::string s = "\"";
            s += symbol.is_valid() ? std::string(symbol.as_str()) : "";
            s += "\"";
            return s;
        }
        case Kind::Bool:
            return bool_value ? "true" : "false";
        case Kind::Unit:
            return "()";
    }
    return "<const>";
}

// ─── LirPlace ────────────────────────────────────────────────────────────────

inline LirPlace LirPlace::local(LirLocalId id) {
    return LirPlace{Kind::Local, id, cstc::symbol::kInvalidSymbol};
}

inline LirPlace LirPlace::field(LirLocalId id, cstc::symbol::Symbol name) {
    return LirPlace{Kind::Field, id, name};
}

// ─── LirOperand ──────────────────────────────────────────────────────────────

inline LirOperand LirOperand::copy(LirPlace place) {
    LirOperand op;
    op.kind  = Kind::Copy;
    op.place = place;
    return op;
}

inline LirOperand LirOperand::from_const(LirConst constant) {
    LirOperand op;
    op.kind     = Kind::Const;
    op.constant = constant;
    return op;
}

} // namespace cstc::lir

#endif // CICEST_COMPILER_CSTC_LIR_LIR_IMPL_HPP
