#pragma once

#include "common.hpp"

#include <string>
#include <utility>
#include <variant>

namespace lexer {

struct Identifier {
    std::string name;
};

struct IntegerLiteral {
    long long value = 0;
};

struct RealLiteral {
    double value = 0;
};

struct BooleanLiteral {
    bool value = false;
};

struct StringLiteral {
    std::string value;
};

using Literal = std::variant<BooleanLiteral, IntegerLiteral, RealLiteral, StringLiteral>;

/*
 * Keyword or punctuation - something without internal information
 */
struct SyntaxPart {
    enum class Type : char {
        And,
        Array,
        Arrow,
        Assignment,
        Begin,
        Boolean,
        CloseBracket,
        CloseParenthesis,
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
        If,
        In,
        Integer,
        Is,
        Less,
        LessEqual,
        Loop,
        Minus,
        Modulo,
        Multiply,
        NewLine,
        Not,
        NotEqual,
        OpenBracket,
        OpenParenthesis,
        Or,
        Plus,
        Print,
        Range,
        Real,
        Record,
        Return,
        Reverse,
        Routine,
        Semicolon,
        Then,
        Type,
        Var,
        While,
        Xor,
    };

    Type type;
};

struct Token {
    using Payload = std::variant<Literal, Identifier, SyntaxPart>;

    Span span;
    Payload payload;

    Token(Span span, Payload payload) : span{span}, payload{std::move(payload)} {}
};

} // namespace lexer
