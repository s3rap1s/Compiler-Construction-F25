#include "lexer.hpp"

#include "lexing_error.hpp"
#include "tokens.hpp"

#include <charconv>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

const std::unordered_map<std::string_view, SyntaxPart::Type> kKeywordMap = [] { // NOLINT(cert-err58-cpp)
    std::unordered_map<std::string_view, SyntaxPart::Type> map;
    map["and"] = SyntaxPart::Type::And;
    map["array"] = SyntaxPart::Type::Array;
    map["begin"] = SyntaxPart::Type::Begin;
    map["boolean"] = SyntaxPart::Type::Boolean;
    map["else"] = SyntaxPart::Type::Else;
    map["end"] = SyntaxPart::Type::End;
    map["for"] = SyntaxPart::Type::For;
    map["if"] = SyntaxPart::Type::If;
    map["in"] = SyntaxPart::Type::In;
    map["integer"] = SyntaxPart::Type::Integer;
    map["is"] = SyntaxPart::Type::Is;
    map["loop"] = SyntaxPart::Type::Loop;
    map["not"] = SyntaxPart::Type::Not;
    map["or"] = SyntaxPart::Type::Or;
    map["print"] = SyntaxPart::Type::Print;
    map["real"] = SyntaxPart::Type::Real;
    map["record"] = SyntaxPart::Type::Record;
    map["return"] = SyntaxPart::Type::Return;
    map["reverse"] = SyntaxPart::Type::Reverse;
    map["routine"] = SyntaxPart::Type::Routine;
    map["then"] = SyntaxPart::Type::Then;
    map["type"] = SyntaxPart::Type::Type;
    map["var"] = SyntaxPart::Type::Var;
    map["while"] = SyntaxPart::Type::While;
    map["xor"] = SyntaxPart::Type::Xor;
    return map;
}();

std::optional<SyntaxPart::Type> findKeyword(std::string_view token) {
    auto it = kKeywordMap.find(token);
    return it != kKeywordMap.end() ? std::optional{it->second} : std::nullopt;
}

const std::unordered_map<std::string_view, SyntaxPart::Type> kPunctuationMap = [] { // NOLINT(cert-err58-cpp)
    std::unordered_map<std::string_view, SyntaxPart::Type> map;
    map["=>"] = SyntaxPart::Type::Arrow;
    map[":="] = SyntaxPart::Type::Assignment;
    map["]"] = SyntaxPart::Type::ClosedBracket;
    map[")"] = SyntaxPart::Type::ClosedParenthesis;
    map[":"] = SyntaxPart::Type::Colon;
    map[","] = SyntaxPart::Type::Comma;
    map["/"] = SyntaxPart::Type::Divide;
    map["."] = SyntaxPart::Type::Dot;
    map["="] = SyntaxPart::Type::Equal;
    map[">"] = SyntaxPart::Type::Greater;
    map[">="] = SyntaxPart::Type::GreaterEqual;
    map["<"] = SyntaxPart::Type::Less;
    map["<="] = SyntaxPart::Type::LessEqual;
    map["-"] = SyntaxPart::Type::Minus;
    map["%"] = SyntaxPart::Type::Modulo;
    map["*"] = SyntaxPart::Type::Multiply;
    map["/="] = SyntaxPart::Type::NotEqual;
    map["["] = SyntaxPart::Type::OpenBracket;
    map["("] = SyntaxPart::Type::OpenParenthesis;
    map["+"] = SyntaxPart::Type::Plus;
    map[".."] = SyntaxPart::Type::Range;
    map[";"] = SyntaxPart::Type::Semicolon;
    return map;
}();

std::optional<SyntaxPart::Type> findPunctuation(std::string_view token) {
    auto it = kPunctuationMap.find(token);
    return it != kPunctuationMap.end() ? std::optional{it->second} : std::nullopt;
}

} // namespace

[[nodiscard]] Span Lexer::getCurrentSpan(std::size_t token_start) const {
    return Span{.line_no = line_no, .begin = token_start, .end = char_pos};
}

