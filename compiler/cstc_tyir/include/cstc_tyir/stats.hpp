#ifndef CICEST_COMPILER_CSTC_TYIR_STATS_HPP
#define CICEST_COMPILER_CSTC_TYIR_STATS_HPP

#include <cstddef>
#include <variant>

#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir {

/// Aggregated node statistics over a (typically folded) TyIR program.
///
/// These counters back the quantitative RQ2 evidence: how much of a program the
/// compile-time interpreter folded away, and how many calls remain residualized
/// behind runtime barriers.
struct ProgramNodeStats {
    /// Total TyIR nodes: every expression, statement, and block in every
    /// function body.
    std::size_t total_nodes = 0;
    /// Direct calls that remain as runtime-barrier residualizations
    /// (`call-residue: runtime-barrier` in printed TyIR).
    std::size_t residual_calls = 0;
};

namespace detail {

inline void count_expr_nodes(const TyExprPtr& expr, ProgramNodeStats& stats);
inline void count_block_nodes(const TyBlockPtr& block, ProgramNodeStats& stats);

/// Extracts the block to descend into for both `TyRuntimeBlock` expressions and
/// plain block expressions (returned by value to keep the borrow local).
[[nodiscard]] inline TyBlockPtr counted_block(const TyRuntimeBlock& expr) { return expr.body; }

[[nodiscard]] inline TyBlockPtr counted_block(const TyBlockPtr& block) { return block; }

inline void count_stmt_nodes(const TyStmt& stmt, ProgramNodeStats& stats) {
    ++stats.total_nodes;
    std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, TyLetStmt>)
                count_expr_nodes(node.init, stats);
            else
                count_expr_nodes(node.expr, stats);
        },
        stmt);
}

inline void count_expr_nodes(const TyExprPtr& expr, ProgramNodeStats& stats) {
    if (expr == nullptr)
        return;
    ++stats.total_nodes;
    std::visit(
        [&](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (
                std::is_same_v<Node, TyLiteral> || std::is_same_v<Node, LocalRef>
                || std::is_same_v<Node, EnumVariantRef> || std::is_same_v<Node, TyContinue>) {
                return;
            } else if constexpr (std::is_same_v<Node, TyStructInit>) {
                for (const TyStructInitField& field : node.fields)
                    count_expr_nodes(field.value, stats);
            } else if constexpr (
                std::is_same_v<Node, TyBorrow> || std::is_same_v<Node, TyUnary>) {
                count_expr_nodes(node.rhs, stats);
            } else if constexpr (std::is_same_v<Node, TyBinary>) {
                count_expr_nodes(node.lhs, stats);
                count_expr_nodes(node.rhs, stats);
            } else if constexpr (std::is_same_v<Node, TyFieldAccess>) {
                count_expr_nodes(node.base, stats);
            } else if constexpr (std::is_same_v<Node, TyCall>) {
                if (call_residue_for_expr(*expr, node) == CallResidue::RuntimeBarrier)
                    ++stats.residual_calls;
                for (const TyExprPtr& arg : node.args)
                    count_expr_nodes(arg, stats);
            } else if constexpr (std::is_same_v<Node, TyDeferredGenericCall>) {
                for (const TyExprPtr& arg : node.args)
                    count_expr_nodes(arg, stats);
            } else if constexpr (std::is_same_v<Node, TyDeclProbe>) {
                if (node.expr.has_value())
                    count_expr_nodes(*node.expr, stats);
            } else if constexpr (
                std::is_same_v<Node, TyRuntimeBlock> || std::is_same_v<Node, TyBlockPtr>) {
                count_block_nodes(counted_block(node), stats);
            } else if constexpr (std::is_same_v<Node, TyIf>) {
                count_expr_nodes(node.condition, stats);
                count_block_nodes(node.then_block, stats);
                if (node.else_branch.has_value())
                    count_expr_nodes(*node.else_branch, stats);
            } else if constexpr (std::is_same_v<Node, TyLoop>) {
                count_block_nodes(node.body, stats);
            } else if constexpr (std::is_same_v<Node, TyWhile>) {
                count_expr_nodes(node.condition, stats);
                count_block_nodes(node.body, stats);
            } else if constexpr (std::is_same_v<Node, TyFor>) {
                if (node.init.has_value())
                    count_expr_nodes(node.init->init, stats);
                if (node.condition.has_value())
                    count_expr_nodes(*node.condition, stats);
                if (node.step.has_value())
                    count_expr_nodes(*node.step, stats);
                count_block_nodes(node.body, stats);
            } else if constexpr (
                std::is_same_v<Node, TyBreak> || std::is_same_v<Node, TyReturn>) {
                if (node.value.has_value())
                    count_expr_nodes(*node.value, stats);
            }
        },
        expr->node);
}

inline void count_block_nodes(const TyBlockPtr& block, ProgramNodeStats& stats) {
    if (block == nullptr)
        return;
    ++stats.total_nodes;
    for (const TyStmt& stmt : block->stmts)
        count_stmt_nodes(stmt, stats);
    if (block->tail.has_value())
        count_expr_nodes(*block->tail, stats);
}

} // namespace detail

/// Counts TyIR nodes and residual runtime-barrier calls across all function
/// bodies of `program`.
[[nodiscard]] inline ProgramNodeStats count_program_nodes(const TyProgram& program) {
    ProgramNodeStats stats;
    for (const TyItem& item : program.items) {
        if (const auto* fn = std::get_if<TyFnDecl>(&item))
            detail::count_block_nodes(fn->body, stats);
    }
    return stats;
}

} // namespace cstc::tyir

#endif // CICEST_COMPILER_CSTC_TYIR_STATS_HPP
