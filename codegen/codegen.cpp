#include "codegen.hpp"

#include "Support/raw_ostream.h"
#include "analyzer/symbol_table.hpp"
#include "codegen_error.hpp"
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
#include <llvm/Support/raw_os_ostream.h>

#include <cassert>
#include <climits>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace codegen {

using namespace parser;
using namespace analyzer;
using namespace llvm;

// NOLINTBEGIN(*recursion*)
struct CodeGenerator {
  private:
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder{context};
    llvm::Module module{"Module", context};

    FunctionType* printfType = FunctionType::get(builder.getInt32Ty(), {makePointerType()}, true);
    Function* printfFunc = Function::Create(printfType, Function::ExternalLinkage, "printf", &module);

    std::unordered_map<std::string, Value*> namedValues;
    std::unordered_map<std::string, Function*> functions;

    const SymbolTable& symbolTable; // NOLINT(*ref*)
    const Program& program;         // NOLINT(*ref*)

    llvm::Type* makePointerType() {
        return PointerType::getUnqual(context);
    }

    llvm::Type* getLLVMType(const analyzer::TypeInfo& typeInfo) {
        const auto& type = typeInfo.definition;
        return std::visit(overloaded{
                              [this](const analyzer::IntegerTypeInfo&) -> llvm::Type* { return builder.getInt32Ty(); },
                              [this](const analyzer::RealTypeInfo&) -> llvm::Type* { return builder.getDoubleTy(); },
                              [this](const analyzer::BooleanTypeInfo&) -> llvm::Type* { return builder.getInt1Ty(); },
                              [this, type](const analyzer::ArrayTypeInfo& arrayType) -> llvm::Type* {
                                  llvm::Type* elementType =
                                      getLLVMType(symbolTable.getTypeInfo(arrayType.element_type));
                                  return llvm::ArrayType::get(elementType, arrayType.size);
                              },
                              [this, type](const analyzer::RecordTypeInfo& recordType) -> llvm::Type* {
                                  std::vector<llvm::Type*> fieldTypes;
                                  fieldTypes.reserve(recordType.fields.size());
                                  for (const auto& [name, typeId] : recordType.fields) {
                                      fieldTypes.push_back(getLLVMType(symbolTable.getTypeInfo(typeId)));
                                  }
                                  return StructType::get(context, fieldTypes);
                              },
                          },
                          type);
    }

    Value* generateCast(Value* value, TypeId fromType, TypeId toType, const Span& span) {
        if (fromType == toType)
            return value;

        const TypeInfo& fromInfo = symbolTable.getTypeInfo(fromType);
        const TypeInfo& toInfo = symbolTable.getTypeInfo(toType);

        if (std::holds_alternative<IntegerTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<RealTypeInfo>(toInfo.definition)) {
            return builder.CreateSIToFP(value, builder.getDoubleTy(), "casttmp");
        }
        if (std::holds_alternative<IntegerTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<BooleanTypeInfo>(toInfo.definition)) {
            return builder.CreateICmpNE(value, ConstantInt::get(builder.getInt32Ty(), 0), "casttmp");
        }
        if (std::holds_alternative<RealTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<IntegerTypeInfo>(toInfo.definition)) {
            return builder.CreateFPToSI(value, builder.getInt32Ty(), "casttmp");
        }
        if (std::holds_alternative<BooleanTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<IntegerTypeInfo>(toInfo.definition)) {
            return builder.CreateZExt(value, builder.getInt32Ty(), "casttmp");
        }
        if (std::holds_alternative<BooleanTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<RealTypeInfo>(toInfo.definition)) {
            return builder.CreateUIToFP(value, builder.getDoubleTy(), "casttmp");
        }

        throw CodegenError{"Cannot cast between specified types", span};
    }

    Value* generateExpression(const parser::Expression& expr) {
        Value* result = generateBooleanExpression(expr.first);

        for (const auto& [op, boolExpr] : expr.rest) {
            Value* right = generateBooleanExpression(boolExpr);

            switch (op) {
            case parser::Expression::Operator::And:
                result = builder.CreateAnd(result, right, "andtmp");
                break;
            case parser::Expression::Operator::Or:
                result = builder.CreateOr(result, right, "ortmp");
                break;
            case parser::Expression::Operator::Xor:
                result = builder.CreateXor(result, right, "xortmp");
                break;
            }
        }

        return result;
    }

