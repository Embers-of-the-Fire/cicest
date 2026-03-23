#pragma once

#include <cstc_module/module.hpp>
#include <cstc_parser/diagnostics.hpp>
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

[[nodiscard]] inline cstc::ast::Program load_module_program(
    cstc::span::SourceMap& source_map, const std::filesystem::path& root_path,
    const std::filesystem::path& std_root_path) {
    const auto loaded = cstc::module::load_program(source_map, root_path, std_root_path);
    if (!loaded.has_value())
        throw std::runtime_error(cstc::module::format_module_error(source_map, loaded.error()));
    return *loaded;
}

} // namespace cstc::cli_support
