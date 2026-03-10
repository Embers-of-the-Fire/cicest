#ifndef CICEST_COMPILER_CSTC_HIR_PRINTER_HPP
#define CICEST_COMPILER_CSTC_HIR_PRINTER_HPP

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include <cstc_hir/hir.hpp>

namespace cstc::hir {

namespace detail {

class HirPrinterImpl {
public:
    explicit HirPrinterImpl(std::ostream& out)
        : out_(out) {}

    void print_module(const Module& module) {
        if (module.declarations.empty()) {
            out_ << "(empty)\n";
            return;
        }

        for (std::size_t index = 0; index < module.declarations.size(); ++index) {
            print_declaration(module.declarations[index]);
            if (index + 1U < module.declarations.size())
                out_ << '\n';
        }
    }

private:
    class IndentScope {
    public:
        explicit IndentScope(HirPrinterImpl& printer)
            : printer_(printer) {
            ++printer_.indent_;
        }

        ~IndentScope() { --printer_.indent_; }

    private:
        HirPrinterImpl& printer_;
    };

    [[nodiscard]] static std::string join(const std::vector<std::string>& parts, std::string_view sep) {
        std::string result;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (index != 0)
                result += sep;
            result += parts[index];
        }
        return result;
    }

    [[nodiscard]] static std::string contract_kind_text(TypeContractKind kind) {
        switch (kind) {
        case TypeContractKind::Runtime: return "runtime";
        case TypeContractKind::NotRuntime: return "!runtime";
        }

        return "<unknown-contract>";
    }

    [[nodiscard]] static std::string contract_block_kind_text(ContractBlockKind kind) {
        switch (kind) {
        case ContractBlockKind::Runtime: return "runtime";
        case ContractBlockKind::Const: return "const";
        }

        return "<unknown-contract-block>";
    }

    [[nodiscard]] static const Type* strip_nested_runtime_contracts(const Type* type) {
        const Type* current = type;
        while (current != nullptr) {
            const auto* contract = std::get_if<ContractType>(&current->kind);
            if (contract == nullptr || contract->kind != TypeContractKind::Runtime)
                break;
            current = contract->inner.get();
        }

        return current;
    }

    [[nodiscard]] std::string format_type(const Type& type) {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, PathType>) {
                    std::string text = join(kind.segments, "::");
                    if (text.empty())
                        text = "<empty-type>";

                    if (!kind.args.empty()) {
                        text += "<";
                        for (std::size_t index = 0; index < kind.args.size(); ++index) {
                            if (index != 0)
                                text += ", ";
                            text += kind.args[index] == nullptr ? "<missing-type>"
                                                                : format_type(*kind.args[index]);
                        }
                        text += ">";
                    }

                    return text;
                } else if constexpr (std::is_same_v<Kind, ContractType>) {
                    const auto* inner = kind.inner.get();
                    if (kind.kind == TypeContractKind::Runtime)
                        inner = strip_nested_runtime_contracts(inner);

                    if (inner == nullptr)
                        return contract_kind_text(kind.kind) + " <missing-type>";
                    return contract_kind_text(kind.kind) + " " + format_type(*inner);
                } else if constexpr (std::is_same_v<Kind, RefType>) {
                    if (kind.inner == nullptr)
                        return "&<missing-type>";
                    return "&" + format_type(*kind.inner);
                } else if constexpr (std::is_same_v<Kind, FnPointerType>) {
                    std::string text = "fn(";
                    for (std::size_t index = 0; index < kind.params.size(); ++index) {
                        if (index != 0)
                            text += ", ";
                        text += kind.params[index] == nullptr ? "<missing-type>"
                                                              : format_type(*kind.params[index]);
                    }
                    text += ") -> ";
                    text += kind.result == nullptr ? "<missing-type>" : format_type(*kind.result);
                    return text;
                } else if constexpr (std::is_same_v<Kind, InferredType>) {
                    return "_";
                }

