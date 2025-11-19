#include "parser.hpp"

#include "common.hpp"
#include "lexer/iterator.hpp"
#include "lexer/lexer.hpp"
#include "lexer/tokens.hpp"
#include "parser/ast.hpp"
#include "parser/syntax_error.hpp"
#include "utils.hpp"

#include <algorithm>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#define BIND(var, monad)                                                                                               \
    auto&& var##E = monad;                                                                                             \
    if (!var##E)                                                                                                       \
        return std::unexpected{std::move(var##E).error()};                                                             \
    auto&& var = *var##E;

#define BIND_VOID(monad)                                                                                               \
    if (auto&& exp = monad; !exp)                                                                                      \
        return std::unexpected{std::move(exp).error()};

#define BIND_SET(var, monad)                                                                                           \
    {                                                                                                                  \
        auto&& monad_ = monad;                                                                                         \
        if (!monad_)                                                                                                   \
            return std::unexpected{std::move(monad_).error()};                                                         \
        var = *std::move(monad_);                                                                                      \
    }

namespace parser {

namespace {

using lexer::SyntaxPart;
using lexer::Token;

template <typename T>
struct SpecificToken {
    [[no_unique_address]] T payload;
    Span span;
};

// NOLINTBEGIN(*-no-recursion)
class Parser {
    lexer::Lexer& lexer; // NOLINT(*ref-data*)
    std::optional<lexer::Lexer::LexingResult> token_store = std::nullopt;
    lexer::TokenIterator next_token_it{lexer, token_store}; // initialize after lexer and token_store fields

    Span last_span{};
    bool newline_encountered = false;

    template <typename T>
    using ParsingExpected = std::expected<T, SyntaxError>;

    void skipToken() {
        newline_encountered = false;
        last_span = next_token_it->span;
        ++next_token_it;
    }

    void skipNewline() {
        ++next_token_it;
    }

    [[nodiscard]] bool isSuccessfullEnd() const {
        // either last result is not an error or no tokens were fetched since the beginning
        return next_token_it == std::default_sentinel && (!token_store || *token_store);
    }

    [[nodiscard]] Span getLastSpan() const {
        if (next_token_it == std::default_sentinel)
            return last_span;
        return next_token_it->span;
    }

    template <typename T>
    [[nodiscard]] static std::unexpected<SyntaxError> makeError(Span span, T&& payload) {
        return std::unexpected{SyntaxError{.span = span, .payload = std::forward<T>(payload)}};
    }

    template <typename T>
        requires IsPartOfVariant<T, Token::Payload>
    [[nodiscard]] ParsingExpected<SpecificToken<T>> getToken() {
        while (true) {
            if (next_token_it == std::default_sentinel) {
                if (isSuccessfullEnd())
                    return makeError(getLastSpan(), TokenExpected{Proxy<T>{}});
                return makeError(getLastSpan(), std::move(token_store->error()));
            }

            Token& t = *next_token_it;
            if (auto* sp = std::get_if<SyntaxPart>(&t.payload); sp && *sp == SyntaxPart::NewLine) {
                newline_encountered = true;
                skipNewline();
                continue;
            }
            if (!std::holds_alternative<T>(t.payload))
                return makeError(t.span, TokenExpected{Proxy<T>{}});
            return SpecificToken<T>{std::get<T>(t.payload), t.span};
        }
    }

    template <typename T>
        requires IsPartOfVariant<T, Token::Payload>
    ParsingExpected<SpecificToken<T>> consumeToken() {
        ParsingExpected<SpecificToken<T>> t = getToken<T>();
        if (t)
            skipToken();
        return t;
    }

    template <SyntaxPart keyword>
    [[nodiscard]] ParsingExpected<SpecificToken<Unit>> assertKeyword() {
        ParsingExpected<SpecificToken<SyntaxPart>> sp = getToken<SyntaxPart>();
        if (!sp || sp->payload != keyword)
            return makeError(getLastSpan(), KeywordExpected{keyword});
        return SpecificToken<Unit>{.payload = {}, .span = sp->span};
    }

    template <SyntaxPart keyword>
    ParsingExpected<SpecificToken<Unit>> consumeKeyword() {
        ParsingExpected<SpecificToken<Unit>> kw = assertKeyword<keyword>();
        if (kw)
            skipToken();
        return kw;
    }

    template <SyntaxPart... keywords>
    [[nodiscard]] ParsingExpected<SpecificToken<SyntaxPart>> assertKeywords() {
        static constexpr auto kws = {keywords...};
        ParsingExpected<SpecificToken<SyntaxPart>> sp = getToken<SyntaxPart>();
        if (!sp || !std::ranges::contains(kws, sp->payload))
            return makeError(getLastSpan(), KeywordsExpected{kws});
        return sp;
    }

    template <SyntaxPart... keywords>
    ParsingExpected<SpecificToken<SyntaxPart>> consumeKeywords() {
        ParsingExpected<SpecificToken<SyntaxPart>> t = assertKeywords<keywords...>();
        if (t)
            skipToken();
        return t;
    }

    template <typename T>
        requires IsPartOfVariant<T, lexer::Literal>
    ParsingExpected<SpecificToken<T>> consumeLiteral() {
        ParsingExpected<SpecificToken<lexer::Literal>> lit = getToken<lexer::Literal>();
        if (!lit || !std::holds_alternative<T>(lit->payload))
            return makeError(getLastSpan(), LiteralExpected{Proxy<T>{}});
        skipToken();
        return SpecificToken<T>{std::move(std::get<T>(lit->payload)), lit->span};
    }

    [[nodiscard]] bool assertSeparator() const {
        if (newline_encountered)
            return true;
        if (next_token_it == std::default_sentinel)
            return false;
        const SyntaxPart* sp = std::get_if<SyntaxPart>(&next_token_it->payload);
        return sp != nullptr && (*sp == SyntaxPart::NewLine || *sp == SyntaxPart::Semicolon);
    }

    bool consumeSeparator() {
        bool found = assertSeparator();
        if (found && !newline_encountered)
            skipToken();
        return found;
    }

    ParsingExpected<Program> parseProgram() {
        Program program;
        while (true) {
            auto keyword_exp = assertKeywords<SyntaxPart::Var, SyntaxPart::Type, SyntaxPart::Routine>();
            if (!keyword_exp && isSuccessfullEnd())
                return program;
            if (!keyword_exp)
                return std::unexpected{std::move(keyword_exp).error()};

            SyntaxPart keyword = keyword_exp->payload;
            if (keyword == SyntaxPart::Var) {
                BIND(vd, parseVariableDeclaration());
                program.declarations.emplace_back(std::move(vd));
            } else if (keyword == SyntaxPart::Type) {
                BIND(td, parseTypeDeclaration());
                program.declarations.emplace_back(std::move(td));
            } else if (keyword == SyntaxPart::Routine) {
                BIND(rd, parseRoutineDeclaration());
                program.declarations.emplace_back(std::move(rd));
            } else {
                std::unreachable();
            }

            if (consumeSeparator())
                continue;
            return makeError(getLastSpan(), SeparatorExpected{});
        }
    }

    ParsingExpected<VariableDeclaration> parseVariableDeclaration() {
        BIND_VOID(consumeKeyword<SyntaxPart::Var>());
        BIND(id, consumeToken<lexer::Identifier>());
        BIND(defininition_keyword, (consumeKeywords<SyntaxPart::Is, SyntaxPart::Colon>()));

        std::optional<Type> type;
        std::optional<Expression> init_value;
        if (defininition_keyword.payload == SyntaxPart::Colon) {
            BIND_SET(type, parseType());
            if (consumeKeyword<SyntaxPart::Is>())
                BIND_SET(init_value, parseExpression());
        } else if (defininition_keyword.payload == SyntaxPart::Is) {
            BIND_SET(init_value, parseExpression());
        } else {
            std::unreachable();
        }

        return VariableDeclaration{
            .name = {.span = id.span, .text = std::move(id.payload).name},
            .type = std::move(type),
            .value = std::move(init_value),
        };
    }

    ParsingExpected<TypeDeclaration> parseTypeDeclaration() {
        BIND_VOID(consumeKeyword<SyntaxPart::Type>());
        BIND(id, consumeToken<lexer::Identifier>());
        BIND_VOID(consumeKeyword<SyntaxPart::Is>());
        BIND(type, parseType());
        return TypeDeclaration{.name = {.span = id.span, .text = std::move(id.payload).name}, .type = std::move(type)};
    }

    ParsingExpected<RoutineDeclaration> parseRoutineDeclaration() { // NOLINT(*complexity)
        BIND_VOID(consumeKeyword<SyntaxPart::Routine>());
        BIND(id, consumeToken<lexer::Identifier>());

        BIND_VOID(consumeKeyword<SyntaxPart::OpenParenthesis>());
        std::vector<ParameterDeclaration> params;
        if (!consumeKeyword<SyntaxPart::CloseParenthesis>()) {
            while (true) {
                BIND(param_id, consumeToken<lexer::Identifier>());
                BIND_VOID(consumeKeyword<SyntaxPart::Colon>());
                BIND(type, parseType());
                params.push_back({{.span = param_id.span, .text = std::move(param_id.payload).name}, std::move(type)});
                BIND(keyword, (consumeKeywords<SyntaxPart::Comma, SyntaxPart::CloseParenthesis>()));
                if (keyword.payload == SyntaxPart::Comma)
                    continue;
                if (keyword.payload == SyntaxPart::CloseParenthesis)
                    break;
                std::unreachable();
            }
        }

        std::optional<Type> return_type;
        if (auto colon = consumeKeyword<SyntaxPart::Colon>())
            BIND_SET(return_type, parseType());

        std::optional<std::variant<Block, Expression>> body;

        if (auto keyword = assertKeywords<SyntaxPart::Is, SyntaxPart::Arrow>()) {
            skipToken();
            if (keyword->payload == SyntaxPart::Is) {
                BIND_SET(body, parseBlock());
                BIND_VOID(consumeKeyword<SyntaxPart::End>());
            } else if (keyword->payload == SyntaxPart::Arrow) {
                BIND_SET(body, parseExpression());
            }
        }
        return RoutineDeclaration{.name = {.span = id.span, .text = std::move(id.payload).name},
                                  .parameters = std::move(params),
                                  .body = std::move(body),
                                  .return_type{std::move(return_type)}};
    }

    ParsingExpected<Type> parseType() {
        if (auto id = consumeToken<lexer::Identifier>())
            return Identifier{.span = id->span, .text = std::move(id->payload).name};

        auto keyword = assertKeywords<SyntaxPart::Integer,
                                      SyntaxPart::Real,
                                      SyntaxPart::Boolean,
                                      SyntaxPart::Array,
                                      SyntaxPart::Record>();
        if (!keyword)
            return makeError(getLastSpan(), TypeExpected{});

        switch (keyword->payload) {
        case SyntaxPart::Integer:
            skipToken();
            return IntegerType{keyword->span};
        case SyntaxPart::Real:
            skipToken();
            return RealType{keyword->span};
        case SyntaxPart::Boolean:
            skipToken();
            return BoolType{keyword->span};
        case SyntaxPart::Array: {
            BIND(array, parseArray());
            return std::move(array);
        }
        case SyntaxPart::Record: {
            BIND(record, parseRecord());
            return std::move(record);
        }
        default:
            std::unreachable();
        }
    }

    ParsingExpected<ArrayType> parseArray() {
        BIND_VOID(consumeKeyword<SyntaxPart::Array>());
        BIND_VOID(consumeKeyword<SyntaxPart::OpenBracket>());
        std::optional<Expression> size;
        if (!assertKeyword<SyntaxPart::CloseBracket>())
            BIND_SET(size, parseExpression());
        BIND_VOID(consumeKeyword<SyntaxPart::CloseBracket>());
        BIND(type, parseType());
        return ArrayType{
            .size = std::move(size),
            .element_type = std::make_unique<Type>(std::move(type)),
        };
    }

    ParsingExpected<RecordType> parseRecord() {
        BIND_VOID(consumeKeyword<SyntaxPart::Record>());
        RecordType record{};
        while (!consumeKeyword<SyntaxPart::End>()) {
            BIND(var, parseVariableDeclaration());
            record.fields.push_back(std::move(var));
        }
        return record;
    }

    ParsingExpected<Expression> parseExpression() {
        BIND(first, parseBooleanExpression());
        decltype(Expression::rest) rest;

        using SPT = SyntaxPart;
        while (auto op_keyword = consumeKeywords<SPT::And, SPT::Or, SPT::Xor>()) {
            BIND(next, parseBooleanExpression());

            using Op = Expression::Operator;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {
                {SPT::And, Op::And}, {SPT::Or, Op::Or}, {SPT::Xor, Op::Xor}};
            Op operation = std::ranges::find(map, op_keyword->payload, &MapPair::first)->second;

            rest.emplace_back(operation, std::move(next));
        }
        return Expression{.first = std::move(first), .rest = std::move(rest)};
    }

    ParsingExpected<BooleanExpression> parseBooleanExpression() {
        if (auto kw = consumeKeyword<SyntaxPart::Not>()) {
            BIND(operand, parsePrimary());
            return NotExpression{.not_span = kw->span, .operand = std::move(operand)};
        }
        BIND(relation, parseRelation());
        return std::move(relation);
    }

    ParsingExpected<Relation> parseRelation() {
        BIND(first, parseNumberExpression());

        using SPT = SyntaxPart;
        if (auto op_keyword = consumeKeywords<SPT::Less,
                                              SPT::LessEqual,
                                              SPT::Greater,
                                              SPT::GreaterEqual,
                                              SPT::Equal,
                                              SPT::NotEqual>()) {
            BIND(next, parseNumberExpression());

            using Op = Relation::Operator;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {{SPT::Less, Op::Less},
                                                                   {SPT::LessEqual, Op::LessOrEqual},
                                                                   {SPT::Greater, Op::Greater},
                                                                   {SPT::GreaterEqual, Op::GreaterOrEqual},
                                                                   {SPT::Equal, Op::Equal},
                                                                   {SPT::NotEqual, Op::NotEqual}};
            Op op = std::ranges::find(map, op_keyword->payload, &MapPair::first)->second;

            decltype(Relation::second) second{{.operator_ = op, .next_operand = std::move(next)}};
            return Relation{.first = std::move(first), .second = std::move(second)};
        }
        return Relation{.first = std::move(first), .second = std::nullopt};
    }

    ParsingExpected<NumberExpression> parseNumberExpression() {
        BIND(first, parseSummand());
        decltype(NumberExpression::rest) rest;

        using SPT = SyntaxPart;
        while (auto op_keyword = consumeKeywords<SPT::Plus, SPT::Minus>()) {
            BIND(next, parseSummand());

            using Op = NumberExpression::Operator;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {{SPT::Plus, Op::Plus}, {SPT::Minus, Op::Minus}};
            Op operation = std::ranges::find(map, op_keyword->payload, &MapPair::first)->second;

            rest.emplace_back(operation, std::move(next));
        }
        return NumberExpression{std::move(first), std::move(rest)};
    }

    ParsingExpected<Summand> parseSummand() {
        BIND(first, parsePrimary());
        decltype(Summand::rest) rest;

        using SPT = SyntaxPart;
        while (auto op_keyword = consumeKeywords<SPT::Multiply, SPT::Divide, SPT::Modulo>()) {
            BIND(next, parsePrimary());

            using Op = Summand::Operator;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {
                {SPT::Multiply, Op::Multiply}, {SPT::Divide, Op::Divide}, {SPT::Modulo, Op::Modulo}};
            Op operation = std::ranges::find(map, op_keyword->payload, &MapPair::first)->second;

            rest.emplace_back(operation, std::move(next));
        }
        return Summand{std::move(first), std::move(rest)};
    }

    ParsingExpected<Primary> parsePrimary() { // NOLINT(*complexity)
        if (auto id = consumeToken<lexer::Identifier>()) {
            if (assertKeyword<SyntaxPart::OpenParenthesis>()) {
                BIND(call, parseRoutineCall({id->span, std::move(id->payload.name)}));
                return std::move(call);
            }
            BIND(modifyable, parseModifablePrimary({id->span, std::move(id->payload.name)}));
            return std::move(modifyable);
        }

        if (consumeKeyword<SyntaxPart::OpenParenthesis>()) {
            BIND(expr, parseExpression());
            BIND_VOID(consumeKeyword<SyntaxPart::CloseParenthesis>());
            return ParenthesizedExpression{std::make_unique<Expression>(std::move(expr))};
        }

        if (auto lit = consumeLiteral<lexer::IntegerLiteral>())
            return IntegerLiteral{.span = lit->span, .value = lit->payload.value};
        if (auto lit = consumeLiteral<lexer::RealLiteral>())
            return RealLiteral{.span = lit->span, .value = lit->payload.value};
        if (auto lit = consumeLiteral<lexer::BooleanLiteral>())
            return BooleanLiteral{.span = lit->span, .value = lit->payload.value};

        if (auto sign = consumeKeywords<SyntaxPart::Plus, SyntaxPart::Minus>()) {
            BIND(operand, parsePrimary());
            return UnarySign{.sign_span = sign->span,
                             .operand = std::make_unique<Primary>(std::move(operand)),
                             .sign =
                                 sign->payload == SyntaxPart::Plus ? UnarySign::Sign::Plus : UnarySign::Sign::Minus};
        }

        return makeError(getLastSpan(), PrimaryExpressionExpected{});
    }

    ParsingExpected<RoutineCall> parseRoutineCall(Identifier routine_name) {
        RoutineCall call;
        call.routine_name = std::move(routine_name);
        if (consumeKeyword<SyntaxPart::OpenParenthesis>()) {
            if (!consumeKeyword<SyntaxPart::CloseParenthesis>()) {
                while (true) {
                    BIND(arg, parseExpression());
                    call.arguments.push_back(std::move(arg));
                    BIND(keyword, (consumeKeywords<SyntaxPart::Comma, SyntaxPart::CloseParenthesis>()));
                    if (keyword.payload == SyntaxPart::Comma)
                        continue;
                    if (keyword.payload == SyntaxPart::CloseParenthesis)
                        break;
                    std::unreachable();
                }
            }
        }
        return call;
    }

    ParsingExpected<ModifiablePrimary> parseModifablePrimary(Identifier variable) {
        ModifiablePrimary mp;
        mp.variable = std::move(variable);
        while (auto op_keyword = consumeKeywords<SyntaxPart::Dot, SyntaxPart::OpenBracket>()) {
            if (op_keyword->payload == SyntaxPart::Dot) {
                BIND(field, consumeToken<lexer::Identifier>());
                mp.accessors.emplace_back(std::in_place_index<1>, field.span, std::move(field.payload.name));
            } else {
                BIND(index, parseExpression());
                BIND_VOID(consumeKeyword<SyntaxPart::CloseBracket>());
                mp.accessors.emplace_back(std::move(index));
            }
        }
        return mp;
    }

    ParsingExpected<Block> parseBlock() { // NOLINT(*complexity)
        Block block;
        while (true) {
            if (auto id = consumeToken<lexer::Identifier>()) {
                // assertSeparator should be first to not skip it by mistake in other functions
                if (assertSeparator() || assertKeyword<SyntaxPart::OpenParenthesis>()) {
                    BIND(call, parseRoutineCall({id->span, std::move(id->payload.name)}));
                    block.emplace_back(std::move(call));
                } else if (auto op =
                               assertKeywords<SyntaxPart::Assignment, SyntaxPart::Dot, SyntaxPart::OpenBracket>()) {
                    BIND(assignment, parseAssignment({id->span, std::move(id->payload.name)}));
                    block.emplace_back(std::move(assignment));
                } else {
                    return makeError(getLastSpan(),
                                     KeywordsExpected{{SyntaxPart::OpenParenthesis,
                                                       SyntaxPart::Assignment,
                                                       SyntaxPart::Dot,
                                                       SyntaxPart::OpenBracket}});
                }
            } else if (auto keyword = assertKeywords<SyntaxPart::Var,
                                                     SyntaxPart::Type,
                                                     SyntaxPart::While,
                                                     SyntaxPart::For,
                                                     SyntaxPart::If,
                                                     SyntaxPart::Print,
                                                     SyntaxPart::Return>()) {
                switch (keyword->payload) {
                case SyntaxPart::Var: {
                    BIND(var_declaration, parseVariableDeclaration());
                    block.emplace_back(std::move(var_declaration));
                    break;
                }
                case SyntaxPart::Type: {
                    BIND(type_declaration, parseTypeDeclaration());
                    block.emplace_back(std::move(type_declaration));
                    break;
                }
                case SyntaxPart::While: {
                    BIND(while_loop, parseWhileLoop());
                    block.emplace_back(std::move(while_loop));
                    break;
                }
                case SyntaxPart::For: {
                    BIND(for_loop, parseForLoop());
                    block.emplace_back(std::move(for_loop));
                    break;
                }
                case SyntaxPart::If: {
                    BIND(if_statement, parseIfStatement());
                    block.emplace_back(std::move(if_statement));
                    break;
                }
                case SyntaxPart::Print: {
                    BIND(print, parsePrintStatement());
                    block.emplace_back(std::move(print));
                    break;
                }
                case SyntaxPart::Return: {
                    BIND(return_statement, parseReturnStatement());
                    block.emplace_back(std::move(return_statement));
                    break;
                }
                default:
                    std::unreachable();
                }
            } else {
                break;
            }

            // if parsed body line succesfully then check separator to continue parsing body
            if (consumeSeparator())
                continue;
            break;
        }
        return block;
    }

    ParsingExpected<AssignmentStatement> parseAssignment(Identifier base) {
        BIND(target, parseModifablePrimary(std::move(base)));
        BIND_VOID(consumeKeyword<SyntaxPart::Assignment>());
        BIND(expr, parseExpression());
        return AssignmentStatement{.target = std::move(target), .expression = std::move(expr)};
    }

    ParsingExpected<WhileStatement> parseWhileLoop() {
        BIND_VOID(consumeKeyword<SyntaxPart::While>());
        BIND(condition, parseExpression());
        BIND_VOID(consumeKeyword<SyntaxPart::Loop>());
        BIND(body, parseBlock());
        BIND_VOID(consumeKeyword<SyntaxPart::End>());
        return WhileStatement{.condition = std::move(condition), .body = std::move(body)};
    }

    ParsingExpected<ForStatement> parseForLoop() {
        BIND_VOID(consumeKeyword<SyntaxPart::For>());
        BIND(counter, consumeToken<lexer::Identifier>());

        BIND_VOID(consumeKeyword<SyntaxPart::In>());
        BIND(first_expr, parseExpression());
        std::optional<Expression> second_expr;
        if (consumeKeyword<SyntaxPart::Range>())
            BIND_SET(second_expr, parseExpression());

        bool reversed = consumeKeyword<SyntaxPart::Reverse>().has_value();

        BIND_VOID(consumeKeyword<SyntaxPart::Loop>());
        BIND(body, parseBlock());
        BIND_VOID(consumeKeyword<SyntaxPart::End>());

        if (second_expr) {
            return ForStatement{.variable_name = {.span = counter.span, .text = std::move(counter.payload).name},
                                .range{std::in_place_index<1>, std::move(first_expr), std::move(*second_expr)},
                                .body = std::move(body),
                                .is_reversed = reversed};
        }
        return ForStatement{.variable_name = {.span = counter.span, .text = std::move(counter.payload).name},
                            .range = std::move(first_expr),
                            .body = std::move(body),
                            .is_reversed = reversed};
    }

    ParsingExpected<IfStatement> parseIfStatement() {
        BIND_VOID(consumeKeyword<SyntaxPart::If>());
        BIND(condition, parseExpression());

        BIND_VOID(consumeKeyword<SyntaxPart::Then>());
        BIND(true_branch, parseBlock());

        std::optional<Block> false_branch;
        if (consumeKeyword<SyntaxPart::Else>())
            BIND_SET(false_branch, parseBlock());

        BIND_VOID(consumeKeyword<SyntaxPart::End>());
        return IfStatement{.condition = std::move(condition),
                           .true_branch = std::move(true_branch),
                           .false_branch = std::move(false_branch)};
    }

    ParsingExpected<PrintStatement> parsePrintStatement() {
        BIND_VOID(consumeKeyword<SyntaxPart::Print>());
        PrintStatement print;

        if (assertSeparator())
            return print;

        if (auto string = consumeLiteral<lexer::StringLiteral>())
            print.arguments.emplace_back(std::in_place_index<1>, string->span, std::move(string->payload).value);
        else if (auto expr = parseExpression())
            print.arguments.emplace_back(std::move(*expr));
        else
            return print;

        while (consumeKeyword<SyntaxPart::Comma>()) {
            if (auto string = consumeLiteral<lexer::StringLiteral>())
                print.arguments.emplace_back(std::in_place_index<1>, string->span, std::move(string->payload).value);
            else if (auto expr = parseExpression())
                print.arguments.emplace_back(std::move(*expr));
            else
                return makeError(getLastSpan(), StringLiteralOrExpressionExpected{});
        }
        return print;
    }

    ParsingExpected<ReturnStatement> parseReturnStatement() {
        BIND_VOID(consumeKeyword<SyntaxPart::Return>());

        std::optional<Expression> value;
        if (!assertSeparator()) {
            BIND_SET(value, parseExpression());
        }
        return ReturnStatement{std::move(value)};
    }

  public:
    explicit Parser(lexer::Lexer& lexer) : lexer{lexer} {}

    std::expected<Program, SyntaxError> parse() {
        return parseProgram();
    }
};
// NOLINTEND(*-no-recursion)

} // namespace

std::expected<Program, SyntaxError> parse(lexer::Lexer& lexer) {
    Parser parser{lexer};
    return parser.parse();
}

} // namespace parser
