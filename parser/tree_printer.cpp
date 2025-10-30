#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

class TreePrinter {
    std::ostream* out;
    int indent_level = 0;

    void print_indent() {
        for (int i = 0; i < indent_level; ++i) {
            *out << "   ";
        }
    }

    void newline() {
        *out << "\n";
    }

public:
    explicit TreePrinter(std::ostream* output_stream) : out(output_stream) {}

    // Program
    void print(const parser::Program& program) {
        for (const auto& decl : program.declarations) {
            std::visit([this](const auto& d) { this->print(d); }, decl);
        }
    }

    // Declarations
    void print(const parser::VariableDeclaration& decl) {
        print_indent();
        *out << "VariableDeclaration: " << decl.identifier;
        if (decl.type) {
            *out << " : ";
            indent_level++;
            newline();
            print(*decl.type);
            indent_level--;
        }
        if (decl.value) {
            *out << " = ";
            indent_level++;
            newline();
            print(*decl.value);
            indent_level--;
        }
    }

    void print(const parser::TypeDeclaration& decl) {
        print_indent();
        *out << "TypeDeclaration: " << decl.identifier << " = ";
        print(decl.type);
        newline();
    }

    void print(const parser::RoutineDeclaration& decl) {
        print_indent();
        *out << "RoutineDeclaration: " << decl.identifier;
        
        if (!decl.parameters.empty()) {
            *out << "(";
            for (size_t i = 0; i < decl.parameters.size(); ++i) {
                if (i > 0) *out << ", ";
                print(decl.parameters[i].type);
                *out << ": "<< decl.parameters[i].identifier;
            }
            *out << ")";
        }
        
        if (decl.return_type) {
            *out << " : ";
            print(*decl.return_type);
        }
        
        if (decl.body) {
            *out << ", Body: ";
            newline();
            indent_level++;
            std::visit([this](const auto& b) { this->print(b); }, *decl.body);
            indent_level--;
        }
    }

    // Types
    void print(const parser::Type& type) {
        std::visit([this](const auto& t) { this->print(t); }, type);
    }

    void print(const parser::IntegerType& /*unused*/) {
        *out << "Integer";
    }

    void print(const parser::RealType& /*unused*/) {
        *out << "Real";
    }

    void print(const parser::BoolType& /*unused*/) {
        *out << "Boolean";
    }

    void print(const parser::RecordType& record) {
        print_indent();
        *out << "Record";
        indent_level++;
        if (!record.fields.empty()) {
            *out << " {";
            newline();
            for (size_t i = 0; i < record.fields.size(); ++i) {
                const auto& field = record.fields[i];
                print(*field);
                if (i < record.fields.size() - 1){
                    *out << ", ";
                }
                newline();
            }
            print_indent();
            *out << "}";
        }
        newline();
        indent_level--;
    }

    void print(const parser::ArrayType& array) {
        print_indent();
        *out << "Array";
        if (array.size) {
            *out << "[";
            indent_level++;
            newline();
            print(*array.size);
            indent_level--;
            newline();
            print_indent();
            *out << "]";
        }
        *out << " of ";
        print(*array.element_type);
    }

    void print(const std::string& type_name) {
        *out << type_name;
    }

    // Expressions
    void print(const parser::Expression& expr) {
        print_indent();
        *out << "Expression: ";
        newline();
        indent_level++;
        print(expr.first);
        for (const auto& [op, bool_expr] : expr.rest) {
            newline();
            print_indent();
            *out << "Operation: ";
            switch (op) {
                case parser::Expression::Operation::And: *out << "AND "; break;
                case parser::Expression::Operation::Or: *out << "OR "; break;
                case parser::Expression::Operation::Xor: *out << "XOR "; break;
            }
            newline();
            print(bool_expr);
        }
        indent_level--;
    }

    void print(const parser::BooleanExpression& bool_expr) {
        std::visit([this](const auto& be) { this->print(be); }, bool_expr);
    }

    void print(const parser::Relation& relation) {
        print_indent();
        *out << "Relation: ";
        newline();
        indent_level++;
        print(relation.first);
        if (relation.second) {
            newline();
            print_indent();
            *out << "Operation: ";
            switch (relation.second->first) {
                case parser::Relation::Operation::Less: *out << "< "; break;
                case parser::Relation::Operation::LessOrEqual: *out << "<= "; break;
                case parser::Relation::Operation::Greater: *out << "> "; break;
                case parser::Relation::Operation::GreaterOrEqual: *out << ">= "; break;
                case parser::Relation::Operation::Equal: *out << "= "; break;
                case parser::Relation::Operation::NotEqual: *out << "/= "; break;
            }
            newline();
            print(relation.second->second);
        }
        indent_level--;
    }

    void print(const parser::NotExpression& not_expr) {
        print_indent();
        *out << "NOT";
        indent_level++;
        newline();
        print(not_expr.operand);
        indent_level--;
    }

    void print(const parser::NumberExpression& num_expr) {
        print_indent();
        *out << "NumberExpression: ";
        newline();
        indent_level++;
        print(num_expr.first);
        for (const auto& [op, summand] : num_expr.rest) {
            newline();
            print_indent();
            *out << "Operation: ";
            switch (op) {
                case parser::NumberExpression::Operation::Plus: *out << "+ "; break;
                case parser::NumberExpression::Operation::Minus: *out << "- "; break;
            }
            newline();
            print(summand);
        }
        indent_level--;
    }
    
    void print(const parser::Summand& summand) {
        print_indent();
        *out << "Summand: ";
        newline();
        indent_level++;
        print(summand.first);
        
        for (const auto& [op, primary] : summand.rest) {
            newline();
            print_indent();
            *out << "Operation: ";
            switch (op) {
                case parser::Summand::Operation::Multiply: *out << " * "; break;
                case parser::Summand::Operation::Divide: *out << " / "; break;
                case parser::Summand::Operation::Modulo: *out << " % "; break;
            }
            newline();
            print(primary);
        }
        indent_level--;
    }

