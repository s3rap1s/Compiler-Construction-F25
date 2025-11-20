#include "compiler.hpp"

#include "analyzer/symbol_table.hpp"
#include "compiler/compile_error.hpp"
#include "parser/ast.hpp"

#include <cassert>
#include <iostream>
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
#include <utility>
#include <variant>
#include <vector>

namespace compiler {

using namespace parser;
using namespace analyzer;
using namespace llvm;

// NOLINTBEGIN(*recursion*)
struct Compiler {
  private:
    std::unique_ptr<llvm::LLVMContext> context = std::make_unique<LLVMContext>();
    std::unique_ptr<llvm::IRBuilder<>> builder = std::make_unique<IRBuilder<>>(*context);
    std::unique_ptr<llvm::Module> module = std::make_unique<Module>("Module", *context);

    // Declare printf function
    FunctionType* printfType = FunctionType::get(builder->getInt32Ty(), {builder->getInt8Ty()->getPointerTo()}, true);
    Function* printfFunc = Function::Create(printfType, Function::ExternalLinkage, "printf", module.get());

    std::unordered_map<std::string, Value*> namedValues;
    std::unordered_map<std::string, Function*> functions;

    const SymbolTable& symbolTable; // NOLINT(*ref*)
    const Program& program;         // NOLINT(*ref*)

