# src/parser — Analizador sintáctico SQL

Este módulo consume directamente la salida del analizador léxico (`src/lexer`): la
secuencia de `sql::Token` que produce `sql::Lexer::tokenize()` dentro de `LexResult`.
Reconoce una consulta `SELECT`, construye un AST y devuelve diagnósticos con posición.
La especificación de sintaxis está en [grammar.md](grammar.md).

Componentes previstos (añadir cada `.cpp` explícitamente a `phase1_core` en el
`CMakeLists.txt` de la raíz — nunca GLOB):

- AST de consulta, expresiones aritméticas y condiciones booleanas en `ast.hpp`.
- Parser de descenso recursivo por precedencia en `parser.hpp` / `parser.cpp`.
- El parser no es LL(1) estricto en el caso de `(`, porque puede iniciar una expresión
  aritmética o una condición agrupada; usa retroceso controlado en ese punto. La decisión
  y los ejemplos están documentados en [grammar.md](grammar.md).
- La entrada debe contener exactamente una consulta terminada en `;` y EOF.
