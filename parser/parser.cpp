#include "parser.hpp"

#include "common.hpp"
#include "lexer/iterator.hpp"
#include "lexer/lexer.hpp"
#include "lexer/tokens.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/syntax_error.hpp"
#include "parser/types.hpp"
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

template <typename T, typename V, std::size_t I>
struct ElementInVariantCheck : std::bool_constant<std::is_same_v<T, std::variant_alternative_t<I, V>> ||
                                                  ElementInVariantCheck<T, V, I - 1>::value> {};

template <typename T, typename V>
struct ElementInVariantCheck<T, V, 0> : std::bool_constant<std::is_same_v<T, std::variant_alternative_t<0, V>>> {};

template <typename T, typename V>
concept IsPartOfVariant = ElementInVariantCheck<T, V, std::variant_size_v<V> - 1>::value;

using lexer::Token;
using SyntaxPartType = lexer::SyntaxPart::Type;

// NOLINTBEGIN(*-no-recursion)
class Parser {
    lexer::Lexer lexer;
    std::optional<lexer::Lexer::ResultType> token_store = std::nullopt;
    lexer::TokenIterator next_token_it{lexer, token_store}; // initialize after lexer and token store
    Span last_span{};

    template <typename T>
    using ParsingExpected = std::expected<T, SyntaxError>;