    void print(const parser::Primary& primary) {
        std::visit([this](const auto& p) { this->print(p); }, primary);
    }

    void print(const parser::IntegerLiteral& lit) {
        print_indent();
        *out << "IntegerLiteral(" << lit.value << ")";
    }

    void print(const parser::RealLiteral& lit) {
        print_indent();
        *out << "RealLiteral(" << lit.value << ")";
    }

    void print(const parser::BooleanLiteral& lit) {
        print_indent();
        *out << "BooleanLiteral(" << (lit.value ? "true" : "false") << ")";
    }

    void print(const parser::RoutineCall& call) {
        print_indent();
        *out << "RoutineCall: " << call.name << "(";
        for (size_t i = 0; i < call.arguments.size(); ++i) {
            if (i > 0) *out << ", ";
            newline();
            indent_level++;
            print(call.arguments[i]);
            indent_level--;
        }
        *out << ")";
    }

    void print(const parser::ModifiablePrimary& mp) {
        print_indent();
        *out << "Identifier: " << mp.variable;
        for (const auto& accessor : mp.accessors) {
            std::visit([this](const auto& acc) {
                using T = std::decay_t<decltype(acc)>;
                if constexpr (std::is_same_v<T, parser::Expression>) {
                    *out << "[";
                    indent_level++;
                    newline();
                    print(acc);
                    indent_level--;
                    newline();
                    print_indent();
                    *out << "]";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    *out << "." << acc;
                }
            }, accessor);
        }
    }

    void print(const parser::UnarySign& unary) {
        newline();
        print_indent();
        *out << "Operation: ";
        switch (unary.sign) {
            case parser::UnarySign::Sign::Plus: *out << "+ "; break;
            case parser::UnarySign::Sign::Minus: *out << "- "; break;
        }
        newline();
        print(*unary.operand);
    }

    void print(const std::shared_ptr<parser::Expression>& expr_ptr) {
        print_indent();
        *out << "(";
        newline();
        print(*expr_ptr);
        newline();
        print_indent();
        *out << ")";
    }

    // Statements and Block
    void print(const parser::Block& block) {
        for (const auto& element : block) {
            std::visit([this](const auto& elem) { this->print(elem); }, element);
            newline();
        }
    }

    void print(const parser::Statement& stmt) {
        std::visit([this](const auto& s) { this->print(s); }, stmt);
    }

    void print(const parser::AssignmentStatement& assign) {
        print_indent();
        *out << "Assignment: ";
        indent_level++;
        newline();
        print_indent();
        *out << "To:";
        newline();
        indent_level++;
        print(assign.target);
        indent_level--;
        newline();
        print_indent();
        *out << "Value:";
        newline();
        indent_level++;
        print(assign.expression);
        indent_level--;
        indent_level--;
    }

    void print(const parser::IfStatement& if_stmt) {
        print_indent();
        *out << "If: ";
        indent_level++;
        newline();
        print(if_stmt.condition);
        indent_level--;
        newline();
        print_indent();
        *out << "Then:";
        newline();
        indent_level++;
        print(if_stmt.true_branch);
        indent_level--;
        
        if (if_stmt.false_branch) {
            print_indent();
            *out << "Else:";
            newline();
            indent_level++;
            print(*if_stmt.false_branch);
            indent_level--;
        }
        newline();
    }

    void print(const parser::WhileStatement& while_stmt) {
        print_indent();
        *out << "While: ";
        print(while_stmt.condition);
        newline();
        
        indent_level++;
        print_indent();
        *out << "Body:";
        newline();
        indent_level++;
        print(while_stmt.body);
        indent_level--;
    }

    void print(const parser::ForStatement& for_stmt) {
        print_indent();
        *out << "For: " << for_stmt.counter << " in ";
        newline();
        indent_level++;
        std::visit([this](const auto& r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, parser::Expression>) {
                print(r);
            } else if constexpr (std::is_same_v<T, std::pair<parser::Expression, parser::Expression>>) {
                print(r.first);
                newline();
                print_indent();
                *out << ".. ";
                newline();
                print(r.second);
            }
        }, for_stmt.range);
        
        if (for_stmt.is_reversed) {
            newline();
            print_indent();
            *out << "REVERSE";
        }
        indent_level--;
        newline();
        
        indent_level++;
        print_indent();
        *out << "Body:";
        newline();
        indent_level++;
        print(for_stmt.body);
        indent_level--;
    }

    void print(const parser::PrintStatement& print_stmt) {
        print_indent();
        *out << "Print: ";
        newline();
        indent_level++;
        for (size_t i = 0; i < print_stmt.arguments.size(); ++i) {
            if (i > 0) *out << ", \n";
            std::visit([this](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, parser::Expression>) {
                    print(a);
                } else if constexpr (std::is_same_v<T, parser::StringLiteral>) {
                    print_indent();
                    *out << "\"" << a.value << "\"";
                }
            }, print_stmt.arguments[i]);
        }
        indent_level--;
    }

    void print(const parser::ReturnStatement& return_stmt) {
        print_indent();
        *out << "Return";
        if (return_stmt.value) {
            *out << ": ";
            newline();
            indent_level++;
            print(*return_stmt.value);
            indent_level--;
        }
    }

    void print(const parser::StringLiteral& str_lit) {
        *out << "StringLiteral(\"" << str_lit.value << "\")";
    }
};

} // namespace

namespace parser {

void print_tree(const Program& program, std::ostream* out = &std::cout) { // NOLINT
    TreePrinter printer(out);
    printer.print(program);
}

} // namespace parser