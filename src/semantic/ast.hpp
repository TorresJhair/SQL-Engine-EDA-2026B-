#pragma once

#include "lexer/token.hpp"
#include "semantic/catalog.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sql {

// Nodo del Arbol de Sintaxis Abstracta para expresiones y condiciones del WHERE.
// Queda "decorado" por el analizador semantico con el tipo inferido (campo type).
// En Binary, op distingue aritmetico (+ - * /), comparacion (= <> < > <= >=) y
// logico (AND, OR). Unary representa la negacion unaria (-) sobre left.
struct AstNode {
    enum class Kind { Column, IntLit, FloatLit, StringLit, Unary, Binary };

    Kind kind;
    std::string lexeme;                 // nombre de columna o texto del literal
    TokenType op = TokenType::END_OF_FILE;
    DataType type = DataType::Unknown;  // decoracion: tipo inferido
    int line = 1;
    int column = 1;
    std::shared_ptr<AstNode> left;
    std::shared_ptr<AstNode> right;
};

// Item de la lista de proyeccion. Tras el analisis, type queda resuelto y, si vino de
// `SELECT *`, star_expanded es true.
struct ProjItem {
    std::string name;
    int line = 1;
    int column = 1;
    DataType type = DataType::Unknown;
    bool star_expanded = false;
};

// Raiz del AST de una sentencia SELECT.
struct SelectStmt {
    bool select_all = false;             // true antes de expandir `*`
    std::vector<ProjItem> projection;    // tras el analisis, `*` queda expandido aqui
    std::string table;
    int table_line = 1;
    int table_column = 1;
    std::shared_ptr<AstNode> where;      // nulo si no hay clausula WHERE
};

}  // namespace sql