    Value* generateBooleanExpression(const parser::BooleanExpression& expr) {
        return std::visit(overloaded{[this](const Relation& rel) { return generateRelation(rel); },
                                     [this](const NotExpression& notExpr) { return generateNotExpression(notExpr); }},
                          expr);
    }

    Value* generateRelation(const parser::Relation& relation) {
        Value* left = generateNumberExpression(relation.first);

        if (!relation.second) {
            return left;
        }

        Value* right = generateNumberExpression(relation.second->next_operand);
        auto op = relation.second->operator_;

        bool isFloatingPoint = left->getType()->isFloatingPointTy() || right->getType()->isFloatingPointTy();

        if (isFloatingPoint) {
            if (left->getType()->isIntegerTy()) {
                left = builder.CreateSIToFP(left, builder.getDoubleTy());
            }
            if (right->getType()->isIntegerTy()) {
                right = builder.CreateSIToFP(right, builder.getDoubleTy());
            }

            switch (op) {
            case parser::Relation::Operator::Less:
                return builder.CreateFCmpOLT(left, right, "cmptmp");
            case parser::Relation::Operator::LessOrEqual:
                return builder.CreateFCmpOLE(left, right, "cmptmp");
            case parser::Relation::Operator::Greater:
                return builder.CreateFCmpOGT(left, right, "cmptmp");
            case parser::Relation::Operator::GreaterOrEqual:
                return builder.CreateFCmpOGE(left, right, "cmptmp");
            case parser::Relation::Operator::Equal:
                return builder.CreateFCmpOEQ(left, right, "cmptmp");
            case parser::Relation::Operator::NotEqual:
                return builder.CreateFCmpONE(left, right, "cmptmp");
            }
        } else {
            switch (op) {
            case parser::Relation::Operator::Less:
                return builder.CreateICmpSLT(left, right, "cmptmp");
            case parser::Relation::Operator::LessOrEqual:
                return builder.CreateICmpSLE(left, right, "cmptmp");
            case parser::Relation::Operator::Greater:
                return builder.CreateICmpSGT(left, right, "cmptmp");
            case parser::Relation::Operator::GreaterOrEqual:
                return builder.CreateICmpSGE(left, right, "cmptmp");
            case parser::Relation::Operator::Equal:
                return builder.CreateICmpEQ(left, right, "cmptmp");
            case parser::Relation::Operator::NotEqual:
                return builder.CreateICmpNE(left, right, "cmptmp");
            }
        }

        return nullptr;
    }

    Value* generateNotExpression(const parser::NotExpression& notExpr) {
        Value* operand = generatePrimary(notExpr.operand);
        return builder.CreateNot(operand, "nottmp");
    }

    Value* generateNumberExpression(const parser::NumberExpression& numExpr) {
        Value* result = generateSummand(numExpr.first);

        for (const auto& operation : numExpr.rest) {
            Value* right = generateSummand(operation.next_operand);

            if (result->getType()->isIntegerTy() && right->getType()->isIntegerTy()) {
                switch (operation.operator_) {
                case parser::NumberExpression::Operator::Plus:
                    result = builder.CreateAdd(result, right, "addtmp");
                    break;
                case parser::NumberExpression::Operator::Minus:
                    result = builder.CreateSub(result, right, "subtmp");
                    break;
                }
            } else {
                if (result->getType()->isIntegerTy()) {
                    result = builder.CreateSIToFP(result, builder.getDoubleTy());
                }
                if (right->getType()->isIntegerTy()) {
                    right = builder.CreateSIToFP(right, builder.getDoubleTy());
                }

                switch (operation.operator_) {
                case parser::NumberExpression::Operator::Plus:
                    result = builder.CreateFAdd(result, right, "faddtmp");
                    break;
                case parser::NumberExpression::Operator::Minus:
                    result = builder.CreateFSub(result, right, "fsubtmp");
                    break;
                }
            }
        }
        return result;
    }