    llvm::Type* getLLVMType(const analyzer::TypeInfo& typeInfo) {
        const auto& type = typeInfo.definition;
        return std::visit(overloaded{
                              [this](const analyzer::IntegerTypeInfo&) -> llvm::Type* { return builder->getInt32Ty(); },
                              [this](const analyzer::RealTypeInfo&) -> llvm::Type* { return builder->getDoubleTy(); },
                              [this](const analyzer::BooleanTypeInfo&) -> llvm::Type* { return builder->getInt1Ty(); },
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
                                  return StructType::get(*context, fieldTypes);
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
            return builder->CreateSIToFP(value, builder->getDoubleTy(), "casttmp");
        }
        if (std::holds_alternative<RealTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<IntegerTypeInfo>(toInfo.definition)) {
            return builder->CreateFPToSI(value, builder->getInt32Ty(), "casttmp");
        }
        if (std::holds_alternative<IntegerTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<BooleanTypeInfo>(toInfo.definition)) {
            return builder->CreateICmpNE(value, ConstantInt::get(builder->getInt32Ty(), 0), "booltmp");
        }
        if (std::holds_alternative<BooleanTypeInfo>(fromInfo.definition) &&
            std::holds_alternative<IntegerTypeInfo>(toInfo.definition)) {
            return builder->CreateZExt(value, builder->getInt32Ty(), "inttmp");
        }

        throw CompileError{"Cannot cast between specified types", span};
    }

    Value* generateExpression(const parser::Expression& expr) {
        Value* result = generateBooleanExpression(expr.first);

        for (const auto& [op, boolExpr] : expr.rest) {
            Value* right = generateBooleanExpression(boolExpr);

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

        // Determine comparison type based on operand types
        bool isFloatingPoint = left->getType()->isFloatingPointTy() || right->getType()->isFloatingPointTy();

        if (isFloatingPoint) {
            // Convert integers to floats if needed
            if (left->getType()->isIntegerTy()) {
                left = builder->CreateSIToFP(left, builder->getDoubleTy());
            }
            if (right->getType()->isIntegerTy()) {
                right = builder->CreateSIToFP(right, builder->getDoubleTy());
            }

            switch (op) {
            case parser::Relation::Operator::Less:
                return builder->CreateFCmpOLT(left, right, "cmptmp");
            case parser::Relation::Operator::LessOrEqual:
                return builder->CreateFCmpOLE(left, right, "cmptmp");
            case parser::Relation::Operator::Greater:
                return builder->CreateFCmpOGT(left, right, "cmptmp");
            case parser::Relation::Operator::GreaterOrEqual:
                return builder->CreateFCmpOGE(left, right, "cmptmp");
            case parser::Relation::Operator::Equal:
                return builder->CreateFCmpOEQ(left, right, "cmptmp");
            case parser::Relation::Operator::NotEqual:
                return builder->CreateFCmpONE(left, right, "cmptmp");
            }
        } else {
            switch (op) {
            case parser::Relation::Operator::Less:
                return builder->CreateICmpSLT(left, right, "cmptmp");
            case parser::Relation::Operator::LessOrEqual:
                return builder->CreateICmpSLE(left, right, "cmptmp");
            case parser::Relation::Operator::Greater:
                return builder->CreateICmpSGT(left, right, "cmptmp");
            case parser::Relation::Operator::GreaterOrEqual:
                return builder->CreateICmpSGE(left, right, "cmptmp");
            case parser::Relation::Operator::Equal:
                return builder->CreateICmpEQ(left, right, "cmptmp");
            case parser::Relation::Operator::NotEqual:
                return builder->CreateICmpNE(left, right, "cmptmp");
            }
        }

        return nullptr; // unreachable
    }

    Value* generateNotExpression(const parser::NotExpression& notExpr) {
        Value* operand = generatePrimary(notExpr.operand);
        return builder->CreateNot(operand, "nottmp");
    }

    Value* generateNumberExpression(const parser::NumberExpression& numExpr) {
        Value* result = generateSummand(numExpr.first);

        for (const auto& operation : numExpr.rest) {
            Value* right = generateSummand(operation.next_operand);

            if (result->getType()->isIntegerTy() && right->getType()->isIntegerTy()) {
                switch (operation.operator_) {
                case parser::NumberExpression::Operator::Plus:
                    result = builder->CreateAdd(result, right, "addtmp");
                    break;
                case parser::NumberExpression::Operator::Minus:
                    result = builder->CreateSub(result, right, "subtmp");
                    break;
                }
            } else {
                // Convert to floating point if needed
                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateSIToFP(result, builder->getDoubleTy());
                }
                if (right->getType()->isIntegerTy()) {
                    right = builder->CreateSIToFP(right, builder->getDoubleTy());
                }

                switch (operation.operator_) {
                case parser::NumberExpression::Operator::Plus:
                    result = builder->CreateFAdd(result, right, "faddtmp");
                    break;
                case parser::NumberExpression::Operator::Minus:
                    result = builder->CreateFSub(result, right, "fsubtmp");
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
                    result = builder->CreateMul(result, right, "multmp");
                    break;
                case parser::Summand::Operator::Divide:
                    result = builder->CreateSDiv(result, right, "divtmp");
                    break;
                case parser::Summand::Operator::Modulo:
                    result = builder->CreateSRem(result, right, "modtmp");
                    break;
                }
            } else {
                // Convert to floating point if needed

                if (result->getType()->isIntegerTy()) {
                    result = builder->CreateSIToFP(result, builder->getDoubleTy());
                }
                if (right->getType()->isIntegerTy()) {
                    right = builder->CreateSIToFP(right, builder->getDoubleTy());
                }

                switch (operation.operator_) {
                case parser::Summand::Operator::Multiply:
                    result = builder->CreateFMul(result, right, "fmultmp");
                    break;
                case parser::Summand::Operator::Divide:
                    result = builder->CreateFDiv(result, right, "fdivtmp");
                    break;
                case parser::Summand::Operator::Modulo:
                    throw CompileError("Modulo operation not supported for floating point types", {0, 0, 0, 0});
                }
            }
        }
        return result;
    }

    Value* generatePrimary(const parser::Primary& primary) {
        return std::visit(
            overloaded{
                [this](const IntegerLiteral& lit) -> Value* {
                    return ConstantInt::getSigned(builder->getInt32Ty(), lit.value);
                },
                [this](const RealLiteral& lit) -> Value* { return ConstantFP::get(builder->getDoubleTy(), lit.value); },
                [this](const BooleanLiteral& lit) -> Value* {
                    return ConstantInt::get(builder->getInt1Ty(), static_cast<int64_t>(lit.value));
                },
                [this](const RoutineCall& call) -> Value* { return generateRoutineCall(call); },
                [this](const ModifiablePrimary& mp) -> Value* { return generateModifiablePrimary(mp); },
                [this](const UnarySign& sign) -> Value* {
                    Value* result = generatePrimary(*sign.operand);
                    if (sign.sign == UnarySign::Sign::Minus) {
                        if (result->getType()->isIntegerTy()) {
                            return builder->CreateNeg(result, "negtmp");
                        }
                        return builder->CreateFNeg(result, "fnegtmp");
                    }
                    return result;
                },
                [this](const ParenthesizedExpression& par) -> Value* { return generateExpression(*par.expression); }},
            primary);
    }

    Value* generateModifiablePrimary(const parser::ModifiablePrimary& primary) {
        // Look up the variable
        Value* base = namedValues[primary.variable.text];
        if (!base) {
            throw CompileError{"Undeclared variable: " + primary.variable.text, primary.variable.span};
        }

        // For array iteration, we need the POINTER, not the value
        // Only load at the very end if we're not doing array access
        Value* current = base; // Start with the pointer
        TypeId currentTypeId = primary.variable_type;

        // Process accessors
        for (const auto& accessor : primary.accessors) {
            if (std::holds_alternative<Index>(accessor.key)) {
                const auto& index = std::get<Index>(accessor.key);

                // Generate index expression
                Value* indexValue = generateExpression(index.value);

                // Get array type info
                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    throw CompileError{"Indexing non-array type", index.bracket_span};
                }

                const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);

                // Generate GEP for array access - current is already a pointer
                std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0), indexValue};

