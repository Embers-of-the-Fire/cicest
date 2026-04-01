#ifndef CICEST_COMPILER_CSTC_LIR_BUILDER_BUILDER_HPP
#define CICEST_COMPILER_CSTC_LIR_BUILDER_BUILDER_HPP

/// @file builder.hpp
/// @brief TyIR → LIR lowering pass.
///
/// This pass transforms a fully type-annotated `cstc::tyir::TyProgram` into a
/// flat, SSA-like `cstc::lir::LirProgram`.  It performs:
///
///  1. **Type declaration forwarding** — struct and enum declarations are
///     copied verbatim from TyIR into LIR (field types are already resolved).
///
///  2. **Control-flow graph construction** — each `TyFnDecl` body (a nested
///     tree of `TyBlock`/`TyExpr` nodes) is translated into a flat list of
///     `LirBasicBlock`s connected via `LirTerminator`s.
///
///  3. **Expression flattening** — every compound expression is broken into
///     a sequence of `LirAssign` statements; sub-expressions are stored into
///     freshly allocated `LirLocalId` temporaries.
///
///  4. **Control-flow lowering** — `TyIf` becomes `SwitchBool` + merge block,
///     `TyLoop` becomes a back-edge to a header block, `TyWhile`/`TyFor`
///     become condition blocks with `SwitchBool`, `break`/`continue`/`return`
///     become unconditional `Jump` or `Return` terminators.
///
/// ## Usage
///
/// ```cpp
/// cstc::symbol::SymbolSession session;
/// const auto tyir = cstc::tyir_builder::lower_program(ast);
/// if (!tyir) { /* handle error */ }
///
/// const auto lir = cstc::lir_builder::lower_program(*tyir);
/// std::cout << cstc::lir::format_program(lir);
/// ```
///
/// ## Error handling
///
/// Given valid TyIR (produced by the type-checking pass), this lowering is
/// structurally infallible.  The function therefore returns `LirProgram`
/// directly without wrapping in `std::expected`.

#include <cstc_lir/lir.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::lir_builder {

/// Lowers a fully type-annotated TyIR program to a flat LIR program.
///
/// Requires an active `SymbolSession` on the calling thread.
[[nodiscard]] lir::LirProgram lower_program(const tyir::TyProgram& program);

} // namespace cstc::lir_builder

#endif // CICEST_COMPILER_CSTC_LIR_BUILDER_BUILDER_HPP
