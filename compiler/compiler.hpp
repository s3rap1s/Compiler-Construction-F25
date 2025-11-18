#pragma once

#include "analyzer/symbol_table.hpp"
#include "compiler/compile_error.hpp"
#include "parser/declarations.hpp"

#include <optional>

namespace compiler {

std::optional<CompileError> compile(parser::Program& ast, analyzer::SymbolTable* symbolTable);

} // namespace compiler
