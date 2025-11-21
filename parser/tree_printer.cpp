#include "tree_printer.hpp"

#include "parser/ast.hpp"
#include "utils.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace {

class TreePrinter {
    std::ostream& out; // NOLINT(*ref-data*)
    int indent_level = 0;
    bool compact_mode = true;

    void print_indent() {
        for (int i = 0; i < indent_level; ++i) {
            out << "    ";
        }
    }

    void newline() {
        out << '\n';
    }

  public:
    explicit TreePrinter(std::ostream& output_stream = std::cout) : out{output_stream} {}

    // Program
    void print(const parser::Program& program) {
        for (const auto& decl : program.declarations) {
            std::visit([this](const auto& d) { this->printDeduced(d); }, decl);
        }
    }

    // Declarations
    void print(const parser::VariableDeclaration& decl) {
        print_indent();
        out << "VariableDeclaration: " << decl.name.text;
        if (decl.type) {
            out << " : ";
            printDeduced(*decl.type);
        }
        if (decl.value) {
            out << " = ";
            indent_level++;
            newline();
            printDeduced(*decl.value);
            indent_level--;
        }
    }

    void print(const parser::TypeDeclaration& decl) {
        print_indent();
        out << "TypeDeclaration: " << decl.name.text << " = ";
        printDeduced(decl.type);
        newline();
    }

    void print(const parser::RoutineDeclaration& decl) {
        print_indent();
        out << "RoutineDeclaration: " << decl.name.text;

        if (!decl.parameters.empty()) {
            out << "(";
            for (size_t i = 0; i < decl.parameters.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << decl.parameters[i].name.text << ": ";
                printDeduced(decl.parameters[i].type);
            }
            out << ")";
        }

        if (decl.return_type) {
            out << " : ";
            printDeduced(*decl.return_type);
        }

        if (decl.body) {
            newline();
            print_indent();
            out << "Body: ";
            newline();
            indent_level++;
            std::visit([this](const auto& b) { this->printDeduced(b); }, *decl.body);
            indent_level--;
        }
        newline();
    }

    // Types
    void print(const parser::Type& type) {
        std::visit([this](const auto& t) { this->printDeduced(t); }, type);
    }

    void print(const parser::IntegerType& /*unused*/) {
        out << "Integer";
    }

    void print(const parser::RealType& /*unused*/) {
        out << "Real";
    }

    void print(const parser::BoolType& /*unused*/) {
        out << "Boolean";
    }

    void print(const parser::RecordType& record) {
        print_indent();
        out << "Record";
        indent_level++;
        if (!record.fields.empty()) {
            newline();
            for (std::size_t i = 0; i < record.fields.size(); ++i) {
                const parser::VariableDeclaration& field = record.fields[i];
                printDeduced(field);
                if (i < record.fields.size() - 1) {
                    out << ", ";
                    newline();
                }
            }
        }
        newline();
        indent_level--;
    }

    void print(const parser::ArrayType& array) {
        print_indent();
        out << "Array";
        if (array.size) {
            out << "[";
            indent_level++;
            newline();
            printDeduced(*array.size);
            indent_level--;
            newline();
            print_indent();
            out << "]";
        }
        out << " of ";
        printDeduced(*array.element_type);
        newline();
    }

    void print(const parser::Identifier& id) {
        out << id.text;
    }

    // Expressions
    void print(const parser::Expression& expr) {
        if (expr.rest.empty() && compact_mode) {
            printDeduced(expr.first);
            return;
        }
        print_indent();
        out << "Expression: ";
        newline();
        indent_level++;
        printDeduced(expr.first);
        for (const auto& [op, bool_expr] : expr.rest) {
            newline();
            print_indent();
            out << "Operation: ";
            switch (op) {
            case parser::Expression::Operator::And:
                out << "AND ";
                break;
            case parser::Expression::Operator::Or:
                out << "OR ";
                break;
            case parser::Expression::Operator::Xor:
                out << "XOR ";
                break;
            }
            newline();
            printDeduced(bool_expr);
        }
        indent_level--;
    }

    void print(const parser::BooleanExpression& bool_expr) {
        std::visit([this](const auto& be) { this->printDeduced(be); }, bool_expr);
    }

