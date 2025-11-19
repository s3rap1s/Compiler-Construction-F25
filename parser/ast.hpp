#pragma once

#include "common.hpp"
#include "lexer/tokens.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace parser {

struct Identifier {
    Span span;
    std::string text;
};

using TypeId = std::size_t;

/* ============
 * Expressions
 * ============
 */

struct Expression;

// Not an general expression. Only for print statements
struct StringLiteral {
    Span span;
    std::string value;
};

struct IntegerLiteral {
    Span span;
    decltype(lexer::IntegerLiteral::value) value;
};

struct RealLiteral {
    Span span;
    decltype(lexer::RealLiteral::value) value;
};

struct BooleanLiteral {
    Span span;
    decltype(lexer::BooleanLiteral::value) value;
};

struct ParenthesizedExpression {
    std::unique_ptr<Expression> expression;
};

struct RoutineCall;
struct ModifiablePrimary;
struct UnarySign;
using Primary = std::variant<IntegerLiteral,
                             RealLiteral,
                             BooleanLiteral,
                             RoutineCall,
                             ModifiablePrimary,
                             UnarySign,
                             ParenthesizedExpression>;

struct RoutineCall {
    Identifier routine_name;
    std::vector<Expression> arguments;
    TypeId type = -1;
};

struct ModifiablePrimary {
    struct Accessor; // wait until Expression is defined

    Identifier variable;
    std::vector<Accessor> accessors;
    TypeId variable_type = -1;
};

struct UnarySign {
    enum class Sign : char {
        Plus,
        Minus,
    };

    Span sign_span;
    std::unique_ptr<Primary> operand;
    Sign sign;
    TypeId type = -1;
};

struct Summand {
    enum class Operator : char {
        Multiply,
        Divide,
        Modulo,
    };
    struct Operation {
        Operator operator_;
        Primary next_operand;
        TypeId result_type = -1;
    };

    Primary first; // same as Factor
    std::vector<Operation> rest;
    TypeId type = -1;

    // A constructor's declaration keeps the Clangd (but not Clang++) problem away
    inline Summand(Primary first, std::vector<Operation> rest);
};

struct NumberExpression {
    enum class Operator : char {
        Plus,
        Minus,
    };
    struct Operation {
        Operator operator_;
        Summand next_operand;
        TypeId result_type = -1;
    };

    Summand first;
    std::vector<Operation> rest;
    TypeId type = -1;

    inline NumberExpression(Summand first, std::vector<Operation> rest);

    // NumberExpression() = default; // Explicit default constructor keeps the Clang/Clangd problem away // NOLINT
    // no longer needed due to custom constructor
};

struct Relation {
    enum class Operator : char {
        Less,
        LessOrEqual,
        Greater,
        GreaterOrEqual,
        Equal,
        NotEqual,
    };
    struct Operation {
        Operator operator_;
        NumberExpression next_operand;
    };

    NumberExpression first;
    std::optional<Operation> second;
    TypeId type = -1;
};

struct NotExpression {
    Span not_span;
    Primary operand;
    TypeId type = -1;
};

using BooleanExpression = std::variant<Relation, NotExpression>;

struct Expression {
    enum class Operator : char {
        And,
        Or,
        Xor,
    };

    BooleanExpression first;
    std::vector<std::pair<Operator, BooleanExpression>> rest;
    TypeId type = -1;
};

struct Index {
    Span bracket_span;
    Expression value;
};

struct ModifiablePrimary::Accessor {
    std::variant<Index, Identifier> key;
    TypeId type = -1;

    template <typename... Args>
        requires std::constructible_from<decltype(key), Args&&...>
    explicit Accessor(Args&&... args) : key{std::forward<Args>(args)...} {}
};

/* ============
 * Types
 * ============
 */
struct IntegerType {
    Span span;
};

struct RealType {
    Span span;
};

struct BoolType {
    Span span;
};

struct RecordType;
struct ArrayType;
using Type = std::variant<IntegerType, RealType, BoolType, RecordType, ArrayType, Identifier>;

struct VariableDeclaration;
struct RecordType {
    std::vector<VariableDeclaration> fields;
};

struct ArrayType {
    std::optional<Expression> size;
    std::unique_ptr<Type> element_type;
    std::size_t computed_size = -1;
};

/* ============
 * Statements
 * ============
 */
struct AssignmentStatement;
struct WhileStatement;
struct ForStatement;
struct IfStatement;
struct PrintStatement;
struct ReturnStatement;
struct NoopStatement {};
using Statement = std::variant<AssignmentStatement,
                               RoutineCall,
                               WhileStatement,
                               ForStatement,
                               IfStatement,
                               PrintStatement,
                               ReturnStatement,
                               NoopStatement>;

struct VariableDeclaration;
struct TypeDeclaration;
using Block = std::vector<std::variant<VariableDeclaration, TypeDeclaration, Statement>>;
// Why declarations are not considered statements?

struct AssignmentStatement {
    ModifiablePrimary target;
    Expression expression;
};

struct IfStatement {
    Expression condition;
    Block true_branch;
    std::optional<Block> false_branch;
};

struct WhileStatement {
    Expression condition;
    Block body;
};

struct ForStatement {
    Identifier variable_name;
    std::variant<Expression, std::pair<Expression, Expression>> range;
    Block body;
    bool is_reversed;
};

struct PrintStatement {
    std::vector<std::variant<Expression, StringLiteral>> arguments;
};

struct ReturnStatement {
    std::optional<Expression> value;
};

/* ============
 * Declarations
 * ============
 */
struct VariableDeclaration {
    Identifier name;
    std::optional<Type> type;
    std::optional<Expression> value;
    // mamoi klyanus', ne budet dva optional pustimi. (c) Maxim Fomin
    TypeId resolved_type = -1;
};

struct ParameterDeclaration {
    Identifier name;
    Type type;
    TypeId resolved_type = -1;
};

struct RoutineDeclaration {
    struct ReturnType {
        Type type;
        TypeId resolved = -1;
    };

    Identifier name;
    std::vector<ParameterDeclaration> parameters;
    std::optional<std::variant<Block, Expression>> body;
    std::optional<ReturnType> return_type;
};

struct TypeDeclaration {
    Identifier name;
    Type type;
    TypeId resolved_type = -1;
};

struct Program {
    std::vector<std::variant<VariableDeclaration, TypeDeclaration, RoutineDeclaration>> declarations;
};

/* =========================
 * Constructors' definitions
 * =========================
 */
NumberExpression::NumberExpression(Summand first, std::vector<Operation> rest)
    : first{std::move(first)}, rest{std::move(rest)} {}

Summand::Summand(Primary first, std::vector<Operation> rest) : first{std::move(first)}, rest{std::move(rest)} {}

/* =========================
 * Helpers
 * =========================
 */
Span getSpan(const Expression& expr);

Span getSpan(const BooleanExpression& expr);

Span getSpan(const Relation& relation);

Span getSpan(const NumberExpression& expr);

Span getSpan(const Summand& summand);

Span getSpan(const Primary& primary);

} // namespace parser
