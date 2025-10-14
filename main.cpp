#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "lexer/lexer.hpp"
#include "lexer/lexing_error.hpp"
#include "lexer/token_printer.hpp"
#include "lexer/tokens.hpp"
#include "parser/parser.hpp"
#include "parser/syntax_error.hpp"
#include "utils.hpp"

using namespace lexer;
using namespace parser;

namespace {

std::string readFile(std::fstream& file) {
    auto file_size = file.tellg();
    std::string s(file_size, '\0');
    file.seekg(0);
    file.read(s.data(), file_size);
    return s;
}

template <typename... Args>
void logError(std::format_string<Args...> format, Args&&... args) {
    std::println(format, std::forward<Args>(args)...);
}

void handleLexingError(const LexingError& error) {
    std::visit(overloaded{
                   [](const IntegerLiteralError& e) { logError("Wrong integer literal: {}", e.literal); },
                   [](const RealLiteralError& e) { logError("Wrong real literal: {}", e.literal); },
                   [](const UnknownToken& e) { logError("Unknown token: {}", e.token); },
               },
               error);
}

std::string representListOfKeywords(std::span<const SyntaxPart> sps) {
    using namespace std::views;
    using namespace std::literals;
    return sps | transform(representSyntaxPart) | join_with(", "sv) | std::ranges::to<std::string>();
}

void handleSyntaxError(const SyntaxError& error) {
    const Span& error_location = error.span;
    logError("Error at {}:{}", error_location.line_no, error_location.begin);
    std::visit(
        overloaded{
            [](const LexingError& e) { handleLexingError(e); },
            [](const KeywordExpected& e) { logError("Expected {}", representSyntaxPart(e.keyword)); },
            [](const KeywordsExpected& e) { logError("Expected one of: {}", representListOfKeywords(e.options)); },
            [](const TokenExpected& e) { logError("Expected {}", e.expected); },
            [](const RoutineParamOrCloseParExpected&) {
                std::println(stderr,
                             "Expected a parameter declaration or {}",
                             representSyntaxPart(SyntaxPart::CloseParenthesis));
            },
            [](const TypeExpected&) { logError("Expected a type"); },
            [](const LiteralExpected& e) { logError("Expected {} literal", e.expected); },
            [](const NumberLiteralExpected&) { logError("Expected a number literal"); },
            [](const PrimaryExpressionExpected&) { logError("Expected an expression"); },
            [](const StringLiteralOrExpressionExpected&) { logError("Expected a string or an expression"); },
            [](const DeclarationExpected&) { logError("Expected a declaration"); },
            [](const SeparatorExpected&) {
                logError("Expected a newline or {}", representSyntaxPart(SyntaxPart::Semicolon));
            },
        },
        error.payload);
}

} // namespace

int main(int argc, const char** argv) {
    if (argc < 2) {
        logError("Specify a file to analyze");
        return EXIT_FAILURE;
    }

    // read entire file into string
    std::fstream file{argv[1], file.in | file.ate};
    if (!file) {
        logError("Failed to open the file");
        return EXIT_FAILURE;
    }
    std::string program_text = readFile(file);

    Lexer lexer{std::move(program_text)};
    std::expected<Program, SyntaxError> ast = parse(std::move(lexer));
    if (!ast) {
        handleSyntaxError(ast.error());
        return EXIT_FAILURE;
    }

    std::println("ok");
}
