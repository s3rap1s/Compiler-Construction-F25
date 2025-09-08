#include <fstream>
#include <string>
#include <utility>

#include "lexer.hpp"

int main() {
    std::string s;
    std::fstream file{"examples/loops.impp", file.in};
    for (std::string buf; std::getline(file, buf);) {
        s += buf;
    }

    Lexer lexer{std::move(s)};
    while (auto token = lexer.getNextToken()) {
        token->print();
    }
}
