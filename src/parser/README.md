# src/parser — Analizador sintáctico predictivo LL(1) (Práctica 5)

Este módulo implementa el parser LL(1) de la Fase 2. Consume directamente la salida
del analizador léxico (`src/lexer`): la secuencia de `sql::Token` que produce
`sql::Lexer::tokenize()` dentro de `LexResult`.

Componentes previstos (añadir cada `.cpp` explícitamente a `phase1_core` en el
`CMakeLists.txt` de la raíz — nunca GLOB):

- Gramática CFG del subconjunto SQL (SELECT / FROM / WHERE), sin recursión izquierda
  y factorizada a la izquierda.
- Conjuntos FIRST y FOLLOW.
- Tabla de parseo LL(1).
- Motor de reconocimiento basado en pila, con reporte preciso de errores sintácticos.
