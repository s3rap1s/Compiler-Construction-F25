#pragma once

#include "parser/declarations.hpp"

#include <ostream>

namespace parser {

void print_tree(const Program& program, std::ostream& out = std::cout);

} // namespace parser