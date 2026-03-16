#ifndef CICEST_COMPILER_CSTC_LIR_LIR_HPP
#define CICEST_COMPILER_CSTC_LIR_LIR_HPP

/// @file lir.hpp
/// @brief Low-level Intermediate Representation (LIR) node definitions.
///
/// LIR is a flat, SSA-like IR positioned between TyIR and code generation.
/// It corresponds roughly to Rust's MIR (Mid-level Intermediate Representation):
///
///   AST → [TyIR Builder] → TyIR → [LIR Builder] → **LIR** → [Codegen] → LLVM IR
///
/// # Key design differences from TyIR
///
/// - **Flat control flow**: function bodies are explicit control-flow graphs
///   (lists of `LirBasicBlock`), not nested AST-like block expressions.
/// - **Explicit local IDs**: every local variable / temporary is represented
///   by a numeric `LirLocalId`; a `LirLocalDecl` table maps IDs to types.
/// - **Places and operands**: writes go to `LirPlace` (a local or a field
///   projection); reads produce `LirOperand` (copy of a place, or a constant).
/// - **Flat statements**: every expression is broken into one-assignment-per-
///   instruction (`LirStmt = LirAssign`).
/// - **Explicit terminators**: every block ends with a `LirTerminator`
///   (`Return`, `Jump`, `SwitchBool`, or `Unreachable`).
///
/// # Relationship to LLVM IR
///
/// Converting LIR to LLVM IR requires:
///   - Mapping `LirLocalId` → `llvm::AllocaInst` (or SSA `Value` after mem2reg).
///   - Mapping `LirPlace::Field` → `llvm::GetElementPtrInst`.
///   - Mapping `LirRvalue::BinaryOp` / `UnaryOp` → arithmetic/comparison instructions.
///   - Mapping `LirRvalue::Call` → `llvm::CallInst`.
///   - Mapping `LirTerminator::SwitchBool` → `llvm::BranchInst (conditional)`.
///   - Mapping struct/enum type declarations → `llvm::StructType`.
///
/// # Design notes
/// - All string data is represented as interned `Symbol` values; a
///   `cstc::symbol::SymbolSession` must be live for any symbol operations.
/// - The `Ty` type system is re-used from `cstc_tyir` — no new type layer.
/// - `UnaryOp` / `BinaryOp` are re-used from `cstc_ast`.
/// - Spans use global byte offsets (same convention as all prior stages).
/// - The module is header-only; `printer.hpp` provides the human-readable CFG
///   formatter.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <cstc_ast/ast.hpp>
#include <cstc_span/span.hpp>
#include <cstc_symbol/symbol.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::lir {

// Re-export commonly used names so callers can write `lir::Ty` etc.
using tyir::Ty;
using tyir::ty::bool_;
using tyir::ty::named;
using tyir::ty::never;
using tyir::ty::num;
using tyir::ty::str;
using tyir::ty::unit;

// ─── IDs ────────────────────────────────────────────────────────────────────

/// Index of a local variable / SSA register within a function's locals table.
///
/// Parameters occupy the lowest IDs (0 … params.size()-1).
/// Temporaries and user-named locals are allocated after that.
using LirLocalId = std::uint32_t;

/// Index of a basic block within a function's block list.
///
/// The entry block is always `kEntryBlock` (0).
using LirBlockId = std::uint32_t;

/// Sentinel value for an uninitialized / invalid local.
inline constexpr LirLocalId kInvalidLocal = UINT32_MAX;
/// Sentinel value for an uninitialized / invalid block.
inline constexpr LirBlockId kInvalidBlock = UINT32_MAX;
/// The entry basic block of every function.
inline constexpr LirBlockId kEntryBlock = 0;

// ─── Constants ──────────────────────────────────────────────────────────────

/// A compile-time constant value embedded directly in an operand.
struct LirConst {
    /// Category of constant.
    enum class Kind {
        /// Numeric literal (e.g. `42`, `3.14`).  Text stored in `symbol`.
        Num,
        /// String literal (e.g. `"hello"`).  Text stored in `symbol`.
        Str,
        /// Boolean literal.  Value stored in `bool_value`.
        Bool,
        /// Unit literal `()`.
        Unit,
    };

