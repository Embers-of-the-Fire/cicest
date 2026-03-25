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
[[nodiscard]] inline lir::LirProgram lower_program(const tyir::TyProgram& program);

} // namespace cstc::lir_builder

#include <cassert>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <cstc_symbol/symbol.hpp>

namespace cstc::lir_builder {

namespace detail {

// ─── FnBuilder ────────────────────────────────────────────────────────────────

/// Context used while lowering a single `TyFnDecl` to a `LirFnDef`.
///
/// Manages:
/// - Fresh `LirLocalId` allocation.
/// - Fresh `LirBlockId` allocation.
/// - The current block being built.
/// - A scope stack mapping source variable names → local IDs.
/// - A loop stack for `break`/`continue` target resolution.
class FnBuilder {
public:
    // ─── Locals ──────────────────────────────────────────────────────────────

    /// Allocates a new local slot of the given type.
    lir::LirLocalId alloc_local(
        cstc::tyir::Ty ty, std::optional<cstc::symbol::Symbol> debug_name = std::nullopt) {
        return alloc_local_impl(std::move(ty), std::move(debug_name), true);
    }

    /// Allocates a new hidden local slot that does not belong to the current
    /// lexical scope.
    lir::LirLocalId alloc_hidden_local(
        cstc::tyir::Ty ty, std::optional<cstc::symbol::Symbol> debug_name = std::nullopt) {
        return alloc_local_impl(std::move(ty), std::move(debug_name), false);
    }

private:
    lir::LirLocalId alloc_local_impl(
        cstc::tyir::Ty ty, std::optional<cstc::symbol::Symbol> debug_name, bool record_in_scope) {
        const lir::LirLocalId id = static_cast<lir::LirLocalId>(locals_.size());
        locals_.push_back({id, std::move(ty), debug_name});
        if (record_in_scope && !scope_.empty())
            scope_.back().locals.push_back(id);
        return id;
    }

public:
    /// Registers a hidden function return slot that must not participate in
    /// lexical drop emission.
    void set_return_slot(lir::LirLocalId id) { return_slot_ = id; }

    [[nodiscard]] std::optional<lir::LirLocalId> return_slot() const { return return_slot_; }

    [[nodiscard]] const lir::LirLocalDecl& local_decl(lir::LirLocalId id) const {
        return locals_.at(id);
    }

    // ─── Blocks ───────────────────────────────────────────────────────────────

    /// Allocates a new (empty) basic block and returns its ID.
    lir::LirBlockId alloc_block() {
        const lir::LirBlockId id = static_cast<lir::LirBlockId>(blocks_.size());
        lir::LirBasicBlock block;
        block.id = id;
        // Placeholder terminator — will be overwritten before the block is sealed.
        block.terminator = lir::LirTerminator{lir::LirUnreachable{}, {}};
        blocks_.push_back(std::move(block));
        block_has_predecessor_.push_back(false);
        return id;
    }

    /// Sets `block_id` as the currently-active block (future `emit_*` calls
    /// will append to it).
    void set_current_block(lir::LirBlockId id) {
        assert(id < blocks_.size());
        current_block_ = id;
        block_terminated_ = false;
    }

    [[nodiscard]] lir::LirBlockId current_block_id() const { return current_block_; }
    [[nodiscard]] bool block_has_predecessor(lir::LirBlockId id) const {
        assert(id < block_has_predecessor_.size());
        return block_has_predecessor_[id];
    }

    /// Returns true if the current block has already been given a terminator
    /// (e.g. by a `return`/`break`/`continue` statement inside it).
    [[nodiscard]] bool is_terminated() const { return block_terminated_; }

    /// Appends a statement to the current block.
    void emit_stmt(lir::LirStmt stmt) {
        assert(current_block_ < blocks_.size());
        blocks_[current_block_].stmts.push_back(std::move(stmt));
    }

    /// Sets the terminator of the current block and marks it as terminated.
    void seal_block(lir::LirTerminator term) {
        assert(current_block_ < blocks_.size());
        assert(!block_terminated_ && "current block already sealed");
        mark_successors(term);
        blocks_[current_block_].terminator = std::move(term);
        block_terminated_ = true;
    }

