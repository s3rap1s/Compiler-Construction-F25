#pragma once

#include <exception>
#include <string>
#include <utility>

#include "common.hpp"

namespace codegen {

struct CodegenError : std::exception {
    std::string what;
    Span span;

    explicit CodegenError(std::string what, Span span) : what{std::move(what)}, span{span} {}
};

} // namespace codegen