    void skipToken() {
        last_span = next_token_it->span;
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
    [[nodiscard]] ParsingExpected<T> getToken() {
        while (true) {
            if (next_token_it == std::default_sentinel) {
                if (isSuccessfullEnd())
                    return std::unexpected{SyntaxError{.span = getLastSpan(), .payload = UnexpectedEndOfFile{}}};
                return std::unexpected{SyntaxError{.span = getLastSpan(), .payload = std::move(token_store->error())}};
            }

            Token& t = *next_token_it;
            if (auto* sp = std::get_if<lexer::SyntaxPart>(&t.payload); sp && sp->type == SyntaxPartType::NewLine) {
                skipToken();
                continue;
            }
            if (!std::holds_alternative<T>(t.payload))
                return makeError(t.span, TokenExpected{Proxy<T>{}});
            return std::get<T>(t.payload);
        }
    }

    template <typename T>
        requires IsPartOfVariant<T, Token::Payload>
    ParsingExpected<T> consumeToken() {
        ParsingExpected<T> t = getToken<T>();
        if (t)
            skipToken();
        return t;
    }

    template <SyntaxPartType keyword>
    [[nodiscard]] ParsingExpected<void> assertKeyword() {
        ParsingExpected<lexer::SyntaxPart> t = getToken<lexer::SyntaxPart>();
        if (!t || t->type != keyword)
            return makeError(getLastSpan(), KeywordExpected{keyword});
        return {};
    }

    template <SyntaxPartType keyword>
    ParsingExpected<void> consumeKeyword() {
        ParsingExpected<void> t = assertKeyword<keyword>();
        if (t)
            skipToken();
        return t;
    }

    template <SyntaxPartType... keywords>
    [[nodiscard]] ParsingExpected<SyntaxPartType> assertKeywords() {
        static constexpr auto kws = {keywords...};
        auto t = getToken<lexer::SyntaxPart>();
        if (!t || !std::ranges::contains(kws, t->type))
            return makeError(getLastSpan(), KeywordsExpected{kws});
        return t->type;
    }

    template <SyntaxPartType... keywords>
    ParsingExpected<SyntaxPartType> consumeKeywords() {
        ParsingExpected<SyntaxPartType> t = assertKeywords<keywords...>();
        if (t)
            skipToken();
        return t;
    }

    template <typename T>
        requires IsPartOfVariant<T, lexer::Literal>
    ParsingExpected<T> consumeLiteral() {
        ParsingExpected<lexer::Literal> lit = getToken<lexer::Literal>();
        if (!lit || !std::holds_alternative<T>(*lit))
            return makeError(getLastSpan(), LiteralExpected{Proxy<T>{}});
        return std::move(std::get<T>(*lit));
    }

    [[nodiscard]] bool assertSeparator() const {
        if (next_token_it == std::default_sentinel)
            return false;
        const auto* sp = std::get_if<lexer::SyntaxPart>(&next_token_it->payload);
        return sp != nullptr && (sp->type == SyntaxPartType::NewLine || sp->type == SyntaxPartType::Semicolon);
    }

    bool consumeSeparator() {
        bool found = assertSeparator();
        if (found)
            skipToken();
        return found;
    }

    ParsingExpected<Program> parseProgram() {
        Program program;
        while (true) {
            auto keywordE = assertKeywords<SyntaxPartType::Var, SyntaxPartType::Type, SyntaxPartType::Routine>();
            if (!keywordE && std::holds_alternative<UnexpectedEndOfFile>(keywordE.error().payload))
                return program;
            if (!keywordE)
                return std::unexpected{std::move(keywordE).error()};

            SyntaxPartType keyword = *keywordE;
            if (keyword == SyntaxPartType::Var) {
                BIND(vd, parseVariableDeclaration());
                program.declarations.emplace_back(std::move(vd));
            } else if (keyword == SyntaxPartType::Type) {
                BIND(td, parseTypeDeclaration());
                program.declarations.emplace_back(std::move(td));
            } else if (keyword == SyntaxPartType::Routine) {
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
        BIND_VOID(consumeKeyword<SyntaxPartType::Var>());
        BIND(id, consumeToken<lexer::Identifier>());
        BIND(defininition_keyword, (consumeKeywords<SyntaxPartType::Is, SyntaxPartType::Colon>()));

        std::optional<Type> type;
        std::optional<Expression> init_value;
        if (defininition_keyword == SyntaxPartType::Colon) {
            BIND_SET(type, parseType());
            if (consumeKeyword<SyntaxPartType::Is>())
                BIND_SET(init_value, parseExpression());
        } else if (defininition_keyword == SyntaxPartType::Is) {
            BIND_SET(init_value, parseExpression());
        } else {
            std::unreachable();
        }

        return VariableDeclaration{
            .identifier = std::move(id).name,
            .type = std::move(type),
            .value = std::move(init_value),
        };
    }

    ParsingExpected<TypeDeclaration> parseTypeDeclaration() {
        BIND_VOID(consumeKeyword<SyntaxPartType::Type>());
        BIND(id, consumeToken<lexer::Identifier>());
        BIND_VOID(consumeKeyword<SyntaxPartType::Is>());
        BIND(type, parseType());
        return TypeDeclaration{.identifier = std::move(id.name), .type = std::move(type)};
    }

    ParsingExpected<RoutineDeclaration> parseRoutineDeclaration() { // NOLINT(*complexity)
        BIND_VOID(consumeKeyword<SyntaxPartType::Routine>());
        BIND(id, consumeToken<lexer::Identifier>());

        BIND_VOID(consumeKeyword<SyntaxPartType::OpenParenthesis>());
        std::vector<ParameterDecalration> params;
        if (!consumeKeyword<SyntaxPartType::CloseParenthesis>()) {
            while (true) {
                BIND(param_id, consumeToken<lexer::Identifier>());
                BIND_VOID(consumeKeyword<SyntaxPartType::Colon>());
                BIND(type, parseType());
                params.emplace_back(std::move(param_id).name, std::move(type));
                BIND(keyword, (consumeKeywords<SyntaxPartType::Comma, SyntaxPartType::CloseParenthesis>()));
                if (keyword == SyntaxPartType::Comma)
                    continue;
                if (keyword == SyntaxPartType::CloseParenthesis)
                    break;
                std::unreachable();
            }
        }

        std::optional<Type> return_type;
        if (auto colon = consumeKeyword<SyntaxPartType::Colon>())
            BIND_SET(return_type, parseType());

        std::optional<std::variant<Block, Expression>> body;
        BIND(keyword, (consumeKeywords<SyntaxPartType::Is, SyntaxPartType::End>()));
        if (keyword == SyntaxPartType::Is) {
            BIND_SET(body, parseBlock());
            BIND_VOID(consumeKeyword<SyntaxPartType::End>());
        } else if (keyword == SyntaxPartType::Arrow) {
            BIND_SET(body, parseExpression());
        } else {
            std::unreachable();
        }

        return RoutineDeclaration{.identifier = std::move(id).name,
                                  .parameters = std::move(params),
                                  .body = std::move(body),
                                  .return_type = std::move(return_type)};
    }

    ParsingExpected<Type> parseType() {
        if (auto id = consumeToken<lexer::Identifier>())
            return std::move(id)->name;

        auto keyword = assertKeywords<SyntaxPartType::Integer,
                                      SyntaxPartType::Real,
                                      SyntaxPartType::Boolean,
                                      SyntaxPartType::Array,
                                      SyntaxPartType::Record>();
        if (!keyword)
            return makeError(getLastSpan(), TypeExpected{});

        switch (*keyword) {
        case SyntaxPartType::Integer:
            skipToken();
            return IntegerType{};
        case SyntaxPartType::Real:
            skipToken();
            return RealType{};
        case SyntaxPartType::Boolean:
            skipToken();
            return BoolType{};
        case SyntaxPartType::Array: {
            BIND(array, parseArray());
            return std::move(array);
        }
        case SyntaxPartType::Record: {
            BIND(record, parseRecord());
            return std::move(record);
        }
        default:
            std::unreachable();
        }
    }

    ParsingExpected<ArrayType> parseArray() {
        BIND_VOID(consumeKeyword<SyntaxPartType::Array>());
        BIND_VOID(consumeKeyword<SyntaxPartType::OpenBracket>());
        std::optional<Expression> size;
        if (!assertKeyword<SyntaxPartType::CloseBracket>())
            BIND_SET(size, parseExpression());
        BIND_VOID(consumeKeyword<SyntaxPartType::CloseBracket>());
        BIND(type, parseType());
        return ArrayType{
            .size = std::move(size),
            .element_type = std::make_unique<Type>(std::move(type)),
        };
    }

    ParsingExpected<RecordType> parseRecord() {
        BIND_VOID(consumeKeyword<SyntaxPartType::Record>());
        RecordType record;
        while (!consumeKeyword<SyntaxPartType::End>()) {
            BIND(var, parseVariableDeclaration());
            record.fields.push_back(std::move(var));
        }
        return record;
    }

    ParsingExpected<Expression> parseExpression() {
        Expression expression;
        BIND_SET(expression.first, parseRelation());

        using SPT = SyntaxPartType;
        while (auto op_keyword = consumeKeywords<SPT::And, SPT::Or, SPT::Xor>()) {
            BIND(next, parseRelation());

            using Op = Expression::Operation;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {
                {SPT::And, Op::And}, {SPT::Or, Op::Or}, {SPT::Xor, Op::Xor}};
            Op operation = std::ranges::find(map, *op_keyword, &MapPair::first)->second;

            expression.rest.emplace_back(operation, std::move(next));
        }
        return expression;
    }

    ParsingExpected<Relation> parseRelation() {
        Relation relation;
        BIND_SET(relation.first, parseNumberExression());

        using SPT = SyntaxPartType;
        if (auto op_keyword = consumeKeywords<SPT::Less,
                                              SPT::LessEqual,
                                              SPT::Greater,
                                              SPT::GreaterEqual,
                                              SPT::Equal,
                                              SPT::NotEqual>()) {
            BIND(next, parseNumberExression());

            using Op = Relation::Operation;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {{SPT::Less, Op::Less},
                                                                   {SPT::LessEqual, Op::LessOrEqual},
                                                                   {SPT::Greater, Op::Greater},
                                                                   {SPT::GreaterEqual, Op::GreaterOrEqual},
                                                                   {SPT::Equal, Op::Equal},
                                                                   {SPT::Equal, Op::Equal}};
            Op op = std::ranges::find(map, *op_keyword, &MapPair::first)->second;

            relation.second = {op, std::move(next)};
        }
        return relation;
    }

    ParsingExpected<NumberExpression> parseNumberExression() {
        NumberExpression expression;
        BIND_SET(expression.first, parseSummand());

        using SPT = SyntaxPartType;
        while (auto op_keyword = consumeKeywords<SPT::Plus, SPT::Minus>()) {
            BIND(next, parseSummand());

            using Op = NumberExpression::Operation;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {{SPT::Plus, Op::Plus}, {SPT::Minus, Op::Minus}};
            Op operation = std::ranges::find(map, *op_keyword, &MapPair::first)->second;

            expression.rest.emplace_back(operation, std::move(next));
        }
        return expression;
    }

    ParsingExpected<Summand> parseSummand() {
        Summand expression;
        BIND_SET(expression.first, parseFactor());
        using SPT = SyntaxPartType;
        while (auto op_keyword = consumeKeywords<SPT::Multiply, SPT::Divide, SPT::Modulo>()) {
            BIND(next, parseFactor());
            using Op = Summand::Operation;
            using MapPair = std::pair<SPT, Op>;
            static constexpr std::initializer_list<MapPair> map = {
                {SPT::Multiply, Op::Multiply}, {SPT::Divide, Op::Divide}, {SPT::Modulo, Op::Modulo}};
            Op operation = std::ranges::find(map, *op_keyword, &MapPair::first)->second;
            expression.rest.emplace_back(operation, std::move(next));
        }
        return expression;
    }

    ParsingExpected<Factor> parseFactor() { // NOLINT(*complexity)
        if (consumeKeyword<SyntaxPartType::OpenParenthesis>()) {
            BIND(expr, parseExpression());
            BIND_VOID(consumeKeyword<SyntaxPartType::CloseParenthesis>());
            return std::make_unique<Expression>(std::move(expr));
        }

        if (auto lit = consumeLiteral<lexer::BooleanLiteral>())
            return lit;

        if (consumeKeyword<SyntaxPartType::Not>()) {
            BIND(lit, consumeLiteral<lexer::IntegerLiteral>());
            return BooleanLiteral{lit.value == 0};
        }

        if (auto sign = consumeKeywords<SyntaxPartType::Plus, SyntaxPartType::Minus>()) {
            bool minus = *sign == SyntaxPartType::Minus;
            if (auto lit = consumeLiteral<lexer::IntegerLiteral>())
                return IntegerLiteral{minus ? -lit->value : lit->value};
            if (auto lit = consumeLiteral<lexer::RealLiteral>())
                return RealLiteral{minus ? -lit->value : lit->value};
            return makeError(getLastSpan(), NumberLiteralExpected{});
        }

        if (auto id = consumeToken<lexer::Identifier>()) {
            BIND(op,
                 (assertKeywords<SyntaxPartType::Dot, SyntaxPartType::OpenBracket, SyntaxPartType::OpenParenthesis>()));
            if (op == SyntaxPartType::OpenParenthesis) {
                BIND(call, parseRoutineCall(std::move(*id)));
                return std::move(call);
            }
            BIND(modifyable, parseModifablePrimary(std::move(*id)));
            return std::move(modifyable);
        }

        return makeError(getLastSpan(), PrimaryExpressionExpected{});
    }

    ParsingExpected<RoutineCall> parseRoutineCall(lexer::Identifier routine) {
        RoutineCall call;
        call.name = std::move(routine).name;
        if (consumeKeyword<SyntaxPartType::OpenParenthesis>()) {
            if (!consumeKeyword<SyntaxPartType::CloseParenthesis>()) {
                while (true) {
                    BIND(arg, parseExpression());
                    call.arguments.push_back(std::move(arg));
                    BIND(keyword, (consumeKeywords<SyntaxPartType::Comma, SyntaxPartType::CloseParenthesis>()));
                    if (keyword == SyntaxPartType::Comma)
                        continue;
                    if (keyword == SyntaxPartType::CloseParenthesis)
                        break;
                    std::unreachable();
                }
            }
        }
        return call;
    }

    ParsingExpected<ModifiablePrimary> parseModifablePrimary(lexer::Identifier variable) {
        ModifiablePrimary mp;
        mp.variable = std::move(variable).name;
        while (auto op_keyword = consumeKeywords<SyntaxPartType::Dot, SyntaxPartType::OpenBracket>()) {
            if (*op_keyword == SyntaxPartType::Dot) {
                BIND(field, consumeToken<lexer::Identifier>());
                mp.accessors.emplace_back(std::move(field).name);
            } else {
                BIND(index, parseExpression());
                BIND_VOID(consumeKeyword<SyntaxPartType::CloseBracket>());
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
                if (assertSeparator() || assertKeyword<SyntaxPartType::OpenParenthesis>()) {
                    BIND(call, parseRoutineCall(std::move(*id)));
                    block.emplace_back(std::move(call));
                } else if (auto op = assertKeywords<SyntaxPartType::Assignment,
                                                    SyntaxPartType::Dot,
                                                    SyntaxPartType::OpenBracket>()) {
                    BIND(assignment, parseAssignment(std::move(*id)));
                    block.emplace_back(std::move(assignment));
                } else {
                    return makeError(getLastSpan(),
                                     KeywordsExpected{{SyntaxPartType::OpenParenthesis,
                                                       SyntaxPartType::Assignment,
                                                       SyntaxPartType::Dot,
                                                       SyntaxPartType::OpenBracket}});
                }
            } else if (auto keyword = assertKeywords<SyntaxPartType::Var,
                                                     SyntaxPartType::Type,
                                                     SyntaxPartType::While,
                                                     SyntaxPartType::For,
                                                     SyntaxPartType::If,
                                                     SyntaxPartType::Print>()) {
                switch (*keyword) {
                case SyntaxPartType::Var: {
                    BIND(var_declaration, parseVariableDeclaration());
                    block.emplace_back(std::move(var_declaration));
                }
                case SyntaxPartType::Type: {
                    BIND(type_declaration, parseTypeDeclaration());
                    block.emplace_back(std::move(type_declaration));
                }
                case SyntaxPartType::While: {
                    BIND(while_loop, parseWhileLoop());
                    block.emplace_back(std::move(while_loop));
                }
                case SyntaxPartType::For: {
                    BIND(for_loop, parseForLoop());
                    block.emplace_back(std::move(for_loop));
                }
                case SyntaxPartType::If: {
                    BIND(if_statement, parseIfStatement());
                    block.emplace_back(std::move(if_statement));
                }
                case SyntaxPartType::Print: {
                    BIND(print, parsePrintStatement());
                    block.emplace_back(std::move(print));
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

    ParsingExpected<AssignmentStatement> parseAssignment(lexer::Identifier base) {
        BIND(target, parseModifablePrimary(std::move(base)));
        BIND_VOID(consumeKeyword<SyntaxPartType::Assignment>());
        BIND(expr, parseExpression());
        return AssignmentStatement{.target = std::move(target), .expression = std::move(expr)};
    }

    ParsingExpected<WhileStatement> parseWhileLoop() {
        BIND_VOID(consumeKeyword<SyntaxPartType::While>());
        BIND(condition, parseExpression());
        BIND_VOID(consumeKeyword<SyntaxPartType::Loop>());
        BIND(body, parseBlock());
        BIND_VOID(consumeKeyword<SyntaxPartType::End>());
        return WhileStatement{.condition = std::move(condition), .body = std::move(body)};
    }

    ParsingExpected<ForStatement> parseForLoop() {
        BIND_VOID(consumeKeyword<SyntaxPartType::For>());
        BIND(counter, consumeToken<lexer::Identifier>());

        BIND_VOID(consumeKeyword<SyntaxPartType::In>());
        BIND(first_expr, parseExpression());
        std::optional<Expression> second_expr;
        if (consumeKeyword<SyntaxPartType::Range>())
            BIND_SET(second_expr, parseExpression());

        bool reversed = consumeKeyword<SyntaxPartType::Reverse>().has_value();

        BIND_VOID(consumeKeyword<SyntaxPartType::Loop>());
        BIND(body, parseBlock());
        BIND_VOID(consumeKeyword<SyntaxPartType::End>());

        if (second_expr) {
            return ForStatement{.counter = std::move(counter).name,
                                .range{std::in_place_index<1>, std::move(first_expr), std::move(*second_expr)},
                                .body = std::move(body),
                                .is_reversed = reversed};
        }
        return ForStatement{.counter = std::move(counter).name,
                            .range = std::move(first_expr),
                            .body = std::move(body),
                            .is_reversed = reversed};
    }

    ParsingExpected<IfStatement> parseIfStatement() {
        BIND_VOID(consumeKeyword<SyntaxPartType::If>());
        BIND(condition, parseExpression());

        BIND_VOID(consumeKeyword<SyntaxPartType::Then>());
        BIND(true_branch, parseBlock());

        std::optional<Block> false_branch;
        if (consumeKeyword<SyntaxPartType::Else>())
            BIND_SET(false_branch, parseBlock());

        BIND_VOID(consumeKeyword<SyntaxPartType::End>());
        return IfStatement{.condition = std::move(condition),
                           .true_branch = std::move(true_branch),
                           .false_branch = std::move(false_branch)};
    }

    ParsingExpected<PrintStatement> parsePrintStatement() {
        BIND_VOID(consumeKeyword<SyntaxPartType::Print>());
        PrintStatement print;

        if (assertSeparator())
            return print;

        if (auto string = consumeLiteral<StringLiteral>())
            print.arguments.emplace_back(std::move(*string));
        else if (auto expr = parseExpression())
            print.arguments.emplace_back(std::move(*expr));
        else
            return print;

        while (consumeKeyword<SyntaxPartType::Comma>()) {
            if (auto string = consumeLiteral<StringLiteral>())
                print.arguments.emplace_back(std::move(*string));
            else if (auto expr = parseExpression())
                print.arguments.emplace_back(std::move(*expr));
            else
                return makeError(getLastSpan(), StringLiteralOrExpressionExpected{});
        }
        return print;
    }

  public:
    explicit Parser(lexer::Lexer lexer) : lexer{std::move(lexer)} {}

    std::expected<Program, SyntaxError> parse() {
        return parseProgram();
    }
};
// NOLINTEND(*-no-recursion)

} // namespace

std::expected<Program, SyntaxError> parse(lexer::Lexer lexer) {
    Parser parser{std::move(lexer)};
    return parser.parse();
}

} // namespace parser
