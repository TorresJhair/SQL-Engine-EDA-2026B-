#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/ast_builder.hpp"

#include <iostream>
#include <string>

namespace {
int passed = 0;
int failed = 0;

#define CHECK(name, condition) do { \
    if (condition) { ++passed; std::cout << "[OK]   " << name << '\n'; } \
    else { ++failed; std::cout << "[FAIL] " << name << " (linea " << __LINE__ << ")\n"; } \
} while (false)

sql::SelectStmt build(const std::string& src) {
    const sql::LexResult lexed = sql::Lexer(src).tokenize();
    sql::Parser parser(lexed);
    const sql::ParseResult parsed = parser.parse();
    return sql::buildAst(parsed, parser.grammar());
}
}  // namespace

int main() {
    using K = sql::AstNode::Kind;
    using sql::TokenType;

    {
        const auto s = build("SELECT nombre, edad FROM estudiantes;");
        CHECK("proyeccion de 2 columnas", !s.select_all && s.projection.size() == 2 &&
              s.projection[0].name == "nombre" && s.projection[1].name == "edad");
        CHECK("tabla y sin where", s.table == "estudiantes" && !s.where);
        CHECK("posicion de columna registrada", s.projection[0].line == 1 &&
              s.projection[0].column >= 1);
    }
    {
        const auto s = build("SELECT * FROM cursos;");
        CHECK("SELECT * marca select_all", s.select_all && s.projection.empty() &&
              s.table == "cursos");
    }
    {
        // Precedencia: AND liga mas fuerte que OR.
        const auto s = build("SELECT a FROM t WHERE a = 1 OR b = 2 AND c > 3;");
        CHECK("raiz del WHERE es OR", s.where && s.where->kind == K::Binary &&
              s.where->op == TokenType::KEYWORD_OR);
        CHECK("rama derecha del OR es AND", s.where && s.where->right &&
              s.where->right->op == TokenType::KEYWORD_AND);
        CHECK("rama izquierda del OR es comparacion =", s.where && s.where->left &&
              s.where->left->op == TokenType::EQUAL &&
              s.where->left->left->kind == K::Column &&
              s.where->left->right->kind == K::IntLit);
    }
    {
        // Asociatividad izquierda: a - b - c  =>  (a - b) - c.
        const auto s = build("SELECT x FROM t WHERE a - b - c > 0;");
        CHECK("comparacion en la raiz", s.where && s.where->op == TokenType::GREATER);
        CHECK("resta asociada a la izquierda", s.where && s.where->left &&
              s.where->left->op == TokenType::MINUS &&
              s.where->left->left->op == TokenType::MINUS);
    }
    {
        // Menos unario y parentesis aritmeticos.
        const auto s = build("SELECT x FROM t WHERE (a + 1) > -2;");
        CHECK("lado izquierdo es suma (parentesis sin nodo)", s.where &&
              s.where->left->op == TokenType::PLUS);
        CHECK("lado derecho es menos unario", s.where && s.where->right &&
              s.where->right->kind == K::Unary && s.where->right->op == TokenType::MINUS &&
              s.where->right->left->kind == K::IntLit);
    }

    std::cout << "Resumen: " << passed << " OK, " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
