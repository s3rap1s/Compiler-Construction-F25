#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <variant>

#include "lexer/lexer.hpp"
#include "lexer/lexing_error.hpp"
#include "lexer/token_printer.hpp"
#include "utils.hpp"

using namespace lexer;

namespace {

void handleLexingError(const LexingError& error) {
    std::visit(overloaded{
                   [](const IntegerLiteralError& e) { std::println("Wrong integer literal: {}", e.literal); },
                   [](const RealLiteralError& e) { std::println("Wrong real literal: {}", e.literal); },
                   [](const UnknownToken& e) { std::println("Unknown token: {}", e.token); },
               },
               error);
}

} // namespace

int main(int argc, const char** argv) {
    if (argc < 2) {
        std::println(stderr, "Specify a file to analyze");
        return EXIT_FAILURE;
    }

    // read entire file into string
    std::fstream file{argv[1], file.in | file.ate};
    if (!file) {
        std::println(stderr, "Failed to open the file");
        return EXIT_FAILURE;
    }
    auto file_size = file.tellg();
    std::string s(file_size, '\0');
    file.seekg(0);
    file.read(s.data(), file_size);

    Lexer lexer{std::move(s)};
    while (true) {
        auto resultO = lexer.getNextToken();
        if (!resultO)
            break;

        Lexer::ResultType& result = *resultO;
        if (!result) {
            handleLexingError(result.error());
            return EXIT_FAILURE;
        }

        TokenPrinter::print(*result);
    }
}
