#pragma once

#include <array>
#include <cstddef>
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

    virtual void print() const {
        std::println("Token {} on line {}, [{},{})",
                     tokenRepresentations[static_cast<std::size_t>(code)],
                     span.line_no,
                     span.begin,
                     span.end);
    }
};

struct Literal : Token {};

struct Identifier : Token {
    std::string name;

    void print() const override {
        std::print("Identifier {} ", name);
        Token::print();
    }
};

struct IntegerLiteral : Literal {
    long long value = 0;

    void print() const override {
        std::print("Literal {} ", value);
        Token::print();
    }
};

struct RealLiteral : Literal {
    double value = 0;

    void print() const override {
        std::print("Literal {} ", value);
        Token::print();
    }
};

struct BooleanLiteral : Literal {
    bool value = false;

    void print() const override {
        std::print("Literal {} ", value);
        Token::print();
    }
};

struct StringLiteral : Literal {
    std::string value;

    void print() const override {
        std::print("Literal \"{}\" ", value);
        Token::print();
    }
};
