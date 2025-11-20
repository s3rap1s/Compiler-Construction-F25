#include "compiler.hpp"

#include "analyzer/symbol_table.hpp"
#include "compiler/compile_error.hpp"
#include "parser/ast.hpp"

#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
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

    llvm::Type* getLLVMType(const analyzer::TypeInfo& typeInfo) {
        const auto& type = typeInfo.definition;
        return std::visit(overloaded{
                              [this](const analyzer::IntegerTypeInfo&) -> llvm::Type* { return builder->getInt32Ty(); },
                              [this](const analyzer::RealTypeInfo&) -> llvm::Type* { return builder->getDoubleTy(); },
                              [this](const analyzer::BooleanTypeInfo&) -> llvm::Type* { return builder->getInt1Ty(); },
                              [this, type](const analyzer::ArrayTypeInfo&) -> llvm::Type* {
                                  const auto& arrayType = std::get<analyzer::ArrayTypeInfo>(type);
                                  llvm::Type* elementType =
                                      getLLVMType(symbolTable->getTypeInfo(arrayType.element_type));
                                  return llvm::ArrayType::get(elementType, arrayType.size);
                              },
                              [this, type](const analyzer::RecordTypeInfo&) -> llvm::Type* {
                                  const auto& recordType = std::get<RecordTypeInfo>(type);
                                  std::vector<llvm::Type*> fieldTypes;
                                  for (const auto& [name, typeId] : recordType.fields) {
                                      fieldTypes.push_back(getLLVMType(symbolTable->getTypeInfo(typeId)));
                                  }
                                  return StructType::get(*context, fieldTypes);
                              },
                          },
                          type);
    }

    // Value* generateCast(const Value* llvmExpression, TypeId typeId, const Span& span) {
    //     llvm::Type* type = llvmExpression->getType();
    //     if (type != builder->getInt32Ty() && type != builder->getDoubleTy() && type != builder->getInt1Ty()) {
    //         throw CompileError{"Expression cannot be casted to specified type", span};
    //     }
    //     if (typeId == analyzer::SymbolTable::BooleanTypeId) {
    //     }
    //     if (typeId == analyzer::SymbolTable::IntegerTypeId) {
    //     }
    //     if (typeId == analyzer::SymbolTable::RealTypeId) {
    //     }
    // }

    Value* generateExpression(const parser::Expression& expr) {
        Value* result = generateBooleanExpression(expr.first);

        for (const auto& [op, expr] : expr.rest) {
            Value* right = generateBooleanExpression(expr);

            switch (op) {
            case parser::Expression::Operator::And:
                result = builder->CreateAnd(result, right, "andtmp");
                break;
            case parser::Expression::Operator::Or:
                result = builder->CreateOr(result, right, "ortmp");
                break;
            case parser::Expression::Operator::Xor:
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
            case parser::Relation::Operator::Less:
                result = builder->CreateICmpSLT(result, right);
                break;
            case parser::Relation::Operator::LessOrEqual:
                result = builder->CreateICmpSLE(result, right);
                break;
            case parser::Relation::Operator::Greater:
                result = builder->CreateICmpSGT(result, right);
                break;
            case parser::Relation::Operator::GreaterOrEqual:
                result = builder->CreateICmpSGE(result, right);
                break;
            case parser::Relation::Operator::Equal:
                result = builder->CreateICmpEQ(result, right);
                break;
            case parser::Relation::Operator::NotEqual:
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

        for (const auto& operation : numExpr.rest) {
            const auto& op = operation.operator_;
            const auto& summand = operation.next_operand;
            Value* right = generateSummand(summand);

            switch (op) {
            case parser::NumberExpression::Operator::Plus:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateAdd(result, right, "addtmp");
                } else {
                    result = builder->CreateFAdd(result, right, "addtmp");
                }
                break;
            case parser::NumberExpression::Operator::Minus:
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

        for (const auto& operation : summand.rest) {
            const auto& primary = operation.next_operand;
            const auto& op = operation.operator_;
            Value* right = generatePrimary(primary);

            switch (op) {
            case parser::Summand::Operator::Multiply:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateMul(result, right, "multmp");
                } else {
                    result = builder->CreateFMul(result, right, "multmp");
                }
                break;
            case parser::Summand::Operator::Divide:
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateSDiv(result, right, "divtmp");
                } else {
                    result = builder->CreateFDiv(result, right, "divtmp");
                }
                break;
            case parser::Summand::Operator::Modulo:
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
            return llvm::ConstantInt::get((llvm::Type::getInt1Ty(*context)),
                                          static_cast<int64_t>(std::get<BooleanLiteral>(primary).value));
        }
        if (std::holds_alternative<parser::RoutineCall>(primary)) {
            return generateRoutineCall(std::get<parser::RoutineCall>(primary));
        }
        if (std::holds_alternative<parser::ModifiablePrimary>(primary)) {
            return generateModifiablePrimary(std::get<parser::ModifiablePrimary>(primary));
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

    Value* generateModifiablePrimary(const parser::ModifiablePrimary& primary) {}

    Value* generateRoutineCall(const parser::RoutineCall& call) {
        std::vector<Value*> parameters;
        parameters.reserve(call.arguments.size());
        for (const auto& argument : call.arguments) {
            parameters.push_back(generateExpression(argument));
        }

        return builder->CreateCall(module->getFunction(call.routine_name.text), parameters, "calltmp");
    }

    void generateGlobalVariableDeclaration(const parser::VariableDeclaration& declaration) { // TODO void?
        llvm::Type* llvmType = nullptr;
        Value* initialValue = nullptr;

        if (declaration.type) {
            llvmType = getLLVMType(symbolTable->getTypeInfo(declaration.resolved_type));
            if (declaration.value) {
                initialValue = generateExpression(*declaration.value);
                if (declaration.resolved_type != declaration.value->type) {
                    // initialValue = generateCast(
                    //     generateExpression(*declaration.value), declaration.value->type,
                    //     getSpan(*declaration.value));
                    // TODO
                }
            } else {
                initialValue = Constant::getNullValue(llvmType);
            }
        } else if (declaration.value) {
            initialValue = generateExpression(*declaration.value);
            llvmType = getLLVMType(symbolTable->getTypeInfo(declaration.resolved_type));
            assert(initialValue->getType() == llvmType);
        } else {
            throw CompileError{"Variable declaration " + declaration.name.text +
                                   " must have either type or initial value",
                               declaration.name.span}; // Ya zshe mamoi klyalsya, ne budet takogo (c) Maxim Fomin
        }
        auto* initialConst = dyn_cast<Constant>(initialValue);
        auto* globalVariable = new GlobalVariable(
            llvmType, false, llvm::GlobalValue::InternalLinkage, initialConst, declaration.name.text);
        namedValues[declaration.name.text] = globalVariable;
    }

    void generateLocalVariableDeclaration(const parser::VariableDeclaration& declaration) { // TODO void?
        llvm::Type* llvmType = nullptr;
        Value* initialValue = nullptr;

        if (declaration.type) {
            llvmType = getLLVMType(symbolTable->getTypeInfo(declaration.resolved_type));
            if (declaration.value) {
                initialValue = generateExpression(*declaration.value);
                if (declaration.resolved_type != declaration.value->type) {
                    // initialValue = generateCast(
                    //     generateExpression(*declaration.value), declaration.value->type,
                    //     getSpan(*declaration.value));
                    // TODO
                }
            } else {
                initialValue = Constant::getNullValue(llvmType);
            }
        } else if (declaration.value) {
            initialValue = generateExpression(*declaration.value);
            llvmType = getLLVMType(symbolTable->getTypeInfo(declaration.resolved_type));
            assert(initialValue->getType() == llvmType);
        } else {
            throw CompileError{"Variable declaration " + declaration.name.text +
                                   " must have either type or initial value",
                               declaration.name.span}; // Ya zshe mamoi klyalsya, ne budet takogo (c) Maxim Fomin
        }

        BasicBlock* currentBlock = builder->GetInsertBlock();
        Function* currentFunction = builder->GetInsertBlock()->getParent();
        IRBuilder<> allocaBuilder(&currentFunction->getEntryBlock(), currentFunction->getEntryBlock().begin());

        AllocaInst* alloca = allocaBuilder.CreateAlloca(llvmType, nullptr, declaration.name.text);
        builder->SetInsertPoint(currentBlock);
        builder->CreateStore(initialValue, alloca);
        namedValues[declaration.name.text] = alloca;
    }

    Function* generateRoutineDeclaration(const parser::RoutineDeclaration& declaration) {
        llvm::Type* returnType = declaration.return_type
                                     ? getLLVMType(symbolTable->getTypeInfo(declaration.return_type->resolved))
                                     : builder->getVoidTy();
        std::vector<llvm::Type*> paramTypes;
        paramTypes.reserve(declaration.parameters.size());
        for (const auto& param : declaration.parameters) {
            paramTypes.push_back(getLLVMType(symbolTable->getTypeInfo(param.resolved_type)));
        }
        FunctionType* functionType = FunctionType::get(returnType, paramTypes, false);
        Function* function =
            Function::Create(functionType, Function::InternalLinkage, declaration.name.text, module.get());
        long long idx = 0;
        for (auto& param : function->args()) {
            param.setName(declaration.parameters[idx].name.text);
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
                generateGlobalVariableDeclaration(std::get<VariableDeclaration>(declaration));
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
