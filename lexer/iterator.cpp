#include "iterator.hpp"

#include "lexer/lexer.hpp"

#include <iterator>
#include <optional>
#include <utility>

namespace lexer {

TokenIterator::TokenIterator(Lexer& lexer, std::optional<Lexer::LexingResult>& store) : lexer{&lexer}, store{store} {
    ++*this;
}

auto TokenIterator::operator*() const -> value_type& {
    return **store.get();
}

auto TokenIterator::operator->() const -> value_type* {
    return &**store.get();
}

TokenIterator& TokenIterator::operator++() {
    std::optional<Lexer::LexingResult> resultO = lexer->getNextToken();
    if (!resultO)
        lexer = nullptr;
    else {
        Lexer::LexingResult& result = *resultO;
        if (!result)
            lexer = nullptr;
        store.get() = std::move(result);
    }

    return *this;
}

void TokenIterator::operator++(int) {
    ++*this;
}

bool TokenIterator::operator==(std::default_sentinel_t /*unused*/) const {
    return lexer == nullptr;
}

static_assert(std::input_iterator<TokenIterator>);

} // namespace lexer
