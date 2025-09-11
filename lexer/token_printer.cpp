#include "token_printer.hpp"

#include "tokens.hpp"
#include "utils.hpp"

#include <print>
#include <string>
#include <variant>

namespace {

constexpr auto kSyntaxPartSpellings = [] {
    using Type = SyntaxPart::Type;
    auto toSizeT = [](Type c) { return static_cast<std::size_t>(c); };
    constexpr std::size_t kSize = toSizeT(Type::Xor) + 1;

    std::array<std::string_view, kSize> map{};
    map[toSizeT(Type::And)] = "and";
    map[toSizeT(Type::Array)] = "array";
    map[toSizeT(Type::Arrow)] = "=>";
    map[toSizeT(Type::Assignment)] = ":=";
    map[toSizeT(Type::Begin)] = "begin";
    map[toSizeT(Type::Boolean)] = "boolean";
    map[toSizeT(Type::ClosedBracket)] = "]";
    map[toSizeT(Type::ClosedParenthesis)] = ")";
    map[toSizeT(Type::Colon)] = ":";
    map[toSizeT(Type::Comma)] = ",";
    map[toSizeT(Type::Divide)] = "/";
    map[toSizeT(Type::Dot)] = ".";
    map[toSizeT(Type::Else)] = "else";
    map[toSizeT(Type::End)] = "end";
    map[toSizeT(Type::Equal)] = "=";
    map[toSizeT(Type::For)] = "for";
    map[toSizeT(Type::Greater)] = ">";
    map[toSizeT(Type::GreaterEqual)] = ">=";
    map[toSizeT(Type::If)] = "if";
    map[toSizeT(Type::In)] = "in";
    map[toSizeT(Type::Integer)] = "integer";
    map[toSizeT(Type::Is)] = "is";
    map[toSizeT(Type::Less)] = "<";
    map[toSizeT(Type::LessEqual)] = "<=";
    map[toSizeT(Type::Loop)] = "loop";
    map[toSizeT(Type::Minus)] = "-";
    map[toSizeT(Type::Modulo)] = "%";
    map[toSizeT(Type::Multiply)] = "*";
    map[toSizeT(Type::Not)] = "not";
    map[toSizeT(Type::NotEqual)] = "/=";
    map[toSizeT(Type::OpenBracket)] = "[";
    map[toSizeT(Type::OpenParenthesis)] = "(";
    map[toSizeT(Type::Or)] = "or";
    map[toSizeT(Type::Plus)] = "+";
    map[toSizeT(Type::Print)] = "print";
    map[toSizeT(Type::Range)] = "..";
    map[toSizeT(Type::Real)] = "real";
    map[toSizeT(Type::Record)] = "record";
    map[toSizeT(Type::Return)] = "return";
    map[toSizeT(Type::Reverse)] = "reverse";
    map[toSizeT(Type::Routine)] = "routine";
    map[toSizeT(Type::Semicolon)] = ";";
    map[toSizeT(Type::Then)] = "then";
    map[toSizeT(Type::Type)] = "type";
    map[toSizeT(Type::Var)] = "var";
    map[toSizeT(Type::While)] = "while";
    map[toSizeT(Type::Xor)] = "xor";
    return map;
}();

bool isPunctuation(SyntaxPart::Type type) {
    using Type = SyntaxPart::Type;
    switch (type) {
    case Type::Arrow:
    case Type::Assignment:
    case Type::ClosedBracket:
    case Type::ClosedParenthesis:
    case Type::Colon:
    case Type::Comma:
    case Type::Divide:
    case Type::Dot:
    case Type::Equal:
    case Type::Greater:
    case Type::GreaterEqual:
    case Type::Less:
    case Type::LessEqual:
    case Type::Minus:
    case Type::Modulo:
    case Type::Multiply:
    case Type::NotEqual:
    case Type::OpenBracket:
    case Type::OpenParenthesis:
    case Type::Plus:
    case Type::Range:
    case Type::Semicolon:
        return true;
    default:
        return false;
    }
}

std::string representSyntaxPart(const SyntaxPart& sp) {
    std::string repr;
    if (!isPunctuation(sp.type))
        repr += '<';
    repr += kSyntaxPartSpellings[static_cast<std::size_t>(sp.type)];
    if (!isPunctuation(sp.type))
        repr += '>';
    return repr;
}

std::string representLiteral(const Literal& literal) {
    using namespace std::literals;
    auto matcher = overloaded{
        [](const BooleanLiteral& bl) { return bl.value ? "true"s : "false"s; },
        [](const IntegerLiteral& il) { return std::to_string(il.value); },
        [](const RealLiteral& rl) { return std::to_string(rl.value); },
        [](const StringLiteral& sl) { return '"' + sl.value + '"'; },
    };
    return "literal " + std::visit(matcher, literal);
}

} // namespace

void TokenPrinter::print(const Token& token) {
    auto matcher = overloaded{
        [](const Identifier& id) { return std::format("identifier \"{}\"", id.name); },
        [](const SyntaxPart& sp) { return representSyntaxPart(sp); },
        [](const Literal& lit) { return representLiteral(lit); },
    };
    const auto& [span, payload] = token;
    std::string repr = std::visit(matcher, payload);
    std::println("Token {} on line {}, [{}, {})", repr, span.line_no, span.begin, span.end);
}
