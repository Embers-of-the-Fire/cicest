#ifndef CICEST_COMPILER_CSTC_TYIR_PRINTER_HPP
#define CICEST_COMPILER_CSTC_TYIR_PRINTER_HPP

/// @file printer.hpp
/// @brief Human-readable tree formatter for TyIR programs.
///
/// Usage:
/// @code
///   cstc::symbol::SymbolSession session;
///   const auto result = cstc::lower::lower_program(ast_program);
///   if (result) {
///       std::cout << cstc::tyir::format_program(*result);
///   }
/// @endcode
///
/// The output format mirrors the AST printer but annotates every expression
/// and block node with its resolved `Ty` (e.g. `TyBinary(+): num`).

#include <string>

#include <cstc_ast/printer.hpp>
#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir {

/// Renders a human-readable indented tree view of the typed program.
///
/// Symbol ids are resolved through the current session's global symbol table.
[[nodiscard]] inline std::string format_program(const TyProgram& program);

} // namespace cstc::tyir

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace cstc::tyir {

namespace detail {

inline void indent(std::ostringstream& out, std::size_t level) {
    out << std::string(level * 2, ' ');
}

[[nodiscard]] inline std::string_view unary_name(cstc::ast::UnaryOp op) {
    switch (op) {
    case cstc::ast::UnaryOp::Borrow: return "&";
    case cstc::ast::UnaryOp::Negate: return "-";
    case cstc::ast::UnaryOp::Not: return "!";
    }
    return "?";
}

[[nodiscard]] inline std::string_view use_kind_suffix(ValueUseKind use_kind) {
    switch (use_kind) {
    case ValueUseKind::Copy: return "";
    case ValueUseKind::Move: return " [move]";
    case ValueUseKind::Borrow: return " [borrow]";
    }
    return "";
}

[[nodiscard]] inline std::string_view binary_name(cstc::ast::BinaryOp op) {
    switch (op) {
    case cstc::ast::BinaryOp::Add: return "+";
    case cstc::ast::BinaryOp::Sub: return "-";
    case cstc::ast::BinaryOp::Mul: return "*";
    case cstc::ast::BinaryOp::Div: return "/";
    case cstc::ast::BinaryOp::Mod: return "%";
    case cstc::ast::BinaryOp::Eq: return "==";
    case cstc::ast::BinaryOp::Ne: return "!=";
    case cstc::ast::BinaryOp::Lt: return "<";
    case cstc::ast::BinaryOp::Le: return "<=";
    case cstc::ast::BinaryOp::Gt: return ">";
    case cstc::ast::BinaryOp::Ge: return ">=";
    case cstc::ast::BinaryOp::And: return "&&";
    case cstc::ast::BinaryOp::Or: return "||";
    }
    return "?";
}

// Forward declarations for mutual recursion.
inline void print_ty_expr(std::ostringstream& out, const TyExprPtr& expr, std::size_t level);
inline void print_ty_block(std::ostringstream& out, const TyBlockPtr& block, std::size_t level);

inline void print_ty_block(std::ostringstream& out, const TyBlockPtr& block, std::size_t level) {
    indent(out, level);
    out << "TyBlock: " << block->ty.display() << "\n";

    for (const TyStmt& stmt : block->stmts) {
        std::visit(
            [&](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, TyLetStmt>) {
                    indent(out, level + 1);
                    if (s.discard)
                        out << "Let _: " << s.ty.display() << " =\n";
                    else
                        out << "Let " << s.name.as_str() << ": " << s.ty.display() << " =\n";
                    print_ty_expr(out, s.init, level + 2);
                } else {
                    indent(out, level + 1);
                    out << "TyExprStmt\n";
                    print_ty_expr(out, s.expr, level + 2);
                }
            },
            stmt);
    }

    if (block->tail.has_value()) {
        indent(out, level + 1);
        out << "Tail\n";
        print_ty_expr(out, *block->tail, level + 2);
    }
}

inline void print_ty_expr(std::ostringstream& out, const TyExprPtr& expr, std::size_t level) {
    std::visit(
        [&](const auto& node) {
            using N = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<N, TyLiteral>) {
                indent(out, level);
                switch (node.kind) {
                case TyLiteral::Kind::Num:
                case TyLiteral::Kind::Str:
                    out << "TyLiteral(" << node.symbol.as_str() << "): " << expr->ty.display()
                        << "\n";
                    break;
                case TyLiteral::Kind::OwnedStr:
                    out << "TyLiteral(owned " << node.symbol.as_str() << "): " << expr->ty.display()
                        << "\n";
                    break;
                case TyLiteral::Kind::Bool:
                    out << "TyLiteral(" << (node.bool_value ? "true" : "false")
                        << "): " << expr->ty.display() << "\n";
                    break;
                case TyLiteral::Kind::Unit:
                    out << "TyLiteral(()): " << expr->ty.display() << "\n";
                    break;
                }
            } else if constexpr (std::is_same_v<N, LocalRef>) {
                indent(out, level);
                out << "TyLocal(" << node.name.as_str() << ")" << use_kind_suffix(node.use_kind)
                    << ": " << expr->ty.display() << "\n";
            } else if constexpr (std::is_same_v<N, EnumVariantRef>) {
                indent(out, level);
                out << "TyVariant(" << node.enum_name.as_str() << "::" << node.variant_name.as_str()
                    << "): " << expr->ty.display() << "\n";
            } else if constexpr (std::is_same_v<N, TyStructInit>) {
                indent(out, level);
                out << "TyStructInit(" << node.type_name.as_str() << "): " << expr->ty.display()
                    << "\n";
                if (!node.generic_args.empty()) {
                    indent(out, level + 1);
                    out << "GenericArgs\n";
                    for (const Ty& arg : node.generic_args) {
                        indent(out, level + 2);
                        out << arg.display() << "\n";
                    }
                }
                for (const TyStructInitField& field : node.fields) {
                    indent(out, level + 1);
                    out << field.name.as_str() << ":\n";
                    print_ty_expr(out, field.value, level + 2);
                }
            } else if constexpr (std::is_same_v<N, TyBorrow>) {
                indent(out, level);
                out << "TyBorrow: " << expr->ty.display() << "\n";
                print_ty_expr(out, node.rhs, level + 1);
            } else if constexpr (std::is_same_v<N, TyUnary>) {
                indent(out, level);
                out << "TyUnary(" << unary_name(node.op) << "): " << expr->ty.display() << "\n";
                print_ty_expr(out, node.rhs, level + 1);
            } else if constexpr (std::is_same_v<N, TyBinary>) {
                indent(out, level);
                out << "TyBinary(" << binary_name(node.op) << "): " << expr->ty.display() << "\n";
                print_ty_expr(out, node.lhs, level + 1);
                print_ty_expr(out, node.rhs, level + 1);
            } else if constexpr (std::is_same_v<N, TyFieldAccess>) {
                indent(out, level);
                out << "TyFieldAccess(." << node.field.as_str() << ")"
                    << use_kind_suffix(node.use_kind) << ": " << expr->ty.display() << "\n";
                print_ty_expr(out, node.base, level + 1);
            } else if constexpr (std::is_same_v<N, TyCall>) {
                indent(out, level);
                out << "TyCall(" << node.fn_name.as_str() << "): " << expr->ty.display() << "\n";
                if (!node.generic_args.empty()) {
                    indent(out, level + 1);
                    out << "GenericArgs\n";
                    for (const Ty& arg : node.generic_args) {
                        indent(out, level + 2);
                        out << arg.display() << "\n";
                    }
                }
                for (const TyExprPtr& arg : node.args) {
                    indent(out, level + 1);
                    out << "Arg\n";
                    print_ty_expr(out, arg, level + 2);
                }
            } else if constexpr (std::is_same_v<N, TyDeferredGenericCall>) {
                indent(out, level);
                out << "TyDeferredGenericCall(" << node.fn_name.as_str()
                    << "): " << expr->ty.display() << "\n";
                if (!node.generic_args.empty()) {
                    indent(out, level + 1);
                    out << "GenericArgs\n";
                    for (const std::optional<Ty>& arg : node.generic_args) {
                        indent(out, level + 2);
                        out << (arg.has_value() ? arg->display() : "_") << "\n";
                    }
                }
                for (const TyExprPtr& arg : node.args) {
                    indent(out, level + 1);
                    out << "Arg\n";
                    print_ty_expr(out, arg, level + 2);
                }
            } else if constexpr (std::is_same_v<N, TyDeclProbe>) {
                indent(out, level);
                out << "TyDeclProbe: " << expr->ty.display();
                if (node.is_invalid)
                    out << " [invalid]";
                out << "\n";
                if (node.expr.has_value())
                    print_ty_expr(out, *node.expr, level + 1);
            } else if constexpr (std::is_same_v<N, TyRuntimeBlock>) {
                indent(out, level);
                out << "TyRuntimeBlock: " << expr->ty.display() << "\n";
                print_ty_block(out, node.body, level + 1);
            } else if constexpr (std::is_same_v<N, TyBlockPtr>) {
                print_ty_block(out, node, level);
            } else if constexpr (std::is_same_v<N, TyIf>) {
                indent(out, level);
                out << "TyIf: " << expr->ty.display() << "\n";
                indent(out, level + 1);
                out << "Condition\n";
                print_ty_expr(out, node.condition, level + 2);
                indent(out, level + 1);
                out << "Then\n";
                print_ty_block(out, node.then_block, level + 2);
                if (node.else_branch.has_value()) {
                    indent(out, level + 1);
                    out << "Else\n";
                    print_ty_expr(out, *node.else_branch, level + 2);
                }
            } else if constexpr (std::is_same_v<N, TyLoop>) {
                indent(out, level);
                out << "TyLoop: " << expr->ty.display() << "\n";
                print_ty_block(out, node.body, level + 1);
            } else if constexpr (std::is_same_v<N, TyWhile>) {
                indent(out, level);
                out << "TyWhile: " << expr->ty.display() << "\n";
                indent(out, level + 1);
                out << "Condition\n";
                print_ty_expr(out, node.condition, level + 2);
                indent(out, level + 1);
                out << "Body\n";
                print_ty_block(out, node.body, level + 2);
            } else if constexpr (std::is_same_v<N, TyFor>) {
                indent(out, level);
                out << "TyFor: " << expr->ty.display() << "\n";
                if (node.init.has_value()) {
                    const TyForInit& init = *node.init;
                    indent(out, level + 1);
                    out << "Init\n";
                    indent(out, level + 2);
                    if (init.discard)
                        out << "Let _: " << init.ty.display() << " =\n";
                    else
                        out << "Let " << init.name.as_str() << ": " << init.ty.display() << " =\n";
                    print_ty_expr(out, init.init, level + 3);
                }
                if (node.condition.has_value()) {
                    indent(out, level + 1);
                    out << "Condition\n";
                    print_ty_expr(out, *node.condition, level + 2);
                }
                if (node.step.has_value()) {
                    indent(out, level + 1);
                    out << "Step\n";
                    print_ty_expr(out, *node.step, level + 2);
                }
                indent(out, level + 1);
                out << "Body\n";
                print_ty_block(out, node.body, level + 2);
            } else if constexpr (std::is_same_v<N, TyBreak>) {
                indent(out, level);
                out << "TyBreak: !\n";
                if (node.value.has_value())
                    print_ty_expr(out, *node.value, level + 1);
            } else if constexpr (std::is_same_v<N, TyContinue>) {
                indent(out, level);
                out << "TyContinue: !\n";
            } else if constexpr (std::is_same_v<N, TyReturn>) {
                indent(out, level);
                out << "TyReturn: !\n";
                if (node.value.has_value())
                    print_ty_expr(out, *node.value, level + 1);
            }
        },
        expr->node);
}

