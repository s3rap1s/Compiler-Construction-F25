#include "compiler.hpp"

#include "analyzer/symbol_table.hpp"
#include "compiler/compile_error.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"

#include <llvm/IR/Constants.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <variant>

namespace compiler {

using namespace parser;
using namespace analyzer;
using namespace llvm;

struct Compiler {
  private:
    std::unique_ptr<llvm::LLVMContext> context = std::make_unique<LLVMContext>();
    std::unique_ptr<llvm::IRBuilder<>> builder = std::make_unique<IRBuilder<>>(*context);
    std::unique_ptr<llvm::Module> module = std::make_unique<Module>("Module", *context);

    SymbolTable* symbolTable;
    Program& program; // NOLINT(*ref*)

    llvm::Type* getLLVMType(const parser::Type& type) {
        if (std::holds_alternative<parser::IntegerType>(type)) {
            return builder->getInt32Ty();
        }
        if (std::holds_alternative<parser::RealType>(type)) {
            return builder->getDoubleTy();
        }
        if (std::holds_alternative<parser::BoolType>(type)) {
            return builder->getInt1Ty();
        }
        if (std::holds_alternative<parser::ArrayType>(type)) {
            const auto& arrayType = std::get<parser::ArrayType>(type);
            llvm::Type* elementType = getLLVMType(*arrayType.element_type);

            if (arrayType.size) {
                if (auto* size = dyn_cast<ConstantInt>(generateExpression(*arrayType.size))) {
                    return llvm::ArrayType::get(elementType, size->getSExtValue());
                }
                // TODO: if size is not int, cast it to int (from double or bool)
            }
            return builder->getPtrTy(); // IDK about it
        }
        if (std::holds_alternative<parser::RecordType>(type)) {
            const auto& recordType = std::get<RecordType>(type);
            std::vector<llvm::Type*> fieldTypes;
            for (const auto& field : recordType.fields) {
                fieldTypes.push_back(getLLVMType(*field.type));
            }
            return StructType::get(*context, fieldTypes);
        }
        return getLLVMType(symbolTable->resolveType(std::get<std::string>(type)));
    }

    Value* generateExpression(const parser::Expression& expr) {
        Value* result = generateBooleanExpression(expr.first);

        for (const auto& [op, expr] : expr.rest) {
            Value* right = generateBooleanExpression(expr);

            switch (op) {
            case parser::Expression::Operation::And:
                result = builder->CreateAnd(result, right, "andtmp");
                break;
            case parser::Expression::Operation::Or:
                result = builder->CreateOr(result, right, "ortmp");
                break;
            case parser::Expression::Operation::Xor:
                result = builder->CreateXor(result, right, "xortmp");
                break;
            }
        }

        return result;
    }

    void generateVariableDeclaration(const parser::VariableDeclaration& declaration) {
        llvm::Type* llvmType = nullptr;
        Value* initialValue = nullptr;

        if (declaration.type) {
            llvmType = getLLVMType(*declaration.type);
        } else if (declaration.value) {
            initialValue = generateExpression(*declaration.value);
            llvmType = initialValue->getType();
        } else {
            throw CompileError{"Variable declaration " + declaration.identifier +
                                   " must have either type or initial value",
                               declaration.span};
        }
    }

    void generateCode() {
        for (const auto& declaration : program.declarations) {
            if (std::holds_alternative<TypeDeclaration>(declaration)) {
                generateTypeDeclaration(std::get<TypeDeclaration>(declaration));
            } else if (std::holds_alternative<RoutineDeclaration>(declaration)) {
                generateRoutineDeclaration(std::get<RoutineDeclaration>(declaration));
            } else if (std::holds_alternative<VariableDeclaration>(declaration)) {
                generateVariableDeclaration(std::get<VariableDeclaration>(declaration));
            }
        }
    }

  public:
    explicit Compiler(Program& ast, SymbolTable* symbolTable) : symbolTable{symbolTable}, program{ast} {}

    std::optional<CompileError> compile() {}
};

std::optional<CompileError> compile(Program& ast, SymbolTable* symbolTable) {
    Compiler compiler{ast, symbolTable};
    return compiler.compile();
}

} // namespace compiler
