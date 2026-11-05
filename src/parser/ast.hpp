#pragma once

#include "lexer/token.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sql {

struct Expression {
    enum class Kind {
        Identifier,
        IntegerLiteral,
        DecimalLiteral,
        StringLiteral,
        UnaryMinus,
        Binary
    };

    Kind kind;
    std::string value;
    TokenType op = TokenType::END_OF_FILE;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct Condition {
    enum class Kind { Comparison, And, Or };

    Kind kind;
    TokenType op = TokenType::END_OF_FILE;
    std::shared_ptr<Expression> left_expression;
    std::shared_ptr<Expression> right_expression;
    std::shared_ptr<Condition> left_condition;
    std::shared_ptr<Condition> right_condition;
};

struct SelectStatement {
    bool select_all = false;
    std::vector<std::string> columns;
    std::string table;
    std::shared_ptr<Condition> where;
};

}  // namespace sql