    Value* generateSummand(const parser::Summand& summand) {
        Value* result = generatePrimary(summand.first);

        for (const auto& operation : summand.rest) {
            Value* right = generatePrimary(operation.next_operand);

            if (result->getType()->isIntegerTy() && right->getType()->isIntegerTy()) {
                switch (operation.operator_) {
                case parser::Summand::Operator::Multiply:
                    result = builder.CreateMul(result, right, "multmp");
                    break;
                case parser::Summand::Operator::Divide:
                    result = builder.CreateSDiv(result, right, "divtmp");
                    break;
                case parser::Summand::Operator::Modulo:
                    result = builder.CreateSRem(result, right, "modtmp");
                    break;
                }
            } else {

                if (result->getType()->isIntegerTy()) {
                    result = builder.CreateSIToFP(result, builder.getDoubleTy());
                }
                if (right->getType()->isIntegerTy()) {
                    right = builder.CreateSIToFP(right, builder.getDoubleTy());
                }

                switch (operation.operator_) {
                case parser::Summand::Operator::Multiply:
                    result = builder.CreateFMul(result, right, "fmultmp");
                    break;
                case parser::Summand::Operator::Divide:
                    result = builder.CreateFDiv(result, right, "fdivtmp");
                    break;
                case parser::Summand::Operator::Modulo:
                    throw CodegenError("Modulo operation not supported for floating point types", getSpan(summand));
                }
            }
        }
        return result;
    }

    Value* generatePrimary(const parser::Primary& primary) {
        return std::visit(
            overloaded{
                [this](const IntegerLiteral& lit) -> Value* {
                    return ConstantInt::getSigned(builder.getInt32Ty(), lit.value);
                },
                [this](const RealLiteral& lit) -> Value* { return ConstantFP::get(builder.getDoubleTy(), lit.value); },
                [this](const BooleanLiteral& lit) -> Value* {
                    return ConstantInt::get(builder.getInt1Ty(), static_cast<int64_t>(lit.value));
                },
                [this](const RoutineCall& call) -> Value* { return generateRoutineCall(call); },
                [this](const ModifiablePrimary& mp) -> Value* { return generateModifiablePrimary(mp); },
                [this](const UnarySign& sign) -> Value* {
                    Value* result = generatePrimary(*sign.operand);
                    if (sign.sign == UnarySign::Sign::Minus) {
                        if (result->getType()->isIntegerTy()) {
                            return builder.CreateNeg(result, "negtmp");
                        }
                        return builder.CreateFNeg(result, "fnegtmp");
                    }
                    return result;
                },
                [this](const ParenthesizedExpression& par) -> Value* { return generateExpression(*par.expression); }},
            primary);
    }

