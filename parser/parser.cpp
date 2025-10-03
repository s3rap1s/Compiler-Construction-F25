#include "parser.hpp"

#include "lexer/lexer.hpp"
#include "lexer/tokens.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/syntax_error.hpp"
#include "parser/types.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#define BIND(var, monad)                                                                                               \
    auto&& var##E = monad;                                                                                             \
    if (!var##E)                                                                                                       \
        return std::unexpected{var##E.error()};                                                                        \
    auto&& var = *var##E;

#define BIND_UNIT(var, monad)                                                                                          \
    if (auto&& var##E = monad; !var##E)                                                                                \
        return std::unexpected{var##E.error()};

#define BIND_SET(var, monad)                                                                                           \
    auto&& var##E = monad;                                                                                             \
    if (!var##E)                                                                                                       \
        return std::unexpected{var##E.error()};                                                                        \
    var = std::forward_like<decltype(var##E)>(*var##E);

namespace parser {

namespace {

using lexer::Token;
using SyntaxPartType = lexer::SyntaxPart::Type;

// NOLINTBEGIN(*-no-recursion)
class Parser {
    std::vector<Token> tokens;
    std::vector<Token>::iterator next_token_it = tokens.begin();

    template <typename T>
    using ParsingExpected = std::expected<T, SyntaxError>;

    template <typename T>
    [[nodiscard]] ParsingExpected<T> getToken() const {
        if (next_token_it == tokens.end())
            return std::unexpected{UnexpectedEndOfFile{}};
        Token& t = *next_token_it;
        if (!std::holds_alternative<T>(t.payload))
            return std::unexpected{UnexpectedTokenType{t}};
        return std::get<T>(t.payload);
    }

    template <typename T>
    [[nodiscard]] ParsingExpected<T> consumeToken() {
        auto t = getToken<T>();
        if (t)
            ++next_token_it;
        return t;
    }

    ParsingExpected<void> assertKeyword(SyntaxPartType keyword) {
        auto t = getToken<lexer::SyntaxPart>();
        if (!t && std::holds_alternative<UnexpectedTokenType>(t.error()))
            return std::unexpected{KeywordExpected{keyword}};
        return t.and_then([keyword, this](lexer::SyntaxPart& sp) -> ParsingExpected<void> {
            if (sp.type == keyword) {
                ++next_token_it;
                return {};
            }
            return std::unexpected{KeywordExpected{keyword}};
        });
    }

    ParsingExpected<Program> parseProgram() {
        Program program;
        while (true) {
            if (next_token_it == tokens.end())
                break;
            Token& t = *next_token_it;

            if (!std::holds_alternative<lexer::SyntaxPart>(t.payload))
                return std::unexpected{
                    KeywordsExpected{{SyntaxPartType::Var, SyntaxPartType::Type, SyntaxPartType::Routine}}};

            switch (std::get<lexer::SyntaxPart>(t.payload).type) {
            case SyntaxPartType::Var:
                if (auto vd = parseVariableDeclaration())
                    program.declarations.emplace_back(std::move(*vd));
                else
                    return std::unexpected{vd.error()};
                break;
            case SyntaxPartType::Type:
                if (auto td = parseTypeDeclaration())
                    program.declarations.emplace_back(std::move(*td));
                else
                    return std::unexpected{td.error()};
                break;
            case SyntaxPartType::Routine:
                if (auto rd = parseRoutineDeclaration())
                    program.declarations.emplace_back(std::move(*rd));
                else
                    return std::unexpected{rd.error()};
                break;
            default:
                return std::unexpected{
                    KeywordsExpected{{SyntaxPartType::Var, SyntaxPartType::Type, SyntaxPartType::Routine}}};
            }
        }
        return program;
    }

    ParsingExpected<VariableDeclaration> parseVariableDeclaration() {
        BIND_UNIT(var_keyword, assertKeyword(SyntaxPartType::Var));
        BIND(id, consumeToken<lexer::Identifier>());

        if (auto colon = assertKeyword(SyntaxPartType::Colon)) {
            BIND(type, parseType());

            auto is = assertKeyword(SyntaxPartType::Is);
            if (!is)
                return VariableDeclaration{
                    .identifier = std::move(id.name), .type = std::move(type), .value = std::nullopt};

            BIND(init_value, parseExpression());
            return VariableDeclaration{
                .identifier = std::move(id.name), .type = std::move(type), .value = std::move(init_value)};
        }

        if (auto is = assertKeyword(SyntaxPartType::Is)) {
            BIND(init_value, parseExpression());
            return VariableDeclaration{
                .identifier = std::move(id.name), .type = std::nullopt, .value = std::move(init_value)};
        }

        return std::unexpected{KeywordsExpected{{SyntaxPartType::Is, SyntaxPartType::Colon}}};
    }

    ParsingExpected<TypeDeclaration> parseTypeDeclaration() {
        BIND_UNIT(type_keyword, assertKeyword(SyntaxPartType::Type));
        BIND(id, consumeToken<lexer::Identifier>());
        BIND_UNIT(is, assertKeyword(SyntaxPartType::Is));
        BIND(type, parseType());
        return TypeDeclaration{.identifier = std::move(id.name), .type = std::move(type)};
    }

    ParsingExpected<RoutineDeclaration> parseRoutineDeclaration() {
        BIND_UNIT(routine_keyword, assertKeyword(SyntaxPartType::Routine));
        BIND(id, consumeToken<lexer::Identifier>());

        BIND_UNIT(open_par, assertKeyword(SyntaxPartType::OpenParenthesis));
        std::vector<ParameterDecalration> params;
        while (true) {
            auto param_id = consumeToken<lexer::Identifier>();
            if (!param_id)
                break;
            BIND_UNIT(colon, assertKeyword(SyntaxPartType::Colon));
            BIND(type, parseType());
            params.emplace_back(std::move(param_id->name), std::move(type));
        }
        if (auto closed_par = assertKeyword(SyntaxPartType::ClosedParenthesis); !closed_par)
            return std::unexpected{RoutineParamOrCloseParExpected{}};

        std::optional<Type> return_type;
        if (auto colon = assertKeyword(SyntaxPartType::Colon)) {
            BIND_SET(return_type, parseType());
        }

        decltype(RoutineDeclaration::body) body;
        if (auto is = assertKeyword(SyntaxPartType::Is)) {
            BIND_SET(body, parseBody());
            BIND_UNIT(end, assertKeyword(SyntaxPartType::End));
        }
        if (auto arrow = assertKeyword(SyntaxPartType::Arrow)) {
            BIND_SET(body, parseExpression());
        }

        return RoutineDeclaration{.identifier = std::move(id.name),
                                  .parameters = std::move(params),
                                  .body = std::move(body),
                                  .return_type = std::move(return_type)};
    }

    ParsingExpected<Type> parseType() {
        Type type;
        if (auto id = consumeToken<lexer::Identifier>())
            type = std::move(id->name);
        else if (auto sp = getToken<lexer::SyntaxPart>()) {
            switch (sp->type) {
            case SyntaxPartType::Integer:
                ++next_token_it;
                type = IntegerType{};
                break;
            case SyntaxPartType::Real:
                ++next_token_it;
                type = RealType{};
                break;
            case SyntaxPartType::Boolean:
                ++next_token_it;
                type = BoolType{};
                break;
            case SyntaxPartType::Array: {
                BIND_SET(type, parseArray());
                break;
            }
            case SyntaxPartType::Record: {
                BIND_SET(type, parseRecord());
                break;
            }
            default:
                return std::unexpected{TypeExpected{}};
            }
            return type;
        }
        return std::unexpected{TypeExpected{}};
    }

    ParsingExpected<ArrayType> parseArray() {
        BIND_UNIT(array_keyword, assertKeyword(SyntaxPartType::Array));
        BIND_UNIT(open_bracket, assertKeyword(SyntaxPartType::OpenBracket));
        std::optional<Expression> size;
        if (auto sizeE = parseExpression())
            size = std::move(*sizeE);
        BIND_UNIT(close_bracket, assertKeyword(SyntaxPartType::ClosedBracket));
        BIND(type, parseType());
        return ArrayType{.size = std::move(size), .element_type = std::make_unique<Type>(std::move(type))};
    }

    ParsingExpected<RecordType> parseRecord() {
        BIND_UNIT(record_keyword, assertKeyword(SyntaxPartType::Record));
        RecordType record;
        while (true) {
            auto var = parseVariableDeclaration();
            if (!var)
                break;
            record.fields.push_back(std::move(*var));
        }
        return record;
    }

    ParsingExpected<Expression> parseExpression() {}

    ParsingExpected<Block> parseBody() {}

  public:
    explicit Parser(lexer::Lexer lexer) {
        while (true) {
            auto tokenME = lexer.getNextToken();
            if (!tokenME)
                throw;
            if (!*tokenME)
                break;
            tokens.push_back(**tokenME);
        }
    }

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
