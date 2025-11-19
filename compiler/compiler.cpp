#include "compiler.hpp"

#include "analyzer/symbol_table.hpp"
#include "compiler/compile_error.hpp"
#include "parser/ast.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Casting.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace compiler {

using namespace parser;
using namespace analyzer;
using namespace llvm;

struct Compiler {
  private:
    std::unique_ptr<llvm::LLVMContext> context = std::make_unique<LLVMContext>();
    std::unique_ptr<llvm::IRBuilder<>> builder = std::make_unique<IRBuilder<>>(*context);
    std::unique_ptr<llvm::Module> module = std::make_unique<Module>("Module", *context);
    std::unordered_map<std::string, Value*> namedValues;

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
            return builder->getPtrTy();
        }
        if (std::holds_alternative<parser::RecordType>(type)) {
            const auto& recordType = std::get<RecordType>(type);
            std::vector<llvm::Type*> fieldTypes;
            for (const auto& field : recordType.fields) {
                fieldTypes.push_back(getLLVMType(*field.type));
            }
            return StructType::get(*context, fieldTypes);
        }
        return getLLVMType(symbolTable->resolveType(std::get<Identifier>(type)));
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

    Value* generateBooleanExpression(const parser::BooleanExpression& expr) {
        if (std::holds_alternative<parser::Relation>(expr)) {
            return generateRelation(std::get<parser::Relation>(expr));
        }
        return generateNotExpression(std::get<parser::NotExpression>(expr));
    }

    Value* generateRelation(const parser::Relation& relation) {
        Value* result = generateNumberExpression(relation.first);

        if (relation.second) {
            const auto& [op, numExpr] = *relation.second;
            Value* right = generateNumberExpression(numExpr);
            switch (op) {
            case parser::Relation::Operation::Less:
                result = builder->CreateICmpSLT(result, right);
                break;
            case parser::Relation::Operation::LessOrEqual:
                result = builder->CreateICmpSLE(result, right);
                break;
            case parser::Relation::Operation::Greater:
                result = builder->CreateICmpSGT(result, right);
                break;
            case parser::Relation::Operation::GreaterOrEqual:
                result = builder->CreateICmpSGE(result, right);
                break;
            case parser::Relation::Operation::Equal:
                result = builder->CreateICmpEQ(result, right);
                break;
            case parser::Relation::Operation::NotEqual:
                result = builder->CreateICmpNE(result, right);
                break;
            }
        }

        return result;
    }

    Value* generateNotExpression(const parser::NotExpression& notExpr) {
        return builder->CreateNot(generatePrimary(notExpr.operand));
    }

    Value* generateNumberExpression(const parser::NumberExpression& numExpr) {
        Value* result = generateSummand(numExpr.first);

        for (const auto& [op, summand] : numExpr.rest) {
            Value* right = generateSummand(summand);

            switch (op) {
            case parser::NumberExpression::Operation::Plus:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateAdd(result, right, "addtmp");
                } else {
                    result = builder->CreateFAdd(result, right, "addtmp");
                }
                break;
            case parser::NumberExpression::Operation::Minus:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateSub(result, right, "subtmp");
                } else {
                    result = builder->CreateFSub(result, right, "subtmp");
                }
                break;
            }
        }
        return result;
    }

    Value* generateSummand(const parser::Summand& summand) {
        Value* result = generatePrimary(summand.first);

        for (const auto& [op, primary] : summand.rest) {
            Value* right = generatePrimary(primary);

            switch (op) {
            case parser::Summand::Operation::Multiply:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateMul(result, right, "multmp");
                } else {
                    result = builder->CreateFMul(result, right, "multmp");
                }
                break;
            case parser::Summand::Operation::Divide:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateSDiv(result, right, "divtmp");
                } else {
                    result = builder->CreateFDiv(result, right, "divtmp");
                }
                break;
            case parser::Summand::Operation::Modulo:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateSRem(result, right, "modtmp");
                } else {
                    throw CompileError("Modulo operation not supported for floating point types",
                                       {.begin = 0, .end = 0, .line_no = 0, .column_no = 0}); // TODO: normal span
                }
                break;
            }
        }
        return result;
    }

    Value* generatePrimary(const parser::Primary& primary) {
        if (std::holds_alternative<parser::IntegerLiteral>(primary)) {
            return llvm::ConstantInt::getSigned((llvm::Type::getInt32Ty(*context)),
                                                std::get<IntegerLiteral>(primary).value);
        }
        if (std::holds_alternative<parser::RealLiteral>(primary)) {
            return llvm::ConstantFP::get((llvm::Type::getDoubleTy(*context)), std::get<RealLiteral>(primary).value);
        }
        if (std::holds_alternative<parser::BooleanLiteral>(primary)) {
            return llvm::ConstantInt::getSigned((llvm::Type::getInt1Ty(*context)),
                                                static_cast<int64_t>(std::get<BooleanLiteral>(primary).value));
        }
        if (std::holds_alternative<parser::RoutineCall>(primary)) {
            return generateRoutineCall(std::get<parser::RoutineCall>(primary));
        }
        if (std::holds_alternative<parser::ModifiablePrimary>(primary)) {
            // TODO
        }
        if (std::holds_alternative<parser::UnarySign>(primary)) {
            const auto& unSign = std::get<parser::UnarySign>(primary);
            Value* result = generatePrimary(*unSign.operand);
            if (unSign.sign == parser::UnarySign::Sign::Minus) {
                if (result->getType()->isIntegerTy()) {
                    return builder->CreateNeg(result, "negtmp");
                }
                return builder->CreateFNeg(result, "negtmp");
            }
            return result;
        }
        return generateExpression(*std::get<parser::ParenthesizedExpression>(primary).expression);
    }

    Value* generateRoutineCall(const parser::RoutineCall& call) {
        std::vector<Value*> parameters;
        parameters.reserve(call.arguments.size());
        for (const auto& argument : call.arguments) {
            parameters.push_back(generateExpression(argument));
        }

        return builder->CreateCall(module->getFunction(call.routine_name.text), parameters, "calltmp");
    }

    void generateVariableDeclaration(const parser::VariableDeclaration& declaration) { // TODO void?
        llvm::Type* llvmType = nullptr;
        Value* initialValue = nullptr;

        if (declaration.type) {
            llvmType = getLLVMType(*declaration.type);
        } else if (declaration.value) {
            initialValue = generateExpression(*declaration.value);
            llvmType = initialValue->getType();
        } else {
            throw CompileError{"Variable declaration " + declaration.name.text +
                                   " must have either type or initial value",
                               declaration.name.span}; // Ya zshe mamoi klyalsya, ne budet takogo (c) Maxim Fomin
        }
        // TODO: what next?
    }

    Function* generateRoutineDeclaration(const parser::RoutineDeclaration& routine) {
        llvm::Type* returnType = routine.return_type ? getLLVMType(*routine.return_type) : builder->getVoidTy();
        std::vector<llvm::Type*> paramTypes;
        paramTypes.reserve(routine.parameters.size());
        for (const auto& param : routine.parameters) {
            paramTypes.push_back(getLLVMType(param.type));
        }
        FunctionType* functionType = FunctionType::get(returnType, paramTypes, false);
        Function* function = Function::Create(functionType, Function::InternalLinkage, routine.name.text, module.get());
        long long idx = 0;
        for (auto& param : function->args()) {
            param.setName(routine.parameters[idx].name.text);
        }
        return function;
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