inline void print_ty_item(std::ostringstream& out, const TyItem& item, std::size_t level) {
    std::visit(
        [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, TyStructDecl>) {
                indent(out, level);
                out << "TyStructDecl " << node.name.as_str();
                if (node.lang_name.is_valid())
                    out << " [[lang = \"" << node.lang_name.as_str() << "\"]]";
                cstc::ast::detail::print_generic_params(out, node.generic_params);
                if (node.is_zst) {
                    if (node.where_clause.empty()) {
                        out << " ;\n";
                    } else {
                        out << "\n";
                        cstc::ast::detail::print_where_clause(out, node.where_clause, level + 1);
                        indent(out, level);
                        out << ";\n";
                    }
                } else {
                    out << "\n";
                    cstc::ast::detail::print_where_clause(out, node.where_clause, level + 1);
                    for (const TyFieldDecl& field : node.fields) {
                        indent(out, level + 1);
                        out << field.name.as_str() << ": " << field.ty.display() << "\n";
                    }
                }
            } else if constexpr (std::is_same_v<T, TyEnumDecl>) {
                indent(out, level);
                out << "TyEnumDecl " << node.name.as_str();
                if (node.lang_name.is_valid())
                    out << " [[lang = \"" << node.lang_name.as_str() << "\"]]";
                cstc::ast::detail::print_generic_params(out, node.generic_params);
                out << "\n";
                cstc::ast::detail::print_where_clause(out, node.where_clause, level + 1);
                for (const TyEnumVariant& variant : node.variants) {
                    indent(out, level + 1);
                    out << variant.name.as_str();
                    if (variant.discriminant.has_value())
                        out << " = " << variant.discriminant->as_str();
                    out << "\n";
                }
            } else if constexpr (std::is_same_v<T, TyFnDecl>) {
                indent(out, level);
                out << "TyFnDecl " << node.name.as_str();
                cstc::ast::detail::print_generic_params(out, node.generic_params);
                out << "(";
                for (std::size_t i = 0; i < node.params.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << node.params[i].name.as_str() << ": " << node.params[i].ty.display();
                }
                out << ") -> " << node.return_ty.display() << "\n";
                cstc::ast::detail::print_where_clause(out, node.where_clause, level + 1);
                print_ty_block(out, node.body, level + 1);
            } else if constexpr (std::is_same_v<T, TyExternFnDecl>) {
                indent(out, level);
                out << "TyExternFnDecl \"" << node.abi.as_str() << "\" " << node.name.as_str()
                    << "(";
                for (std::size_t i = 0; i < node.params.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << node.params[i].name.as_str() << ": " << node.params[i].ty.display();
                }
                out << ") -> " << node.return_ty.display() << "\n";
            } else if constexpr (std::is_same_v<T, TyExternStructDecl>) {
                indent(out, level);
                out << "TyExternStructDecl \"" << node.abi.as_str() << "\" " << node.name.as_str()
                    << (node.lang_name.is_valid()
                            ? " [[lang = \"" + std::string(node.lang_name.as_str()) + "\"]]"
                            : "")
                    << "\n";
            }
        },
        item);
}

} // namespace detail

inline std::string format_program(const TyProgram& program) {
    std::ostringstream out;
    out << "TyProgram\n";
    for (const TyItem& item : program.items)
        detail::print_ty_item(out, item, 1);
    return out.str();
}

} // namespace cstc::tyir

#endif // CICEST_COMPILER_CSTC_TYIR_PRINTER_HPP
