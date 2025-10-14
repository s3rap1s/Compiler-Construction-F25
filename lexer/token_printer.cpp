#include "token_printer.hpp"

#include "tokens.hpp"
#include "utils.hpp"

#include <print>
#include <string>
#include <variant>

namespace lexer {

namespace {

constexpr std::array kSyntaxPartSpellings = [] {
    auto toSizeT = [](SyntaxPart c) { return static_cast<std::size_t>(c); };
    constexpr std::size_t kSize = toSizeT(SyntaxPart::Xor) + 1;

    std::array<std::string_view, kSize> map{};
    map[toSizeT(SyntaxPart::And)] = "and";
    map[toSizeT(SyntaxPart::Array)] = "array";
    map[toSizeT(SyntaxPart::Arrow)] = "=>";
    map[toSizeT(SyntaxPart::Assignment)] = ":=";
    map[toSizeT(SyntaxPart::Begin)] = "begin";
    map[toSizeT(SyntaxPart::Boolean)] = "boolean";
    map[toSizeT(SyntaxPart::CloseBracket)] = "]";
    map[toSizeT(SyntaxPart::CloseParenthesis)] = ")";
    map[toSizeT(SyntaxPart::Colon)] = ":";
    map[toSizeT(SyntaxPart::Comma)] = ",";
    map[toSizeT(SyntaxPart::Divide)] = "/";
    map[toSizeT(SyntaxPart::Dot)] = ".";
    map[toSizeT(SyntaxPart::Else)] = "else";
    map[toSizeT(SyntaxPart::End)] = "end";
    map[toSizeT(SyntaxPart::Equal)] = "=";
    map[toSizeT(SyntaxPart::For)] = "for";
    map[toSizeT(SyntaxPart::Greater)] = ">";
    map[toSizeT(SyntaxPart::GreaterEqual)] = ">=";
    map[toSizeT(SyntaxPart::If)] = "if";
    map[toSizeT(SyntaxPart::In)] = "in";
    map[toSizeT(SyntaxPart::Integer)] = "integer";
    map[toSizeT(SyntaxPart::Is)] = "is";
    map[toSizeT(SyntaxPart::Less)] = "<";
    map[toSizeT(SyntaxPart::LessEqual)] = "<=";
    map[toSizeT(SyntaxPart::Loop)] = "loop";
    map[toSizeT(SyntaxPart::Minus)] = "-";
    map[toSizeT(SyntaxPart::Modulo)] = "%";
    map[toSizeT(SyntaxPart::Multiply)] = "*";
    map[toSizeT(SyntaxPart::NewLine)] = "\\n";
    map[toSizeT(SyntaxPart::Not)] = "not";
    map[toSizeT(SyntaxPart::NotEqual)] = "/=";
    map[toSizeT(SyntaxPart::OpenBracket)] = "[";
    map[toSizeT(SyntaxPart::OpenParenthesis)] = "(";
    map[toSizeT(SyntaxPart::Or)] = "or";
    map[toSizeT(SyntaxPart::Plus)] = "+";
    map[toSizeT(SyntaxPart::Print)] = "print";
    map[toSizeT(SyntaxPart::Range)] = "..";
    map[toSizeT(SyntaxPart::Real)] = "real";
    map[toSizeT(SyntaxPart::Record)] = "record";
    map[toSizeT(SyntaxPart::Return)] = "return";
    map[toSizeT(SyntaxPart::Reverse)] = "reverse";
    map[toSizeT(SyntaxPart::Routine)] = "routine";
    map[toSizeT(SyntaxPart::Semicolon)] = ";";
    map[toSizeT(SyntaxPart::Then)] = "then";
    map[toSizeT(SyntaxPart::Type)] = "type";
    map[toSizeT(SyntaxPart::Var)] = "var";
    map[toSizeT(SyntaxPart::While)] = "while";
    map[toSizeT(SyntaxPart::Xor)] = "xor";
    return map;
}();

} // namespace

std::string representSyntaxPart(SyntaxPart sp) {
    std::string repr;
    repr += '\'';
    repr += getSyntaxPartSpelling(sp);
    repr += '\'';
    return repr;
}

std::string representLiteral(const Literal& literal) {
    using namespace std::literals;
    constexpr auto matcher = overloaded{
        [](const BooleanLiteral& bl) { return bl.value ? "true"s : "false"s; },
        [](const IntegerLiteral& il) { return std::to_string(il.value); },
        [](const RealLiteral& rl) { return std::to_string(rl.value); },
        [](const StringLiteral& sl) { return '"' + sl.value + '"'; },
    };
    return "literal " + std::visit(matcher, literal);
}

std::string representToken(const Token& token) {
    constexpr auto matcher = overloaded{
        [](const Identifier& id) { return std::format("identifier \"{}\"", id.name); },
        [](const SyntaxPart& sp) { return representSyntaxPart(sp); },
        [](const Literal& lit) { return representLiteral(lit); },
    };
    return std::visit(matcher, token.payload);
}

std::string_view getSyntaxPartSpelling(SyntaxPart sp) {
    return kSyntaxPartSpellings[static_cast<std::size_t>(sp)];
}

void print(const Token& token) {
    const auto& [span, payload] = token;
    std::string repr = representToken(token);
    std::println("Token {} on line {}, [{}, {})", repr, span.line_no, span.begin, span.end);
}

} // namespace lexer
