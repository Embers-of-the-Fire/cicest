#ifndef CICEST_CSTC_AST_TOKEN_TYPES_PUNCT_HPP
#define CICEST_CSTC_AST_TOKEN_TYPES_PUNCT_HPP

#include <string_view>
#include <tao/pegtl.hpp>

namespace cstc::ast::punct {

/// Literal punctuation token.
template <char... Ch>
class PunctToken {
protected:
    /// Raw string of the punctuation token, null-terminated for convenience.
    ///
    /// This is **discouraged** to be used directly. Instead, use `text` for a `std::string_view` of
    /// the punctuation.
    static constexpr char raw[sizeof...(Ch) + 1] = {Ch..., '\0'};

public:
    /// PEGTL pattern for the punctuation token, which matches the exact sequence of characters.
    using Pattern = tao::pegtl::string<Ch...>;

    /// Text representation of the punctuation token as a `std::string_view`, which is more
    /// convenient to use than `raw`.
    static constexpr std::string_view text{raw, sizeof...(Ch)};
};

/* Single character punctuations */

using Not = PunctToken<'!'>;
using Plus = PunctToken<'+'>;
using Minus = PunctToken<'-'>;
/// Aka. multiply character.
using Star = PunctToken<'*'>;
using Percent = PunctToken<'%'>;
using BitOr = PunctToken<'|'>;
using BitXor = PunctToken<'^'>;
using BitAnd = PunctToken<'&'>;
using At = PunctToken<'@'>;
using Comma = PunctToken<','>;
using Semicolon = PunctToken<';'>;
using Colon = PunctToken<':'>;
using Less = PunctToken<'<'>;
using Greater = PunctToken<'>'>;

/* Multi-character punctuations */

using LeftShift = PunctToken<'<', '<'>;
using RightShift = PunctToken<'>', '>'>;
using Equal = PunctToken<'='>;
using NotEqual = PunctToken<'!', '='>;
using LessOrEqual = PunctToken<'<', '='>;
using GreaterOrEqual = PunctToken<'>', '='>;
using AndAnd = PunctToken<'&', '&'>;
using OrOr = PunctToken<'|', '|'>;
using PathSep = PunctToken<':', ':'>;
using SingleArrow = PunctToken<'-', '>'>;
using DoubleArrow = PunctToken<'=', '>'>;

}; // namespace cstc::ast::punct

#endif // CICEST_CSTC_AST_TOKEN_TYPES_PUNCT_HPP
