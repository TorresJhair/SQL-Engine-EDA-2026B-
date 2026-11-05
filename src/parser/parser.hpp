#pragma once

#include "lexer/lexer.hpp"
#include "parser/ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sql {

struct ParseDiagnostic {
    std::string message;
    std::string lexeme;
    int line = 1;
    int column = 1;
    bool lexical = false;
};

struct ParseResult {
    std::optional<SelectStatement> statement;
    std::vector<ParseDiagnostic> errors;
    bool ok() const { return statement.has_value() && errors.empty(); }
};

class Parser {
public:
    explicit Parser(const LexResult& input);
    ParseResult parse();

private:
    struct Failure {
        ParseDiagnostic diagnostic;
        size_t position;
    };

    LexResult input_;
    size_t position_ = 0;

    const Token& current() const;
    bool match(TokenType type);
    Token expect(TokenType type);
    [[noreturn]] void fail(const std::string& expected) const;

    SelectStatement parseStatement();
    std::shared_ptr<Condition> parseCondition();
    std::shared_ptr<Condition> parseDisjunction();
    std::shared_ptr<Condition> parseConjunction();
    std::shared_ptr<Condition> parsePredicate();
    std::shared_ptr<Condition> parseComparison();
    std::shared_ptr<Expression> parseExpression();
    std::shared_ptr<Expression> parseTerm();
    std::shared_ptr<Expression> parseFactor();
};

}  // namespace sql