                current = builder->CreateGEP(getLLVMType(typeInfo), current, indices, "arrayidx");
                currentTypeId = arrayInfo.element_type;
            } else {
                const auto& field = std::get<Identifier>(accessor.key);

                // Get record type info
                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<RecordTypeInfo>(typeInfo.definition)) {
                    throw CompileError{"Accessing field of non-record type", field.span};
                }

                const auto& recordInfo = std::get<RecordTypeInfo>(typeInfo.definition);

                // Find field index
                int fieldIndex = -1;
                for (int i = 0; i < recordInfo.fields.size(); ++i) {
                    if (recordInfo.fields[i].first == field.text) {
                        fieldIndex = i;
                        break;
                    }
                }

                if (fieldIndex == -1) {
                    throw CompileError{"No such field in record: " + field.text, field.span};
                }

                // Generate GEP for field access - current is already a pointer
                std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0),
                                               ConstantInt::get(builder->getInt32Ty(), fieldIndex)};

                current = builder->CreateGEP(getLLVMType(typeInfo), current, indices, "fieldptr");
                currentTypeId = recordInfo.fields[fieldIndex].second;
            }
        }

        // Only load if this is being used as a value (not for array iteration)
        // For now, always load - we'll need to differentiate usage contexts
        Value* loadedValue =
            builder->CreateLoad(getLLVMType(symbolTable.getTypeInfo(currentTypeId)), current, "loadtmp");
        return loadedValue;
    }

    Value* generateModifiablePrimary1(const parser::ModifiablePrimary& primary) {
        // Look up the variable
        Value* base = namedValues[primary.variable.text];
        if (!base) {
            throw CompileError{"Undeclared variable: " + primary.variable.text, primary.variable.span};
        }

        // Load the base value
        Value* current =
            builder->CreateLoad(getLLVMType(symbolTable.getTypeInfo(primary.variable_type)), base, "loadtmp");

        // Process accessors
        TypeId currentTypeId = primary.variable_type;

        for (const auto& accessor : primary.accessors) {
            if (std::holds_alternative<Index>(accessor.key)) {
                const auto& index = std::get<Index>(accessor.key);

                // Generate index expression
                Value* indexValue = generateExpression(index.value);

                // Get array type info
                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    throw CompileError{"Indexing non-array type", index.bracket_span};
                }

                const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);

                // Generate GEP for array access
                std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0), indexValue};

                current = builder->CreateGEP(getLLVMType(typeInfo), current, indices, "arrayidx");
                current = builder->CreateLoad(
                    getLLVMType(symbolTable.getTypeInfo(arrayInfo.element_type)), current, "elemload");
                currentTypeId = arrayInfo.element_type;
            } else {
                const auto& field = std::get<Identifier>(accessor.key);

                // Get record type info
                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<RecordTypeInfo>(typeInfo.definition)) {
                    throw CompileError{"Accessing field of non-record type", field.span};
                }

                const auto& recordInfo = std::get<RecordTypeInfo>(typeInfo.definition);

                // Find field index
                int fieldIndex = -1;
                for (int i = 0; i < recordInfo.fields.size(); ++i) {
                    if (recordInfo.fields[i].first == field.text) {
                        fieldIndex = i;
                        break;
                    }
                }

                if (fieldIndex == -1) {
                    throw CompileError{"No such field in record: " + field.text, field.span};
                }

                // Generate GEP for field access
                std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0),
                                               ConstantInt::get(builder->getInt32Ty(), fieldIndex)};

                current = builder->CreateGEP(getLLVMType(typeInfo), current, indices, "fieldptr");
                current = builder->CreateLoad(
                    getLLVMType(symbolTable.getTypeInfo(recordInfo.fields[fieldIndex].second)), current, "fieldload");
                currentTypeId = recordInfo.fields[fieldIndex].second;
            }
        }

        return current;
    }

    Value* generateRoutineCall(const parser::RoutineCall& call) {
        // Look up function
        Function* function = module->getFunction(call.routine_name.text);
        if (!function) {
            throw CompileError{"Undefined routine: " + call.routine_name.text, call.routine_name.span};
        }

        // Generate arguments
        std::vector<Value*> args;
        args.reserve(call.arguments.size());
        for (const auto& arg : call.arguments) {
            args.push_back(generateExpression(arg));
        }

        return builder->CreateCall(function, args, "calltmp");
    }

    void generateAssignment(const AssignmentStatement& assignment) {
        Value* rhs = generateExpression(assignment.expression);

        // Generate LHS - we need the address, not the value
        Value* lhs = generateModifiablePrimaryAddress(assignment.target);

        builder->CreateStore(rhs, lhs);
    }

    Value* generateModifiablePrimaryAddress(const parser::ModifiablePrimary& primary) {
        Value* base = namedValues[primary.variable.text];
        if (!base) {
            throw CompileError{"Undeclared variable: " + primary.variable.text, primary.variable.span};
        }

        Value* current = base;
        TypeId currentTypeId = primary.variable_type;

        for (const auto& accessor : primary.accessors) {
            if (std::holds_alternative<Index>(accessor.key)) {
                const auto& index = std::get<Index>(accessor.key);
                Value* indexValue = generateExpression(index.value);

                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<ArrayTypeInfo>(typeInfo.definition)) {
                    throw CompileError{"Indexing non-array type", index.bracket_span};
                }

                const auto& arrayInfo = std::get<ArrayTypeInfo>(typeInfo.definition);

                std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0), indexValue};

                current = builder->CreateGEP(getLLVMType(typeInfo), current, indices, "arrayidx");
                currentTypeId = arrayInfo.element_type;
            } else {
                const auto& field = std::get<Identifier>(accessor.key);

                const TypeInfo& typeInfo = symbolTable.getTypeInfo(currentTypeId);
                if (!std::holds_alternative<RecordTypeInfo>(typeInfo.definition)) {
                    throw CompileError{"Accessing field of non-record type", field.span};
                }

                const auto& recordInfo = std::get<RecordTypeInfo>(typeInfo.definition);

                int fieldIndex = -1;
                for (unsigned long i = 0; i < recordInfo.fields.size(); ++i) {
                    if (recordInfo.fields[i].first == field.text) {
                        fieldIndex = i;
                        break;
                    }
                }

                if (fieldIndex == -1) {
                    throw CompileError{"No such field in record: " + field.text, field.span};
                }

                std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0),
                                               ConstantInt::get(builder->getInt32Ty(), fieldIndex)};

                current = builder->CreateGEP(getLLVMType(typeInfo), current, indices, "fieldptr");
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
            *module, llvmType, false, GlobalValue::ExternalLinkage, initialValue, declaration.name.text);
        namedValues[declaration.name.text] = globalVariable;
    }

    void generateLocalVariableDeclaration(const parser::VariableDeclaration& declaration) {
        llvm::Type* llvmType = getLLVMType(symbolTable.getTypeInfo(declaration.resolved_type));

        Function* currentFunction = builder->GetInsertBlock()->getParent();
        IRBuilder<> allocaBuilder(&currentFunction->getEntryBlock(), currentFunction->getEntryBlock().begin());
        AllocaInst* alloca = allocaBuilder.CreateAlloca(llvmType, nullptr, declaration.name.text);

        if (declaration.value) {
            Value* value = generateExpression(*declaration.value);
            if (declaration.resolved_type != declaration.value->type) {
                value = generateCast(value, declaration.value->type, declaration.resolved_type, declaration.name.span);
            }
            builder->CreateStore(value, alloca);
        } else {
            builder->CreateStore(Constant::getNullValue(llvmType), alloca);
        }

        namedValues[declaration.name.text] = alloca;
    }

    void generateTypeDeclaration(const TypeDeclaration& declaration) {
        // Types are already handled in getLLVMType, nothing to do here
    }

    void generateStatement(const Statement& stmt) {
        std::visit(overloaded{[this](const AssignmentStatement& assignment) { generateAssignment(assignment); },
                              [this](const RoutineCall& call) { generateRoutineCall(call); },
                              [this](const WhileStatement& whileStmt) { generateWhileLoop(whileStmt); },
                              [this](const ForStatement& forStmt) { generateForLoop(forStmt); },
                              [this](const IfStatement& ifStmt) { generateIfStatement(ifStmt); },
                              [this](const PrintStatement& printStmt) { generatePrintStatement(printStmt); },
                              [this](const ReturnStatement& retStmt) { generateReturnStatement(retStmt); },
                              [](const NoopStatement&) { /* Do nothing */ }},
                   stmt);
    }

    void generateWhileLoop(const WhileStatement& whileStmt) {
        Function* function = builder->GetInsertBlock()->getParent();

        BasicBlock* condBlock = BasicBlock::Create(*context, "while.cond", function);
        BasicBlock* bodyBlock = BasicBlock::Create(*context, "while.body", function);
        BasicBlock* endBlock = BasicBlock::Create(*context, "while.end", function);

        // Jump to condition block
        builder->CreateBr(condBlock);

        // Condition block
        builder->SetInsertPoint(condBlock);
        Value* condValue = generateExpression(whileStmt.condition);
        builder->CreateCondBr(condValue, bodyBlock, endBlock);

        // Body block
        builder->SetInsertPoint(bodyBlock);
        generateBlock(whileStmt.body);
        builder->CreateBr(condBlock);

        // End block
        builder->SetInsertPoint(endBlock);
    }

    void generateForLoop(const ForStatement& forStmt) {
        Function* function = builder->GetInsertBlock()->getParent();

        // Create basic blocks for the loop
        BasicBlock* condBlock = BasicBlock::Create(*context, "for.cond", function);
        BasicBlock* bodyBlock = BasicBlock::Create(*context, "for.body", function);
        BasicBlock* endBlock = BasicBlock::Create(*context, "for.end", function);

        // Save current named values to restore later
        auto oldNamedValues = namedValues;

        // Handle different range types
        if (std::holds_alternative<Expression>(forStmt.range)) {
            // Case 1: Loop over array
            generateArrayForLoop(forStmt, condBlock, bodyBlock, endBlock, function);
        } else {
            // Case 2: Loop over integer range
            generateRangeForLoop(forStmt, condBlock, bodyBlock, endBlock, function);
        }

        // Restore named values
        namedValues = std::move(oldNamedValues);

        // Continue from end block
        builder->SetInsertPoint(endBlock);
    }

    void generateArrayForLoop(const ForStatement& forStmt,
                              BasicBlock* condBlock,
                              BasicBlock* bodyBlock,
                              BasicBlock* endBlock,
                              Function* function) {
        const auto& arrayExpr = std::get<Expression>(forStmt.range);

        // Generate code for the array expression - but we need the ARRAY POINTER, not the value
        Value* arrayPtr = nullptr;

        // Check if this is a modifiable primary (variable reference)
        if (const auto* rel = std::get_if<Relation>(&arrayExpr.first)) {
            if (const auto* mp = std::get_if<ModifiablePrimary>(&rel->first.first.first)) {
                arrayPtr = generateModifiablePrimaryAddress(*mp); // Get the pointer, not the value
            } else {
                // For other expressions, we need to create a temporary allocation
                Value* arrayValue = generateExpression(arrayExpr);
                IRBuilder<> allocaBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
                AllocaInst* tempAlloca = allocaBuilder.CreateAlloca(
                    getLLVMType(symbolTable.getTypeInfo(arrayExpr.type)), nullptr, "temp_array");
                builder->CreateStore(arrayValue, tempAlloca);
                arrayPtr = tempAlloca;
            }
        } else {
            std::unreachable();
        }

        // Get array type information
        TypeId arrayTypeId = arrayExpr.type;
        const TypeInfo& arrayTypeInfo = symbolTable.getTypeInfo(arrayTypeId);

        if (!std::holds_alternative<ArrayTypeInfo>(arrayTypeInfo.definition)) {
            throw CompileError{"For loop range must be an array", forStmt.variable_name.span};
        }

        const auto& arrayInfo = std::get<ArrayTypeInfo>(arrayTypeInfo.definition);
        llvm::Type* elementType = getLLVMType(symbolTable.getTypeInfo(arrayInfo.element_type));

        // Create index variable
        IRBuilder<> allocaBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
        AllocaInst* indexAlloca = allocaBuilder.CreateAlloca(builder->getInt32Ty(), nullptr, "index");

        // Initialize index
        if (forStmt.is_reversed) {
            // Start from last element (size - 1)
            builder->CreateStore(ConstantInt::get(builder->getInt32Ty(), arrayInfo.size - 1), indexAlloca);
        } else {
            // Start from first element (0)
            builder->CreateStore(ConstantInt::get(builder->getInt32Ty(), 0), indexAlloca);
        }

        // Create loop variable
        AllocaInst* loopVarAlloca = allocaBuilder.CreateAlloca(elementType, nullptr, forStmt.variable_name.text);
        namedValues[forStmt.variable_name.text] = loopVarAlloca;

        // Jump to condition block
        builder->CreateBr(condBlock);

        // Condition block
        builder->SetInsertPoint(condBlock);
        Value* index = builder->CreateLoad(builder->getInt32Ty(), indexAlloca, "index");

        Value* condValue = nullptr;
        if (forStmt.is_reversed) {
            // Check if index >= 0
            condValue = builder->CreateICmpSGE(index, ConstantInt::get(builder->getInt32Ty(), 0), "loopcond");
        } else {
            // Check if index < array size
            condValue =
                builder->CreateICmpSLT(index, ConstantInt::get(builder->getInt32Ty(), arrayInfo.size), "loopcond");
        }

        builder->CreateCondBr(condValue, bodyBlock, endBlock);

        // Body block
        builder->SetInsertPoint(bodyBlock);

        // Load current array element using the ARRAY POINTER
        std::vector<Value*> indices = {ConstantInt::get(builder->getInt32Ty(), 0), index};
        Value* elementPtr = builder->CreateGEP(getLLVMType(arrayTypeInfo), arrayPtr, indices, "elementptr");
        Value* element = builder->CreateLoad(elementType, elementPtr, "element");

        // Store element in loop variable
        builder->CreateStore(element, loopVarAlloca);

        // Generate loop body
        generateBlock(forStmt.body);

        // Update index
        if (forStmt.is_reversed) {
            // Decrement index
            Value* nextIndex = builder->CreateSub(index, ConstantInt::get(builder->getInt32Ty(), 1), "nextindex");
            builder->CreateStore(nextIndex, indexAlloca);
        } else {
            // Increment index
            Value* nextIndex = builder->CreateAdd(index, ConstantInt::get(builder->getInt32Ty(), 1), "nextindex");
            builder->CreateStore(nextIndex, indexAlloca);
        }

        // Jump back to condition
        builder->CreateBr(condBlock);
    }

    void generateRangeForLoop(const ForStatement& forStmt,
                              BasicBlock* condBlock,
                              BasicBlock* bodyBlock,
                              BasicBlock* endBlock,
                              Function* function) {
        const auto& range = std::get<std::pair<Expression, Expression>>(forStmt.range);
        const auto& startExpr = range.first;
        const auto& endExpr = range.second;

        // Generate code for start and end expressions
        Value* startValue = generateExpression(startExpr);
        Value* endValue = generateExpression(endExpr);

        // Convert to integers if needed
        if (startValue->getType()->isFloatingPointTy()) {
            startValue = builder->CreateFPToSI(startValue, builder->getInt32Ty(), "startconv");
        }
        if (endValue->getType()->isFloatingPointTy()) {
            endValue = builder->CreateFPToSI(endValue, builder->getInt32Ty(), "endconv");
        }

        // Create loop variable
        IRBuilder<> allocaBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
        AllocaInst* loopVarAlloca =
            allocaBuilder.CreateAlloca(builder->getInt32Ty(), nullptr, forStmt.variable_name.text);
        namedValues[forStmt.variable_name.text] = loopVarAlloca;

        // Initialize loop variable
        if (forStmt.is_reversed) {
            builder->CreateStore(endValue, loopVarAlloca);
        } else {
            builder->CreateStore(startValue, loopVarAlloca);
        }

        // Jump to condition block
        builder->CreateBr(condBlock);

        // Condition block
        builder->SetInsertPoint(condBlock);
        Value* loopVar = builder->CreateLoad(builder->getInt32Ty(), loopVarAlloca, "loopvar");

        Value* condValue = nullptr;
        if (forStmt.is_reversed) {
            // Check if loopVar >= start
            condValue = builder->CreateICmpSGE(loopVar, startValue, "loopcond");
        } else {
            // Check if loopVar <= end
            condValue = builder->CreateICmpSLE(loopVar, endValue, "loopcond");
        }

        builder->CreateCondBr(condValue, bodyBlock, endBlock);

        // Body block
        builder->SetInsertPoint(bodyBlock);

        // Generate loop body
        generateBlock(forStmt.body);

        // Update loop variable
        if (forStmt.is_reversed) {
            // Decrement loop variable
            Value* nextValue = builder->CreateSub(loopVar, ConstantInt::get(builder->getInt32Ty(), 1), "nextval");
            builder->CreateStore(nextValue, loopVarAlloca);
        } else {
            // Increment loop variable
            Value* nextValue = builder->CreateAdd(loopVar, ConstantInt::get(builder->getInt32Ty(), 1), "nextval");
            builder->CreateStore(nextValue, loopVarAlloca);
        }

        // Jump back to condition
        builder->CreateBr(condBlock);
    }

    void generateIfStatement(const IfStatement& ifStmt) {
        Value* condValue = generateExpression(ifStmt.condition);

        Function* function = builder->GetInsertBlock()->getParent();
        BasicBlock* thenBlock = BasicBlock::Create(*context, "if.then", function);
        BasicBlock* elseBlock = BasicBlock::Create(*context, "if.else", function);
        BasicBlock* mergeBlock = BasicBlock::Create(*context, "if.merge", function);

        builder->CreateCondBr(condValue, thenBlock, elseBlock);

        // Then block
        builder->SetInsertPoint(thenBlock);
        generateBlock(ifStmt.true_branch);
        builder->CreateBr(mergeBlock);

        // Else block
        builder->SetInsertPoint(elseBlock);
        if (ifStmt.false_branch) {
            generateBlock(*ifStmt.false_branch);
        }
        builder->CreateBr(mergeBlock);

        // Merge block
        builder->SetInsertPoint(mergeBlock);
    }

    void generatePrintStatement(const PrintStatement& printStmt) {
        for (const auto& arg : printStmt.arguments) {
            if (std::holds_alternative<parser::StringLiteral>(arg)) {
                const auto& str = std::get<parser::StringLiteral>(arg);
                Value* formatStr = builder->CreateGlobalStringPtr(str.value);
                builder->CreateCall(printfFunc, {formatStr});
            } else {
                const auto& expr = std::get<Expression>(arg);
                Value* value = generateExpression(expr);

                // Create format string based on type
                Value* formatStr = nullptr;
                if (value->getType()->isIntegerTy(32)) {
                    formatStr = builder->CreateGlobalStringPtr("%d\n");
                } else if (value->getType()->isDoubleTy()) {
                    formatStr = builder->CreateGlobalStringPtr("%f\n");
                } else if (value->getType()->isIntegerTy(1)) {
                    formatStr = builder->CreateGlobalStringPtr("%s\n");
                    // Convert boolean to string
                    Value* trueStr = builder->CreateGlobalStringPtr("true");
                    Value* falseStr = builder->CreateGlobalStringPtr("false");
                    value = builder->CreateSelect(value, trueStr, falseStr);
                }

                if (formatStr) {
                    builder->CreateCall(printfFunc, {formatStr, value});
                }
            }
        }
    }

    void generateReturnStatement(const ReturnStatement& retStmt) {
        if (retStmt.value) {
            Value* retValue = generateExpression(*retStmt.value);
            builder->CreateRet(retValue);
        } else {
            builder->CreateRetVoid();
        }
    }

    void generateBlock(const Block& block) {
        for (const auto& element : block) {
            std::visit(overloaded{[this](const VariableDeclaration& var) { generateLocalVariableDeclaration(var); },
                                  [this](const TypeDeclaration& type) { generateTypeDeclaration(type); },
                                  [this](const Statement& stmt) { generateStatement(stmt); }},
                       element);
        }
    }

    Function* generateRoutineDeclaration(const parser::RoutineDeclaration& declaration) {
        llvm::Type* returnType = declaration.return_type
                                     ? getLLVMType(symbolTable.getTypeInfo(declaration.return_type->resolved))
                                     : builder->getVoidTy();
        std::vector<llvm::Type*> paramTypes;
        paramTypes.reserve(declaration.parameters.size());
        for (const auto& param : declaration.parameters) {
            paramTypes.push_back(getLLVMType(symbolTable.getTypeInfo(param.resolved_type)));
        }
        FunctionType* functionType = FunctionType::get(returnType, paramTypes, false);
        Function* function =
            Function::Create(functionType, Function::ExternalLinkage, declaration.name.text, module.get());

        // Set parameter names
        unsigned idx = 0;
        for (auto& arg : function->args()) {
            arg.setName(declaration.parameters[idx].name.text);
            ++idx;
        }

        return function;
    }

    void generateRoutineBody(const RoutineDeclaration& declaration, Function* function) {
        // Create entry block
        BasicBlock* block = BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(block);

        // Clear named values for this function scope
        auto oldNamedValues = std::move(namedValues);
        namedValues.clear();

        // Allocate and store parameters
        for (auto& arg : function->args()) {
            AllocaInst* alloca = builder->CreateAlloca(arg.getType(), nullptr, arg.getName());
            builder->CreateStore(&arg, alloca);
            namedValues[std::string(arg.getName())] = alloca;
        }

        // Generate function body
        if (declaration.body) {
            if (std::holds_alternative<Block>(*declaration.body)) {
                generateBlock(std::get<Block>(*declaration.body));

                // Если функция должна возвращать значение, но нет return statement
                if (!function->getReturnType()->isVoidTy()) {
                    // Добавляем возврат значения по умолчанию
                    if (function->getReturnType()->isIntegerTy(32)) {
                        builder->CreateRet(ConstantInt::get(builder->getInt32Ty(), 0));
                    } else if (function->getReturnType()->isDoubleTy()) {
                        builder->CreateRet(ConstantFP::get(builder->getDoubleTy(), 0.0));
                    } else if (function->getReturnType()->isIntegerTy(1)) {
                        builder->CreateRet(ConstantInt::get(builder->getInt1Ty(), 0));
                    } else {
                        builder->CreateRetVoid();
                    }
                }
            } else {
                // Handle expression body (arrow functions)
                Value* result = generateExpression(std::get<Expression>(*declaration.body));
                builder->CreateRet(result);
            }
        } else {
            // Для функций без тела - возврат по умолчанию
            if (!function->getReturnType()->isVoidTy()) {
                builder->CreateRet(Constant::getNullValue(function->getReturnType()));
            } else {
                builder->CreateRetVoid();
            }
        }

        // Verify function
        std::string verification_error;
        llvm::raw_string_ostream error_stream(verification_error);
        if (verifyFunction(*function, &error_stream)) {
            throw CompileError{"Function verification failed: " + verification_error, declaration.name.span};
        }

        // Restore named values
        namedValues = std::move(oldNamedValues);
    }

    void generateCode() {
        // First pass: generate global variables and function declarations
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

        // Second pass: generate function bodies
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
    explicit Compiler(const Program& ast, const SymbolTable& symbolTable) : symbolTable{symbolTable}, program{ast} {}

    void saveLLVMIRToFile(llvm::Module& module, const std::string& filename) {
        std::error_code ec;
        llvm::raw_fd_ostream out(filename, ec);
        if (ec) {
            std::cerr << std::format("Failed to open {} for writing: {}", filename, ec.message());
            return;
        }
        module.print(out, nullptr);
        out.close();
        std::cerr << std::format("LLVM IR saved to {}", filename);
    }

    std::expected<std::unique_ptr<llvm::Module>, CompileError> compile() {
        try {
            generateCode();
            saveLLVMIRToFile(*module, "output.ll");
            return nullptr;
            // return std::move(module);
        } catch (const CompileError& error) {
            return std::unexpected{error};
        }
    }
};
// NOLINTEND(*recursion*)

std::expected<std::unique_ptr<llvm::Module>, CompileError> compile(const Program& ast, const SymbolTable& symbolTable) {
    Compiler compiler{ast, symbolTable};
    return compiler.compile();
}

} // namespace compiler
