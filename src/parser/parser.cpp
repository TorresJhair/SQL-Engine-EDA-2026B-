#include "parser/parser.hpp"

#include <algorithm>
#include <utility>

namespace sql {
namespace {

bool isComparison(TokenType type) {
    return type == TokenType::EQUAL || type == TokenType::NOT_EQUAL ||
           type == TokenType::LESS || type == TokenType::GREATER ||
           type == TokenType::LESS_EQUAL || type == TokenType::GREATER_EQUAL;
}

ParseDiagnostic syntaxDiagnostic(const Token& token, const std::string& expected) {
    const std::string found = token.type == TokenType::END_OF_FILE
        ? "fin de entrada" : std::string("'") + token.lexeme + "' (" + tokenTypeName(token.type) + ")";
    return {"se esperaba " + expected + ", se encontro " + found,
            token.lexeme, token.line, token.column, false};
}

std::shared_ptr<Expression> makeExpression(Expression::Kind kind,
                                           std::string value = {},
                                           TokenType op = TokenType::END_OF_FILE,
                                           std::shared_ptr<Expression> left = {},
                                           std::shared_ptr<Expression> right = {}) {
    auto expression = std::make_shared<Expression>();
    expression->kind = kind;
    expression->value = std::move(value);
    expression->op = op;
    expression->left = std::move(left);
    expression->right = std::move(right);
    return expression;
}

std::shared_ptr<Condition> makeLogical(Condition::Kind kind,
                                      std::shared_ptr<Condition> left,
                                      std::shared_ptr<Condition> right) {
    auto condition = std::make_shared<Condition>();
    condition->kind = kind;
    condition->left_condition = std::move(left);
    condition->right_condition = std::move(right);
    return condition;
}

}  // namespace

Parser::Parser(const LexResult& input) : input_(input) {
    if (input_.tokens.empty() || input_.tokens.back().type != TokenType::END_OF_FILE) {
        const Token end = input_.tokens.empty() ? Token{TokenType::END_OF_FILE, "", 1, 1}
                                                : Token{TokenType::END_OF_FILE, "",
                                                        input_.tokens.back().line,
                                                        input_.tokens.back().column +
                                                            static_cast<int>(input_.tokens.back().lexeme.size())};
        input_.tokens.push_back(end);
    }
}

const Token& Parser::current() const {
    return input_.tokens[std::min(position_, input_.tokens.size() - 1)];
}

bool Parser::match(TokenType type) {
    if (current().type != type) return false;
    ++position_;
    return true;
}

Token Parser::expect(TokenType type) {
    if (current().type != type) fail(tokenTypeName(type));
    return input_.tokens[position_++];
}

[[noreturn]] void Parser::fail(const std::string& expected) const {
    throw Failure{syntaxDiagnostic(current(), expected), position_};
}

ParseResult Parser::parse() {
    ParseResult result;
    if (!input_.errors.empty()) {
        result.errors.reserve(input_.errors.size());
        for (const auto& error : input_.errors) {
            result.errors.push_back({error.message, error.lexeme, error.line, error.column, true});
        }
        return result;
    }

    position_ = 0;
    try {
        result.statement = parseStatement();
    } catch (const Failure& failure) {
        result.errors.push_back(failure.diagnostic);
    }
    return result;
}

SelectStatement Parser::parseStatement() {
    expect(TokenType::KEYWORD_SELECT);

    SelectStatement statement;
    if (match(TokenType::STAR)) {
        statement.select_all = true;
    } else {
        statement.columns.push_back(expect(TokenType::IDENTIFIER).lexeme);
        while (match(TokenType::COMMA)) {
            statement.columns.push_back(expect(TokenType::IDENTIFIER).lexeme);
        }
    }

    expect(TokenType::KEYWORD_FROM);
    statement.table = expect(TokenType::IDENTIFIER).lexeme;
    if (match(TokenType::KEYWORD_WHERE)) statement.where = parseCondition();
    expect(TokenType::SEMICOLON);
    expect(TokenType::END_OF_FILE);
    return statement;
}

std::shared_ptr<Condition> Parser::parseCondition() { return parseDisjunction(); }

std::shared_ptr<Condition> Parser::parseDisjunction() {
    auto condition = parseConjunction();
    while (match(TokenType::KEYWORD_OR)) {
        condition = makeLogical(Condition::Kind::Or, std::move(condition), parseConjunction());
    }
    return condition;
}

std::shared_ptr<Condition> Parser::parseConjunction() {
    auto condition = parsePredicate();
    while (match(TokenType::KEYWORD_AND)) {
        condition = makeLogical(Condition::Kind::And, std::move(condition), parsePredicate());
    }
    return condition;
}

std::shared_ptr<Condition> Parser::parsePredicate() {
    const size_t start = position_;
    Failure comparison_failure{syntaxDiagnostic(current(), "comparacion"), position_};
    try {
        return parseComparison();
    } catch (const Failure& failure) {
        comparison_failure = failure;
    }

    position_ = start;
    try {
        expect(TokenType::LPAREN);
        auto nested = parseCondition();
        expect(TokenType::RPAREN);
        return nested;
    } catch (const Failure& grouped_failure) {
        position_ = start;
        throw grouped_failure.position >= comparison_failure.position
            ? grouped_failure : comparison_failure;
    }
}

std::shared_ptr<Condition> Parser::parseComparison() {
    auto left = parseExpression();
    if (!isComparison(current().type)) fail("operador de comparacion");
    const TokenType op = input_.tokens[position_++].type;
    auto right = parseExpression();

    auto condition = std::make_shared<Condition>();
    condition->kind = Condition::Kind::Comparison;
    condition->op = op;
    condition->left_expression = std::move(left);
    condition->right_expression = std::move(right);
    return condition;
}

std::shared_ptr<Expression> Parser::parseExpression() {
    auto expression = parseTerm();
    while (current().type == TokenType::PLUS || current().type == TokenType::MINUS) {
        const TokenType op = input_.tokens[position_++].type;
        expression = makeExpression(Expression::Kind::Binary, {}, op,
                                    std::move(expression), parseTerm());
    }
    return expression;
}

std::shared_ptr<Expression> Parser::parseTerm() {
    auto expression = parseFactor();
    while (current().type == TokenType::STAR || current().type == TokenType::SLASH) {
        const TokenType op = input_.tokens[position_++].type;
        expression = makeExpression(Expression::Kind::Binary, {}, op,
                                    std::move(expression), parseFactor());
    }
    return expression;
}

std::shared_ptr<Expression> Parser::parseFactor() {
    if (match(TokenType::MINUS)) {
        return makeExpression(Expression::Kind::UnaryMinus, {}, TokenType::MINUS,
                              parseFactor());
    }

    const Token token = current();
    switch (token.type) {
        case TokenType::IDENTIFIER:
            ++position_;
            return makeExpression(Expression::Kind::Identifier, token.lexeme);
        case TokenType::INTEGER_LITERAL:
            ++position_;
            return makeExpression(Expression::Kind::IntegerLiteral, token.lexeme);
        case TokenType::DECIMAL_LITERAL:
            ++position_;
            return makeExpression(Expression::Kind::DecimalLiteral, token.lexeme);
        case TokenType::STRING_LITERAL:
            ++position_;
            return makeExpression(Expression::Kind::StringLiteral, token.lexeme);
        case TokenType::LPAREN: {
            ++position_;
            auto expression = parseExpression();
            expect(TokenType::RPAREN);
            return expression;
        }
        default:
            fail("identificador, literal, '-' o '('");
    }
}

}  // namespace sql
