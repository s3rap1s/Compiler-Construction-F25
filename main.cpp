#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include "lexer.hpp"

int main(int argc, const char** argv) {
    if (argc < 2) {
        std::cerr << "Specify a file to analyze\n";
        return 1;
    }

    // read entire file into string
    std::fstream file{argv[1], file.in | file.ate};
    auto file_size = file.tellg();
    std::string s(file_size, '\0');
    file.seekg(0);
    file.read(s.data(), file_size);

    Lexer lexer{std::move(s)};
    while (auto token = lexer.getNextToken()) {
        token->print();
    }
}