    Value* generateModifiablePrimary(const parser::ModifiablePrimary& primary) { // NOLINT(*complexity*)
        Value* base = namedValues[primary.variable.text];
        if (!base) {
            throw CodegenError{"Undeclared variable: " + primary.variable.text, primary.variable.span};
        }

        Value* current = base;
        TypeId currentTypeId = primary.variable_type;

        for (const auto& accessor : primary.accessors) {
            if (std::holds_alternative<Index>(accessor.key)) {
                const auto& index = std::get<Index>(accessor.key);

                Value* indexValue = generateExpression(index.value);

                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    throw CodegenError{"Indexing non-array type", index.bracket_span};
                }

                const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);

                std::vector<Value*> indices = {ConstantInt::get(builder.getInt32Ty(), 0), indexValue};

                current = builder.CreateGEP(getLLVMType(typeInfo), current, indices, "arrayidx");
                currentTypeId = arrayInfo.element_type;
            } else {
                const auto& field = std::get<Identifier>(accessor.key);

                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    if (field.text == "size") {
                        const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);
                        return ConstantInt::get(builder.getInt32Ty(), arrayInfo.size);
                    }
                    throw CodegenError{"No such field in array: " + field.text, field.span};
                }
                if (!std::holds_alternative<RecordTypeInfo>(typeInfo.definition)) {
                    throw CodegenError{"Accessing field of non-record type", field.span};
                }

                const auto& recordInfo = std::get<RecordTypeInfo>(typeInfo.definition);

                std::size_t fieldIndex = -1;
                for (std::size_t i = 0; i < recordInfo.fields.size(); ++i) {
                    if (recordInfo.fields[i].first == field.text) {
                        fieldIndex = i;
                        break;
                    }
                }

                if (fieldIndex == static_cast<std::size_t>(-1)) {
                    throw CodegenError{"No such field in record: " + field.text, field.span};
                }

                std::vector<Value*> indices = {ConstantInt::get(builder.getInt32Ty(), 0),
                                               ConstantInt::get(builder.getInt32Ty(), fieldIndex)};

                current = builder.CreateGEP(getLLVMType(typeInfo), current, indices, "fieldptr");
                currentTypeId = recordInfo.fields[fieldIndex].second;
            }
        }

        Value* loadedValue =
            builder.CreateLoad(getLLVMType(symbolTable.getTypeInfo(currentTypeId)), current, "loadtmp");
        return loadedValue;
    }

    Value* generateRoutineCall(const parser::RoutineCall& call) {
        Function* function = module.getFunction(call.routine_name.text);
        if (!function) {
            throw CodegenError{"Undefined routine: " + call.routine_name.text, call.routine_name.span};
        }

        std::vector<Value*> args;
        args.reserve(call.arguments.size());
        for (const auto& arg : call.arguments) {
            args.push_back(generateExpression(arg));
        }

        if (function->getReturnType()->isVoidTy()) {
            return builder.CreateCall(function, args);
        }
        return builder.CreateCall(function, args, !args.empty() ? "calltmp" : "");
    }

    void generateAssignment(const AssignmentStatement& assignment) {
        Value* rhs = generateExpression(assignment.expression);

        Value* lhs = generateModifiablePrimaryAddress(assignment.target);

        builder.CreateStore(rhs, lhs);
    }

    Value* generateModifiablePrimaryAddress(const parser::ModifiablePrimary& primary) { // NOLINT(*complexity*)
        Value* base = namedValues[primary.variable.text];
        if (!base) {
            throw CodegenError{"Undeclared variable: " + primary.variable.text, primary.variable.span};
        }

        Value* current = base;
        TypeId currentTypeId = primary.variable_type;

        for (const auto& accessor : primary.accessors) {
            if (std::holds_alternative<Index>(accessor.key)) {
                const auto& index = std::get<Index>(accessor.key);
                Value* indexValue = generateExpression(index.value);

                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    throw CodegenError{"Indexing non-array type", index.bracket_span};
                }

                const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);

                std::vector<Value*> indices = {ConstantInt::get(builder.getInt32Ty(), 0), indexValue};

                current = builder.CreateGEP(getLLVMType(typeInfo), current, indices, "arrayidx");
                currentTypeId = arrayInfo.element_type;
            } else {
                const auto& field = std::get<Identifier>(accessor.key);

                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    if (field.text == "size") {
                        const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);
                        return ConstantInt::get(builder.getInt32Ty(), arrayInfo.size);
                    }
                    throw CodegenError{"No such field in array: " + field.text, field.span};
                }
                if (!std::holds_alternative<RecordTypeInfo>(typeInfo.definition)) {
                    throw CodegenError{"Accessing field of non-record type", field.span};
                }

                const auto& recordInfo = std::get<RecordTypeInfo>(typeInfo.definition);

                std::size_t fieldIndex = -1;
                for (std::size_t i = 0; i < recordInfo.fields.size(); ++i) {
                    if (recordInfo.fields[i].first == field.text) {
                        fieldIndex = i;
                        break;
                    }
                }

                if (fieldIndex == static_cast<std::size_t>(-1)) {
                    throw CodegenError{"No such field in record: " + field.text, field.span};
                }

                std::vector<Value*> indices = {ConstantInt::get(builder.getInt32Ty(), 0),
                                               ConstantInt::get(builder.getInt32Ty(), fieldIndex)};

                current = builder.CreateGEP(getLLVMType(typeInfo), current, indices, "fieldptr");
                currentTypeId = recordInfo.fields[fieldIndex].second;
            }
        }

        return current;
    }

    void generateGlobalVariableDeclaration(const parser::VariableDeclaration& declaration) {
        llvm::Type* llvmType = getLLVMType(symbolTable.getTypeInfo(declaration.resolved_type));
        Constant* initialValue = nullptr;

        if (declaration.value) {
            Value* value = generateExpression(*declaration.value);
            if (declaration.resolved_type != declaration.value->type) {
                value = generateCast(value, declaration.value->type, declaration.resolved_type, declaration.name.span);
            }
            initialValue = dyn_cast<Constant>(value);
        } else {
            initialValue = Constant::getNullValue(llvmType);
        }

        auto* globalVariable = new GlobalVariable(
            module, llvmType, false, GlobalValue::ExternalLinkage, initialValue, declaration.name.text);
        namedValues[declaration.name.text] = globalVariable;
    }

    void generateLocalVariableDeclaration(const parser::VariableDeclaration& declaration) {
        llvm::Type* llvmType = getLLVMType(symbolTable.getTypeInfo(declaration.resolved_type));

        Function* currentFunction = builder.GetInsertBlock()->getParent();
        IRBuilder<> allocaBuilder(&currentFunction->getEntryBlock(), currentFunction->getEntryBlock().begin());
        AllocaInst* alloca = allocaBuilder.CreateAlloca(llvmType, nullptr, declaration.name.text);

        if (declaration.value) {
            Value* value = generateExpression(*declaration.value);
            if (declaration.resolved_type != declaration.value->type) {
                value = generateCast(value, declaration.value->type, declaration.resolved_type, declaration.name.span);
            }
            builder.CreateStore(value, alloca);
        } else {
            builder.CreateStore(Constant::getNullValue(llvmType), alloca);
        }

        namedValues[declaration.name.text] = alloca;
    }

    void generateTypeDeclaration(const TypeDeclaration& declaration) {}

    void generateStatement(const Statement& stmt) {
        std::visit(overloaded{
                       [this](const VariableDeclaration& var) { generateLocalVariableDeclaration(var); },
                       [this](const TypeDeclaration& type) { generateTypeDeclaration(type); },

                       [this](const AssignmentStatement& assignment) { generateAssignment(assignment); },
                       [this](const RoutineCall& call) { generateRoutineCall(call); },
                       [this](const WhileStatement& whileStmt) { generateWhileLoop(whileStmt); },
                       [this](const ForStatement& forStmt) { generateForLoop(forStmt); },
                       [this](const IfStatement& ifStmt) { generateIfStatement(ifStmt); },
                       [this](const PrintStatement& printStmt) { generatePrintStatement(printStmt); },
                       [this](const ReturnStatement& retStmt) { generateReturnStatement(retStmt); },
                   },
                   stmt);
    }

    void generateWhileLoop(const WhileStatement& whileStmt) {
        Function* function = builder.GetInsertBlock()->getParent();

        BasicBlock* condBlock = BasicBlock::Create(context, "while.cond", function);
        BasicBlock* bodyBlock = BasicBlock::Create(context, "while.body", function);
        BasicBlock* endBlock = BasicBlock::Create(context, "while.end", function);

        builder.CreateBr(condBlock);

        builder.SetInsertPoint(condBlock);
        Value* condValue = generateExpression(whileStmt.condition);
        builder.CreateCondBr(condValue, bodyBlock, endBlock);

        builder.SetInsertPoint(bodyBlock);
        generateBlock(whileStmt.body);
        builder.CreateBr(condBlock);

        builder.SetInsertPoint(endBlock);
    }

    void generateForLoop(const ForStatement& forStmt) {
        Function* function = builder.GetInsertBlock()->getParent();

        BasicBlock* condBlock = BasicBlock::Create(context, "for.cond", function);
        BasicBlock* bodyBlock = BasicBlock::Create(context, "for.body", function);
        BasicBlock* endBlock = BasicBlock::Create(context, "for.end", function);

        auto oldNamedValues = namedValues;

        if (std::holds_alternative<Expression>(forStmt.range)) {
            generateArrayForLoop(forStmt, condBlock, bodyBlock, endBlock, function);
        } else {
            generateRangeForLoop(forStmt, condBlock, bodyBlock, endBlock, function);
        }

        namedValues = std::move(oldNamedValues);

        builder.SetInsertPoint(endBlock);
    }

    void generateArrayForLoop(const ForStatement& forStmt,
                              BasicBlock* condBlock,
                              BasicBlock* bodyBlock,
                              BasicBlock* endBlock,
                              Function* function) {
        const auto& arrayExpr = std::get<Expression>(forStmt.range);

        Value* arrayPtr = nullptr;

        if (const auto* rel = std::get_if<Relation>(&arrayExpr.first)) {
            if (const auto* mp = std::get_if<ModifiablePrimary>(&rel->first.first.first)) {
                arrayPtr = generateModifiablePrimaryAddress(*mp);
            } else {
                Value* arrayValue = generateExpression(arrayExpr);
                IRBuilder<> allocaBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
                AllocaInst* tempAlloca = allocaBuilder.CreateAlloca(
                    getLLVMType(symbolTable.getTypeInfo(arrayExpr.type)), nullptr, "temp_array");
                builder.CreateStore(arrayValue, tempAlloca);
                arrayPtr = tempAlloca;
            }
        } else {
            std::unreachable();
        }

        TypeId arrayTypeId = arrayExpr.type;
        const TypeInfo& arrayTypeInfo = symbolTable.getTypeInfo(arrayTypeId);

        if (!std::holds_alternative<ArrayTypeInfo>(arrayTypeInfo.definition)) {
            throw CodegenError{"For loop range must be an array", forStmt.variable_name.span};
        }

        const auto& arrayInfo = std::get<ArrayTypeInfo>(arrayTypeInfo.definition);
        llvm::Type* elementType = getLLVMType(symbolTable.getTypeInfo(arrayInfo.element_type));

        IRBuilder<> allocaBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
        AllocaInst* indexAlloca = allocaBuilder.CreateAlloca(builder.getInt32Ty(), nullptr, "index");

        if (forStmt.is_reversed) {
            builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), arrayInfo.size - 1), indexAlloca);
        } else {
            builder.CreateStore(ConstantInt::get(builder.getInt32Ty(), 0), indexAlloca);
        }

        AllocaInst* loopVarAlloca = allocaBuilder.CreateAlloca(elementType, nullptr, forStmt.variable_name.text);
        namedValues[forStmt.variable_name.text] = loopVarAlloca;

        builder.CreateBr(condBlock);

        builder.SetInsertPoint(condBlock);
        Value* index = builder.CreateLoad(builder.getInt32Ty(), indexAlloca, "index");

        Value* condValue = nullptr;
        if (forStmt.is_reversed) {
            condValue = builder.CreateICmpSGE(index, ConstantInt::get(builder.getInt32Ty(), 0), "loopcond");
        } else {
            condValue =
                builder.CreateICmpSLT(index, ConstantInt::get(builder.getInt32Ty(), arrayInfo.size), "loopcond");
        }

        builder.CreateCondBr(condValue, bodyBlock, endBlock);

        builder.SetInsertPoint(bodyBlock);

        std::vector<Value*> indices = {ConstantInt::get(builder.getInt32Ty(), 0), index};
        Value* elementPtr = builder.CreateGEP(getLLVMType(arrayTypeInfo), arrayPtr, indices, "elementptr");
        Value* element = builder.CreateLoad(elementType, elementPtr, "element");

        builder.CreateStore(element, loopVarAlloca);

        generateBlock(forStmt.body);

        if (forStmt.is_reversed) {
            Value* nextIndex = builder.CreateSub(index, ConstantInt::get(builder.getInt32Ty(), 1), "nextindex");
            builder.CreateStore(nextIndex, indexAlloca);
        } else {
            Value* nextIndex = builder.CreateAdd(index, ConstantInt::get(builder.getInt32Ty(), 1), "nextindex");
            builder.CreateStore(nextIndex, indexAlloca);
        }

        builder.CreateBr(condBlock);
    }

    void generateRangeForLoop(const ForStatement& forStmt,
                              BasicBlock* condBlock,
                              BasicBlock* bodyBlock,
                              BasicBlock* endBlock,
                              Function* function) {
        const auto& range = std::get<std::pair<Expression, Expression>>(forStmt.range);
        const auto& startExpr = range.first;
        const auto& endExpr = range.second;

        Value* startValue = generateExpression(startExpr);
        Value* endValue = generateExpression(endExpr);

        if (startValue->getType()->isFloatingPointTy()) {
            startValue = builder.CreateFPToSI(startValue, builder.getInt32Ty(), "startconv");
        }
        if (endValue->getType()->isFloatingPointTy()) {
            endValue = builder.CreateFPToSI(endValue, builder.getInt32Ty(), "endconv");
        }

        IRBuilder<> allocaBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
        AllocaInst* loopVarAlloca =
            allocaBuilder.CreateAlloca(builder.getInt32Ty(), nullptr, forStmt.variable_name.text);
        namedValues[forStmt.variable_name.text] = loopVarAlloca;

        if (forStmt.is_reversed) {
            builder.CreateStore(endValue, loopVarAlloca);
        } else {
            builder.CreateStore(startValue, loopVarAlloca);
        }

        builder.CreateBr(condBlock);

        builder.SetInsertPoint(condBlock);
        Value* loopVar = builder.CreateLoad(builder.getInt32Ty(), loopVarAlloca, "loopvar");

        Value* condValue = nullptr;
        if (forStmt.is_reversed) {
            condValue = builder.CreateICmpSGE(loopVar, startValue, "loopcond");
        } else {
            condValue = builder.CreateICmpSLE(loopVar, endValue, "loopcond");
        }

        builder.CreateCondBr(condValue, bodyBlock, endBlock);

        builder.SetInsertPoint(bodyBlock);

        generateBlock(forStmt.body);

        if (forStmt.is_reversed) {
            Value* nextValue = builder.CreateSub(loopVar, ConstantInt::get(builder.getInt32Ty(), 1), "nextval");
            builder.CreateStore(nextValue, loopVarAlloca);
        } else {
            Value* nextValue = builder.CreateAdd(loopVar, ConstantInt::get(builder.getInt32Ty(), 1), "nextval");
            builder.CreateStore(nextValue, loopVarAlloca);
        }

        builder.CreateBr(condBlock);
    }

    void generateIfStatement(const IfStatement& ifStmt) {
        Value* condValue = generateExpression(ifStmt.condition);

        Function* function = builder.GetInsertBlock()->getParent();
        BasicBlock* thenBlock = BasicBlock::Create(context, "if.then", function);
        BasicBlock* elseBlock = BasicBlock::Create(context, "if.else", function);
        BasicBlock* mergeBlock = BasicBlock::Create(context, "if.merge", function);

        builder.CreateCondBr(condValue, thenBlock, elseBlock);

        builder.SetInsertPoint(thenBlock);
        generateBlock(ifStmt.true_branch);
        builder.CreateBr(mergeBlock);

        builder.SetInsertPoint(elseBlock);
        if (ifStmt.false_branch) {
            generateBlock(*ifStmt.false_branch);
        }
        builder.CreateBr(mergeBlock);

        builder.SetInsertPoint(mergeBlock);
    }

    void generatePrintStatement(const PrintStatement& printStmt) {
        for (const auto& arg : printStmt.arguments) {
            if (std::holds_alternative<parser::StringLiteral>(arg)) {
                const auto& str = std::get<parser::StringLiteral>(arg);
                Value* formatStr = builder.CreateGlobalString(str.value);
                builder.CreateCall(printfFunc, {formatStr});
                formatStr = builder.CreateGlobalString(" ");
                builder.CreateCall(printfFunc, {formatStr});
            } else {
                const auto& expr = std::get<Expression>(arg);
                Value* value = generateExpression(expr);

                Value* formatStr = nullptr;
                if (value->getType()->isIntegerTy(CHAR_BIT * sizeof(int))) {
                    formatStr = builder.CreateGlobalString("%d ");
                } else if (value->getType()->isDoubleTy()) {
                    formatStr = builder.CreateGlobalString("%f ");
                } else if (value->getType()->isIntegerTy(1)) {
                    formatStr = builder.CreateGlobalString("%s ");
                    Value* trueStr = builder.CreateGlobalString("true ");
                    Value* falseStr = builder.CreateGlobalString("false ");
                    value = builder.CreateSelect(value, trueStr, falseStr);
                }

                if (formatStr) {
                    builder.CreateCall(printfFunc, {formatStr, value});
                } else {
                    const auto& expr = std::get<Expression>(arg);
                    throw CodegenError{"Cannot print type '" + symbolTable.getTypeInfo(expr.type).name + "'",
                                       getSpan(expr)};
                }
            }
        }
        builder.CreateCall(printfFunc, {builder.CreateGlobalString("\n")});
    }

    void generateReturnStatement(const ReturnStatement& retStmt) {
        if (retStmt.value) {
            Value* retValue = generateExpression(*retStmt.value);
            builder.CreateRet(retValue);
        } else {
            builder.CreateRetVoid();
        }
    }

    void generateBlock(const Block& block) {
        for (const Statement& element : block) {
            generateStatement(element);
        }
    }

    Function* generateRoutineDeclaration(const parser::RoutineDeclaration& declaration) {
        llvm::Type* returnType = declaration.resolved_return_type
                                     ? getLLVMType(symbolTable.getTypeInfo(*declaration.resolved_return_type))
                                     : builder.getVoidTy();
        std::vector<llvm::Type*> paramTypes;
        paramTypes.reserve(declaration.parameters.size());
        for (const auto& param : declaration.parameters) {
            paramTypes.push_back(getLLVMType(symbolTable.getTypeInfo(param.resolved_type)));
        }

        FunctionType* functionType = FunctionType::get(returnType, paramTypes, false);
        Function* function =
            Function::Create(functionType, Function::ExternalLinkage, declaration.name.text, &module);

        std::size_t idx = 0;
        for (auto& arg : function->args()) {
            arg.setName(declaration.parameters[idx].name.text);
            ++idx;
        }

        return function;
    }

    void generateRoutineBody(const RoutineDeclaration& declaration, Function* function) {
        BasicBlock* block = BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(block);

        auto oldNamedValues = namedValues;

        for (auto& arg : function->args()) {
            AllocaInst* alloca = builder.CreateAlloca(arg.getType(), nullptr, arg.getName());
            builder.CreateStore(&arg, alloca);
            namedValues[std::string(arg.getName())] = alloca;
        }

        if (declaration.body) {
            if (std::holds_alternative<Block>(*declaration.body)) {
                generateBlock(std::get<Block>(*declaration.body));
                // return for void functions can be ommited
                if (!symbolTable.getRoutines().find(declaration.name.text)->second.last_return &&
                    !declaration.return_type)
                    builder.CreateRetVoid();
            } else {
                Value* result = generateExpression(std::get<Expression>(*declaration.body));
                builder.CreateRet(result);
            }
        } else {
            if (!function->getReturnType()->isVoidTy()) {
                builder.CreateRet(Constant::getNullValue(function->getReturnType()));
            } else {
                builder.CreateRetVoid();
            }
        }

        std::string verification_error;
        llvm::raw_string_ostream error_stream(verification_error);
        if (verifyFunction(*function, &error_stream)) {
            throw CodegenError{"Function verification failed: " + verification_error, declaration.name.span};
        }

        namedValues = std::move(oldNamedValues);
    }

    void generateCode() {
        for (const auto& declaration : program.declarations) {
            if (std::holds_alternative<VariableDeclaration>(declaration)) {
                generateGlobalVariableDeclaration(std::get<VariableDeclaration>(declaration));
            } else if (std::holds_alternative<RoutineDeclaration>(declaration)) {
                const auto& routine = std::get<RoutineDeclaration>(declaration);
                if (!routine.body)
                    continue;
                Function* func = generateRoutineDeclaration(routine);
                functions[std::get<RoutineDeclaration>(declaration).name.text] = func;
            }
        }

        for (const auto& declaration : program.declarations) {
            if (std::holds_alternative<RoutineDeclaration>(declaration)) {
                const auto& routine = std::get<RoutineDeclaration>(declaration);
                if (routine.body) {
                    Function* func = functions[routine.name.text];
                    if (func) {
                        generateRoutineBody(routine, func);
                    }
                }
            }
        }
    }

  public:
    explicit CodeGenerator(const Program& ast, const SymbolTable& symbolTable)
        : symbolTable{symbolTable}, program{ast} {}

    std::expected<void, CodegenError> generate(llvm::raw_ostream& out) {
        try {
            generateCode();
            module.print(out, nullptr);
            return {};
        } catch (const CodegenError& error) {
            return std::unexpected{error};
        }
    }
};
// NOLINTEND(*recursion*)

std::expected<void, CodegenError> generate_code(const Program& ast, const SymbolTable& symbolTable, std::ostream& out) {
    CodeGenerator codegen{ast, symbolTable};
    llvm::raw_os_ostream stream_adaptor{out};
    return codegen.generate(stream_adaptor);
}

} // namespace codegen
