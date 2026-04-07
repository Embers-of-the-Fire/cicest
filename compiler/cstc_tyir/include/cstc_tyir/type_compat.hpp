#ifndef CICEST_COMPILER_CSTC_TYIR_TYPE_COMPAT_HPP
#define CICEST_COMPILER_CSTC_TYIR_TYPE_COMPAT_HPP

/// @file type_compat.hpp
/// @brief Shared TyIR type-compatibility helpers.

#include <memory>
#include <optional>

#include <cstc_tyir/tyir.hpp>

namespace cstc::tyir {

/// Returns true when two types share the same non-`runtime` shape.
[[nodiscard]] inline bool same_type_shape(const Ty& lhs, const Ty& rhs) {
    return lhs.same_shape_as(rhs);
}

/// Returns true when `actual` may appear where `expected` is required.
///
/// `Never` (bottom type) is compatible with any expected type. Outside of
/// `Never`, the only implicit conversion is `T -> runtime T`, applied
/// structurally to the type tree.
[[nodiscard]] inline bool compatible(const Ty& actual, const Ty& expected) {
    if (actual.is_never())
        return true;
    if (!same_type_shape(actual, expected))
        return false;
    if (actual.is_runtime && !expected.is_runtime)
        return false;
    if (actual.kind == TyKind::Named) {
        for (std::size_t index = 0; index < actual.generic_args.size(); ++index) {
            if (!compatible(actual.generic_args[index], expected.generic_args[index]))
                return false;
        }
        return true;
    }
    if (actual.kind != TyKind::Ref)
        return true;
    if (actual.pointee == nullptr || expected.pointee == nullptr)
        return actual.pointee == expected.pointee;
    return compatible(*actual.pointee, *expected.pointee);
}

/// Returns true when `actual` matches the structural shape expected by an
/// ordinary expression operator or condition.
///
/// Unlike `compatible`, this ignores the `runtime` qualifier and therefore
/// does not permit expression checks to double as coercion sites.
[[nodiscard]] inline bool matches_type_shape(const Ty& actual, const Ty& expected) {
    return actual.is_never() || same_type_shape(actual, expected);
}

/// Returns the least common supertype of `lhs` and `rhs`, if one exists.
///
/// The runtime qualifier joins by promotion: mixing `T` with `runtime T`
/// yields `runtime T`.
[[nodiscard]] inline std::optional<Ty> common_type(const Ty& lhs, const Ty& rhs) {
    if (lhs.is_never())
        return rhs;
    if (rhs.is_never())
        return lhs;
    if (!same_type_shape(lhs, rhs))
        return std::nullopt;

    Ty joined = lhs;
    joined.is_runtime = lhs.is_runtime || rhs.is_runtime;
    if (!joined.display_name.is_valid())
        joined.display_name = rhs.display_name;

    if (joined.kind == TyKind::Named) {
        joined.generic_args.clear();
        joined.generic_args.reserve(lhs.generic_args.size());
        for (std::size_t index = 0; index < lhs.generic_args.size(); ++index) {
            auto arg = common_type(lhs.generic_args[index], rhs.generic_args[index]);
            if (!arg.has_value())
                return std::nullopt;
            joined.generic_args.push_back(std::move(*arg));
        }
    }

    if (joined.kind == TyKind::Ref) {
        if (lhs.pointee == nullptr || rhs.pointee == nullptr) {
            if (lhs.pointee != rhs.pointee)
                return std::nullopt;
        } else {
            auto pointee = common_type(*lhs.pointee, *rhs.pointee);
            if (!pointee.has_value())
                return std::nullopt;
            joined.pointee = std::make_shared<Ty>(std::move(*pointee));
        }
    }

    return joined;
}

} // namespace cstc::tyir

#endif
