#pragma once

#include <cstc_ansi_color/ansi_color.hpp>
#include <cstc_error_report/report.hpp>
#include <cstc_module/module.hpp>
#include <cstc_parser/diagnostics.hpp>
#include <cstc_resource_path/resource_path.hpp>
#include <cstc_span/span.hpp>
#include <cstc_tyir_builder/builder.hpp>
#include <cstc_tyir_interp/interp.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cstc::cli_support {

namespace detail {

[[nodiscard]] inline std::optional<cstc::error_report::SourceSpan> append_report_span(
    cstc::error_report::SourceDatabase& database, const cstc::span::SourceMap& source_map,
    cstc::span::SourceSpan span) {
    const auto resolved = source_map.resolve_span(span);
    if (!resolved.has_value())
        return std::nullopt;

    const cstc::span::SourceFile* file = source_map.file(resolved->file_id);
    if (file == nullptr)
        return std::nullopt;

    const cstc::error_report::SourceId source_id = database.add_source(file->name, file->source);
    return database.make_span(source_id, resolved->local.start, resolved->local.end);
}

[[nodiscard]] inline std::string render_report(
    const cstc::error_report::SourceDatabase& database,
    const cstc::error_report::Diagnostic& diagnostic) {
    return cstc::error_report::render(
        database, diagnostic,
        cstc::error_report::RenderOptions{
            .color = cstc::ansi_color::detect_emission(),
            .context_lines = 1,
        });
}

[[nodiscard]] inline std::string format_instantiation_phase(cstc::tyir::InstantiationPhase phase) {
    switch (phase) {
    case cstc::tyir::InstantiationPhase::TypeChecking: return "type checking";
    case cstc::tyir::InstantiationPhase::ConstEval: return "const-eval";
    case cstc::tyir::InstantiationPhase::Monomorphization: return "monomorphization";
    }
    return "unknown";
}

[[nodiscard]] inline std::string
    format_instantiation_frame(const cstc::tyir::InstantiationFrame& frame) {
    std::string rendered;
    if (frame.display_name.is_valid())
        rendered = std::string(frame.display_name.as_str());
    else if (frame.item_name.is_valid())
        rendered = std::string(frame.item_name.as_str());
    else
        rendered = "<unknown>";

    rendered += "<";
    for (std::size_t index = 0; index < frame.generic_args.size(); ++index) {
        if (index > 0)
            rendered += ", ";
        rendered += frame.generic_args[index].display();
    }
    rendered += ">";
    return rendered;
}

inline void append_instantiation_limit_children(
    cstc::error_report::SourceDatabase& database, const cstc::span::SourceMap& source_map,
    cstc::error_report::Diagnostic& diagnostic,
    const std::optional<cstc::tyir::InstantiationLimitDiagnostic>& instantiation_limit) {
    if (!instantiation_limit.has_value())
        return;

    cstc::error_report::Diagnostic limit_note;
    limit_note.severity = cstc::error_report::Severity::Note;
    limit_note.message = "active " + format_instantiation_phase(instantiation_limit->phase)
                       + " recursion limit: " + std::to_string(instantiation_limit->active_limit);
    diagnostic.children.push_back(std::move(limit_note));

    for (auto it = instantiation_limit->stack.rbegin(); it != instantiation_limit->stack.rend();
         ++it) {
        cstc::error_report::Diagnostic child;
        child.severity = cstc::error_report::Severity::Note;
        child.message = "instantiated as `" + format_instantiation_frame(*it) + "`";

        if (const auto report_span = detail::append_report_span(database, source_map, it->span);
            report_span.has_value()) {
            child.labels.push_back(
                cstc::error_report::Label{
                    .span = *report_span,
                    .message = "instantiation entered here",
                    .style = cstc::error_report::LabelStyle::Primary,
                });
        }

        diagnostic.children.push_back(std::move(child));
    }
}

} // namespace detail

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
    cstc::error_report::SourceDatabase database;
    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Error;
    diagnostic.message = "type error: " + error.message;

    if (const auto report_span = detail::append_report_span(database, source_map, error.span);
        report_span.has_value()) {
        diagnostic.labels.push_back(
            cstc::error_report::Label{
                .span = *report_span,
                .message = error.message,
                .style = cstc::error_report::LabelStyle::Primary,
            });
    }

    detail::append_instantiation_limit_children(
        database, source_map, diagnostic, error.instantiation_limit);

    return detail::render_report(database, diagnostic);
}

[[nodiscard]] inline std::string format_eval_error(
    const cstc::span::SourceMap& source_map, const cstc::tyir_interp::EvalError& error) {
    cstc::error_report::SourceDatabase database;
    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Error;
    diagnostic.message = "const-eval error: " + error.message;

    if (const auto report_span = detail::append_report_span(database, source_map, error.span);
        report_span.has_value()) {
        diagnostic.labels.push_back(
            cstc::error_report::Label{
                .span = *report_span,
                .message = error.message,
                .style = cstc::error_report::LabelStyle::Primary,
            });
    }

    for (auto it = error.stack.rbegin(); it != error.stack.rend(); ++it) {
        std::string fn_name =
            it->fn_name.is_valid() ? std::string(it->fn_name.as_str()) : "<unknown>";
        cstc::error_report::Diagnostic child;
        child.severity = cstc::error_report::Severity::Note;
        child.message = "in function `" + fn_name + "`";

        if (const auto report_span = detail::append_report_span(database, source_map, it->span);
            report_span.has_value()) {
            child.labels.push_back(
                cstc::error_report::Label{
                    .span = *report_span,
                    .message = "called from here",
                    .style = cstc::error_report::LabelStyle::Primary,
                });
        }

        diagnostic.children.push_back(std::move(child));
    }

    detail::append_instantiation_limit_children(
        database, source_map, diagnostic, error.instantiation_limit);

    return detail::render_report(database, diagnostic);
}

[[nodiscard]] inline std::expected<cstc::tyir::TyProgram, std::string> lower_and_fold_program(
    const cstc::span::SourceMap& source_map, const cstc::ast::Program& program) {
    const auto lowered = cstc::tyir_builder::lower_program(program);
    if (!lowered.has_value())
        return std::unexpected(format_type_error(source_map, lowered.error()));

    const auto folded = cstc::tyir_interp::fold_program(*lowered);
    if (!folded.has_value())
        return std::unexpected(format_eval_error(source_map, folded.error()));

    return *folded;
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
