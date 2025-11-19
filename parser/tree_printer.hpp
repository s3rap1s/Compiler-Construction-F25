#pragma once

#include "parser/ast.hpp"

#include <iostream>

namespace parser {

void print_tree(const Program& program, std::ostream& out = std::cout);

} // namespace parser
