#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "lexer/lexer.hpp"
#include "parser_ll1/parser.hpp"
#include "semantic/ast_builder.hpp"
#include "semantic/semantic.hpp"

namespace {

void printUsage(std::ostream& out, const char* prog) {
    out << "Uso: " << prog << " [--schema catalogo.csv] [consulta.sql]\n"
           "  Sin archivo de consulta, lee el SQL de stdin.\n"
           "  --schema  carga el catalogo desde un CSV (tabla,columna,tipo[,pk]);\n"
           "            sin el, usa el esquema de prueba en memoria (3 tablas).\n";
}

void printDiagnostics(const std::vector<sql::SemDiag>& diags) {
    for (const auto& d : diags) {
        std::cerr << (d.warning ? "[ADVERTENCIA SEMANTICA] " : "[ERROR SEMANTICO] ")
                  << "linea " << d.line << ", columna " << d.column << ": " << d.message << '\n';
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string schema_path;
    std::string query_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(std::cout, argv[0]); return 0; }
        if (arg == "--schema") {
            if (i + 1 >= argc) { std::cerr << "Falta la ruta para --schema\n"; return 2; }
            schema_path = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Opcion desconocida: " << arg << '\n';
            printUsage(std::cerr, argv[0]);
            return 2;
        }
        if (!query_path.empty()) { printUsage(std::cerr, argv[0]); return 2; }
        query_path = arg;
    }

    // Catalogo: CSV o esquema por defecto en memoria.
    sql::Catalog catalog;
    try {
        catalog = schema_path.empty() ? sql::defaultSchema() : sql::loadCsv(schema_path);
    } catch (const std::exception& e) {
        std::cerr << "No se pudo cargar el catalogo: " << e.what() << '\n';
        return 2;
    }

    // Consulta: archivo o stdin.
    std::string input;
    if (!query_path.empty()) {
        std::ifstream file(query_path);
        if (!file) { std::cerr << "No se pudo abrir: " << query_path << '\n'; return 2; }
        input.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } else {
        input.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }

    // 1. Lexico.
    const sql::LexResult lexed = sql::Lexer(input).tokenize();
    if (!lexed.ok()) {
        for (const auto& e : lexed.errors) {
            std::cerr << "[ERROR LEXICO] linea " << e.line << ", columna " << e.column << ": "
                      << e.message << '\n';
        }
        std::cout << "CONSULTA RECHAZADA (lexico).\n";
        return 1;
    }

    // 2. Sintactico (LL(1)). Sin AST ni semantica si no acepta.
    sql::Parser parser(lexed);
    const sql::ParseResult parsed = parser.parse();
    if (!parsed.ok()) {
        for (const auto& e : parsed.errors) {
            std::cerr << "[ERROR SINTACTICO] linea " << e.line << ", columna " << e.column << ": "
                      << e.message << '\n';
        }
        std::cout << "CONSULTA RECHAZADA (sintaxis).\n";
        return 1;
    }

    // 3. Semantico: construir AST y validar contra el catalogo.
    const sql::SemResult sem = sql::analyze(sql::buildAst(parsed, parser.grammar()), catalog);
    printDiagnostics(sem.diagnostics);
    if (!sem.ok) {
        std::cout << "CONSULTA RECHAZADA (semantica).\n";
        return 1;
    }

    std::cout << "CONSULTA ACEPTADA. AST decorado:\n\n";
    sql::printDecoratedAst(sem.stmt, std::cout);
    return 0;
}
