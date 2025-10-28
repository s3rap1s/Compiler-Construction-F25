#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"

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
            *out << "  ";
        }
    }

    void newline() {
        *out << "\n";
    }

public:
    explicit TreePrinter(std::ostream* output_stream = &std::cout) : out(output_stream) {}

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
            print(*decl.type);
        }
        if (decl.value) {
            *out << " = ";
            print(*decl.value);
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
        *out << "Record";
        if (!record.fields.empty()) {
            *out << " {";
            for (const auto & field : record.fields) {
                print(*field);
                *out << ", ";
            }
            *out << "}";
        }
    }

    void print(const parser::ArrayType& array) {
        *out << "Array";
        if (array.size) {
            *out << "[";
            print(*array.size);
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
        *out << "Expression: ";
        print(expr.first);
        for (const auto& [op, bool_expr] : expr.rest) {
            switch (op) {
                case parser::Expression::Operation::And: *out << " AND "; break;
                case parser::Expression::Operation::Or: *out << " OR "; break;
                case parser::Expression::Operation::Xor: *out << " XOR "; break;
            }
            print(bool_expr);
                newline();
        }
    }

    void print(const parser::BooleanExpression& bool_expr) {
        std::visit([this](const auto& be) { this->print(be); }, bool_expr);
    }

    void print(const parser::Relation& relation) {
        *out << "Relation: ";
        print(relation.first);
        if (relation.second) {
            switch (relation.second->first) {
                case parser::Relation::Operation::Less: *out << " < "; break;
                case parser::Relation::Operation::LessOrEqual: *out << " <= "; break;
                case parser::Relation::Operation::Greater: *out << " > "; break;
                case parser::Relation::Operation::GreaterOrEqual: *out << " >= "; break;
                case parser::Relation::Operation::Equal: *out << " = "; break;
                case parser::Relation::Operation::NotEqual: *out << " /= "; break;
            }
            print(relation.second->second);
        }
    }

    void print(const parser::NotExpression& not_expr) {
        *out << "NOT";
        print(not_expr.operand);
    }

    void print(const parser::NumberExpression& num_expr) {
        *out << "NumberExpression: ";
        print(num_expr.first);
        for (const auto& [op, summand] : num_expr.rest) {
            switch (op) {
                case parser::NumberExpression::Operation::Plus: *out << " + "; break;
                case parser::NumberExpression::Operation::Minus: *out << " - "; break;
            }
            print(summand);
        }
    }
    
    void print(const parser::Summand& summand) {
        *out << "Summand: ";
        print(summand.first);
        
        for (const auto& [op, primary] : summand.rest) {
            switch (op) {
                case parser::Summand::Operation::Multiply: *out << " * "; break;
                case parser::Summand::Operation::Divide: *out << " / "; break;
                case parser::Summand::Operation::Modulo: *out << " % "; break;
            }
            print(primary);
        }
    }

    void print(const parser::Primary& primary) {
        std::visit([this](const auto& p) { this->print(p); }, primary);
    }

    void print(const parser::IntegerLiteral& lit) {
        *out << "IntegerLiteral(" << lit.value << ")";
    }

    void print(const parser::RealLiteral& lit) {
        *out << "RealLiteral(" << lit.value << ")";
    }

    void print(const parser::BooleanLiteral& lit) {
        *out << "BooleanLiteral(" << (lit.value ? "true" : "false") << ")";
    }

    void print(const parser::RoutineCall& call) {
        *out << "RoutineCall: " << call.name << "(";
        for (size_t i = 0; i < call.arguments.size(); ++i) {
            if (i > 0) *out << ", ";
            print(call.arguments[i]);
        }
        *out << ")";
    }

    void print(const parser::ModifiablePrimary& mp) {
        *out << "Identifier: " << mp.variable;
        for (const auto& accessor : mp.accessors) {
            std::visit([this](const auto& acc) {
                using T = std::decay_t<decltype(acc)>;
                if constexpr (std::is_same_v<T, parser::Expression>) {
                    *out << "[";
                    print(acc);
                    *out << "]";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    *out << "." << acc;
                }
            }, accessor);
        }
    }

    void print(const parser::UnarySign& unary) {
        switch (unary.sign) {
            case parser::UnarySign::Sign::Plus: *out << " + "; break;
            case parser::UnarySign::Sign::Minus: *out << " - "; break;
        }
        print(*unary.operand);
    }

    void print(const std::shared_ptr<parser::Expression>& expr_ptr) {
        *out << "(";
        print(*expr_ptr);
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
        print(assign.target);
        *out << " = ";
        print(assign.expression);
    }

    void print(const parser::IfStatement& if_stmt) {
        *out << "If: ";
        print(if_stmt.condition);
        newline();
        indent_level++;
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
        indent_level--;
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
        
        std::visit([this](const auto& r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, parser::Expression>) {
                print(r);
            } else if constexpr (std::is_same_v<T, std::pair<parser::Expression, parser::Expression>>) {
                print(r.first);
                *out << " .. ";
                print(r.second);
            }
        }, for_stmt.range);
        
        if (for_stmt.is_reversed) {
            *out << " REVERSE";
        }
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
        for (size_t i = 0; i < print_stmt.arguments.size(); ++i) {
            if (i > 0) *out << ", ";
            std::visit([this](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, parser::Expression>) {
                    print(a);
                } else if constexpr (std::is_same_v<T, parser::StringLiteral>) {
                    *out << "\"" << a.value << "\"";
                }
            }, print_stmt.arguments[i]);
        }
    }

    void print(const parser::StringLiteral& str_lit) {
        *out << "StringLiteral(\"" << str_lit.value << "\")";
    }
};

} // namespace

namespace parser {

void print_tree(const Program& program, std::ostream& out) {
    TreePrinter printer(&out);
    printer.print(program);
}

} // namespace parser