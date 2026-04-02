#include <cassert>

#include <cstc_error_report/report.hpp>

namespace {

void test_render_error_span_without_color() {
    cstc::error_report::SourceDatabase database;
    const std::string source = "fn main() {\n    let value = 41\n}\n";
    const auto source_id = database.add_source("main.cst", source);
    const auto span = database.make_span(source_id, source.find("41"), source.find("41") + 2);
    assert(span.has_value());

    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Error;
    diagnostic.message = "expected `;` after let binding";
    diagnostic.labels.push_back(
        cstc::error_report::Label{
            .span = *span,
            .message = "missing semicolon here",
            .style = cstc::error_report::LabelStyle::Primary,
        });

    const std::string rendered = cstc::error_report::render(database, diagnostic);
    assert(rendered.find("error: expected `;` after let binding") != std::string::npos);
    assert(rendered.find("--> main.cst:2:17") != std::string::npos);
    assert(rendered.find("2 |     let value = 41") != std::string::npos);
    assert(rendered.find("missing semicolon here") != std::string::npos);
    assert(rendered.find("\x1b[") == std::string::npos);
}

void test_render_comment_from_token_offset() {
    cstc::error_report::SourceDatabase database;
    const std::string source = "let answer = 41;\n";
    const auto source_id = database.add_source("comment.cst", source);
    const auto point = database.make_point(source_id, source.find("41"));
    assert(point.has_value());

    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Warning;
    diagnostic.message = "suspicious literal";
    diagnostic.comments.push_back(
        cstc::error_report::Comment{
            .point = *point,
            .message = "consider naming this constant",
        });

    const std::string rendered = cstc::error_report::render(database, diagnostic);
    assert(rendered.find("warning: suspicious literal") != std::string::npos);
    assert(rendered.find("--> comment.cst:1:14") != std::string::npos);
    assert(rendered.find("= consider naming this constant") != std::string::npos);
}

void test_render_nested_diagnostic_tree() {
    cstc::error_report::SourceDatabase database;
    const std::string main_source = "const BAD = helper();\n";
    const std::string helper_source = "fn helper() -> num {\n    panic();\n}\n";
    const auto main_id = database.add_source("main.cst", main_source);
    const auto helper_id = database.add_source("helper.cst", helper_source);

    const auto root_span =
        database.make_span(main_id, main_source.find("helper"), main_source.find("helper") + 6);
    const auto child_span =
        database.make_span(helper_id, helper_source.find("panic"), helper_source.find("panic") + 5);
    assert(root_span.has_value());
    assert(child_span.has_value());

    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Error;
    diagnostic.message = "const-eval error: failed to evaluate `helper()`";
    diagnostic.labels.push_back(
        cstc::error_report::Label{
            .span = *root_span,
            .message = "during this call",
            .style = cstc::error_report::LabelStyle::Primary,
        });

    cstc::error_report::Diagnostic child;
    child.severity = cstc::error_report::Severity::Note;
    child.message = "in function `helper`";
    child.labels.push_back(
        cstc::error_report::Label{
            .span = *child_span,
            .message = "reached panic here",
            .style = cstc::error_report::LabelStyle::Primary,
        });
    diagnostic.children.push_back(std::move(child));

    const std::string rendered = cstc::error_report::render(database, diagnostic);
    assert(
        rendered.find("error: const-eval error: failed to evaluate `helper()`")
        != std::string::npos);
    assert(rendered.find("note: in function `helper`") != std::string::npos);
    assert(rendered.find("--> main.cst:1:13") != std::string::npos);
    assert(rendered.find("--> helper.cst:2:5") != std::string::npos);
}

void test_render_uses_color_when_explicitly_enabled() {
    cstc::error_report::SourceDatabase database;
    const auto source_id = database.add_source("color.cst", "let x = 0;\n");
    const auto span = database.make_span(source_id, 4, 5);
    assert(span.has_value());

    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Help;
    diagnostic.message = "rename this binding";
    diagnostic.labels.push_back(
        cstc::error_report::Label{
            .span = *span,
            .message = "binding name",
            .style = cstc::error_report::LabelStyle::Primary,
        });

    const std::string rendered = cstc::error_report::render(
        database, diagnostic,
        cstc::error_report::RenderOptions{
            .color = cstc::ansi_color::Emission::Always,
            .context_lines = 0,
        });
    assert(rendered.find("\x1b[") != std::string::npos);
}

void test_render_ignores_comment_with_invalid_public_point() {
    cstc::error_report::SourceDatabase database;
    const std::string source = "let answer = 41;\n";
    const auto source_id = database.add_source("comment.cst", source);
    const cstc::error_report::SourcePoint invalid_point{
        .source_id = source_id,
        .offset = source.size() + 1,
    };

    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Warning;
    diagnostic.message = "suspicious literal";
    diagnostic.comments.push_back(
        cstc::error_report::Comment{
            .point = invalid_point,
            .message = "consider naming this constant",
        });

    const std::string rendered = cstc::error_report::render(database, diagnostic);
    assert(rendered.find("warning: suspicious literal") != std::string::npos);
    assert(rendered.find("comment.cst") == std::string::npos);
    assert(rendered.find("consider naming this constant") == std::string::npos);
}

void test_render_ignores_label_with_invalid_public_span() {
    cstc::error_report::SourceDatabase database;
    const std::string source = "let x = 0;\n";
    const auto source_id = database.add_source("label.cst", source);
    const cstc::error_report::SourceSpan invalid_span{
        .source_id = source_id,
        .start = source.size() + 1,
        .end = source.size() + 1,
    };

    cstc::error_report::Diagnostic diagnostic;
    diagnostic.severity = cstc::error_report::Severity::Error;
    diagnostic.message = "broken span";
    diagnostic.labels.push_back(
        cstc::error_report::Label{
            .span = invalid_span,
            .message = "should be ignored",
            .style = cstc::error_report::LabelStyle::Primary,
        });

    const std::string rendered = cstc::error_report::render(database, diagnostic);
    assert(rendered.find("error: broken span") != std::string::npos);
    assert(rendered.find("label.cst") == std::string::npos);
    assert(rendered.find("should be ignored") == std::string::npos);
}

} // namespace

int main() {
    test_render_error_span_without_color();
    test_render_comment_from_token_offset();
    test_render_nested_diagnostic_tree();
    test_render_uses_color_when_explicitly_enabled();
    test_render_ignores_comment_with_invalid_public_point();
    test_render_ignores_label_with_invalid_public_span();
    return 0;
}
