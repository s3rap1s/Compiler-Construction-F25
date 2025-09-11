#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <utility>

#include "lexer/lexer.hpp"
#include "lexer/token_printer.hpp"

int main(int argc, const char** argv) {
    if (argc < 2) {
        std::println(stderr, "Specify a file to analyze\n");
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
        auto tokenME = lexer.getNextToken();
        if (!tokenME) {
            std::println("Lexing error");
            return EXIT_FAILURE;
        }
        std::optional<Token>& tokenM = *tokenME;
        if (!tokenM)
            break;
        TokenPrinter::print(*tokenM);
    }
}
