#pragma once

#include "parser/grammar.hpp"
#include "parser/parser.hpp"
#include "semantic/ast.hpp"

namespace sql {

// Construye el AST (Traduccion Dirigida por la Sintaxis) a partir de la derivacion por
// la izquierda que dejo el parser LL(1) en `result` (ids de produccion + tokens
// emparejados). Precondicion: result.ok() (la consulta es sintacticamente valida).
SelectStmt buildAst(const ParseResult& result, const Grammar& grammar);

}  // namespace sql
