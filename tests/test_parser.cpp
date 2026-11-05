#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int passed = 0;
int failed = 0;

#define CHECK(name, condition) do { \
    if (condition) { \
        ++passed; std::cout << "[OK]   " << name << '\n'; \
    } else { \
        ++failed; std::cout << "[FAIL] " << name << " (linea " << __LINE__ << ")\n"; \
    } \
} while (false)

sql::ParseResult parse(const std::string& source) {
    const sql::LexResult lexed = sql::Lexer(source).tokenize();
    return sql::Parser(lexed).parse();
}
}  // namespace

int main() {
    using sql::Condition;
    using sql::Expression;
    using sql::TokenType;

    {
        const auto result = parse("SELECT nombre, edad FROM estudiantes;");
        CHECK("consulta sin WHERE y lista de columnas", result.ok());
        CHECK("AST conserva columnas y tabla",
              result.statement && !result.statement->select_all &&
              result.statement->columns == std::vector<std::string>({"nombre", "edad"}) &&
              result.statement->table == "estudiantes" && !result.statement->where);
    }
    {
        const auto result = parse("SELECT * FROM t WHERE a = 1 OR b = 2 AND c > 3;");
        CHECK("consulta SELECT * valida", result.ok() && result.statement->select_all);
        const auto& where = result.statement->where;
        CHECK("AND tiene precedencia mayor que OR",
              where && where->kind == Condition::Kind::Or &&
              where->right_condition && where->right_condition->kind == Condition::Kind::And);
        CHECK("comparacion conserva operador y literales",
              where && where->left_condition &&
              where->left_condition->kind == Condition::Kind::Comparison &&
              where->left_condition->op == TokenType::EQUAL &&
              where->left_condition->right_expression->value == "1");
    }
    {
        const auto result = parse("SELECT x FROM t WHERE (a + 1) > -2.5;");
        const auto& where = result.statement->where;
        CHECK("parentesis aritmeticos, decimal y menos unario", result.ok() && where &&
              where->left_expression->kind == Expression::Kind::Binary &&
              where->left_expression->op == TokenType::PLUS &&
              where->right_expression->kind == Expression::Kind::UnaryMinus &&
              where->right_expression->left->kind == Expression::Kind::DecimalLiteral);
    }
    {
        const auto result = parse("SELECT x FROM t WHERE (a = 1 OR b = 2) AND c <> 'z';");
        const auto& where = result.statement->where;
        CHECK("parentesis logicos con OR dentro de AND", result.ok() && where &&
              where->kind == Condition::Kind::And && where->left_condition &&
              where->left_condition->kind == Condition::Kind::Or);
    }

    const std::vector<std::string> invalid = {
        "SELECT FROM t;", "SELECT a, FROM t;", "SELECT a FROM t WHERE a = ;",
        "SELECT a FROM t WHERE a + > 2;", "SELECT a FROM t WHERE (a = 1;",
        "SELECT a FROM t", "SELECT a FROM t; SELECT b FROM t;"
    };
    for (const auto& source : invalid) {
        const auto result = parse(source);
        CHECK("consulta invalida rechazada", !result.ok() && !result.errors.empty() &&
              result.errors.front().line >= 1 && result.errors.front().column >= 1);
    }
    {
        const auto lexed = sql::Lexer("SELECT a # FROM t;").tokenize();
        const auto result = sql::Parser(lexed).parse();
        CHECK("error lexico se propaga como error de parseo",
              !result.ok() && !result.errors.empty() && result.errors.front().lexical);
    }

    std::cout << "Resumen: " << passed << " OK, " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
