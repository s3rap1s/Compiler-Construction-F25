#pragma once

#include <exception>
#include <string>
#include <utility>

#include "common.hpp"

namespace compiler {

struct CompileError : std::exception {
    std::string what;
    Span span;

    explicit CompileError(std::string what, Span span) : what{std::move(what)}, span{span} {}
};

} // namespace compiler