    // ─── Scope ────────────────────────────────────────────────────────────────

    void push_scope() { scope_.push_back({}); }
    void pop_scope() {
        assert(!scope_.empty());
        scope_.pop_back();
    }

    void bind(cstc::symbol::Symbol name, lir::LirLocalId id) {
        assert(!scope_.empty());
        scope_.back().bindings.emplace(name, id);
    }

    [[nodiscard]] lir::LirLocalId lookup(cstc::symbol::Symbol name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            const auto found = it->bindings.find(name);
            if (found != it->bindings.end())
                return found->second;
        }
        // Should never happen with valid TyIR.
        assert(false && "unresolved local in LIR builder");
        return lir::kInvalidLocal;
    }

    [[nodiscard]] std::size_t scope_depth() const { return scope_.size(); }

    void emit_scope_drops_to_depth(std::size_t target_depth, cstc::span::SourceSpan span) {
        assert(target_depth <= scope_.size());
        for (std::size_t depth = scope_.size(); depth > target_depth; --depth) {
            const ScopeFrame& frame = scope_[depth - 1];
            for (auto it = frame.locals.rbegin(); it != frame.locals.rend(); ++it) {
                const lir::LirLocalDecl& local = locals_.at(*it);
                if (!local.ty.is_move_only())
                    continue;
                emit_stmt(lir::LirDrop{*it, span});
            }
        }
    }

    void emit_current_scope_drops(cstc::span::SourceSpan span) {
        assert(!scope_.empty());
        emit_scope_drops_to_depth(scope_.size() - 1, span);
    }

    // ─── Loop stack ───────────────────────────────────────────────────────────

    struct LoopContext {
        /// Block to jump to on `continue`.
        lir::LirBlockId continue_target;
        /// Block to jump to on `break`.
        lir::LirBlockId break_target;
        /// Local that receives the loop's value on `break expr`.
        /// `kInvalidLocal` when the loop yields `()`.
        lir::LirLocalId break_value_local;
        /// Scope depth that remains active after breaking/continuing this loop.
        std::size_t preserved_scope_depth = 0;
    };

    void push_loop(
        lir::LirBlockId cont, lir::LirBlockId brk, lir::LirLocalId val_local = lir::kInvalidLocal) {
        loop_stack_.push_back({cont, brk, val_local, scope_.size()});
    }
    void pop_loop() {
        assert(!loop_stack_.empty());
        loop_stack_.pop_back();
    }
    [[nodiscard]] const LoopContext& current_loop() const {
        assert(!loop_stack_.empty());
        return loop_stack_.back();
    }

    // ─── Build ────────────────────────────────────────────────────────────────

    /// Consumes the builder and fills `fn` with the collected locals and blocks.
    void finish(lir::LirFnDef& fn) {
        fn.locals = std::move(locals_);
        fn.blocks = std::move(blocks_);
    }

private:
    void mark_successor(lir::LirBlockId id) {
        assert(id < block_has_predecessor_.size());
        block_has_predecessor_[id] = true;
    }

    void mark_successors(const lir::LirTerminator& term) {
        std::visit(
            [&](const auto& node) {
                using N = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<N, lir::LirJump>) {
                    mark_successor(node.target);
                } else if constexpr (std::is_same_v<N, lir::LirSwitchBool>) {
                    mark_successor(node.true_target);
                    mark_successor(node.false_target);
                }
            },
            term.node);
    }

    struct ScopeFrame {
        std::unordered_map<cstc::symbol::Symbol, lir::LirLocalId, cstc::symbol::SymbolHash>
            bindings;
        std::vector<lir::LirLocalId> locals;
    };

    std::vector<lir::LirLocalDecl> locals_;
    std::vector<lir::LirBasicBlock> blocks_;
    std::vector<bool> block_has_predecessor_;
    lir::LirBlockId current_block_ = lir::kInvalidBlock;
    bool block_terminated_ = false;
    std::vector<ScopeFrame> scope_;
    std::vector<LoopContext> loop_stack_;
    std::optional<lir::LirLocalId> return_slot_;
};

// ─── Forward declarations ─────────────────────────────────────────────────────