    /// Constant category.
    Kind kind = Kind::Unit;
    /// Interned source text; valid when `kind` is `Num` or `Str`.
    cstc::symbol::Symbol symbol = cstc::symbol::kInvalidSymbol;
    /// Boolean value; valid only when `kind == Bool`.
    bool bool_value = false;

    // ─── Factories ───────────────────────────────────────────────────────────

    /// Constructs a numeric constant from interned text.
    [[nodiscard]] static LirConst num(cstc::symbol::Symbol sym);
    /// Constructs a string constant from interned text.
    [[nodiscard]] static LirConst str(cstc::symbol::Symbol sym);
    /// Constructs a boolean constant.
    [[nodiscard]] static LirConst bool_(bool value);
    /// Constructs the unit constant.
    [[nodiscard]] static LirConst unit();

    // ─── Queries ─────────────────────────────────────────────────────────────

    /// Returns the `Ty` of this constant.
    [[nodiscard]] Ty ty() const;

    /// Returns a human-readable display string.
    ///
    /// Requires an active `SymbolSession` for `Num` and `Str` kinds.
    [[nodiscard]] std::string display() const;

    friend bool operator==(const LirConst&, const LirConst&) = default;
};

// ─── Places ─────────────────────────────────────────────────────────────────

/// A place is a memory location that can be read (as an operand) or written
/// (as the destination of an assignment statement).
///
/// LIR supports two place forms:
///  - `Local(id)` — the entire local variable.
///  - `Field(base_local, field_name)` — a named field projection on a struct local.
///
/// More complex projections (slice indexing, nested fields) are not yet needed
/// and can be added when required.
struct LirPlace {
    /// Category of place.
    enum class Kind {
        /// The whole local variable.
        Local,
        /// A named field of a struct-typed local (`base.field`).
        Field,
    };

    /// Place category.
    Kind kind = Kind::Local;
    /// The local variable that forms the root of this place.
    LirLocalId local_id = kInvalidLocal;
    /// Field name; valid only when `kind == Field`.
    cstc::symbol::Symbol field_name = cstc::symbol::kInvalidSymbol;

    // ─── Factories ───────────────────────────────────────────────────────────

    /// Constructs a simple local-variable place.
    [[nodiscard]] static LirPlace local(LirLocalId id);
    /// Constructs a field-projection place (`local_id.field_name`).
    [[nodiscard]] static LirPlace field(LirLocalId id, cstc::symbol::Symbol name);

    friend bool operator==(const LirPlace&, const LirPlace&) = default;
};

// ─── Operands ───────────────────────────────────────────────────────────────

/// An SSA-style value: either a copy of a place's current value, or a
/// compile-time constant.
///
/// Operands are the "leaves" of every rvalue computation: every input to a
/// binary op, call argument, return value, etc., is an `LirOperand`.
struct LirOperand {
    /// Category of operand.
    enum class Kind {
        /// A copy of the value currently held by `place`.
        Copy,
        /// A compile-time constant embedded directly.
        Const,
    };

    /// Operand category.
    Kind kind = Kind::Const;
    /// Source place; valid when `kind == Copy`.
    LirPlace place = LirPlace{};
    /// Constant payload; valid when `kind == Const`.
    LirConst constant = LirConst{};

    // ─── Factories ───────────────────────────────────────────────────────────

    /// Operand that copies the value at `place`.
    [[nodiscard]] static LirOperand copy(LirPlace place);
    /// Operand that embeds a compile-time constant.
    [[nodiscard]] static LirOperand from_const(LirConst constant);

    friend bool operator==(const LirOperand&, const LirOperand&) = default;
};

// ─── Rvalue sub-nodes ────────────────────────────────────────────────────────
//
// An rvalue is the right-hand side of an `LirAssign` statement.  Each rvalue
// computes a single value that is stored into the destination place.

/// Trivial rvalue: just move/copy an operand into the destination.
struct LirUse {
    LirOperand operand;
};

/// Arithmetic or logical binary operation.
struct LirBinaryOp {
    cstc::ast::BinaryOp op;
    LirOperand lhs;
    LirOperand rhs;
};

/// Arithmetic or logical unary operation.
struct LirUnaryOp {
    cstc::ast::UnaryOp op;
    LirOperand operand;
};

/// Direct function call (no first-class functions in Cicest).
struct LirCall {
    /// Resolved top-level function name.
    cstc::symbol::Symbol fn_name = cstc::symbol::kInvalidSymbol;
    /// Evaluated argument list (count and types match the callee's signature).
    std::vector<LirOperand> args;
};

