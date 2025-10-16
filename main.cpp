#include <cstddef>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "lexer/lexer.hpp"
#include "lexer/lexing_error.hpp"
#include "lexer/token_printer.hpp"
#include "lexer/tokens.hpp"
#include "parser/parser.hpp"
#include "parser/tree_printer.hpp"
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
    std::print(format, std::forward<Args>(args)...);
}

template <typename... Args>
void logErrorLn(std::format_string<Args...> format, Args&&... args) {
    std::println(format, std::forward<Args>(args)...);
}

void handleLexingError(const LexingError& error) {
    std::visit(overloaded{
                   [](const IntegerLiteralError& e) { logErrorLn("Wrong integer literal: {}", e.literal); },
                   [](const RealLiteralError& e) { logErrorLn("Wrong real literal: {}", e.literal); },
                   [](const UnknownToken& e) { logErrorLn("Unknown token: {}", e.token); },
               },
               error);
}

std::string representListOfKeywords(std::span<const SyntaxPart> sps) {
    using namespace std::views;
    using namespace std::literals;
    return sps | transform(representSyntaxPart) | join_with(", "sv) | std::ranges::to<std::string>();
}

void handleSyntaxError(const SyntaxError& error, std::string_view filename, std::string_view program) {
    using namespace std::views;

    const Span& error_location = error.span;
    logError("{}:{}:{}: error: ", filename, error_location.line_no, error_location.column_no);
    std::visit(
        overloaded{
            [](const LexingError& e) { handleLexingError(e); },
            [](const KeywordExpected& e) { logErrorLn("Expected {}", representSyntaxPart(e.keyword)); },
            [](const KeywordsExpected& e) { logErrorLn("Expected one of: {}", representListOfKeywords(e.options)); },
            [](const TokenExpected& e) { logErrorLn("Expected {}", e.expected); },
            [](const RoutineParamOrCloseParExpected&) {
                std::println(stderr,
                             "Expected a parameter declaration or {}",
                             representSyntaxPart(SyntaxPart::CloseParenthesis));
            },
            [](const TypeExpected&) { logErrorLn("Expected a type"); },
            [](const LiteralExpected& e) { logErrorLn("Expected {} literal", e.expected); },
            [](const NumberLiteralExpected&) { logErrorLn("Expected a number literal"); },
            [](const PrimaryExpressionExpected&) { logErrorLn("Expected an expression"); },
            [](const StringLiteralOrExpressionExpected&) { logErrorLn("Expected a string or an expression"); },
            [](const SeparatorExpected&) {
                logErrorLn("Expected a newline or {}", representSyntaxPart(SyntaxPart::Semicolon));
            },
        },
        error.payload);

    const std::size_t line_start = error_location.begin - (error_location.column_no - 1);
    const std::size_t error_length = error_location.end - error_location.begin;
    logErrorLn("    | {:s}",
               program.substr(line_start) | take_while([](char ch) static { return ch != '\n' && ch != '\r'; }));
    logErrorLn("    | {:s}^{:s}", repeat(' ', error_location.column_no - 1), repeat('~', error_length - 1));
}

} // namespace

int main(int argc, const char** argv) {
    if (argc < 2) {
        logErrorLn("Specify a file to analyze");
        return EXIT_FAILURE;
    }

    // read entire file into string
    std::string_view filename = argv[1];
    std::fstream file{argv[1], file.in | file.ate};
    if (!file) {
        logErrorLn("Failed to open the file");
        return EXIT_FAILURE;
    }
    std::string program_text = readFile(file);

    Lexer lexer{std::move(program_text)};
    std::expected<Program, SyntaxError> ast = parse(lexer);
    if (!ast) {
        program_text = std::move(lexer).getProgramText();
        handleSyntaxError(ast.error(), filename, program_text);
        return EXIT_FAILURE;
    }

    print_tree(*ast);
    
    return EXIT_SUCCESS;
}
