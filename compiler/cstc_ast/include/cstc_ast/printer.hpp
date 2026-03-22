#ifndef CICEST_COMPILER_CSTC_AST_PRINTER_HPP
#define CICEST_COMPILER_CSTC_AST_PRINTER_HPP

#include <string>

#include <cstc_ast/ast.hpp>

namespace cstc::ast {

/// Renders a human-readable tree view of the AST program.
///
/// Symbol ids are resolved through the current session's global symbol table.
[[nodiscard]] inline std::string format_program(const Program& program);

} // namespace cstc::ast

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace cstc::ast {

namespace detail {

inline void indent(std::ostringstream& output, std::size_t level) {
    output << std::string(level * 2, ' ');
}

inline void print_attributes(
    std::ostringstream& output, const std::vector<Attribute>& attributes, std::size_t level) {
    for (const Attribute& attribute : attributes) {
        indent(output, level);
        output << "Attribute [[" << attribute.name.as_str();
        if (attribute.value.has_value())
            output << " = \"" << attribute.value->as_str() << "\"";
        output << "]]\n";
    }
}

[[nodiscard]] inline std::string_view type_name(const TypeRef& type) {
    if (type.symbol.is_valid())
        return type.symbol.as_str();

    switch (type.kind) {
    case TypeKind::Unit: return "Unit";
    case TypeKind::Num: return "num";
    case TypeKind::Str: return "str";
    case TypeKind::Bool: return "bool";
    case TypeKind::Named: return "<named>";
    case TypeKind::Never: return "!";
    }
    return "<unknown-type>";
}

[[nodiscard]] inline std::string_view unary_name(UnaryOp op) {
    switch (op) {
    case UnaryOp::Negate: return "-";
    case UnaryOp::Not: return "!";
    }
    return "?";
}

[[nodiscard]] inline std::string_view binary_name(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::Mod: return "%";
    case BinaryOp::Eq: return "==";
    case BinaryOp::Ne: return "!=";
    case BinaryOp::Lt: return "<";
    case BinaryOp::Le: return "<=";
    case BinaryOp::Gt: return ">";
    case BinaryOp::Ge: return ">=";
    case BinaryOp::And: return "&&";
    case BinaryOp::Or: return "||";
    }
    return "?";
}

inline void print_expr(std::ostringstream& output, const ExprPtr& expr, std::size_t level);

inline void print_block(std::ostringstream& output, const BlockPtr& block, std::size_t level) {
    indent(output, level);
    output << "Block" << "\n";

    for (const Stmt& statement : block->statements) {
        std::visit(
            [&](const auto& stmt_value) {
                using StmtType = std::decay_t<decltype(stmt_value)>;
                if constexpr (std::is_same_v<StmtType, LetStmt>) {
                    indent(output, level + 1);
                    if (stmt_value.discard)
                        output << "Let _";
                    else
                        output << "Let " << stmt_value.name.as_str();
                    if (stmt_value.type_annotation.has_value())
                        output << ": " << type_name(*stmt_value.type_annotation);
                    output << " =\n";
                    print_expr(output, stmt_value.initializer, level + 2);
                } else {
                    indent(output, level + 1);
                    output << "ExprStmt\n";
                    print_expr(output, stmt_value.expr, level + 2);
                }
            },
            statement);
    }

    if (block->tail.has_value()) {
        indent(output, level + 1);
        output << "Tail\n";
        print_expr(output, *block->tail, level + 2);
    }
}

inline void print_expr(std::ostringstream& output, const ExprPtr& expr, std::size_t level) {
    std::visit(
        [&](const auto& node) {
            using ExprType = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<ExprType, LiteralExpr>) {
                indent(output, level);
                switch (node.kind) {
                case LiteralExpr::Kind::Num:
                    output << "NumLit(" << node.symbol.as_str() << ")\n";
                    break;
                case LiteralExpr::Kind::Str:
                    output << "StrLit(" << node.symbol.as_str() << ")\n";
                    break;
                case LiteralExpr::Kind::Bool:
                    output << "BoolLit(" << (node.bool_value ? "true" : "false") << ")\n";
                    break;
                case LiteralExpr::Kind::Unit: output << "UnitLit\n"; break;
                }
            } else if constexpr (std::is_same_v<ExprType, PathExpr>) {
                indent(output, level);
                if (node.tail.has_value()) {
                    output << "Path(" << node.head.as_str() << "::" << node.tail->as_str() << ")\n";
                } else {
                    output << "Path(" << node.head.as_str() << ")\n";
                }
            } else if constexpr (std::is_same_v<ExprType, StructInitExpr>) {
                indent(output, level);
                output << "StructInit(" << node.type_name.as_str() << ")\n";
                for (const StructInitField& field : node.fields) {
                    indent(output, level + 1);
                    output << field.name.as_str() << ":\n";
                    print_expr(output, field.value, level + 2);
                }
            } else if constexpr (std::is_same_v<ExprType, UnaryExpr>) {
                indent(output, level);
                output << "Unary(" << unary_name(node.op) << ")\n";
                print_expr(output, node.rhs, level + 1);
            } else if constexpr (std::is_same_v<ExprType, BinaryExpr>) {
                indent(output, level);
                output << "Binary(" << binary_name(node.op) << ")\n";
                print_expr(output, node.lhs, level + 1);
                print_expr(output, node.rhs, level + 1);
            } else if constexpr (std::is_same_v<ExprType, FieldAccessExpr>) {
                indent(output, level);
                output << "FieldAccess(" << node.field.as_str() << ")\n";
                print_expr(output, node.base, level + 1);
            } else if constexpr (std::is_same_v<ExprType, CallExpr>) {
                indent(output, level);
                output << "Call\n";
                indent(output, level + 1);
                output << "Callee\n";
                print_expr(output, node.callee, level + 2);
                for (const ExprPtr& arg : node.args) {
                    indent(output, level + 1);
                    output << "Arg\n";
                    print_expr(output, arg, level + 2);
                }
            } else if constexpr (std::is_same_v<ExprType, BlockPtr>) {
                print_block(output, node, level);
            } else if constexpr (std::is_same_v<ExprType, IfExpr>) {
                indent(output, level);
                output << "If\n";
                indent(output, level + 1);
                output << "Condition\n";
                print_expr(output, node.condition, level + 2);
                indent(output, level + 1);
                output << "Then\n";
                print_block(output, node.then_block, level + 2);
                if (node.else_branch.has_value()) {
                    indent(output, level + 1);
                    output << "Else\n";
                    print_expr(output, *node.else_branch, level + 2);
                }
            } else if constexpr (std::is_same_v<ExprType, LoopExpr>) {
                indent(output, level);
                output << "Loop\n";
                print_block(output, node.body, level + 1);
            } else if constexpr (std::is_same_v<ExprType, WhileExpr>) {
                indent(output, level);
                output << "While\n";
                indent(output, level + 1);
                output << "Condition\n";
                print_expr(output, node.condition, level + 2);
                print_block(output, node.body, level + 1);
            } else if constexpr (std::is_same_v<ExprType, ForExpr>) {
                indent(output, level);
                output << "For\n";
                if (node.init.has_value()) {
                    std::visit(
                        [&](const auto& init_node) {
                            using InitType = std::decay_t<decltype(init_node)>;
                            indent(output, level + 1);
                            output << "Init\n";
                            if constexpr (std::is_same_v<InitType, ForInitLet>) {
                                indent(output, level + 2);
                                if (init_node.discard) {
                                    output << "Let _";
                                } else {
                                    output << "Let " << init_node.name.as_str();
                                }
                                if (init_node.type_annotation.has_value()) {
                                    output << ": " << type_name(*init_node.type_annotation);
                                }
                                output << " =\n";
                                print_expr(output, init_node.initializer, level + 3);
                            } else {
                                print_expr(output, init_node, level + 2);
                            }
                        },
                        *node.init);
                }

                if (node.condition.has_value()) {
                    indent(output, level + 1);
                    output << "Condition\n";
                    print_expr(output, *node.condition, level + 2);
                }

                if (node.step.has_value()) {
                    indent(output, level + 1);
                    output << "Step\n";
                    print_expr(output, *node.step, level + 2);
                }

                indent(output, level + 1);
                output << "Body\n";
                print_block(output, node.body, level + 2);
            } else if constexpr (std::is_same_v<ExprType, BreakExpr>) {
                indent(output, level);
                output << "Break\n";
                if (node.value.has_value())
                    print_expr(output, *node.value, level + 1);
            } else if constexpr (std::is_same_v<ExprType, ContinueExpr>) {
                indent(output, level);
                output << "Continue\n";
            } else if constexpr (std::is_same_v<ExprType, ReturnExpr>) {
                indent(output, level);
                output << "Return\n";
                if (node.value.has_value())
                    print_expr(output, *node.value, level + 1);
            }
        },
        expr->node);
}