/// A single named-field initializer inside a struct-construction rvalue.
struct LirStructInitField {
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    LirOperand value;
};

/// Struct construction rvalue (`TypeName { field: operand, … }`).
struct LirStructInit {
    cstc::symbol::Symbol type_name = cstc::symbol::kInvalidSymbol;
    std::vector<LirStructInitField> fields;
};

/// Enum variant reference rvalue (`EnumName::Variant`).
struct LirEnumVariantRef {
    cstc::symbol::Symbol enum_name = cstc::symbol::kInvalidSymbol;
    cstc::symbol::Symbol variant_name = cstc::symbol::kInvalidSymbol;
};

/// The right-hand side of an `LirAssign` statement.
struct LirRvalue {
    using Node =
        std::variant<LirUse, LirBinaryOp, LirUnaryOp, LirCall, LirStructInit, LirEnumVariantRef>;

    Node node;
};

// ─── Statements ─────────────────────────────────────────────────────────────

/// A single LIR statement: assignment of an rvalue to a destination place.
///
/// This is the *only* statement kind in LIR — all side-effecting operations
/// (including calls) are expressed as assignments.  A call whose return value
/// is discarded assigns into a throwaway `()` local.
struct LirAssign {
    /// Destination of the assignment.
    LirPlace dest;
    /// Value being computed and stored.
    LirRvalue rhs;
    /// Source location for diagnostics / debug info.
    cstc::span::SourceSpan span;
};

/// Any statement within a basic block (currently only `LirAssign`).
using LirStmt = LirAssign;

// ─── Terminators ────────────────────────────────────────────────────────────

/// Return from the current function with an optional value.
struct LirReturn {
    /// The value to return; `nullopt` means the function returns `()`.
    std::optional<LirOperand> value;
};

/// Unconditional jump to another basic block.
struct LirJump {
    LirBlockId target;
};

/// Conditional branch: jump to `true_target` if `condition` is `true`,
/// otherwise jump to `false_target`.
struct LirSwitchBool {
    LirOperand condition;
    LirBlockId true_target;
    LirBlockId false_target;
};

/// Marks a basic block as unreachable (e.g. after a diverging expression).
///
/// Code generators may treat this as `llvm::UnreachableInst`.
struct LirUnreachable {};

/// The control-flow instruction that ends every basic block.
struct LirTerminator {
    using Node = std::variant<LirReturn, LirJump, LirSwitchBool, LirUnreachable>;

    Node node;
    /// Source location for diagnostics.
    cstc::span::SourceSpan span;
};

// ─── Basic blocks ───────────────────────────────────────────────────────────

/// A basic block: a straight-line sequence of statements followed by exactly
/// one terminator.
///
/// No implicit fall-through; all edges in the CFG are expressed via the
/// block's `terminator`.
struct LirBasicBlock {
    /// Position of this block in the function's block list.
    ///
    /// `blocks[id].id == id` is always true; the field exists for
    /// convenience when iterating over blocks without index tracking.
    LirBlockId id = kInvalidBlock;
    /// Ordered list of assignments.
    std::vector<LirStmt> stmts;
    /// Control-flow instruction that ends this block.
    LirTerminator terminator;
};

// ─── Local declarations ─────────────────────────────────────────────────────

/// Metadata for one local variable slot in a function.
///
/// The locals table is a dense array indexed by `LirLocalId`.
/// Parameter slots occupy indices `0 … params.size()-1`; temporaries and
/// user-named variables are appended after that.
struct LirLocalDecl {
    /// Index of this slot (equals its position in the locals array).
    LirLocalId id = kInvalidLocal;
    /// Type of the value stored in this slot.
    Ty ty;
    /// Optional source name for debugging (set from `let` bindings and params).
    std::optional<cstc::symbol::Symbol> debug_name;
};

// ─── Function definition ────────────────────────────────────────────────────

/// Metadata for a single function parameter.
struct LirParam {
    /// Index of the local slot that holds this parameter's value.
    LirLocalId local = kInvalidLocal;
    /// Parameter name (matches `locals[local].debug_name`).
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Parameter type (matches `locals[local].ty`).
    Ty ty;
    /// Source location.
    cstc::span::SourceSpan span;
};

