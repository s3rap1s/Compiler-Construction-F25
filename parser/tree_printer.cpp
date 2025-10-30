#include "tree_printer.hpp"

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
    std::ostream& out; // NOLINT(*ref-data*)
    int indent_level = 0;
    bool compact_mode = false;

    void print_indent() {
        if (!compact_mode) {
            for (int i = 0; i < indent_level; ++i) {
                out << "  ";
            }
        }
    }

    void newline() {
        if (!compact_mode) {
            out << "\n";
        }
    }

  public:
    explicit TreePrinter(std::ostream& output_stream = std::cout) : out{output_stream} {}

    // Program
    void print(const parser::Program& program) {
        out << "Program:";
        newline();
        indent_level++;
        for (const auto& decl : program.declarations) {
            std::visit([this](const auto& d) { this->printDeduced(d); }, decl);
        }
        indent_level--;
    }

    // Declarations
    void print(const parser::VariableDeclaration& decl) {
        print_indent();
        out << "VariableDeclaration: " << decl.identifier;
        if (decl.type) {
            out << " : ";
            printDeduced(*decl.type);
        }
        if (decl.value) {
            out << " = ";
            printDeduced(*decl.value);
        }
        newline();
    }

    void print(const parser::TypeDeclaration& decl) {
        print_indent();
        out << "TypeDeclaration: " << decl.identifier << " = ";
        printDeduced(decl.type);
        newline();
    }

    void print(const parser::RoutineDeclaration& decl) {
        print_indent();
        out << "RoutineDeclaration: " << decl.identifier;

        if (!decl.parameters.empty()) {
            out << "(";
            for (size_t i = 0; i < decl.parameters.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << decl.parameters[i].identifier << ": ";
                printDeduced(decl.parameters[i].type);
            }
            out << ")";
        }

        if (decl.return_type) {
            out << " : ";
            printDeduced(*decl.return_type);
        }
        newline();

        if (decl.body) {
            indent_level++;
            print_indent();
            out << "Body:";
            newline();
            indent_level++;
            std::visit([this](const auto& b) { this->printDeduced(b); }, *decl.body);
            indent_level -= 2;
        }
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
        out << "Record";
        if (!record.fields.empty()) {
            bool old_compact = compact_mode;
            compact_mode = true;
            out << " {";
            for (size_t i = 0; i < record.fields.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << record.fields[i].identifier;
                if (record.fields[i].type) {
                    out << ": ";
                    printDeduced(*record.fields[i].type);
                }
            }
            out << "}";
            compact_mode = old_compact;
        }
    }

    void print(const parser::ArrayType& array) {
        out << "Array";
        if (array.size) {
            out << "[";
            bool old_compact = compact_mode;
            compact_mode = true;
            printDeduced(*array.size);
            compact_mode = old_compact;
            out << "]";
        }
        out << " of ";
        printDeduced(*array.element_type);
    }

    void print(const std::string& type_name) {
        out << type_name;
    }

    // Expressions
    void print(const parser::Expression& expr) {
        if (compact_mode) {
            printDeduced(expr.first);
            for (const auto& [op, bool_expr] : expr.rest) {
                switch (op) {
                case parser::Expression::Operation::And:
                    out << " AND ";
                    break;
                case parser::Expression::Operation::Or:
                    out << " OR ";
                    break;
                case parser::Expression::Operation::Xor:
                    out << " XOR ";
                    break;
                }
                printDeduced(bool_expr);
            }
        } else {
            out << "Expression:";
            newline();
            indent_level++;
            print_indent();
            out << "First: ";
            printDeduced(expr.first);
            newline();

            for (const auto& [op, bool_expr] : expr.rest) {
                print_indent();
                out << "Operator: ";
                switch (op) {
                case parser::Expression::Operation::And:
                    out << "AND";
                    break;
                case parser::Expression::Operation::Or:
                    out << "OR";
                    break;
                case parser::Expression::Operation::Xor:
                    out << "XOR";
                    break;
                }
                newline();
                print_indent();
                out << "Next: ";
                printDeduced(bool_expr);
                newline();
            }
            indent_level--;
        }
    }

    void print(const parser::BooleanExpression& bool_expr) {
        std::visit([this](const auto& be) { this->printDeduced(be); }, bool_expr);
    }

    void print(const parser::Relation& relation) {
        if (compact_mode) {
            printDeduced(relation.first);
            if (relation.second) {
                switch (relation.second->first) {
                case parser::Relation::Operation::Less:
                    out << " < ";
                    break;
                case parser::Relation::Operation::LessOrEqual:
                    out << " <= ";
                    break;
                case parser::Relation::Operation::Greater:
                    out << " > ";
                    break;
                case parser::Relation::Operation::GreaterOrEqual:
                    out << " >= ";
                    break;
                case parser::Relation::Operation::Equal:
                    out << " = ";
                    break;
                case parser::Relation::Operation::NotEqual:
                    out << " /= ";
                    break;
                }
                printDeduced(relation.second->second);
            }
        } else {
            out << "Relation:";
            newline();
            indent_level++;
            print_indent();
            out << "Left: ";
            printDeduced(relation.first);
            newline();

            if (relation.second) {
                print_indent();
                out << "Operator: ";
                switch (relation.second->first) {
                case parser::Relation::Operation::Less:
                    out << "<";
                    break;
                case parser::Relation::Operation::LessOrEqual:
                    out << "<=";
                    break;
                case parser::Relation::Operation::Greater:
                    out << ">";
                    break;
                case parser::Relation::Operation::GreaterOrEqual:
                    out << ">=";
                    break;
                case parser::Relation::Operation::Equal:
                    out << "=";
                    break;
                case parser::Relation::Operation::NotEqual:
                    out << "/=";
                    break;
                }
                newline();
                print_indent();
                out << "Right: ";
                printDeduced(relation.second->second);
                newline();
            }
            indent_level--;
        }
    }

    void print(const parser::NotExpression& not_expr) {
        if (compact_mode) {
            out << "NOT ";
            printDeduced(not_expr.operand);
        } else {
            out << "NOT";
            newline();
            indent_level++;
            print_indent();
            out << "Operand: ";
            printDeduced(not_expr.operand);
            indent_level--;
        }
    }

    void print(const parser::NumberExpression& num_expr) {
        if (compact_mode) {
            printDeduced(num_expr.first);
            for (const auto& [op, summand] : num_expr.rest) {
                switch (op) {
                case parser::NumberExpression::Operation::Plus:
                    out << " + ";
                    break;
                case parser::NumberExpression::Operation::Minus:
                    out << " - ";
                    break;
                }
                printDeduced(summand);
            }
        } else {
            out << "NumberExpression:";
            newline();
            indent_level++;
            print_indent();
            out << "First: ";
            printDeduced(num_expr.first);
            newline();

            for (const auto& [op, summand] : num_expr.rest) {
                print_indent();
                out << "Operator: ";
                switch (op) {
                case parser::NumberExpression::Operation::Plus:
                    out << "+";
                    break;
                case parser::NumberExpression::Operation::Minus:
                    out << "-";
                    break;
                }
                newline();
                print_indent();
                out << "Next: ";
                printDeduced(summand);
                newline();
            }
            indent_level--;
        }
    }

    void print(const parser::Summand& summand) {
        if (compact_mode) {
            printDeduced(summand.first);
            for (const auto& [op, primary] : summand.rest) {
                switch (op) {
                case parser::Summand::Operation::Multiply:
                    out << " * ";
                    break;
                case parser::Summand::Operation::Divide:
                    out << " / ";
                    break;
                case parser::Summand::Operation::Modulo:
                    out << " % ";
                    break;
                }
                printDeduced(primary);
            }
        } else {
            out << "Summand:";
            newline();
            indent_level++;
            print_indent();
            out << "First: ";
            printDeduced(summand.first);
            newline();

            for (const auto& [op, primary] : summand.rest) {
                print_indent();
                out << "Operator: ";
                switch (op) {
                case parser::Summand::Operation::Multiply:
                    out << "*";
                    break;
                case parser::Summand::Operation::Divide:
                    out << "/";
                    break;
                case parser::Summand::Operation::Modulo:
                    out << "%";
                    break;
                }
                newline();
                print_indent();
                out << "Next: ";
                printDeduced(primary);
                newline();
            }
            indent_level--;
        }
    }

    void print(const parser::Primary& primary) {
        std::visit([this](const auto& p) { this->printDeduced(p); }, primary);
    }

    void print(const parser::IntegerLiteral& lit) {
        out << "IntegerLiteral(" << lit.value << ")";
    }

    void print(const parser::RealLiteral& lit) {
        out << "RealLiteral(" << lit.value << ")";
    }

    void print(const parser::BooleanLiteral& lit) {
        out << "BooleanLiteral(" << (lit.value ? "true" : "false") << ")";
    }

    void print(const parser::RoutineCall& call) {
        out << "RoutineCall: " << call.name << "(";
        for (size_t i = 0; i < call.arguments.size(); ++i) {
            if (i > 0)
                out << ", ";
            bool old_compact = compact_mode;
            compact_mode = true;
            printDeduced(call.arguments[i]);
            compact_mode = old_compact;
        }
        out << ")";
    }

    void print(const parser::ModifiablePrimary& mp) {
        out << mp.variable;
        for (const auto& accessor : mp.accessors) {
            std::visit(
                [this](const auto& acc) {
                    using T = std::decay_t<decltype(acc)>;
                    if constexpr (std::is_same_v<T, parser::Expression>) {
                        out << "[";
                        bool old_compact = compact_mode;
                        compact_mode = true;
                        printDeduced(acc);
                        compact_mode = old_compact;
                        out << "]";
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        out << "." << acc;
                    }
                },
                accessor);
        }
    }

    void print(const parser::UnarySign& unary) {
        switch (unary.sign) {
        case parser::UnarySign::Sign::Plus:
            out << "+";
            break;
        case parser::UnarySign::Sign::Minus:
            out << "-";
            break;
        }
        printDeduced(*unary.operand);
    }

    void print(const std::unique_ptr<parser::Expression>& expr_ptr) {
        out << "(";
        bool old_compact = compact_mode;
        compact_mode = true;
        printDeduced(*expr_ptr);
        compact_mode = old_compact;
        out << ")";
    }

    // Statements and Block
    void print(const parser::Block& block) {
        for (const auto& element : block) {
            std::visit([this](const auto& elem) { this->printDeduced(elem); }, element);
        }
    }

    void print(const parser::Statement& stmt) {
        std::visit([this](const auto& s) { this->printDeduced(s); }, stmt);
    }

    void print(const parser::AssignmentStatement& assign) {
        print_indent();
        out << "Assignment: ";
        printDeduced(assign.target);
        out << " = ";
        printDeduced(assign.expression);
        newline();
    }

    void print(const parser::IfStatement& if_stmt) {
        print_indent();
        out << "If: ";
        printDeduced(if_stmt.condition);
        newline();

        indent_level++;
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
        indent_level--;
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
        indent_level -= 2;
    }

    void print(const parser::ForStatement& for_stmt) {
        print_indent();
        out << "For: " << for_stmt.counter << " in ";

        bool old_compact = compact_mode;
        compact_mode = true;

        std::visit(
            [this](const auto& r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, parser::Expression>) {
                    printDeduced(r);
                } else if constexpr (std::is_same_v<T, std::pair<parser::Expression, parser::Expression>>) {
                    printDeduced(r.first);
                    out << " .. ";
                    printDeduced(r.second);
                }
            },
            for_stmt.range);

        compact_mode = old_compact;

        if (for_stmt.is_reversed) {
            out << " REVERSE";
        }
        newline();

        indent_level++;
        print_indent();
        out << "Body:";
        newline();
        indent_level++;
        printDeduced(for_stmt.body);
        indent_level -= 2;
    }

    void print(const parser::PrintStatement& print_stmt) {
        print_indent();
        out << "Print: ";
        for (size_t i = 0; i < print_stmt.arguments.size(); ++i) {
            if (i > 0)
                out << ", ";
            std::visit(
                [this](const auto& a) {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, parser::Expression>) {
                        bool old_compact = compact_mode;
                        compact_mode = true;
                        printDeduced(a);
                        compact_mode = old_compact;
                    } else if constexpr (std::is_same_v<T, parser::StringLiteral>) {
                        out << "\"" << a.value << "\"";
                    }
                },
                print_stmt.arguments[i]);
        }
        newline();
    }

    void print(const parser::StringLiteral& str_lit) {
        out << "StringLiteral(\"" << str_lit.value << "\")";
    }

  private:
    template <typename Arg>
    using PrintOverload = void (TreePrinter::*)(const Arg&);

    template<typename Arg>
    void printDeduced(const Arg& arg) {
        (this->*static_cast<PrintOverload<Arg>>(&TreePrinter::print))(arg);
    }
};

} // namespace

namespace parser {

void print_tree(const Program& program, std::ostream& out) {
    TreePrinter printer(out);
    printer.print(program);
}

} // namespace parser

