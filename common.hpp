#pragma once

#include <algorithm>
#include <cstddef>

struct Span {
    std::size_t begin;
    std::size_t end;
    std::size_t line_no;
    std::size_t column_no;

    Span operator|(const Span other) const {
        auto new_rect_pos =
            std::min<std::pair<std::size_t, std::size_t>>({line_no, column_no}, {other.line_no, other.column_no});
        return {.begin = std::min(begin, other.begin),
                .end = std::max(end, other.end),
                .line_no = new_rect_pos.first,
                .column_no = new_rect_pos.second};
    }
};
