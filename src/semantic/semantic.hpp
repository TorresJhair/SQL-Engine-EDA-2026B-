#pragma once

#include "semantic/ast.hpp"
#include "semantic/catalog.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace sql {

// Diagnostico semantico con ubicacion heredada del token. warning=false es un error
// (invalida la consulta); warning=true solo advierte (la consulta sigue valida).
struct SemDiag {
    std::string message;
    int line = 1;
    int column = 1;
    bool warning = false;
};

struct SemResult {
    SelectStmt stmt;                      // AST decorado (tipos inferidos, `*` expandido)
    std::vector<SemDiag> diagnostics;     // errores y advertencias, en orden
    bool ok = false;                      // true si no hay errores (las advertencias no cuentan)
};

// Analiza semanticamente la sentencia contra el catalogo: existencia de la tabla (FROM),
// resolucion de columnas (SELECT/WHERE), expansion de `*`, deteccion de duplicados
// (advertencia) y verificacion/coercion de tipos en el WHERE. Decora el AST con los tipos
// inferidos y devuelve los diagnosticos. No lanza excepciones ni accede a disco.
SemResult analyze(SelectStmt stmt, const Catalog& catalog);

// Imprime el AST decorado (proyeccion con tipos, tabla y arbol del WHERE con el tipo
// inferido de cada nodo).
void printDecoratedAst(const SelectStmt& stmt, std::ostream& out);

}  // namespace sql
