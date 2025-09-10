#pragma once

#include <array>
#include <cstddef>
#include <format>
#include <print>
#include <string>

enum class TokenCode : char {
    And,
    Array,
    Arrow,
    Assignment,
    Begin,
    Boolean,
    BooleanLiteral,
    ClosedBracket,
    ClosedParenthesis,
    Colon,
    Comma,
    Divide,
    Dot,
    Else,
    End,
    Equal,
    False,
    For,
    Greater,
    GreaterEqual,
    Identifier,
    If,
    In,
    Integer,
    IntegerLiteral,
    Is,
    Less,
    LessEqual,
    Loop,
    Minus,
    Modulo,
    Multiply,
    Not,
    NotEqual,
    OpenBracket,
    OpenParenthesis,
    Or,
    Plus,
    Print,
    Range,
    Real,
    RealLiteral,
    Record,
    Return,
    Reverse,
    Routine,
    Semicolon,
    StringLiteral,
    Then,
    True,
    Type,
    Var,
    While,
    Xor,
};

constexpr auto tokenRepresentations = []{
    auto toSizeT = [](TokenCode c) { return static_cast<std::size_t>(c); };
    constexpr std::size_t kSize = toSizeT(TokenCode::Xor) + 1;
    std::array<const char*, kSize> map{};
    map[toSizeT(TokenCode::And)] = "and";
    map[toSizeT(TokenCode::Array)] = "array";
    map[toSizeT(TokenCode::Arrow)] = "=>";
    map[toSizeT(TokenCode::Assignment)] = ":=";
    map[toSizeT(TokenCode::Begin)] = "begin";
    map[toSizeT(TokenCode::Boolean)] = "boolean";
    map[toSizeT(TokenCode::BooleanLiteral)] = "booleanliteral";
    map[toSizeT(TokenCode::ClosedBracket)] = "]";
    map[toSizeT(TokenCode::ClosedParenthesis)] = ")";
    map[toSizeT(TokenCode::Colon)] = ":";
    map[toSizeT(TokenCode::Comma)] = ",";
    map[toSizeT(TokenCode::Divide)] = "/";
    map[toSizeT(TokenCode::Dot)] = ".";
    map[toSizeT(TokenCode::Else)] = "else";
    map[toSizeT(TokenCode::End)] = "end";
    map[toSizeT(TokenCode::Equal)] = "=";
    map[toSizeT(TokenCode::False)] = "false";
    map[toSizeT(TokenCode::For)] = "for";
    map[toSizeT(TokenCode::Greater)] = ">";
    map[toSizeT(TokenCode::GreaterEqual)] = ">=";
    map[toSizeT(TokenCode::Identifier)] = "identifier";
    map[toSizeT(TokenCode::If)] = "if";
    map[toSizeT(TokenCode::In)] = "in";
    map[toSizeT(TokenCode::Integer)] = "integer";
    map[toSizeT(TokenCode::IntegerLiteral)] = "integerliteral";
    map[toSizeT(TokenCode::Is)] = "is";
    map[toSizeT(TokenCode::Less)] = "<";
    map[toSizeT(TokenCode::LessEqual)] = "<=";
    map[toSizeT(TokenCode::Loop)] = "loop";
    map[toSizeT(TokenCode::Minus)] = "-";
    map[toSizeT(TokenCode::Modulo)] = "%";
    map[toSizeT(TokenCode::Multiply)] = "*";
    map[toSizeT(TokenCode::Not)] = "not";
    map[toSizeT(TokenCode::NotEqual)] = "/=";
    map[toSizeT(TokenCode::OpenBracket)] = "[";
    map[toSizeT(TokenCode::OpenParenthesis)] = "(";
    map[toSizeT(TokenCode::Or)] = "or";
    map[toSizeT(TokenCode::Plus)] = "+";
    map[toSizeT(TokenCode::Print)] = "print";
    map[toSizeT(TokenCode::Range)] = "..";
    map[toSizeT(TokenCode::Real)] = "real";
    map[toSizeT(TokenCode::RealLiteral)] = "realliteral";
    map[toSizeT(TokenCode::Record)] = "record";
    map[toSizeT(TokenCode::Return)] = "return";
    map[toSizeT(TokenCode::Reverse)] = "reverse";
    map[toSizeT(TokenCode::Routine)] = "routine";
    map[toSizeT(TokenCode::Semicolon)] = ";";
    map[toSizeT(TokenCode::StringLiteral)] = "stringliteral";
    map[toSizeT(TokenCode::Then)] = "then";
    map[toSizeT(TokenCode::True)] = "true";
    map[toSizeT(TokenCode::Type)] = "type";
    map[toSizeT(TokenCode::Var)] = "var";
    map[toSizeT(TokenCode::While)] = "while";
    map[toSizeT(TokenCode::Xor)] = "xor";
    return map;
}();

struct Span {
    std::size_t line_no;
    std::size_t begin;
    std::size_t end;
};

struct Token { // NOLINT(*special-member*)
    Span span;
    TokenCode code;

    virtual ~Token() = default;

    [[nodiscard]] virtual std::string getRepr() const {
        return std::format("<{}>", tokenRepresentations[static_cast<std::size_t>(code)]);
    }

    void print() const {
        std::println("Token {} on line {}, [{},{})", getRepr(), span.line_no, span.begin, span.end);
    }
};

struct Identifier : Token {
    std::string name;

    [[nodiscard]] std::string getRepr() const override {
        return std::format("identifier \"{}\"", name);
    }
};

struct Literal : Token {};

struct IntegerLiteral : Literal {
    long long value = 0;

    [[nodiscard]] std::string getRepr() const override {
        return std::format("literal {}", value);
    }
};

struct RealLiteral : Literal {
    double value = 0;

    [[nodiscard]] std::string getRepr() const override {
        return std::format("literal {}", value);
    }
};

struct BooleanLiteral : Literal {
    bool value = false;

    [[nodiscard]] std::string getRepr() const override {
        return std::format("literal {}", value);
    }
};

struct StringLiteral : Literal {
    std::string value;

    [[nodiscard]] std::string getRepr() const override {
        return std::format("literal \"{}\"", value);
    }
};