[[nodiscard]] lir::LirOperand lower_expr(FnBuilder& builder, const tyir::TyExprPtr& expr);
[[nodiscard]] lir::LirOperand lower_block(FnBuilder& builder, const tyir::TyBlockPtr& block);

[[noreturn]] inline void unsupported_projected_move() {
    assert(false && "moves from projected LIR places are not supported");
    std::abort();
}

[[nodiscard]] inline lir::LirOperand operand_for_local(lir::LirLocalId id, const tyir::Ty& ty) {
    const lir::LirPlace place = lir::LirPlace::local(id);
    if (ty.is_move_only())
        return lir::LirOperand::move(place);
    return lir::LirOperand::copy(place);
}

[[nodiscard]] inline lir::LirOperand operand_for_place(
    const lir::LirPlace& place, const tyir::Ty& ty,
    tyir::ValueUseKind use_kind = tyir::ValueUseKind::Copy) {
    (void)ty;
    if (use_kind == tyir::ValueUseKind::Move) {
        if (place.kind != lir::LirPlace::Kind::Local)
            unsupported_projected_move();
        return lir::LirOperand::move(place);
    }
    return lir::LirOperand::copy(place);
}

[[nodiscard]] inline lir::LirLocalId materialize_operand(
    FnBuilder& builder, lir::LirOperand operand, const tyir::Ty& ty, cstc::span::SourceSpan span) {
    const lir::LirLocalId tmp = builder.alloc_local(ty);
    builder.emit_stmt(
        lir::LirAssign{
            lir::LirPlace::local(tmp), lir::LirRvalue{lir::LirUse{std::move(operand)}}, span});
    return tmp;
}

[[nodiscard]] inline std::optional<lir::LirPlace>
    lower_borrow_place(FnBuilder& builder, const tyir::TyExprPtr& expr) {
    return std::visit(
        [&](const auto& node) -> std::optional<lir::LirPlace> {
            using N = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<N, tyir::LocalRef>) {
                return lir::LirPlace::local(builder.lookup(node.name));
            } else if constexpr (std::is_same_v<N, tyir::TyFieldAccess>) {
                const auto base_place = lower_borrow_place(builder, node.base);
                if (!base_place.has_value())
                    return std::nullopt;
                return base_place->project(node.field);
            } else {
                return std::nullopt;
            }
        },
        expr->node);
}

// ─── Expression lowering ──────────────────────────────────────────────────────