[[nodiscard]] Token Lexer::makeToken(std::size_t token_start, Token::Payload payload) const {
    return Token{getCurrentSpan(token_start), std::move(payload)};
}

std::expected<std::optional<Token>, LexingError> Lexer::getNextToken() {
    // NOLINTBEGIN(*bool-conversion*)
    if (char_pos == file.size())
        return std::nullopt;
    std::size_t token_start = char_pos;
    std::string buffer;

    while (true) {
        if (char_pos > file.size())
            return std::nullopt;
        char cur_char = char_pos == file.size() ? '\n' : file[char_pos];
        switch (current_state) {
        case State::Start:
            token_start = char_pos;
            if (std::isalpha(cur_char)) {
                current_state = State::KeywordOrIdentifier;
                buffer += cur_char;
            } else if (std::isdigit(cur_char)) {
                current_state = State::IntegerLiteral;
                buffer += cur_char;
            } else if (cur_char == '"') {
                current_state = State::StringLiteral;
            } else if (std::isspace(cur_char)) {
            } else {
                current_state = State::Punctuation;
                buffer += cur_char;
            }
            break;
        case State::KeywordOrIdentifier:
            if (std::isalpha(cur_char)) {
                buffer += cur_char;
            } else if (std::isdigit(cur_char)) {
                current_state = State::Identifier;
                buffer += cur_char;
            } else {
                current_state = State::Start;
                if (auto token_code = findKeyword(buffer)) {
                    if (buffer == "true" || buffer == "false")
                        return makeToken(token_start, BooleanLiteral{buffer == "true"});
                    return makeToken(token_start, SyntaxPart{*token_code});
                }
                return makeToken(token_start, Identifier{std::move(buffer)});
            }
            break;
        case State::Identifier:
            if (std::isalnum(cur_char)) {
                buffer += cur_char;
            } else {
                current_state = State::Start;
                return makeToken(token_start, Identifier{std::move(buffer)});
            }
            break;
        case State::IntegerLiteral:
            if (std::isdigit(cur_char)) {
                buffer += cur_char;
            } else if (cur_char == '.') {
                current_state = State::RealLiteral;
                buffer += cur_char;
            } else {
                current_state = State::Start;
                if (long long value = 0;
                    std::from_chars(buffer.data(), buffer.data() + buffer.size(), value).ec == std::errc{})
                    return makeToken(token_start, IntegerLiteral{value});
                throw std::runtime_error{std::format("Wrong integer literal: {}", buffer)};
            }
            break;
        case State::RealLiteral:
            if (std::isdigit(cur_char)) {
                buffer += cur_char;
                break;
            }
            current_state = State::Start;
            if (buffer.back() == '.') {
                --char_pos;
                buffer.pop_back();
            }
            if (double value = 0;
                std::from_chars(buffer.data(), buffer.data() + buffer.size(), value).ec == std::errc{})
                return makeToken(token_start, RealLiteral{value});
            throw std::runtime_error{std::format("Wrong real literal: {}", buffer)};
            break;
        case State::Punctuation:
            if (std::isalnum(cur_char) || std::isspace(cur_char)) {
                current_state = State::Start;
                for (std::size_t size = buffer.size(); size > 0; --size, --char_pos) {
                    if (auto token_type = findPunctuation({buffer.data(), buffer.data() + size})) {
                        return makeToken(token_start, SyntaxPart{*token_type});
                    }
                }
                throw std::runtime_error{std::format("Unknown token: {}", buffer)};
            } else {
                buffer += cur_char;
            }
            break;
        case State::StringLiteral:
            if (cur_char == '"') {
                current_state = State::Start;
                ++char_pos;
                return makeToken(token_start, StringLiteral{std::move(buffer)});
            }
            buffer += cur_char;
            break;
        }
        if (cur_char == '\n' || cur_char == '\r'){
            ++line_no;
            ++char_pos;
            return makeToken(token_start, SyntaxPart{SyntaxPart::Type::NewLine});
        }
        ++char_pos;
    }
    // NOLINTEND(*bool-conversion*)
}
