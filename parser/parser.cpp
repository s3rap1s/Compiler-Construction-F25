#include "parser.hpp"

#include "lexer/lexer.hpp"
#include "lexer/tokens.hpp"
#include "parser/declarations.hpp"
#include "parser/syntax_error.hpp"
#include "parser/types.hpp"

#include <expected>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace parser {

namespace {

using lexer::Token;
using SyntaxPartType = lexer::SyntaxPart::Type;

class Parser {
    std::vector<Token> tokens;
    std::vector<Token>::iterator next_token_it = tokens.begin();

    template <typename T>
    using ParsingExpected = std::expected<T, SyntaxError>;

    template <typename T>
    [[nodiscard]] ParsingExpected<T> getNextToken() const {
        if (next_token_it == tokens.end())
            return std::unexpected{UnexpectedEndOfFile{}};
        Token& t = *next_token_it;
        if (!std::holds_alternative<T>(t.payload))
            return std::unexpected{UnexpectedTokenType{t}};
        return std::get<T>(t.payload);
    }

    ParsingExpected<void> assertKeyword(SyntaxPartType keyword) {
        auto t = getNextToken<lexer::SyntaxPart>();
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
        if (auto var = assertKeyword(SyntaxPartType::Var); !var)
            return std::unexpected{var.error()};

        auto id = getNextToken<lexer::Identifier>();
        if (!id)
            return std::unexpected{id.error()};

        auto colon = assertKeyword(SyntaxPartType::Colon);
        if (colon) {
            auto type = parseType();
            if (!type)
                return std::unexpected{type.error()};

            auto is = assertKeyword(SyntaxPartType::Is);
            if (!is)
                return VariableDeclaration{.identifier = id->name, .type = std::move(*type), .value = std::nullopt};
        }
    }

    ParsingExpected<TypeDeclaration> parseTypeDeclaration() {}

    ParsingExpected<RoutineDeclaration> parseRoutineDeclaration() {}

    ParsingExpected<Type> parseType() {}

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

} // namespace

std::expected<Program, SyntaxError> parse(lexer::Lexer lexer) {
    Parser parser{std::move(lexer)};
    return parser.parse();
}

} // namespace parser