    void print(const parser::Relation& relation) {
        if (!relation.second && compact_mode) {
            printDeduced(relation.first);
            return;
        }
        print_indent();
        out << "Relation: ";
        newline();
        indent_level++;
        printDeduced(relation.first);
        if (relation.second) {
            newline();
            print_indent();
            out << "Operation: ";
            switch (relation.second->operator_) {
            case parser::Relation::Operator::Less:
                out << "< ";
                break;
            case parser::Relation::Operator::LessOrEqual:
                out << "<= ";
                break;
            case parser::Relation::Operator::Greater:
                out << "> ";
                break;
            case parser::Relation::Operator::GreaterOrEqual:
                out << ">= ";
                break;
            case parser::Relation::Operator::Equal:
                out << "= ";
                break;
            case parser::Relation::Operator::NotEqual:
                out << "/= ";
                break;
            }
            newline();
            printDeduced(relation.second->next_operand);
        }
        indent_level--;
    }

    void print(const parser::NotExpression& not_expr) {
        print_indent();
        out << "NOT";
        indent_level++;
        newline();
        printDeduced(not_expr.operand);
        indent_level--;
    }

    void print(const parser::NumberExpression& num_expr) {
        if (num_expr.rest.empty() && compact_mode) {
            printDeduced(num_expr.first);
            return;
        }
        print_indent();
        out << "NumberExpression: ";
        newline();
        indent_level++;
        printDeduced(num_expr.first);
        for (const auto& [op, summand, _] : num_expr.rest) {
            newline();
            print_indent();
            out << "Operation: ";
            switch (op) {
            case parser::NumberExpression::Operator::Plus:
                out << "+ ";
                break;
            case parser::NumberExpression::Operator::Minus:
                out << "- ";
                break;
            }
            newline();
            printDeduced(summand);
        }
        indent_level--;
    }

    void print(const parser::Summand& summand) {
        if (summand.rest.empty() && compact_mode) {
            printDeduced(summand.first);
            return;
        }
        print_indent();
        out << "Summand: ";
        newline();
        indent_level++;
        printDeduced(summand.first);

        for (const auto& [op, primary, _] : summand.rest) {
            newline();
            print_indent();
            out << "Operation: ";
            switch (op) {
            case parser::Summand::Operator::Multiply:
                out << " * ";
                break;
            case parser::Summand::Operator::Divide:
                out << " / ";
                break;
            case parser::Summand::Operator::Modulo:
                out << " % ";
                break;
            }
            newline();
            printDeduced(primary);
        }
        indent_level--;
    }

    void print(const parser::Primary& primary) {
        std::visit([this](const auto& p) { this->printDeduced(p); }, primary);
    }

    void print(const parser::IntegerLiteral& lit) {
        print_indent();
        out << "IntegerLiteral(" << lit.value << ")";
    }

    void print(const parser::RealLiteral& lit) {
        print_indent();
        out << "RealLiteral(" << lit.value << ")";
    }

    void print(const parser::BooleanLiteral& lit) {
        print_indent();
        out << "BooleanLiteral(" << (lit.value ? "true" : "false") << ")";
    }

    void print(const parser::RoutineCall& call) {
        print_indent();
        out << "RoutineCall: " << call.routine_name.text << "(";
        for (size_t i = 0; i < call.arguments.size(); ++i) {
            if (i > 0)
                out << ", ";
            newline();
            indent_level++;
            printDeduced(call.arguments[i]);
            indent_level--;
        }
        out << ")";
    }

    void print(const parser::ModifiablePrimary& mp) {
        if (mp.accessors.empty()) {
            print_indent();
            out << "Identifier: " << mp.variable.text << '\n';
            return;
        }
        print_indent();
        out << "ModifiablePrimary:\n";
        indent_level++;
        print_indent();
        out << "Identifier: " << mp.variable.text << '\n';
        for (const auto& [key, _] : mp.accessors) {
            std::visit(overloaded{
                           [this](const parser::Index& index) {
                               print_indent();
                               out << "Index:\n";
                               indent_level++;
                               printDeduced(index.value);
                               newline();
                               indent_level--;
                           },
                           [this](const parser::Identifier& field_name) {
                               print_indent();
                               out << "Field: " << field_name.text << '\n';
                           },
                       },
                       key);
        }
        indent_level--;
    }

