#pragma once

#include "lexer/lexer.hpp"
#include "lexer/tokens.hpp"

#include <cstddef>
#include <functional>
#include <iterator>

namespace lexer {

class TokenIterator {
  public:
    using difference_type = std::ptrdiff_t;
    using value_type = Token;

  private:
    Lexer* lexer; // nullptr if the end of token stream was reached
    std::reference_wrapper<std::optional<Lexer::ResultType>> store;

  public:
    TokenIterator(Lexer& lexer, std::optional<Lexer::ResultType>& store);
    TokenIterator(const TokenIterator&) = delete;
    TokenIterator(TokenIterator&&) = default;

    ~TokenIterator() = default;

    TokenIterator& operator=(const TokenIterator&) = delete;
    TokenIterator& operator=(TokenIterator&&) = default;

    value_type& operator*() const;
    value_type* operator->() const;

    TokenIterator& operator++();
    void operator++(int);

    bool operator==(std::default_sentinel_t) const;
};

} // namespace lexer