/// Emits statements into `builder` that compute the value of `expr` and
/// returns an operand referring to that value.
///
/// For constants and local references this is free (no instruction emitted).
/// For compound expressions a fresh local is allocated to hold the result.
[[nodiscard]] lir::LirOperand lower_expr(FnBuilder& builder, const tyir::TyExprPtr& expr) {
    return std::visit(
        [&](const auto& node) -> lir::LirOperand {
            using N = std::decay_t<decltype(node)>;

            // ── Literals ──────────────────────────────────────────────────────
            if constexpr (std::is_same_v<N, tyir::TyLiteral>) {
                lir::LirConst c{};
                switch (node.kind) {
                case tyir::TyLiteral::Kind::Num: c = lir::LirConst::num(node.symbol); break;
                case tyir::TyLiteral::Kind::Str: c = lir::LirConst::str(node.symbol); break;
                case tyir::TyLiteral::Kind::Bool: c = lir::LirConst::bool_(node.bool_value); break;
                case tyir::TyLiteral::Kind::Unit: c = lir::LirConst::unit(); break;
                }
                return lir::LirOperand::from_const(c);
            }

            // ── Local reference ───────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::LocalRef>) {
                const lir::LirLocalId id = builder.lookup(node.name);
                return operand_for_place(lir::LirPlace::local(id), expr->ty, node.use_kind);
            }

            // ── Enum variant reference ────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::EnumVariantRef>) {
                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                const lir::LirRvalue rhs{
                    lir::LirEnumVariantRef{node.enum_name, node.variant_name}
                };
                builder.emit_stmt(lir::LirAssign{lir::LirPlace::local(tmp), rhs, expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Struct initialization ─────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyStructInit>) {
                lir::LirStructInit si;
                si.type_name = node.type_name;
                for (const tyir::TyStructInitField& f : node.fields) {
                    const lir::LirOperand val = lower_expr(builder, f.value);
                    si.fields.push_back({f.name, val});
                }
                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                builder.emit_stmt(
                    lir::LirAssign{
                        lir::LirPlace::local(tmp), lir::LirRvalue{std::move(si)}, expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Borrow expression ────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyBorrow>) {
                const auto borrowed_place = lower_borrow_place(builder, node.rhs);
                const lir::LirPlace place = [&]() {
                    if (borrowed_place.has_value())
                        return *borrowed_place;

                    const lir::LirOperand rhs = lower_expr(builder, node.rhs);
                    const lir::LirLocalId owner_tmp =
                        materialize_operand(builder, rhs, node.rhs->ty, expr->span);
                    return lir::LirPlace::local(owner_tmp);
                }();

                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                builder.emit_stmt(
                    lir::LirAssign{
                        lir::LirPlace::local(tmp), lir::LirRvalue{lir::LirBorrow{place}},
                        expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Unary operation ───────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyUnary>) {
                const lir::LirOperand operand = lower_expr(builder, node.rhs);
                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                builder.emit_stmt(
                    lir::LirAssign{
                        lir::LirPlace::local(tmp),
                        lir::LirRvalue{lir::LirUnaryOp{node.op, operand}}, expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Binary operation ──────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyBinary>) {
                const lir::LirOperand lhs = lower_expr(builder, node.lhs);
                const lir::LirOperand rhs = lower_expr(builder, node.rhs);
                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                builder.emit_stmt(
                    lir::LirAssign{
                        lir::LirPlace::local(tmp),
                        lir::LirRvalue{lir::LirBinaryOp{node.op, lhs, rhs}}, expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Field access ──────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyFieldAccess>) {
                const lir::LirPlace field_place = [&]() {
                    if (const auto base_place = lower_borrow_place(builder, node.base);
                        base_place.has_value()) {
                        return base_place->project(node.field);
                    }

                    const lir::LirLocalId base_local = materialize_operand(
                        builder, lower_expr(builder, node.base), node.base->ty, expr->span);
                    return lir::LirPlace::field(base_local, node.field);
                }();

                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                builder.emit_stmt(
                    lir::LirAssign{
                        lir::LirPlace::local(tmp),
                        lir::LirRvalue{
                            lir::LirUse{operand_for_place(field_place, expr->ty, node.use_kind)}},
                        expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Function call ─────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyCall>) {
                std::vector<lir::LirOperand> arg_ops;
                arg_ops.reserve(node.args.size());
                for (const tyir::TyExprPtr& arg : node.args)
                    arg_ops.push_back(lower_expr(builder, arg));
                if (expr->ty.is_unit()) {
                    const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                    builder.emit_stmt(
                        lir::LirAssign{
                            lir::LirPlace::local(tmp),
                            lir::LirRvalue{lir::LirCall{node.fn_name, std::move(arg_ops)}},
                            expr->span});
                    return lir::LirOperand::from_const(lir::LirConst::unit());
                }

                const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                builder.emit_stmt(
                    lir::LirAssign{
                        lir::LirPlace::local(tmp),
                        lir::LirRvalue{lir::LirCall{node.fn_name, std::move(arg_ops)}},
                        expr->span});
                return operand_for_local(tmp, expr->ty);
            }

            // ── Nested block ──────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyBlockPtr>) {
                return lower_block(builder, node);
            }

            // ── If expression ─────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyIf>) {
                const lir::LirOperand cond_op = lower_expr(builder, node.condition);
                if (builder.is_terminated())
                    return lir::LirOperand::from_const(lir::LirConst::unit());

                const std::optional<lir::LirLocalId> result_local =
                    (!expr->ty.is_unit() && !expr->ty.is_never())
                        ? std::optional<lir::LirLocalId>{builder.alloc_local(expr->ty)}
                        : std::nullopt;

                const lir::LirBlockId then_id = builder.alloc_block();
                const lir::LirBlockId else_id =
                    node.else_branch.has_value() ? builder.alloc_block() : lir::kInvalidBlock;
                const lir::LirBlockId merge_id = builder.alloc_block();

                // Seal current block with SwitchBool.
                const lir::LirBlockId actual_else =
                    else_id != lir::kInvalidBlock ? else_id : merge_id;
                builder.seal_block(
                    lir::LirTerminator{
                        lir::LirSwitchBool{cond_op, then_id, actual_else},
                        expr->span
                });

                // Lower then-block.
                builder.set_current_block(then_id);
                const lir::LirOperand then_val = lower_block(builder, node.then_block);
                if (!builder.is_terminated()) {
                    if (result_local.has_value()) {
                        builder.emit_stmt(
                            lir::LirAssign{
                                lir::LirPlace::local(*result_local),
                                lir::LirRvalue{lir::LirUse{then_val}}, expr->span});
                    }
                    builder.seal_block(lir::LirTerminator{lir::LirJump{merge_id}, expr->span});
                }

                // Lower else-block (if any).
                if (node.else_branch.has_value()) {
                    builder.set_current_block(else_id);
                    const lir::LirOperand else_val = lower_expr(builder, *node.else_branch);
                    if (!builder.is_terminated()) {
                        if (result_local.has_value()) {
                            builder.emit_stmt(
                                lir::LirAssign{
                                    lir::LirPlace::local(*result_local),
                                    lir::LirRvalue{lir::LirUse{else_val}}, expr->span});
                        }
                        builder.seal_block(lir::LirTerminator{lir::LirJump{merge_id}, expr->span});
                    }
                }

                if (!builder.block_has_predecessor(merge_id))
                    return lir::LirOperand::from_const(lir::LirConst::unit());

                builder.set_current_block(merge_id);
                if (result_local.has_value())
                    return operand_for_local(*result_local, expr->ty);
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // ── Loop expression ───────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyLoop>) {
                const std::optional<lir::LirLocalId> result_local =
                    (!expr->ty.is_unit() && !expr->ty.is_never())
                        ? std::optional<lir::LirLocalId>{builder.alloc_local(expr->ty)}
                        : std::nullopt;
                const lir::LirBlockId header_id = builder.alloc_block();
                const lir::LirBlockId after_id = builder.alloc_block();

                // Jump from current block into the loop header.
                builder.seal_block(lir::LirTerminator{lir::LirJump{header_id}, expr->span});

                builder.set_current_block(header_id);
                builder.push_loop(
                    header_id, after_id,
                    result_local.has_value() ? *result_local : lir::kInvalidLocal);
                static_cast<void>(lower_block(builder, node.body));
                builder.pop_loop();

                // If the body doesn't diverge, jump back to header.
                if (!builder.is_terminated())
                    builder.seal_block(lir::LirTerminator{lir::LirJump{header_id}, expr->span});

                if (!builder.block_has_predecessor(after_id))
                    return lir::LirOperand::from_const(lir::LirConst::unit());

                builder.set_current_block(after_id);
                if (result_local.has_value())
                    return operand_for_local(*result_local, expr->ty);
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // ── While expression ──────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyWhile>) {
                const lir::LirBlockId cond_id = builder.alloc_block();
                const lir::LirBlockId body_id = builder.alloc_block();
                const lir::LirBlockId after_id = builder.alloc_block();

                builder.seal_block(lir::LirTerminator{lir::LirJump{cond_id}, expr->span});

                // Condition block.
                builder.set_current_block(cond_id);
                const lir::LirOperand cond_op = lower_expr(builder, node.condition);
                if (builder.is_terminated())
                    return lir::LirOperand::from_const(lir::LirConst::unit());
                builder.seal_block(
                    lir::LirTerminator{
                        lir::LirSwitchBool{cond_op, body_id, after_id},
                        expr->span
                });

                // Body block.
                builder.set_current_block(body_id);
                builder.push_loop(cond_id, after_id);
                static_cast<void>(lower_block(builder, node.body));
                builder.pop_loop();
                if (!builder.is_terminated())
                    builder.seal_block(lir::LirTerminator{lir::LirJump{cond_id}, expr->span});

                if (!builder.block_has_predecessor(after_id))
                    return lir::LirOperand::from_const(lir::LirConst::unit());

                builder.set_current_block(after_id);
                // `while` always yields `()`.
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // ── For expression ────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyFor>) {
                // Init clause (optional).
                builder.push_scope();
                if (node.init.has_value()) {
                    const tyir::TyForInit& init = *node.init;
                    const lir::LirOperand init_val = lower_expr(builder, init.init);
                    if (builder.is_terminated()) {
                        builder.pop_scope();
                        return lir::LirOperand::from_const(lir::LirConst::unit());
                    }
                    if (!init.discard) {
                        const lir::LirLocalId loc = builder.alloc_local(init.ty, init.name);
                        builder.emit_stmt(
                            lir::LirAssign{
                                lir::LirPlace::local(loc), lir::LirRvalue{lir::LirUse{init_val}},
                                init.span});
                        builder.bind(init.name, loc);
                    }
                }

                const lir::LirBlockId cond_id = builder.alloc_block();
                const lir::LirBlockId body_id = builder.alloc_block();
                const lir::LirBlockId step_id =
                    node.step.has_value() ? builder.alloc_block() : cond_id;
                const lir::LirBlockId after_id = builder.alloc_block();

                builder.seal_block(lir::LirTerminator{lir::LirJump{cond_id}, expr->span});

                // Condition block.
                builder.set_current_block(cond_id);
                if (node.condition.has_value()) {
                    const lir::LirOperand cond_op = lower_expr(builder, *node.condition);
                    if (builder.is_terminated()) {
                        builder.pop_scope();
                        return lir::LirOperand::from_const(lir::LirConst::unit());
                    }
                    builder.seal_block(
                        lir::LirTerminator{
                            lir::LirSwitchBool{cond_op, body_id, after_id},
                            expr->span
                    });
                } else {
                    // No condition → always enter the body.
                    builder.seal_block(lir::LirTerminator{lir::LirJump{body_id}, expr->span});
                }

                const lir::LirBlockId continue_target = node.step.has_value() ? step_id : cond_id;

                // Body block.
                builder.set_current_block(body_id);
                builder.push_loop(continue_target, after_id);
                static_cast<void>(lower_block(builder, node.body));
                builder.pop_loop();
                if (!builder.is_terminated())
                    builder.seal_block(
                        lir::LirTerminator{lir::LirJump{continue_target}, expr->span});

                // Step block (if present).
                if (node.step.has_value()) {
                    builder.set_current_block(step_id);
                    static_cast<void>(lower_expr(builder, *node.step));
                    if (!builder.is_terminated())
                        builder.seal_block(lir::LirTerminator{lir::LirJump{cond_id}, expr->span});
                }

                if (!builder.block_has_predecessor(after_id)) {
                    builder.pop_scope(); // init scope
                    return lir::LirOperand::from_const(lir::LirConst::unit());
                }

                builder.set_current_block(after_id);
                builder.emit_current_scope_drops(expr->span);
                builder.pop_scope();     // init scope
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // ── Break ─────────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyBreak>) {
                const auto& loop_ctx = builder.current_loop();
                if (node.value.has_value() && loop_ctx.break_value_local != lir::kInvalidLocal) {
                    const lir::LirOperand val = lower_expr(builder, *node.value);
                    if (builder.is_terminated())
                        return lir::LirOperand::from_const(lir::LirConst::unit());
                    builder.emit_stmt(
                        lir::LirAssign{
                            lir::LirPlace::local(loop_ctx.break_value_local),
                            lir::LirRvalue{lir::LirUse{val}}, expr->span});
                }
                builder.emit_scope_drops_to_depth(loop_ctx.preserved_scope_depth, expr->span);
                builder.seal_block(
                    lir::LirTerminator{lir::LirJump{loop_ctx.break_target}, expr->span});
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // ── Continue ──────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyContinue>) {
                const auto& loop_ctx = builder.current_loop();
                builder.emit_scope_drops_to_depth(loop_ctx.preserved_scope_depth, expr->span);
                builder.seal_block(
                    lir::LirTerminator{lir::LirJump{loop_ctx.continue_target}, expr->span});
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // ── Return ────────────────────────────────────────────────────────
            else if constexpr (std::is_same_v<N, tyir::TyReturn>) {
                std::optional<lir::LirOperand> ret_val;
                if (node.value.has_value()) {
                    const lir::LirOperand val = lower_expr(builder, *node.value);
                    if (builder.is_terminated())
                        return lir::LirOperand::from_const(lir::LirConst::unit());
                    if (builder.return_slot().has_value()) {
                        const lir::LirLocalId slot = *builder.return_slot();
                        builder.emit_stmt(
                            lir::LirAssign{
                                lir::LirPlace::local(slot), lir::LirRvalue{lir::LirUse{val}},
                                expr->span});
                        ret_val = operand_for_local(slot, builder.local_decl(slot).ty);
                    } else if (!(*node.value)->ty.is_unit()) {
                        ret_val = val;
                    }
                }
                builder.emit_scope_drops_to_depth(0, expr->span);
                builder.seal_block(lir::LirTerminator{lir::LirReturn{ret_val}, expr->span});
                return lir::LirOperand::from_const(lir::LirConst::unit());
            }

            // Unreachable — all variants handled.
            assert(false && "unhandled TyExpr variant in LIR builder");
            return lir::LirOperand::from_const(lir::LirConst::unit());
        },
        expr->node);
}

/// Lowers a `TyBlock` into the current basic block, emitting one or more
/// blocks as needed.  Returns an operand for the block's value (its tail
/// expression, or `()` if there is none).
[[nodiscard]] lir::LirOperand lower_block(FnBuilder& builder, const tyir::TyBlockPtr& block) {
    const std::optional<lir::LirLocalId> result_local =
        (block->tail.has_value() && !(*block->tail)->ty.is_unit() && !(*block->tail)->ty.is_never())
            ? std::optional<lir::LirLocalId>{builder.alloc_local((*block->tail)->ty)}
            : std::nullopt;

    builder.push_scope();

    for (const tyir::TyStmt& stmt : block->stmts) {
        std::visit(
            [&](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, tyir::TyLetStmt>) {
                    const lir::LirOperand val = lower_expr(builder, s.init);
                    if (builder.is_terminated())
                        return;
                    if (!s.discard) {
                        const lir::LirLocalId loc = builder.alloc_local(s.ty, s.name);
                        builder.emit_stmt(
                            lir::LirAssign{
                                lir::LirPlace::local(loc), lir::LirRvalue{lir::LirUse{val}},
                                s.span});
                        builder.bind(s.name, loc);
                    }
                } else {
                    // TyExprStmt — lower for side effects, discard value.
                    static_cast<void>(lower_expr(builder, s.expr));
                }
            },
            stmt);

        if (builder.is_terminated()) {
            builder.pop_scope();
            return lir::LirOperand::from_const(lir::LirConst::unit());
        }
    }

    if (!builder.is_terminated() && block->tail.has_value()) {
        const lir::LirOperand result = lower_expr(builder, *block->tail);
        if (!builder.is_terminated() && result_local.has_value()) {
            builder.emit_stmt(
                lir::LirAssign{
                    lir::LirPlace::local(*result_local), lir::LirRvalue{lir::LirUse{result}},
                    (*block->tail)->span});
        }
    }

    if (!builder.is_terminated())
        builder.emit_current_scope_drops(block->span);
    builder.pop_scope();

    if (builder.is_terminated())
        return lir::LirOperand::from_const(lir::LirConst::unit());
    if (result_local.has_value())
        return operand_for_local(*result_local, (*block->tail)->ty);
    return lir::LirOperand::from_const(lir::LirConst::unit());
}

// ─── Function lowering ────────────────────────────────────────────────────────

[[nodiscard]] lir::LirFnDef lower_fn(const tyir::TyFnDecl& ty_fn) {
    lir::LirFnDef fn;
    fn.name = ty_fn.name;
    fn.return_ty = ty_fn.return_ty;
    fn.span = ty_fn.span;
    fn.is_runtime = ty_fn.is_runtime;

    FnBuilder builder;

    // Allocate param locals first (they get the lowest IDs).
    builder.push_scope();
    for (const tyir::TyParam& p : ty_fn.params) {
        const lir::LirLocalId id = builder.alloc_local(p.ty, p.name);
        fn.params.push_back({id, p.name, p.ty, p.span});
        builder.bind(p.name, id);
    }
    if (ty_fn.return_ty.is_move_only())
        builder.set_return_slot(builder.alloc_hidden_local(ty_fn.return_ty));

    // Allocate the entry block.
    const lir::LirBlockId entry = builder.alloc_block();
    assert(entry == lir::kEntryBlock);
    builder.set_current_block(entry);

    // Lower the body.
    const lir::LirOperand body_val = lower_block(builder, ty_fn.body);

    // Seal the last (potentially only) block with a return.
    // Skip if the body already ended with a diverging terminator.
    if (!builder.is_terminated()) {
        std::optional<lir::LirOperand> ret_val;
        if (builder.return_slot().has_value()) {
            const lir::LirLocalId slot = *builder.return_slot();
            builder.emit_stmt(
                lir::LirAssign{
                    lir::LirPlace::local(slot), lir::LirRvalue{lir::LirUse{body_val}}, ty_fn.span});
            ret_val = operand_for_local(slot, ty_fn.return_ty);
        } else if (!ty_fn.return_ty.is_unit()) {
            ret_val = body_val;
        }
        builder.emit_scope_drops_to_depth(0, ty_fn.span);
        builder.seal_block(lir::LirTerminator{lir::LirReturn{ret_val}, ty_fn.span});
    }

    builder.pop_scope();
    builder.finish(fn);
    return fn;
}

// ─── Type declaration forwarding ─────────────────────────────────────────────

[[nodiscard]] lir::LirStructDecl forward_struct(const tyir::TyStructDecl& s) {
    lir::LirStructDecl out;
    out.name = s.name;
    out.is_zst = s.is_zst;
    out.span = s.span;
    for (const tyir::TyFieldDecl& f : s.fields)
        out.fields.push_back({f.name, f.ty, f.span});
    return out;
}

[[nodiscard]] lir::LirEnumDecl forward_enum(const tyir::TyEnumDecl& e) {
    lir::LirEnumDecl out;
    out.name = e.name;
    out.span = e.span;
    for (const tyir::TyEnumVariant& v : e.variants)
        out.variants.push_back({v.name, v.discriminant, v.span});
    return out;
}

} // namespace detail

// ─── Public entry point ───────────────────────────────────────────────────────

inline lir::LirProgram lower_program(const tyir::TyProgram& program) {
    lir::LirProgram out;

    for (const tyir::TyItem& item : program.items) {
        std::visit(
            [&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, tyir::TyStructDecl>) {
                    out.structs.push_back(detail::forward_struct(node));
                } else if constexpr (std::is_same_v<T, tyir::TyEnumDecl>) {
                    out.enums.push_back(detail::forward_enum(node));
                } else if constexpr (std::is_same_v<T, tyir::TyExternFnDecl>) {
                    lir::LirExternFnDecl ext;
                    ext.abi = node.abi;
                    ext.name = node.name;
                    ext.link_name = node.link_name;
                    ext.return_ty = node.return_ty;
                    ext.span = node.span;
                    ext.is_runtime = node.is_runtime;
                    for (std::size_t i = 0; i < node.params.size(); ++i) {
                        ext.params.push_back(
                            lir::LirParam{
                                .local = static_cast<lir::LirLocalId>(i),
                                .name = node.params[i].name,
                                .ty = node.params[i].ty,
                                .span = node.params[i].span,
                            });
                    }
                    out.extern_fns.push_back(std::move(ext));
                } else if constexpr (std::is_same_v<T, tyir::TyExternStructDecl>) {
                    lir::LirExternStructDecl ext_s;
                    ext_s.abi = node.abi;
                    ext_s.name = node.name;
                    ext_s.span = node.span;
                    out.extern_structs.push_back(std::move(ext_s));
                } else {
                    out.fns.push_back(detail::lower_fn(node));
                }
            },
            item);
    }

    return out;
}

} // namespace cstc::lir_builder

#endif // CICEST_COMPILER_CSTC_LIR_BUILDER_BUILDER_HPP
