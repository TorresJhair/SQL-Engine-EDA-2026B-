#pragma once

#include "lexer/lexer.hpp"
#include "parser_ll1/grammar.hpp"

#include <string>
#include <vector>

namespace sql {

// Diagnostico de un error lexico o sintactico, con su ubicacion.
struct ParseDiagnostic {
    std::string message;
    std::string lexeme;
    int line = 1;
    int column = 1;
    bool lexical = false;
};

// Una linea de la traza del analizador predictivo por pila.
struct ParseStep {
    std::string stack;   // contenido de la pila (fondo a la izquierda, tope a la derecha)
    std::string input;   // token actual y lo que resta de la entrada
    std::string action;  // "expandir (n) A -> ..." o "emparejar X"
};

struct ParseResult {
    bool accepted = false;
    std::vector<ParseDiagnostic> errors;
    std::vector<ParseStep> trace;
    bool ok() const { return accepted && errors.empty(); }
};

// Analizador sintactico predictivo LL(1) no recursivo: usa una pila explicita y
// la tabla M[A, a] de Grammar. Reconoce (acepta/rechaza) la cadena de tokens del
// lexer; no construye AST.
class Parser {
public:
    explicit Parser(const LexResult& input);
    ParseResult parse();

    const Grammar& grammar() const { return grammar_; }

private:
    Grammar grammar_;
    LexResult input_;
};

}  // namespace sql
