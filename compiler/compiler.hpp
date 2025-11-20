#pragma once

#include "analyzer/symbol_table.hpp"
#include "compiler/compile_error.hpp"
#include "parser/ast.hpp"

#include <llvm/IR/Module.h>

#include <expected>

namespace compiler {

std::expected<std::unique_ptr<llvm::Module>, CompileError> compile(const parser::Program& ast,
                                                                   const analyzer::SymbolTable& symbolTable);

} // namespace compiler
