#pragma once

#include "analyzer/symbol_table.hpp"
#include "codegen/codegen_error.hpp"
#include "parser/ast.hpp"

#include <expected>
#include <iosfwd>
#include <string_view>

namespace codegen {

std::expected<void, CodegenError> generate_code(const parser::Program& ast,
                                                const analyzer::SymbolTable& symbolTable,
                                                std::ostream& out,
                                                std::string_view entry_point);

} // namespace codegen
