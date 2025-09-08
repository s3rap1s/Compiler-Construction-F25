#include <string>

enum class Code {
    Routine,
    Return,
    Int,
    Real,
    Boolean,
    Loop,
    Begin,
    End, 
    Var,
    Is,
    Size,
    Type,
    Array, 
    Record,
    For,
    While, 
    If,
    Reverse,
    In, 
    Arrow, 
    Print,
    Then,
    Else,
    Colon,
    Semicolon,
    Assignment,
    Range,
    Dot,
    Comma,
    Int,
    Real,
    Boolean,
    String,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    NotEqual,
    Equal,
    Multiply,
    Divide,
    Modulo,
    Minus,
    Plus,
    And,
    Not,
    Or,
    Xor,
    OpenParenthesis,
    ClosedParenthesis,
    OpenBracket,
    ClosedBracket,
    Dot
};

struct Span {
    long line_no;
    int begin, end;
};

struct Token {
    Span span;
    Code code;
};

struct Literal : Token {};

struct Identifier : Token {
    std::string name;   
};

struct IntegerLiteral : Literal {
    long value;
};

struct RealLiteral : Literal {
    double value;
};

struct BooleanLiteral : Literal {
    bool value;
};

struct StringLiteral : Literal{
    std::string value;
};

