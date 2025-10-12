#pragma once

#include <cstddef>

struct Span {
    std::size_t line_no;
    std::size_t begin;
    std::size_t end;
};
