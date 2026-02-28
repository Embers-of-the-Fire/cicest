/*
 * Single character literals tests
 */

#include <cassert>
#include <cstc_ast/token_types.hpp>
#include <cstdint>
#include <print>

using namespace cstc::ast::punct;
using namespace std::string_view_literals;
namespace pegtl = tao::pegtl;

using ResultData = std::int32_t;

template <typename Rule>
struct action {};

template <>
struct action<Colon::Pattern> {
    template <typename ActionInput>
    static void apply(const ActionInput& /* unused */, ResultData& data) {
        std::println("Matched Colon!");
        data += 1;
    }
};

int main() {

    Colon colon;
    std::println("Colon: text={:?}", colon.text);

    using Pattern = pegtl::must<pegtl::plus<Colon::Pattern>, pegtl::eof>;
    std::string_view name = ":::"sv;

    pegtl::memory_input in(name, "test_input");
    ResultData data = 0;
    if (!pegtl::parse<Pattern, action>(in, data)) {
        std::println("Parsing failed!");
        return 1;
    }

    assert(data == 3);
    std::println("Found {} colons", data);

    return 0;
}
