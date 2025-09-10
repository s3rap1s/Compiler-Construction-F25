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
    Type,
    Var,
    While,
    Xor,
};

constexpr std::array tokenRepresentations = {
    "and",
    "array",
    "=>",
    ":=",
    "begin",
    "boolean",
    "booleanliteral",
    "]",
    ")",
    ":",
    ",",
    "/",
    ".",
    "else",
    "end",
    "=",
    "for",
    ">",
    ">=",
    "identifier",
    "if",
    "in",
    "integer",
    "integerliteral",
    "is",
    "<",
    "<=",
    "loop",
    "-",
    "%",
    "*",
    "not",
    "/=",
    "[",
    "(",
    "or",
    "+",
    "print",
    "..",
    "real",
    "realliteral",
    "record",
    "return",
    "reverse",
    "routine",
    ";",
    "stringliteral",
    "then",
    "type",
    "var",
    "while",
    "xor",
};

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
