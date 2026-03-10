#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cstc_parser/parser.hpp>

using namespace cstc::ast;
using namespace cstc::parser;

static std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        assert(false);
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static std::vector<std::string> extract_cicest_fenced_blocks(const std::string& markdown) {
    std::vector<std::string> blocks;
    std::istringstream stream(markdown);

    bool in_cicest_block = false;
    std::string current;
    std::string line;
    while (std::getline(stream, line)) {
        if (!in_cicest_block) {
            if (line.starts_with("```cicest")) {
                in_cicest_block = true;
                current.clear();
            }
            continue;
        }

        if (line.starts_with("```")) {
            blocks.push_back(current);
            in_cicest_block = false;
            continue;
        }

        current += line;
        current.push_back('\n');
    }

    return blocks;
}

static void test_example_markdown_cicest_blocks_parse() {
    const std::string example_path = std::string(CICEST_SOURCE_ROOT) + "/docs/language/example.md";
    const std::string markdown = read_file(example_path);
    const auto blocks = extract_cicest_fenced_blocks(markdown);

    assert(!blocks.empty());

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const std::string wrapped = "fn __example_block_" + std::to_string(index) + "() -> i32 {\n"
            + blocks[index] + "\n}\n";

        SymbolTable symbols;
        const auto parsed = parse_source(wrapped, symbols);
        if (!parsed.has_value()) {
            std::cerr << "Failed to parse docs/language/example.md cicest block " << index
                      << "\n\n"
                      << blocks[index]
                      << "\nParse error: " << parsed.error().message << "\n";
            assert(false);
        }
    }
}

int main() {
    test_example_markdown_cicest_blocks_parse();
    return 0;
}
