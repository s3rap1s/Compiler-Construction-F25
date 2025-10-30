#include "analyzer.hpp"

#include "common.hpp"
#include "lexer/iterator.hpp"
#include "lexer/lexer.hpp"
#include "lexer/tokens.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/syntax_error.hpp"
#include "parser/types.hpp"
#include "utils.hpp"

#include <algorithm>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>


namespace analyzer {

class SemanticAnalyzer{
private:
    parser::Program& program; // NOLINT(*ref-data*)

    std::optional<analyzer::SemanticError> analysisChecks(){
        
    }

    std::expected<parser::Program, analyzer::SemanticError> analyzeProgram(){
        analysisChecks();
    }
public:
    explicit SemanticAnalyzer(parser::Program& program) : program{program} {}

    std::expected<parser::Program, analyzer::SemanticError> analyze() {
        return analyzeProgram();
    }
};

std::expected<parser::Program, analyzer::SemanticError> analyze(parser::Program ast) {
    SemanticAnalyzer analyzer{ast};
    return analyzer.analyze();
}

} // namespace analyzer