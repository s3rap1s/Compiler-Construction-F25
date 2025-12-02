#pragma once

#include "analyzer/symbol_table.hpp"
#include "codegen/codegen_error.hpp"
#include "parser/ast.hpp"

#include <expected>
#include <iosfwd>

namespace codegen {

std::expected<void, CodegenError>
generate_code(const parser::Program& ast, const analyzer::SymbolTable& symbolTable, std::ostream& out);

} // namespace codegen
