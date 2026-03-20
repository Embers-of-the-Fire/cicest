#pragma once

#include <cstc_parser/diagnostics.hpp>
#include <cstc_parser/parser.hpp>
#include <cstc_resource_path/resource_path.hpp>
#include <cstc_span/span.hpp>
#include <cstc_tyir_builder/builder.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cstc::cli_support {

[[nodiscard]] inline std::string read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("failed to open input file: " + path.string());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof())
        throw std::runtime_error("failed while reading input file: " + path.string());
    return buffer.str();
}

[[nodiscard]] inline bool paths_refer_to_same_file(
    const std::filesystem::path& lhs, const std::filesystem::path& rhs,
    std::string_view lhs_description, std::string_view rhs_description) {
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(lhs, rhs, error);
    if (error) {
        throw std::runtime_error(
            "failed to compare " + std::string(lhs_description) + " '" + lhs.string() + "' with "
            + std::string(rhs_description) + " '" + rhs.string() + "': " + error.message());
    }

    return equivalent;
}

[[nodiscard]] inline std::string format_type_error(
    const cstc::span::SourceMap& source_map, const cstc::tyir_builder::LowerError& error) {
    if (const auto resolved = source_map.resolve_span(error.span); resolved.has_value()) {
        return "type error " + std::string(resolved->file_name) + ":"
             + std::to_string(resolved->start.line) + ":" + std::to_string(resolved->start.column)
             + ": " + error.message;
    }

    return "type error: " + error.message;
}

[[nodiscard]] inline cstc::ast::Program parse_with_std_prelude(
    cstc::span::SourceMap& source_map, cstc::span::SourceFileId user_file_id,
    const std::filesystem::path& std_root_path) {
    const std::filesystem::path prelude_path =
        cstc::resource_path::resolve_std_dir(std_root_path) / "prelude.cst";

    const cstc::span::SourceFile* user_source = source_map.file(user_file_id);
    if (user_source == nullptr)
        throw std::runtime_error("invalid source file id");

    const bool inject_prelude =
        !paths_refer_to_same_file(user_source->name, prelude_path, "input file", "std prelude");

    cstc::ast::Program prelude_program;
    if (inject_prelude) {
        const std::string prelude_source = read_source_file(prelude_path);
        const cstc::span::SourceFileId prelude_file_id =
            source_map.add_file(prelude_path.string(), prelude_source);
        const cstc::span::SourceFile* prelude_file = source_map.file(prelude_file_id);
        if (prelude_file == nullptr)
            throw std::runtime_error("invalid source file id for std prelude");

        const auto prelude_parsed =
            cstc::parser::parse_source_at(prelude_file->source, prelude_file->start_pos);
        if (!prelude_parsed.has_value()) {
            throw std::runtime_error(
                cstc::parser::format_parse_error(source_map, prelude_parsed.error()));
        }

        prelude_program = *prelude_parsed;
    }

    const cstc::span::SourceFile* source_file = source_map.file(user_file_id);
    if (source_file == nullptr)
        throw std::runtime_error("invalid source file id");

    const auto parsed = cstc::parser::parse_source_at(source_file->source, source_file->start_pos);
    if (!parsed.has_value())
        throw std::runtime_error(cstc::parser::format_parse_error(source_map, parsed.error()));

    cstc::ast::Program merged;
    merged.items.reserve(prelude_program.items.size() + parsed->items.size());
    merged.items.insert(
        merged.items.end(), prelude_program.items.begin(), prelude_program.items.end());
    merged.items.insert(merged.items.end(), parsed->items.begin(), parsed->items.end());
    return merged;
}

} // namespace cstc::cli_support
