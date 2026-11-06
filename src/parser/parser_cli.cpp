#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"

namespace {

void printTrace(const sql::ParseResult& result) {
    std::cout << std::left << std::setw(4) << "N" << " | " << std::setw(46) << "PILA (tope a la derecha)"
              << " | " << std::setw(26) << "ENTRADA" << " | ACCION\n";
    int step = 1;
    for (const auto& s : result.trace) {
        std::cout << std::setw(4) << step++ << " | " << std::setw(46) << s.stack
                  << " | " << std::setw(26) << s.input << " | " << s.action << '\n';
    }
}

int runGrammar() {
    sql::Grammar g;
    g.dumpProductions(std::cout);
    std::cout << '\n';
    g.dumpFirstFollow(std::cout);
    std::cout << '\n';
    g.dumpTable(std::cout);
    return g.conflicts().empty() ? 0 : 1;
}

void printUsage(std::ostream& out, const char* prog) {
    out << "Uso: " << prog << " [--grammar] [--quiet] [archivo.sql]\n"
           "  Sin archivo lee la consulta SQL de stdin.\n"
           "  --grammar  imprime producciones, FIRST/FOLLOW y la tabla LL(1).\n"
           "  --quiet    no imprime la traza paso a paso de la pila.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    bool quiet = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(std::cout, argv[0]); return 0; }
        if (arg == "--grammar") return runGrammar();
        if (arg == "--quiet") { quiet = true; continue; }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Opcion desconocida: " << arg << '\n';
            printUsage(std::cerr, argv[0]);
            return 2;
        }
        if (!path.empty()) { printUsage(std::cerr, argv[0]); return 2; }
        path = arg;
    }

    std::string input;
    if (!path.empty()) {
        std::ifstream file(path);
        if (!file) { std::cerr << "No se pudo abrir: " << path << '\n'; return 2; }
        input.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } else {
        input.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }

    const sql::LexResult lexed = sql::Lexer(input).tokenize();
    const sql::ParseResult result = sql::Parser(lexed).parse();

    if (!quiet && !result.trace.empty()) printTrace(result);

    if (result.ok()) {
        std::cout << "\nCONSULTA ACEPTADA: cumple la gramatica LL(1).\n";
        return 0;
    }

    std::cout << "\nCONSULTA RECHAZADA.\n";
    for (const auto& e : result.errors) {
        std::cerr << (e.lexical ? "[ERROR LEXICO] " : "[ERROR SINTACTICO] ")
                  << "linea " << e.line << ", columna " << e.column << ": " << e.message << '\n';
    }
    return 1;
}
