# src/parser — Analizador sintáctico predictivo LL(1)

Implementa el **analizador sintáctico predictivo no recursivo LL(1)** de la
Práctica 5. Consume directamente la salida del analizador léxico (`src/lexer`):
la secuencia de `sql::Token` que produce `sql::Lexer::tokenize()` dentro de
`LexResult`. Reconoce una consulta `SELECT` (la **acepta o la rechaza**) y emite
diagnósticos con posición y una traza paso a paso de la pila. No construye AST.

## Componentes

- **`grammar.hpp` / `grammar.cpp`** — gramática LL(1): lista de producciones,
  cálculo de `nullable`, `FIRST`, `FOLLOW` y construcción de la tabla `M[A, a]`
  con detección de conflictos. La especificación formal está en
  [grammar.md](grammar.md).
- **`parser.hpp` / `parser.cpp`** — motor de análisis basado en una **pila
  explícita**: empuja `$` y el símbolo inicial, y en cada paso empareja un
  terminal o expande un no terminal según la celda de la tabla. Devuelve
  `ParseResult { accepted, errors, trace }`.
- **`parser_cli.cpp`** — ejecutable `parser` para probar consultas.

## Uso del CLI

```sh
# compilar
cmake --build build --target parser

# aceptar/rechazar una consulta mostrando la traza de la pila
echo "SELECT a, b FROM t WHERE a = 1 AND (b > 2 OR c <= 3);" | ./build/parser
./build/parser consulta.sql        # desde archivo
./build/parser --quiet consulta.sql  # sin traza

# volcar producciones, FIRST/FOLLOW y la tabla LL(1)
./build/parser --grammar
```

La traza imprime, por paso, el contenido de la pila (tope a la derecha), la
entrada restante y la acción (`expandir (n) A → …` o `emparejar X`). Al final
indica `CONSULTA ACEPTADA` o `CONSULTA RECHAZADA` con el error sintáctico/léxico,
su línea, columna y los tokens esperados.

## Manejo de errores

- **Celda vacía** `M[A, a]`: token inesperado al expandir un no terminal; el error
  reporta el conjunto de tokens esperados en ese estado.
- **Desajuste de terminal**: el tope de la pila no coincide con el token actual.
- Los **errores léxicos** se propagan como fallo del parseo (`lexical = true`).
- Ante cualquier error el analizador se detiene y reporta sin colapsar.

## Pruebas

`tests/test_parser.cpp` (objetivo CTest `parser`) comprueba que la gramática es
LL(1) sin conflictos, y acepta/rechaza una batería de consultas válidas e
inválidas verificando la posición del error.

```sh
cmake --build build --target test_parser && ./build/test_parser
```
