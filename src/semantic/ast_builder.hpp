#pragma once

#include "parser_ll1/grammar.hpp"
#include "parser_ll1/parser.hpp"
#include "semantic/ast.hpp"

namespace sql {

// Construye el AST (Traduccion Dirigida por la Sintaxis) a partir de la derivacion por
// la izquierda que dejo el parser LL(1) en `result` (ids de produccion + tokens
// emparejados). Precondicion: result.ok() (la consulta es sintacticamente valida).
SelectStmt buildAst(const ParseResult& result, const Grammar& grammar);

}  // namespace sql
