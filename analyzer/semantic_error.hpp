#pragma once

#include <exception>
#include <string>
#include <utility>

#include "common.hpp"

namespace analyzer {


struct SemanticError : std::exception {
    std::string what;
    Span span;

    explicit SemanticError(std::string what, Span span) : what{std::move(what)}, span{span} {}
    
};

} // namespace analyzer