inline void print_item(std::ostringstream& output, const Item& item, std::size_t level) {
    std::visit(
        [&](const auto& node) {
            using ItemType = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<ItemType, StructDecl>) {
                print_attributes(output, node.attributes, level);
                indent(output, level);
                if (node.is_zst) {
                    output << "StructDecl " << node.name.as_str() << " ;\n";
                } else {
                    output << "StructDecl " << node.name.as_str() << "\n";
                    for (const FieldDecl& field : node.fields) {
                        indent(output, level + 1);
                        output << field.name.as_str() << ": " << type_name(field.type) << "\n";
                    }
                }
            } else if constexpr (std::is_same_v<ItemType, EnumDecl>) {
                print_attributes(output, node.attributes, level);
                indent(output, level);
                output << "EnumDecl " << node.name.as_str() << "\n";
                for (const EnumVariant& variant : node.variants) {
                    indent(output, level + 1);
                    output << variant.name.as_str();
                    if (variant.discriminant.has_value())
                        output << " = " << variant.discriminant->as_str();
                    output << "\n";
                }
            } else if constexpr (std::is_same_v<ItemType, FnDecl>) {
                print_attributes(output, node.attributes, level);
                indent(output, level);
                output << "FnDecl " << node.name.as_str() << "(";
                for (std::size_t index = 0; index < node.params.size(); ++index) {
                    if (index > 0)
                        output << ", ";
                    output << node.params[index].name.as_str() << ": "
                           << type_name(node.params[index].type);
                }
                output << ")";
                if (node.return_type.has_value())
                    output << " -> " << type_name(*node.return_type);
                output << "\n";
                print_block(output, node.body, level + 1);
            } else if constexpr (std::is_same_v<ItemType, ExternFnDecl>) {
                print_attributes(output, node.attributes, level);
                indent(output, level);
                output << "ExternFnDecl \"" << node.abi.as_str() << "\" " << node.name.as_str()
                       << "(";
                for (std::size_t index = 0; index < node.params.size(); ++index) {
                    if (index > 0)
                        output << ", ";
                    output << node.params[index].name.as_str() << ": "
                           << type_name(node.params[index].type);
                }
                output << ")";
                if (node.return_type.has_value())
                    output << " -> " << type_name(*node.return_type);
                output << "\n";
            } else if constexpr (std::is_same_v<ItemType, ExternStructDecl>) {
                print_attributes(output, node.attributes, level);
                indent(output, level);
                output << "ExternStructDecl \"" << node.abi.as_str() << "\" " << node.name.as_str()
                       << "\n";
            }
        },
        item);
}

} // namespace detail

inline std::string format_program(const Program& program) {
    std::ostringstream output;

    output << "Program\n";
    for (const Item& item : program.items)
        detail::print_item(output, item, 1);

    return output.str();
}

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_PRINTER_HPP