    void print(const parser::UnarySign& unary) {
        newline();
        print_indent();
        out << "Operation: ";
        switch (unary.sign) {
        case parser::UnarySign::Sign::Plus:
            out << "+ ";
            break;
        case parser::UnarySign::Sign::Minus:
            out << "- ";
            break;
        }
        newline();
        printDeduced(*unary.operand);
    }

    void print(const parser::ParenthesizedExpression& expr) {
        print_indent();
        out << "(";
        newline();
        printDeduced(*expr.expression);
        newline();
        print_indent();
        out << ")";
    }

    // Statements and Block
    void print(const parser::Block& block) {
        for (const parser::Statement& element : block) {
            print(element);
            newline();
        }
    }

    void print(const parser::Statement& stmt) {
        std::visit([this](const auto& s) { this->printDeduced(s); }, stmt);
    }

    void print(const parser::AssignmentStatement& assign) {
        print_indent();
        out << "Assignment: ";
        newline();
        indent_level++;
        print_indent();
        out << "To:";
        newline();
        indent_level++;
        printDeduced(assign.target);
        indent_level--;
        newline();
        print_indent();
        out << "Value:";
        newline();
        indent_level++;
        printDeduced(assign.expression);
        indent_level--;
        indent_level--;
    }

    void print(const parser::IfStatement& if_stmt) {
        print_indent();
        out << "If: ";
        indent_level++;
        newline();
        printDeduced(if_stmt.condition);
        indent_level--;
        newline();
        print_indent();
        out << "Then:";
        newline();
        indent_level++;
        printDeduced(if_stmt.true_branch);
        indent_level--;

        if (if_stmt.false_branch) {
            print_indent();
            out << "Else:";
            newline();
            indent_level++;
            printDeduced(*if_stmt.false_branch);
            indent_level--;
        }
        newline();
    }

    void print(const parser::WhileStatement& while_stmt) {
        print_indent();
        out << "While: ";
        printDeduced(while_stmt.condition);
        newline();

        indent_level++;
        print_indent();
        out << "Body:";
        newline();
        indent_level++;
        printDeduced(while_stmt.body);
        indent_level--;
    }

    void print(const parser::ForStatement& for_stmt) {
        print_indent();
        out << "For: " << for_stmt.variable_name.text << " in ";
        if (for_stmt.is_reversed) {
            out << "REVERSED ";
        }
        newline();
        indent_level++;
        std::visit(
            [this](const auto& r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, parser::Expression>) {
                    printDeduced(r);
                } else if constexpr (std::is_same_v<T, std::pair<parser::Expression, parser::Expression>>) {
                    printDeduced(r.first);
                    newline();
                    print_indent();
                    out << ".. ";
                    newline();
                    printDeduced(r.second);
                }
            },
            for_stmt.range);

        indent_level--;
        newline();

        print_indent();
        out << "Body:";
        newline();
        indent_level++;
        printDeduced(for_stmt.body);
        indent_level--;
    }

    void print(const parser::PrintStatement& print_stmt) {
        print_indent();
        out << "Print: ";
        newline();
        indent_level++;
        for (size_t i = 0; i < print_stmt.arguments.size(); ++i) {
            if (i > 0)
                out << ", \n";
            std::visit(
                [this](const auto& a) {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, parser::Expression>) {
                        printDeduced(a);
                    } else if constexpr (std::is_same_v<T, parser::StringLiteral>) {
                        print_indent();
                        out << "\"" << a.value << "\"";
                    }
                },
                print_stmt.arguments[i]);
        }
        indent_level--;
    }

    void print(const parser::ReturnStatement& return_stmt) {
        print_indent();
        out << "Return";
        if (return_stmt.value) {
            out << ": ";
            newline();
            indent_level++;
            print(*return_stmt.value);
            indent_level--;
        }
    }

    void print(const parser::StringLiteral& str_lit) {
        out << "StringLiteral(\"" << str_lit.value << "\")";
    }

  private:
    template <typename Arg>
    using PrintOverload = void (TreePrinter::*)(const Arg&);

    template <typename Arg>
    void printDeduced(const Arg& arg) {
        (this->*static_cast<PrintOverload<Arg>>(&TreePrinter::print))(arg);
    }
};

} // namespace

namespace parser {

void print_tree(const Program& program, std::ostream& out) { // NOLINT
    TreePrinter printer(out);
    printer.print(program);
}

} // namespace parser

