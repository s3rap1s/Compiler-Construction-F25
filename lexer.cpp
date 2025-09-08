#include "tokens.hpp"

#include <vector>
#include <memory>
#include <charconv>
#include <stdexcept>
#include <format>
#include <unordered_map>

class Lexer {
    enum class State {Start, KeywordOrIdentifier, Identifier, StringLiteral, IntegerLiteral, RealLiteral, Punctuation};   

    State currentState = State::Start;
    int line_no = 0;
    int char_pos = 0;
    std::string file;
    
    Lexer(std::string file) : file{file} {}

    std::shared_ptr<Token> getNextToken() {
        std::string buffer;
        while (true) {
            char cur_char = file[char_pos++];
            if (cur_char == '\n' || cur_char == '\r')
                line_no++;
            switch (currentState) {
            case State::Start:
                if (std::isalpha(cur_char)){
                    currentState = State::KeywordOrIdentifier;
                    buffer += cur_char;
                } else if (std::isdigit(cur_char)){
                    currentState = State::IntegerLiteral;
                    buffer += cur_char;
                } else if (cur_char == '"'){
                    currentState = State::StringLiteral;
                } else if (std::isspace(cur_char)){
                } else {
                    currentState = State::Punctuation;
                    buffer += cur_char;
                }
                break;
            case State::KeywordOrIdentifier:
                if (std::isalpha(cur_char)){
                    buffer += cur_char;
                } else if (std::isdigit(cur_char)){
                    currentState = State::Identifier;
                    buffer += cur_char;
                } else {
                    currentState = State::Start;
                    return findKeyword(buffer);
                }
                break;
            case State::Identifier:
                if (std::isalnum(cur_char) ){
                    buffer += cur_char;
                } else {
                    currentState = State::Start;
                    return std::make_shared<Identifier>(buffer);
                }
                break;
            case State::IntegerLiteral:
                if (std::isdigit(cur_char) ){
                    buffer += cur_char;
                } else if(cur_char == '.'){
                    currentState = State::RealLiteral;
                    buffer += cur_char;
                } else {
                    currentState = State::Start;
                    long long value;
                    if (std::from_chars(buffer.data(), buffer.data() + buffer.size(), value).ec == std::errc{}){
                        return std::make_shared<IntegerLiteral>(value); 
                    } else {
                        throw std::runtime_error{std::format("Wrong integer literal: {}", buffer)};
                    }  
                }
                break;
            case State::RealLiteral:
                if (std::isdigit(cur_char) ){
                    buffer += cur_char;
                } else {
                    currentState = State::Start;
                    double value;
                    if (std::from_chars(buffer.data(), buffer.data() + buffer.size(), value).ec == std::errc{}){
                        return std::make_shared<RealLiteral>(value); 
                    } else {
                        throw std::runtime_error{std::format("Wrong real literal: {}", buffer)};
                    } 
                }
                break;
            case State::Punctuation:
                if (std::isalnum(cur_char) || std::isspace(cur_char)) {
                    return findPunctuation(buffer);
                } else {
                    buffer += cur_char;
                }
                break;
            case State::StringLiteral:
                if (cur_char == '"') {
                    return std::make_shared<StringLiteral>(buffer);
                } else {
                    buffer += cur_char;
                }
            default:
                break;
            }
        }
    }

    std::shared_ptr<Token> findKeyword(std::string_view buffer) {
        std::unordered_map<std::string, Code> map;
        map["routine"] = Code::Routine;
        map["return"] = Code::Routine;
    }

    std::shared_ptr<Token> findPunctuation(std::string buffer){}
};
