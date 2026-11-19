#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/ast_builder.hpp"
#include "semantic/semantic.hpp"

#include <iostream>
#include <string>

namespace {
int passed = 0;
int failed = 0;

#define CHECK(name, condition) do { \
    if (condition) { ++passed; std::cout << "[OK]   " << name << '\n'; } \
    else { ++failed; std::cout << "[FAIL] " << name << " (linea " << __LINE__ << ")\n"; } \
} while (false)

sql::SemResult run(const std::string& src, const sql::Catalog& cat) {
    const sql::LexResult lexed = sql::Lexer(src).tokenize();
    sql::Parser parser(lexed);
    const sql::ParseResult parsed = parser.parse();
    return sql::analyze(sql::buildAst(parsed, parser.grammar()), cat);
}
}  // namespace

int main() {
    using sql::DataType;
    const sql::Catalog cat = sql::defaultSchema();

    // --- Consultas semanticamente validas ---
    {
        const auto r = run("SELECT nombre, edad FROM estudiantes;", cat);
        CHECK("proyeccion especifica valida", r.ok && r.diagnostics.empty());
        CHECK("tipos de proyeccion inferidos",
              r.stmt.projection[0].type == DataType::Varchar &&
              r.stmt.projection[1].type == DataType::Int);
    }
    {
        const auto r = run("SELECT * FROM estudiantes;", cat);
        CHECK("SELECT * valido y expandido a 5 columnas",
              r.ok && r.stmt.projection.size() == 5);
        CHECK("expansion en orden ordinal con tipos",
              r.stmt.projection[0].name == "id" && r.stmt.projection[0].type == DataType::Int &&
              r.stmt.projection[4].name == "activo" &&
              r.stmt.projection[4].type == DataType::Boolean);
    }
    {
        const auto r = run("SELECT nombre FROM estudiantes WHERE edad > 18 AND promedio >= 3.5;", cat);
        CHECK("WHERE compuesto valido (comparaciones + AND)", r.ok);
        CHECK("la raiz del WHERE infiere BOOLEAN", r.stmt.where->type == DataType::Boolean);
    }
    {
        const auto r = run("SELECT id FROM estudiantes WHERE activo;", cat);
        CHECK("WHERE con columna BOOLEAN sola es valido", r.ok);
    }
    {
        const auto r = run("SELECT id FROM estudiantes WHERE (edad = 1 OR edad = 2) AND activo;", cat);
        CHECK("WHERE con parentesis logicos valido", r.ok);
    }

    // --- Error: entidad inexistente ---
    {
        const auto r = run("SELECT id FROM profesores;", cat);
        CHECK("tabla inexistente rechazada con posicion",
              !r.ok && !r.diagnostics.empty() && !r.diagnostics.front().warning &&
              r.diagnostics.front().line == 1 && r.diagnostics.front().column >= 1);
    }
    {
        const auto r = run("SELECT telefono FROM estudiantes;", cat);
        CHECK("columna inexistente en proyeccion rechazada", !r.ok);
    }
    {
        const auto r = run("SELECT id FROM estudiantes WHERE sueldo > 10;", cat);
        CHECK("columna inexistente en WHERE rechazada", !r.ok);
    }

    // --- Error: incompatibilidad de tipos ---
    {
        const auto r = run("SELECT id FROM estudiantes WHERE nombre > 100;", cat);
        CHECK("VARCHAR > INT rechazado", !r.ok);
    }
    {
        const auto r = run("SELECT id FROM estudiantes WHERE edad = 'veinte';", cat);
        CHECK("INT = VARCHAR rechazado", !r.ok);
    }
    {
        const auto r = run("SELECT id FROM estudiantes WHERE edad AND activo;", cat);
        CHECK("operando no booleano de AND rechazado", !r.ok);
    }
    {
        const auto r = run("SELECT id FROM estudiantes WHERE edad + 1;", cat);
        CHECK("WHERE no booleano (expresion aritmetica) rechazado", !r.ok);
    }
    {
        // Promocion numerica INT/FLOAT permitida.
        const auto r = run("SELECT id FROM estudiantes WHERE edad >= promedio;", cat);
        CHECK("comparacion INT vs FLOAT permitida (promocion)", r.ok);
    }

    // --- Duplicados (advertencia, no invalida) ---
    {
        const auto r = run("SELECT id, id FROM estudiantes;", cat);
        bool has_warning = false;
        for (const auto& d : r.diagnostics) if (d.warning) has_warning = true;
        CHECK("columna repetida: advierte pero sigue valida", r.ok && has_warning);
    }

    std::cout << "Resumen: " << passed << " OK, " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
