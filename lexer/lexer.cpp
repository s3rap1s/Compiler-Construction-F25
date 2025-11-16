#include "lexer.hpp"

#include "lexing_error.hpp"
#include "tokens.hpp"

#include <charconv>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace lexer {

namespace {

const std::unordered_map<std::string_view, SyntaxPart> kKeywordMap = [] { // NOLINT(cert-err58-cpp)
    std::unordered_map<std::string_view, SyntaxPart> map;
    map["and"] = SyntaxPart::And;
    map["array"] = SyntaxPart::Array;
    map["begin"] = SyntaxPart::Begin;
    map["boolean"] = SyntaxPart::Boolean;
    map["else"] = SyntaxPart::Else;
    map["end"] = SyntaxPart::End;
    map["for"] = SyntaxPart::For;
    map["if"] = SyntaxPart::If;
    map["in"] = SyntaxPart::In;
    map["integer"] = SyntaxPart::Integer;
    map["is"] = SyntaxPart::Is;
    map["loop"] = SyntaxPart::Loop;
    map["not"] = SyntaxPart::Not;
    map["or"] = SyntaxPart::Or;
    map["print"] = SyntaxPart::Print;
    map["real"] = SyntaxPart::Real;
    map["record"] = SyntaxPart::Record;
    map["return"] = SyntaxPart::Return;
    map["reverse"] = SyntaxPart::Reverse;
    map["routine"] = SyntaxPart::Routine;
    map["then"] = SyntaxPart::Then;
    map["type"] = SyntaxPart::Type;
    map["var"] = SyntaxPart::Var;
    map["while"] = SyntaxPart::While;
    map["xor"] = SyntaxPart::Xor;
    return map;
}();

std::optional<SyntaxPart> findKeyword(std::string_view token) {
    auto it = kKeywordMap.find(token);
    return it != kKeywordMap.end() ? std::optional{it->second} : std::nullopt;
}

const std::unordered_map<std::string_view, SyntaxPart> kPunctuationMap = [] { // NOLINT(cert-err58-cpp)
    std::unordered_map<std::string_view, SyntaxPart> map;
    map["=>"] = SyntaxPart::Arrow;
    map[":="] = SyntaxPart::Assignment;
    map["]"] = SyntaxPart::CloseBracket;
    map[")"] = SyntaxPart::CloseParenthesis;
    map[":"] = SyntaxPart::Colon;
    map[","] = SyntaxPart::Comma;
    map["/"] = SyntaxPart::Divide;
    map["."] = SyntaxPart::Dot;
    map["="] = SyntaxPart::Equal;
    map[">"] = SyntaxPart::Greater;
    map[">="] = SyntaxPart::GreaterEqual;
    map["<"] = SyntaxPart::Less;
    map["<="] = SyntaxPart::LessEqual;
    map["-"] = SyntaxPart::Minus;
    map["%"] = SyntaxPart::Modulo;
    map["*"] = SyntaxPart::Multiply;
    map["/="] = SyntaxPart::NotEqual;
    map["["] = SyntaxPart::OpenBracket;
    map["("] = SyntaxPart::OpenParenthesis;
    map["+"] = SyntaxPart::Plus;
    map[".."] = SyntaxPart::Range;
    map[";"] = SyntaxPart::Semicolon;
    return map;
}();

std::optional<SyntaxPart> findPunctuation(std::string_view token) {
    auto it = kPunctuationMap.find(token);
    return it != kPunctuationMap.end() ? std::optional{it->second} : std::nullopt;
}

} // namespace

[[nodiscard]] Span Lexer::getCurrentSpan(std::size_t token_start) const {
    return Span{.begin = token_start, .end = char_pos, .line_no = line_no, .column_no = token_start - line_start + 1};
}

[[nodiscard]] Token Lexer::makeToken(std::size_t token_start, Token::Payload payload) const {
    return Token{getCurrentSpan(token_start), std::move(payload)};
}

void Lexer::advance() {
    ++char_pos;
}

auto Lexer::getNextToken() -> std::optional<LexingResult> { // NOLINT(*complexity*)
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
                if (buffer == "true" || buffer == "false")
                    return makeToken(token_start, BooleanLiteral{buffer == "true"});
                if (auto token_code = findKeyword(buffer))
                    return makeToken(token_start, SyntaxPart{*token_code});
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
                return std::unexpected{IntegerLiteralError{buffer}};
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
            return std::unexpected{RealLiteralError{buffer}};
            break;
        case State::Punctuation:
            if (std::isalnum(cur_char) || std::isspace(cur_char)) {
                current_state = State::Start;
                for (std::size_t size = buffer.size(); size > 0; --size, --char_pos) {
                    if (auto token_type = findPunctuation({buffer.data(), buffer.data() + size}))
                        return makeToken(token_start, SyntaxPart{*token_type});
                }
                return std::unexpected{UnknownToken{buffer}};
            } else {
                buffer += cur_char;
            }
            break;
        case State::StringLiteral:
            if (cur_char == '"') {
                current_state = State::Start;
                advance();
                return makeToken(token_start, StringLiteral{std::move(buffer)});
            }
            buffer += cur_char;
            break;
        }
        if (cur_char == '\n' || cur_char == '\r') {
            ++char_pos;
            ++line_no;
            line_start = char_pos;
            return makeToken(token_start, SyntaxPart{SyntaxPart::NewLine});
        }
        advance();
    }
    // NOLINTEND(*bool-conversion*)
}

std::string&& Lexer::getProgramText() && {
    return std::move(file);
}

} // namespace lexer