/// A fully lowered function body represented as a control-flow graph.
///
/// - `locals[0 … params.size()-1]` are parameter slots.
/// - `locals[params.size() … ]` are temporaries and user-defined `let` locals.
/// - `blocks[kEntryBlock]` is the function entry point.
struct LirFnDef {
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Parameter metadata (in declaration order).
    std::vector<LirParam> params;
    /// Declared return type.
    Ty return_ty;
    /// All local variable slots (dense, indexed by `LirLocalId`).
    std::vector<LirLocalDecl> locals;
    /// Basic blocks (dense, indexed by `LirBlockId`; entry = 0).
    std::vector<LirBasicBlock> blocks;
    /// Source location.
    cstc::span::SourceSpan span;
};

// ─── Type declarations ───────────────────────────────────────────────────────
//
// These mirror the TyIR declarations and are kept in LIR so that the program
// is self-contained (a code generator does not need to reach back into TyIR).

/// A named field within a lowered struct declaration.
struct LirStructField {
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    Ty ty;
    cstc::span::SourceSpan span;
};

/// A lowered struct type declaration.
struct LirStructDecl {
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    std::vector<LirStructField> fields;
    /// True when the struct has no fields (zero-sized type).
    bool is_zst = false;
    cstc::span::SourceSpan span;
};

/// A single variant of a lowered enum.
struct LirEnumVariant {
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Optional explicit numeric discriminant text.
    std::optional<cstc::symbol::Symbol> discriminant;
    cstc::span::SourceSpan span;
};

/// A lowered enum type declaration.
struct LirEnumDecl {
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    std::vector<LirEnumVariant> variants;
    cstc::span::SourceSpan span;
};

/// An extern function declaration (no body, just a signature).
struct LirExternFnDecl {
    /// ABI string (e.g. "lang", "c").
    cstc::symbol::Symbol abi = cstc::symbol::kInvalidSymbol;
    /// Function name.
    cstc::symbol::Symbol name = cstc::symbol::kInvalidSymbol;
    /// Parameter metadata.
    std::vector<LirParam> params;
    /// Declared return type.
    Ty return_ty;
    /// Source location.
    cstc::span::SourceSpan span;
};

// ─── Program ────────────────────────────────────────────────────────────────

/// Root of a fully-lowered LIR program.
struct LirProgram {
    std::vector<LirStructDecl> structs;
    std::vector<LirEnumDecl> enums;
    std::vector<LirFnDef> fns;
    std::vector<LirExternFnDecl> extern_fns;
};

} // namespace cstc::lir

#include <sstream>
#include <string>

namespace cstc::lir {

// ─── LirConst ────────────────────────────────────────────────────────────────

inline LirConst LirConst::num(cstc::symbol::Symbol sym) { return LirConst{Kind::Num, sym, false}; }

inline LirConst LirConst::str(cstc::symbol::Symbol sym) { return LirConst{Kind::Str, sym, false}; }

inline LirConst LirConst::bool_(bool value) {
    return LirConst{Kind::Bool, cstc::symbol::kInvalidSymbol, value};
}

inline LirConst LirConst::unit() {
    return LirConst{Kind::Unit, cstc::symbol::kInvalidSymbol, false};
}

inline Ty LirConst::ty() const {
    switch (kind) {
    case Kind::Num: return tyir::ty::num();
    case Kind::Str: return tyir::ty::str();
    case Kind::Bool: return tyir::ty::bool_();
    case Kind::Unit: return tyir::ty::unit();
    }
    return tyir::ty::unit();
}

inline std::string LirConst::display() const {
    switch (kind) {
    case Kind::Num: return symbol.is_valid() ? std::string(symbol.as_str()) : "<num>";
    case Kind::Str: {
        std::string s = "\"";
        s += symbol.is_valid() ? std::string(symbol.as_str()) : "";
        s += "\"";
        return s;
    }
    case Kind::Bool: return bool_value ? "true" : "false";
    case Kind::Unit: return "()";
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
    op.kind = Kind::Copy;
    op.place = place;
    return op;
}

inline LirOperand LirOperand::from_const(LirConst constant) {
    LirOperand op;
    op.kind = Kind::Const;
    op.constant = constant;
    return op;
}

} // namespace cstc::lir

#endif // CICEST_COMPILER_CSTC_LIR_LIR_HPP
