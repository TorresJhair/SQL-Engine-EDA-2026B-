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
    // La gramatica debe ser LL(1): la tabla se construye sin conflictos.
    {
        sql::Grammar g;
        CHECK("gramatica sin conflictos LL(1)", g.conflicts().empty());
        CHECK("tabla tiene entrada M[consulta, SELECT]",
              g.tableEntry(sql::NonTerminal::Consulta, sql::TokenType::KEYWORD_SELECT) >= 0);
        CHECK("celda vacia da -1",
              g.tableEntry(sql::NonTerminal::Consulta, sql::TokenType::KEYWORD_FROM) < 0);
    }

    // Consultas validas (deben ser ACEPTADAS).
    const std::vector<std::string> valid = {
        "SELECT * FROM t;",
        "SELECT nombre, edad FROM estudiantes;",
        "SELECT a FROM t WHERE a = 1;",
        "SELECT * FROM t WHERE a = 1 OR b = 2 AND c > 3;",
        "SELECT x FROM t WHERE (a + 1) > -2.5;",
        "SELECT x FROM t WHERE (a = 1 OR b = 2) AND c <> 'z';",
        "SELECT a, b, c FROM t WHERE a <= b * 2 - 1 AND nombre = 'pepe';",
    };
    for (const auto& source : valid) {
        const auto result = parse(source);
        CHECK(std::string("ACEPTADA: ") + source, result.ok() && !result.trace.empty());
    }

    // Consultas invalidas (deben ser RECHAZADAS, con ubicacion, sin crashear).
    const std::vector<std::string> invalid = {
        "SELECT FROM t;",                      // proyeccion vacia
        "SELECT a, FROM t;",                   // coma sin columna
        "SELECT a FROM t WHERE a = ;",         // comparacion sin lado derecho
        "SELECT a FROM t WHERE a + > 2;",      // operador sin operando
        "SELECT a FROM t WHERE (a = 1;",       // parentesis sin cerrar
        "SELECT a FROM t",                     // falta ;
        "SELECT a FROM t; SELECT b FROM t;",   // tokens despues del ;
        "SELECT a FROM 123;",                  // tabla no es identificador
        "FROM t;",                             // no empieza con SELECT
        "SELECT a WHERE a = 1 FROM t;",        // palabra clave fuera de orden (WHERE antes de FROM)
        "SELECT *, a FROM t;",                 // combinacion invalida de '*' con columnas
    };
    for (const auto& source : invalid) {
        const auto result = parse(source);
        CHECK(std::string("RECHAZADA: ") + source,
              !result.ok() && !result.errors.empty() &&
              result.errors.front().line >= 1 && result.errors.front().column >= 1);
    }

    // Un error lexico se propaga como fallo del parseo.
    {
        const auto result = parse("SELECT a # FROM t;");
        CHECK("error lexico se propaga",
              !result.ok() && !result.errors.empty() && result.errors.front().lexical);
    }

    std::cout << "Resumen: " << passed << " OK, " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
