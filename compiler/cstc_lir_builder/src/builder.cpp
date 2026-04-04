#include <cstc_lir_builder/builder.hpp>

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

[[nodiscard]] static lir::LirOperand lower_expr(FnBuilder& builder, const tyir::TyExprPtr& expr);
[[nodiscard]] static lir::LirOperand lower_block(FnBuilder& builder, const tyir::TyBlockPtr& block);

[[noreturn]] static void unsupported_projected_move() {
    assert(false && "moves from projected LIR places are not supported");
    std::abort();
}

[[nodiscard]] static lir::LirOperand operand_for_local(lir::LirLocalId id, const tyir::Ty& ty) {
    const lir::LirPlace place = lir::LirPlace::local(id);
    if (ty.is_move_only())
        return lir::LirOperand::move(place);
    return lir::LirOperand::copy(place);
}

[[nodiscard]] static lir::LirOperand terminated_operand() {
    return lir::LirOperand::from_const(lir::LirConst::unit());
}

[[nodiscard]] static lir::LirOperand operand_for_place(
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

[[nodiscard]] static lir::LirLocalId materialize_operand(
    FnBuilder& builder, lir::LirOperand operand, const tyir::Ty& ty, cstc::span::SourceSpan span) {
    const lir::LirLocalId tmp = builder.alloc_local(ty);
    builder.emit_stmt(
        lir::LirAssign{
            lir::LirPlace::local(tmp), lir::LirRvalue{lir::LirUse{std::move(operand)}}, span});
    return tmp;
}

[[nodiscard]] static std::optional<lir::LirPlace>
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
[[nodiscard]] static lir::LirOperand lower_expr(FnBuilder& builder, const tyir::TyExprPtr& expr) {
    return std::visit(
        [&](const auto& node) -> lir::LirOperand {
            using N = std::decay_t<decltype(node)>;

            // ── Literals ──────────────────────────────────────────────────────
            if constexpr (std::is_same_v<N, tyir::TyLiteral>) {
                lir::LirConst c{};
                switch (node.kind) {
                case tyir::TyLiteral::Kind::Num: c = lir::LirConst::num(node.symbol); break;
                case tyir::TyLiteral::Kind::Str: c = lir::LirConst::str(node.symbol); break;
                case tyir::TyLiteral::Kind::OwnedStr:
                    c = lir::LirConst::owned_str(node.symbol);
                    break;
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
                    if (builder.is_terminated())
                        return terminated_operand();
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
                    if (builder.is_terminated())
                        return lir::LirPlace{};
                    const lir::LirLocalId owner_tmp =
                        materialize_operand(builder, rhs, node.rhs->ty, expr->span);
                    return lir::LirPlace::local(owner_tmp);
                }();
                if (builder.is_terminated())
                    return terminated_operand();

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
                if (builder.is_terminated())
                    return terminated_operand();
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
                if (builder.is_terminated())
                    return terminated_operand();
                const lir::LirOperand rhs = lower_expr(builder, node.rhs);
                if (builder.is_terminated())
                    return terminated_operand();
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

                    const lir::LirOperand base_operand = lower_expr(builder, node.base);
                    if (builder.is_terminated())
                        return lir::LirPlace{};
                    const lir::LirLocalId base_local =
                        materialize_operand(builder, base_operand, node.base->ty, expr->span);
                    return lir::LirPlace::field(base_local, node.field);
                }();
                if (builder.is_terminated())
                    return terminated_operand();

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
                for (const tyir::TyExprPtr& arg : node.args) {
                    arg_ops.push_back(lower_expr(builder, arg));
                    if (builder.is_terminated())
                        return terminated_operand();
                }
                if (expr->ty.is_never()) {
                    const lir::LirLocalId tmp = builder.alloc_local(expr->ty);
                    builder.emit_stmt(
                        lir::LirAssign{
                            lir::LirPlace::local(tmp),
                            lir::LirRvalue{lir::LirCall{node.fn_name, std::move(arg_ops)}},
                            expr->span});
                    builder.seal_block(lir::LirTerminator{lir::LirUnreachable{}, expr->span});
                    return terminated_operand();
                }
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
[[nodiscard]] static lir::LirOperand
    lower_block(FnBuilder& builder, const tyir::TyBlockPtr& block) {
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

[[nodiscard]] static lir::LirFnDef lower_fn(const tyir::TyFnDecl& ty_fn) {
    lir::LirFnDef fn;
    fn.name = ty_fn.name;
    fn.return_ty = ty_fn.return_ty;
    fn.span = ty_fn.span;

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

[[nodiscard]] static lir::LirStructDecl forward_struct(const tyir::TyStructDecl& s) {
    lir::LirStructDecl out;
    out.name = s.name;
    out.is_zst = s.is_zst;
    out.span = s.span;
    for (const tyir::TyFieldDecl& f : s.fields)
        out.fields.push_back({f.name, f.ty, f.span});
    return out;
}

[[nodiscard]] static lir::LirEnumDecl forward_enum(const tyir::TyEnumDecl& e) {
    lir::LirEnumDecl out;
    out.name = e.name;
    out.span = e.span;
    for (const tyir::TyEnumVariant& v : e.variants)
        out.variants.push_back({v.name, v.discriminant, v.span});
    return out;
}

using TypeSubstitution =
    std::unordered_map<cstc::symbol::Symbol, tyir::Ty, cstc::symbol::SymbolHash>;

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

[[nodiscard]] static std::string sanitize_symbol_fragment(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch) != 0 || ch == '_') {
            out.push_back(static_cast<char>(ch));
            continue;
        }

        const auto hex_digit = [](unsigned char value) {
            return static_cast<char>(value < 10U ? ('0' + value) : ('a' + (value - 10U)));
        };
        out += "_x";
        out.push_back(hex_digit(static_cast<unsigned char>((ch >> 4U) & 0x0FU)));
        out.push_back(hex_digit(static_cast<unsigned char>(ch & 0x0FU)));
    }
    return out;
}

[[nodiscard]] static std::string encode_symbol(cstc::symbol::Symbol symbol) {
    const std::string text = symbol.is_valid() ? std::string(symbol.as_str()) : "invalid";
    return std::to_string(text.size()) + "_" + sanitize_symbol_fragment(text);
}

[[nodiscard]] static std::string encode_type(const tyir::Ty& ty) {
    switch (ty.kind) {
    case tyir::TyKind::Ref:
        return ty.pointee != nullptr ? "R" + encode_type(*ty.pointee) : "Runknown";
    case tyir::TyKind::Unit: return "U";
    case tyir::TyKind::Num: return "N";
    case tyir::TyKind::Str: return "S";
    case tyir::TyKind::Bool: return "B";
    case tyir::TyKind::Never: return "X";
    case tyir::TyKind::Named: {
        std::string out = "T" + encode_symbol(ty.name);
        if (ty.is_runtime)
            out += "_rt";
        if (!ty.generic_args.empty()) {
            out += "_g" + std::to_string(ty.generic_args.size());
            for (const tyir::Ty& arg : ty.generic_args)
                out += "_" + encode_type(arg);
        }
        return out;
    }
    }

    std::unreachable();
}

[[nodiscard]] static std::string instantiation_cache_key(
    cstc::symbol::Symbol base_name, const std::vector<tyir::Ty>& generic_args) {
    std::string key = std::string(base_name.as_str()) + "<";
    for (std::size_t index = 0; index < generic_args.size(); ++index) {
        if (index > 0)
            key += ",";
        key += encode_type(generic_args[index]);
    }
    key += ">";
    return key;
}

[[nodiscard]] static cstc::symbol::Symbol make_instantiated_name(
    cstc::symbol::Symbol base_name, const std::vector<tyir::Ty>& generic_args) {
    std::string mangled(base_name.as_str());
    mangled += "$inst";
    for (const tyir::Ty& arg : generic_args)
        mangled += "$" + encode_type(arg);
    return cstc::symbol::Symbol::intern(mangled);
}

class Monomorphizer {
public:
    explicit Monomorphizer(const tyir::TyProgram& program)
        : program_(program) {}

    [[nodiscard]] lir::LirProgram run() {
        collect_generic_items();

        for (const tyir::TyItem& item : program_.items) {
            std::visit(
                [&](const auto& node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, tyir::TyStructDecl>) {
                        if (node.generic_params.empty())
                            emit_struct_decl(node, {}, node.name);
                    } else if constexpr (std::is_same_v<T, tyir::TyEnumDecl>) {
                        if (node.generic_params.empty())
                            emit_enum_decl(node, {}, node.name);
                    } else if constexpr (std::is_same_v<T, tyir::TyFnDecl>) {
                        if (node.generic_params.empty())
                            emit_fn_decl(node, {}, node.name);
                    } else if constexpr (std::is_same_v<T, tyir::TyExternFnDecl>) {
                        emit_extern_fn_decl(node);
                    } else if constexpr (std::is_same_v<T, tyir::TyExternStructDecl>) {
                        out_.extern_structs.push_back(
                            lir::LirExternStructDecl{node.abi, node.name, node.span});
                    }
                },
                item);
        }

        return std::move(out_);
    }

private:
    void collect_generic_items() {
        for (const tyir::TyItem& item : program_.items) {
            std::visit(
                [&](const auto& node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, tyir::TyStructDecl>) {
                        if (!node.generic_params.empty())
                            generic_structs_.emplace(node.name, &node);
                    } else if constexpr (std::is_same_v<T, tyir::TyEnumDecl>) {
                        if (!node.generic_params.empty())
                            generic_enums_.emplace(node.name, &node);
                    } else if constexpr (std::is_same_v<T, tyir::TyFnDecl>) {
                        if (!node.generic_params.empty())
                            generic_fns_.emplace(node.name, &node);
                    }
                },
                item);
        }
    }

    [[nodiscard]] TypeSubstitution build_substitution(
        const std::vector<cstc::ast::GenericParam>& generic_params,
        const std::vector<tyir::Ty>& generic_args) const {
        assert(generic_params.size() == generic_args.size());
        TypeSubstitution subst;
        subst.reserve(generic_params.size());
        for (std::size_t index = 0; index < generic_params.size(); ++index)
            subst.emplace(generic_params[index].name, generic_args[index]);
        return subst;
    }

    [[nodiscard]] tyir::Ty rewrite_type(const tyir::Ty& ty, const TypeSubstitution& subst) {
        tyir::Ty rewritten = apply_substitution(ty, subst);
        if (rewritten.kind == tyir::TyKind::Ref) {
            if (rewritten.pointee != nullptr)
                rewritten.pointee =
                    std::make_shared<tyir::Ty>(rewrite_type(*rewritten.pointee, subst));
            return rewritten;
        }

        if (rewritten.kind != tyir::TyKind::Named)
            return rewritten;

        std::vector<tyir::Ty> concrete_args;
        concrete_args.reserve(rewritten.generic_args.size());
        for (const tyir::Ty& arg : rewritten.generic_args)
            concrete_args.push_back(rewrite_type(arg, subst));

        if (const auto it = generic_structs_.find(rewritten.name); it != generic_structs_.end()) {
            const cstc::symbol::Symbol instantiated_name =
                ensure_struct_instantiation(*it->second, concrete_args);
            rewritten.name = instantiated_name;
            rewritten.generic_args.clear();
            return rewritten;
        }

        if (const auto it = generic_enums_.find(rewritten.name); it != generic_enums_.end()) {
            const cstc::symbol::Symbol instantiated_name =
                ensure_enum_instantiation(*it->second, concrete_args);
            rewritten.name = instantiated_name;
            rewritten.generic_args.clear();
            return rewritten;
        }

        rewritten.generic_args = std::move(concrete_args);
        return rewritten;
    }

    [[nodiscard]] tyir::TyExprPtr
        rewrite_expr(const tyir::TyExprPtr& expr, const TypeSubstitution& subst) {
        return std::visit(
            [&](const auto& node) -> tyir::TyExprPtr {
                using T = std::decay_t<decltype(node)>;
                const tyir::Ty rewritten_ty = rewrite_type(expr->ty, subst);

                if constexpr (
                    std::is_same_v<T, tyir::TyLiteral> || std::is_same_v<T, tyir::LocalRef>
                    || std::is_same_v<T, tyir::TyContinue>) {
                    return tyir::make_ty_expr(expr->span, node, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::EnumVariantRef>) {
                    tyir::EnumVariantRef rewritten = node;
                    if (const auto it = generic_enums_.find(node.enum_name);
                        it != generic_enums_.end()) {
                        std::vector<tyir::Ty> concrete_args;
                        concrete_args.reserve(expr->ty.generic_args.size());
                        for (const tyir::Ty& arg : expr->ty.generic_args)
                            concrete_args.push_back(rewrite_type(arg, subst));
                        rewritten.enum_name = ensure_enum_instantiation(*it->second, concrete_args);
                    }
                    return tyir::make_ty_expr(expr->span, rewritten, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyStructInit>) {
                    tyir::TyStructInit rewritten;
                    std::vector<tyir::Ty> concrete_args;
                    concrete_args.reserve(node.generic_args.size());
                    for (const tyir::Ty& arg : node.generic_args)
                        concrete_args.push_back(rewrite_type(arg, subst));

                    rewritten.type_name = node.type_name;
                    if (const auto it = generic_structs_.find(node.type_name);
                        it != generic_structs_.end())
                        rewritten.type_name =
                            ensure_struct_instantiation(*it->second, concrete_args);
                    rewritten.fields.reserve(node.fields.size());
                    for (const tyir::TyStructInitField& field : node.fields) {
                        rewritten.fields.push_back(
                            tyir::TyStructInitField{
                                field.name,
                                rewrite_expr(field.value, subst),
                                field.span,
                            });
                    }
                    return tyir::make_ty_expr(expr->span, rewritten, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyBorrow>) {
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyBorrow{rewrite_expr(node.rhs, subst)}, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyUnary>) {
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyUnary{node.op, rewrite_expr(node.rhs, subst)},
                        rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyBinary>) {
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyBinary{
                            node.op, rewrite_expr(node.lhs, subst), rewrite_expr(node.rhs, subst)},
                        rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyFieldAccess>) {
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyFieldAccess{
                            rewrite_expr(node.base, subst), node.field, node.use_kind},
                        rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyCall>) {
                    tyir::TyCall rewritten;
                    std::vector<tyir::Ty> concrete_args;
                    concrete_args.reserve(node.generic_args.size());
                    for (const tyir::Ty& arg : node.generic_args)
                        concrete_args.push_back(rewrite_type(arg, subst));

                    rewritten.fn_name = node.fn_name;
                    if (const auto it = generic_fns_.find(node.fn_name); it != generic_fns_.end())
                        rewritten.fn_name = ensure_fn_instantiation(*it->second, concrete_args);
                    rewritten.args.reserve(node.args.size());
                    for (const tyir::TyExprPtr& arg : node.args)
                        rewritten.args.push_back(rewrite_expr(arg, subst));
                    return tyir::make_ty_expr(expr->span, rewritten, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyBlockPtr>) {
                    return tyir::make_ty_expr(expr->span, rewrite_block(node, subst), rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyIf>) {
                    std::optional<tyir::TyExprPtr> else_branch;
                    if (node.else_branch.has_value())
                        else_branch = rewrite_expr(*node.else_branch, subst);
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyIf{
                            rewrite_expr(node.condition, subst),
                            rewrite_block(node.then_block, subst), std::move(else_branch)},
                        rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyLoop>) {
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyLoop{rewrite_block(node.body, subst)}, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyWhile>) {
                    return tyir::make_ty_expr(
                        expr->span,
                        tyir::TyWhile{
                            rewrite_expr(node.condition, subst), rewrite_block(node.body, subst)},
                        rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyFor>) {
                    tyir::TyFor rewritten;
                    if (node.init.has_value()) {
                        rewritten.init = tyir::TyForInit{
                            node.init->discard,
                            node.init->name,
                            rewrite_type(node.init->ty, subst),
                            rewrite_expr(node.init->init, subst),
                            node.init->span,
                        };
                    }
                    if (node.condition.has_value())
                        rewritten.condition = rewrite_expr(*node.condition, subst);
                    if (node.step.has_value())
                        rewritten.step = rewrite_expr(*node.step, subst);
                    rewritten.body = rewrite_block(node.body, subst);
                    return tyir::make_ty_expr(expr->span, rewritten, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyBreak>) {
                    std::optional<tyir::TyExprPtr> value;
                    if (node.value.has_value())
                        value = rewrite_expr(*node.value, subst);
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyBreak{std::move(value)}, rewritten_ty);
                } else if constexpr (std::is_same_v<T, tyir::TyReturn>) {
                    std::optional<tyir::TyExprPtr> value;
                    if (node.value.has_value())
                        value = rewrite_expr(*node.value, subst);
                    return tyir::make_ty_expr(
                        expr->span, tyir::TyReturn{std::move(value)}, rewritten_ty);
                }

                assert(false && "unhandled TyExpr variant during monomorphization");
                return tyir::make_ty_expr(expr->span, tyir::TyLiteral{}, rewritten_ty);
            },
            expr->node);
    }

    [[nodiscard]] tyir::TyBlockPtr
        rewrite_block(const tyir::TyBlockPtr& block, const TypeSubstitution& subst) {
        auto rewritten = std::make_shared<tyir::TyBlock>();
        rewritten->stmts.reserve(block->stmts.size());
        for (const tyir::TyStmt& stmt : block->stmts) {
            rewritten->stmts.push_back(
                std::visit(
                    [&](const auto& node) -> tyir::TyStmt {
                        using T = std::decay_t<decltype(node)>;
                        if constexpr (std::is_same_v<T, tyir::TyLetStmt>) {
                            return tyir::TyLetStmt{
                                node.discard,
                                node.name,
                                rewrite_type(node.ty, subst),
                                rewrite_expr(node.init, subst),
                                node.span,
                            };
                        } else {
                            return tyir::TyExprStmt{rewrite_expr(node.expr, subst), node.span};
                        }
                    },
                    stmt));
        }
        if (block->tail.has_value())
            rewritten->tail = rewrite_expr(*block->tail, subst);
        rewritten->ty = rewrite_type(block->ty, subst);
        rewritten->span = block->span;
        return rewritten;
    }

    void emit_struct_decl(
        const tyir::TyStructDecl& decl, const TypeSubstitution& subst,
        cstc::symbol::Symbol emitted_name) {
        const std::string emitted_key(emitted_name.as_str());
        if (!emitted_structs_.insert(emitted_key).second)
            return;

        tyir::TyStructDecl concrete;
        concrete.name = emitted_name;
        concrete.is_zst = decl.is_zst;
        concrete.span = decl.span;
        concrete.fields.reserve(decl.fields.size());
        for (const tyir::TyFieldDecl& field : decl.fields) {
            concrete.fields.push_back(
                tyir::TyFieldDecl{field.name, rewrite_type(field.ty, subst), field.span});
        }
        out_.structs.push_back(forward_struct(concrete));
    }

    void emit_enum_decl(
        const tyir::TyEnumDecl& decl, const TypeSubstitution& subst,
        cstc::symbol::Symbol emitted_name) {
        const std::string emitted_key(emitted_name.as_str());
        if (!emitted_enums_.insert(emitted_key).second)
            return;

        tyir::TyEnumDecl concrete;
        concrete.name = emitted_name;
        concrete.span = decl.span;
        concrete.variants = decl.variants;
        concrete.lowered_where_clause.clear();
        concrete.where_clause.clear();
        (void)subst;
        out_.enums.push_back(forward_enum(concrete));
    }

    void emit_fn_decl(
        const tyir::TyFnDecl& decl, const TypeSubstitution& subst,
        cstc::symbol::Symbol emitted_name) {
        const std::string emitted_key(emitted_name.as_str());
        if (!emitted_fns_.insert(emitted_key).second)
            return;

        tyir::TyFnDecl concrete;
        concrete.name = emitted_name;
        concrete.return_ty = rewrite_type(decl.return_ty, subst);
        concrete.body = rewrite_block(decl.body, subst);
        concrete.span = decl.span;
        concrete.is_runtime = decl.is_runtime;
        concrete.params.reserve(decl.params.size());
        for (const tyir::TyParam& param : decl.params) {
            concrete.params.push_back(
                tyir::TyParam{param.name, rewrite_type(param.ty, subst), param.span});
        }
        out_.fns.push_back(lower_fn(concrete));
    }

    void emit_extern_fn_decl(const tyir::TyExternFnDecl& node) {
        lir::LirExternFnDecl ext;
        ext.abi = node.abi;
        ext.name = node.name;
        ext.link_name = node.link_name;
        ext.return_ty = rewrite_type(node.return_ty, {});
        ext.span = node.span;
        for (std::size_t i = 0; i < node.params.size(); ++i) {
            ext.params.push_back(
                lir::LirParam{
                    .local = static_cast<lir::LirLocalId>(i),
                    .name = node.params[i].name,
                    .ty = rewrite_type(node.params[i].ty, {}),
                    .span = node.params[i].span,
                });
        }
        out_.extern_fns.push_back(std::move(ext));
    }

    [[nodiscard]] cstc::symbol::Symbol ensure_struct_instantiation(
        const tyir::TyStructDecl& decl, const std::vector<tyir::Ty>& generic_args) {
        const std::string key = instantiation_cache_key(decl.name, generic_args);
        if (const auto it = instantiated_structs_.find(key); it != instantiated_structs_.end())
            return it->second;

        const cstc::symbol::Symbol emitted_name = make_instantiated_name(decl.name, generic_args);
        instantiated_structs_.emplace(key, emitted_name);
        emit_struct_decl(decl, build_substitution(decl.generic_params, generic_args), emitted_name);
        return emitted_name;
    }

    [[nodiscard]] cstc::symbol::Symbol ensure_enum_instantiation(
        const tyir::TyEnumDecl& decl, const std::vector<tyir::Ty>& generic_args) {
        const std::string key = instantiation_cache_key(decl.name, generic_args);
        if (const auto it = instantiated_enums_.find(key); it != instantiated_enums_.end())
            return it->second;

        const cstc::symbol::Symbol emitted_name = make_instantiated_name(decl.name, generic_args);
        instantiated_enums_.emplace(key, emitted_name);
        emit_enum_decl(decl, build_substitution(decl.generic_params, generic_args), emitted_name);
        return emitted_name;
    }

    [[nodiscard]] cstc::symbol::Symbol ensure_fn_instantiation(
        const tyir::TyFnDecl& decl, const std::vector<tyir::Ty>& generic_args) {
        const std::string key = instantiation_cache_key(decl.name, generic_args);
        if (const auto it = instantiated_fns_.find(key); it != instantiated_fns_.end())
            return it->second;

        const cstc::symbol::Symbol emitted_name = make_instantiated_name(decl.name, generic_args);
        instantiated_fns_.emplace(key, emitted_name);
        emit_fn_decl(decl, build_substitution(decl.generic_params, generic_args), emitted_name);
        return emitted_name;
    }

    const tyir::TyProgram& program_;
    lir::LirProgram out_;

    std::unordered_map<cstc::symbol::Symbol, const tyir::TyStructDecl*, cstc::symbol::SymbolHash>
        generic_structs_;
    std::unordered_map<cstc::symbol::Symbol, const tyir::TyEnumDecl*, cstc::symbol::SymbolHash>
        generic_enums_;
    std::unordered_map<cstc::symbol::Symbol, const tyir::TyFnDecl*, cstc::symbol::SymbolHash>
        generic_fns_;

    std::unordered_map<std::string, cstc::symbol::Symbol> instantiated_structs_;
    std::unordered_map<std::string, cstc::symbol::Symbol> instantiated_enums_;
    std::unordered_map<std::string, cstc::symbol::Symbol> instantiated_fns_;

    std::unordered_set<std::string> emitted_structs_;
    std::unordered_set<std::string> emitted_enums_;
    std::unordered_set<std::string> emitted_fns_;
};

} // namespace detail

// ─── Public entry point ───────────────────────────────────────────────────────

lir::LirProgram lower_program(const tyir::TyProgram& program) {
    detail::Monomorphizer monomorphizer(program);
    return monomorphizer.run();
}

} // namespace cstc::lir_builder