                return "<unknown-type>";
            },
            type.kind);
    }

    [[nodiscard]] std::string format_expr_inline(const Expr& expr) {
        return std::visit(
            [this](const auto& kind) -> std::string {
                using Kind = std::decay_t<decltype(kind)>;

                if constexpr (std::is_same_v<Kind, RawExpr> || std::is_same_v<Kind, LiteralExpr>) {
                    return kind.text;
                } else if constexpr (std::is_same_v<Kind, PathExpr>) {
                    const std::string text = join(kind.segments, "::");
                    return text.empty() ? "<empty-path>" : text;
                } else if constexpr (std::is_same_v<Kind, BinaryExpr>) {
                    const std::string lhs =
                        kind.lhs == nullptr ? "<missing-expr>" : format_expr_inline(*kind.lhs);
                    const std::string rhs =
                        kind.rhs == nullptr ? "<missing-expr>" : format_expr_inline(*kind.rhs);
                    return lhs + " " + kind.op + " " + rhs;
                } else if constexpr (std::is_same_v<Kind, CallExpr>) {
                    const std::string callee =
                        kind.callee == nullptr ? "<missing-callee>" : format_expr_inline(*kind.callee);

                    std::string text = callee + "(";
                    for (std::size_t index = 0; index < kind.args.size(); ++index) {
                        if (index != 0)
                            text += ", ";
                        text += kind.args[index] == nullptr ? "<missing-expr>"
                                                            : format_expr_inline(*kind.args[index]);
                    }
                    text += ")";
                    return text;
                } else if constexpr (std::is_same_v<Kind, MemberAccessExpr>) {
                    const std::string receiver = kind.receiver == nullptr ? "<missing-expr>"
                                                                          : format_expr_inline(*kind.receiver);
                    return "member_access(" + receiver + ", " + kind.member + ")";
                } else if constexpr (std::is_same_v<Kind, MemberCallExpr>) {
                    const std::string receiver = kind.receiver == nullptr ? "<missing-expr>"
                                                                          : format_expr_inline(*kind.receiver);

                    std::string text = "member_call(" + receiver + ", " + kind.member;
                    for (const auto& arg : kind.args)
                        text += ", " + (arg == nullptr ? std::string{"<missing-expr>"}
                                                        : format_expr_inline(*arg));
                    text += ")";
                    return text;
                } else if constexpr (std::is_same_v<Kind, StaticMemberAccessExpr>) {
                    const std::string receiver = kind.receiver == nullptr ? "<missing-expr>"
                                                                          : format_expr_inline(*kind.receiver);
                    return format_type(kind.receiver_type) + "::" + kind.member + "(" + receiver
                        + ")";
                } else if constexpr (std::is_same_v<Kind, StaticMemberCallExpr>) {
                    const std::string receiver = kind.receiver == nullptr ? "<missing-expr>"
                                                                          : format_expr_inline(*kind.receiver);

                    std::string text = format_type(kind.receiver_type) + "::" + kind.member + "(" + receiver;
                    for (const auto& arg : kind.args)
                        text += ", " + (arg == nullptr ? std::string{"<missing-expr>"}
                                                        : format_expr_inline(*arg));
                    text += ")";
                    return text;
                } else if constexpr (std::is_same_v<Kind, ContractBlockExpr>) {
                    return contract_block_kind_text(kind.kind) + " { ... }";
                } else if constexpr (std::is_same_v<Kind, LiftedConstantExpr>) {
                    return "lifted " + kind.name + ": " + format_type(kind.type) + " = " + kind.value;
                } else if constexpr (std::is_same_v<Kind, DeclConstraintExpr>) {
                    return "decl_valid(" + format_type(kind.checked_type) + ")";
                }

                return "<unknown-expr>";
            },
            expr.kind);
    }

    void line(std::string_view text) {
        for (std::size_t level = 0; level < indent_; ++level)
            out_ << "  ";
        out_ << text << '\n';
    }

    [[nodiscard]] std::string declaration_name(const DeclHeader& header) const {
        return std::visit(
            [](const auto& item) -> std::string {
                using Item = std::decay_t<decltype(item)>;

                if constexpr (std::is_same_v<Item, FunctionDecl>) {
                    return item.name;
                } else if constexpr (std::is_same_v<Item, RawDecl>) {
                    return item.name;
                }

                return "<unknown-decl>";
            },
            header);
    }

    [[nodiscard]] std::string format_decl_header(const DeclHeader& header) {
        return std::visit(
            [this](const auto& item) -> std::string {
                using Item = std::decay_t<decltype(item)>;

                if constexpr (std::is_same_v<Item, FunctionDecl>) {
                    std::string text = "fn " + item.name;

                    if (!item.generic_params.empty()) {
                        text += "<";
                        text += join(item.generic_params, ", ");
                        text += ">";
                    }

                    text += "(";
                    for (std::size_t index = 0; index < item.params.size(); ++index) {
                        if (index != 0)
                            text += ", ";
                        text += item.params[index].name + ": " + format_type(item.params[index].type);
                    }
                    text += ") -> ";
                    text += format_type(item.return_type);
                    return text;
                } else if constexpr (std::is_same_v<Item, RawDecl>) {
                    return item.text;
                }

                return "<unknown-header>";
            },
            header);
    }

    void print_expr(const Expr& expr) {
        if (const auto* contract_block = std::get_if<ContractBlockExpr>(&expr.kind)) {
            line(contract_block_kind_text(contract_block->kind) + " {");
            {
                const IndentScope scope{*this};
                for (const auto& body_expr : contract_block->body) {
                    if (body_expr == nullptr)
                        line("<missing-expr>");
                    else
                        print_expr(*body_expr);
                }
            }
            line("}");
            return;
        }

        line(format_expr_inline(expr));
    }

    void print_expr_section(const std::vector<ExprPtr>& expressions) {
        const IndentScope section_scope{*this};
        for (const auto& expression : expressions) {
            if (expression == nullptr)
                line("<missing-expr>");
            else
                print_expr(*expression);
        }
    }

    void print_declaration(const Declaration& declaration) {
        const std::string header = format_decl_header(declaration.header);
        const std::string name = declaration_name(declaration.header);

        line(header);
        line(name + "::body {");
        print_expr_section(declaration.body);
        line("}");
        line(name + "::constraint {");
        print_expr_section(declaration.constraints);
        line("}");
    }

    std::ostream& out_;
    std::size_t indent_ = 0;
};

} // namespace detail

class HirPrinter {
public:
    [[nodiscard]] std::string format(const Module& module) const {
        std::ostringstream out;
        print(out, module);
        return out.str();
    }

    void print(std::ostream& out, const Module& module) const {
        detail::HirPrinterImpl printer{out};
        printer.print_module(module);
    }
};

[[nodiscard]] inline std::string format_hir(const Module& module) {
    return HirPrinter{}.format(module);
}

inline void print_hir(std::ostream& out, const Module& module) {
    HirPrinter{}.print(out, module);
}

} // namespace cstc::hir

#endif // CICEST_COMPILER_CSTC_HIR_PRINTER_HPP
