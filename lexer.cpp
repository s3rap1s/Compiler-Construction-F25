#include "lexer.hpp"

#include "tokens.hpp"

#include <charconv>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

std::unordered_map<std::string_view, TokenCode> getKeywordMap() {
    std::unordered_map<std::string_view, TokenCode> map;
    map["and"] = TokenCode::And;
    map["array"] = TokenCode::Array;
    map["begin"] = TokenCode::Begin;
    map["boolean"] = TokenCode::Boolean;
    map["else"] = TokenCode::Else;
    map["end"] = TokenCode::End;
    map["for"] = TokenCode::For;
    map["if"] = TokenCode::If;
    map["in"] = TokenCode::In;
    map["integer"] = TokenCode::Integer;
    map["is"] = TokenCode::Is;
    map["loop"] = TokenCode::Loop;
    map["not"] = TokenCode::Not;
    map["or"] = TokenCode::Or;
    map["print"] = TokenCode::Print;
    map["real"] = TokenCode::Real;
    map["record"] = TokenCode::Record;
    map["return"] = TokenCode::Return;
    map["reverse"] = TokenCode::Reverse;
    map["routine"] = TokenCode::Routine;
    map["then"] = TokenCode::Then;
    map["type"] = TokenCode::Type;
    map["var"] = TokenCode::Var;
    map["while"] = TokenCode::While;
    map["xor"] = TokenCode::Xor;
    return map;
}

const auto keywordMap = getKeywordMap(); // NOLINT(cert-err58-cpp)

std::optional<TokenCode> findKeyword(std::string_view token) {
    auto it = keywordMap.find(token);
    return it != keywordMap.end() ? std::optional{it->second} : std::nullopt;
}

std::unordered_map<std::string_view, TokenCode> getPunctuationMap() {
    std::unordered_map<std::string_view, TokenCode> map;
    map["=>"] = TokenCode::Arrow;
    map[":="] = TokenCode::Assignment;
    map["]"] = TokenCode::ClosedBracket;
    map[")"] = TokenCode::ClosedParenthesis;
    map[":"] = TokenCode::Colon;
    map[","] = TokenCode::Comma;
    map["/"] = TokenCode::Divide;
    map["."] = TokenCode::Dot;
    map["="] = TokenCode::Equal;
    map[">"] = TokenCode::Greater;
    map[">="] = TokenCode::GreaterEqual;
    map["<"] = TokenCode::Less;
    map["<="] = TokenCode::LessEqual;
    map["-"] = TokenCode::Minus;
    map["%"] = TokenCode::Modulo;
    map["*"] = TokenCode::Multiply;
    map["/="] = TokenCode::NotEqual;
    map["["] = TokenCode::OpenBracket;
    map["("] = TokenCode::OpenParenthesis;
    map["+"] = TokenCode::Plus;
    map[".."] = TokenCode::Range;
    map[";"] = TokenCode::Semicolon;
    return map;
}

const auto punctuationMap = getPunctuationMap(); // NOLINT(cert-err58-cpp)

std::optional<TokenCode> findPunctuation(std::string_view token) {
    auto it = punctuationMap.find(token);
    return it != punctuationMap.end() ? std::optional{it->second} : std::nullopt;
}

} // namespace

[[nodiscard]] Span Lexer::getCurrentSpan(std::size_t token_start) const {
    return Span{.line_no = line_no, .begin = token_start, .end = char_pos};
}

[[nodiscard]] std::shared_ptr<Token> Lexer::makeBasicToken(TokenCode code, std::size_t token_start) const {
    auto token = std::make_shared_for_overwrite<Token>();
    token->span = getCurrentSpan(token_start);
    token->code = code;
    return token;
}

std::shared_ptr<Token> Lexer::getNextToken() {
    // NOLINTBEGIN(*bool-conversion*)
    if (char_pos == file.size())
        return nullptr;
    std::size_t token_start = char_pos;
    std::string buffer;

    while (true) {
        char cur_char = char_pos == file.size() ? '\n' : file[char_pos++];
        if (cur_char == '\n' || cur_char == '\r')
            line_no++;
        switch (currentState) {
        case State::Start:
            token_start = char_pos;
            if (std::isalpha(cur_char)) {
                currentState = State::KeywordOrIdentifier;
                buffer += cur_char;
            } else if (std::isdigit(cur_char)) {
                currentState = State::IntegerLiteral;
                buffer += cur_char;
            } else if (cur_char == '"') {
                currentState = State::StringLiteral;
            } else if (std::isspace(cur_char)) {
            } else {
                currentState = State::Punctuation;
                buffer += cur_char;
            }
            break;
        case State::KeywordOrIdentifier:
            if (std::isalpha(cur_char)) {
                buffer += cur_char;
            } else if (std::isdigit(cur_char)) {
                currentState = State::Identifier;
                buffer += cur_char;
            } else {
                currentState = State::Start;
                if (auto token_code = findKeyword(buffer))
                    return makeBasicToken(*token_code, token_start);
                auto token = std::make_shared_for_overwrite<Identifier>();
                token->span = getCurrentSpan(token_start);
                token->code = TokenCode::Identifier;
                token->name = std::move(buffer);
                return token;
            }
            break;
        case State::Identifier:
            if (std::isalnum(cur_char)) {
                buffer += cur_char;
            } else {
                currentState = State::Start;
                return std::make_shared<Identifier>(buffer);
            }
            break;
        case State::IntegerLiteral:
            if (std::isdigit(cur_char)) {
                buffer += cur_char;
            } else if (cur_char == '.') {
                currentState = State::RealLiteral;
                buffer += cur_char;
            } else {
                currentState = State::Start;
                long long value = 0;
                if (std::from_chars(buffer.data(), buffer.data() + buffer.size(), value).ec == std::errc{})
                    return std::make_shared<IntegerLiteral>(value);
                throw std::runtime_error{std::format("Wrong integer literal: {}", buffer)};
            }
            break;
        case State::RealLiteral:
            if (std::isdigit(cur_char)) {
                buffer += cur_char;
            } else {
                currentState = State::Start;
                double value = 0;
                if (std::from_chars(buffer.data(), buffer.data() + buffer.size(), value).ec == std::errc{})
                    return std::make_shared<RealLiteral>(value);
                throw std::runtime_error{std::format("Wrong real literal: {}", buffer)};
            }
            break;
        case State::Punctuation:
            if (std::isalnum(cur_char) || std::isspace(cur_char)) {
                if (auto token_code = findPunctuation(buffer))
                    return makeBasicToken(*token_code, token_start);
                throw std::runtime_error{std::format("Unknown token: {}", buffer)};
            } else {
                buffer += cur_char;
            }
            break;
        case State::StringLiteral:
            if (cur_char == '"')
                return std::make_shared<StringLiteral>(buffer);
            buffer += cur_char;
            break;
        default:
            break;
        }
    }
    // NOLINTEND(*bool-conversion*)
}
