# src/semantic — Analizador semántico, tabla de símbolos y AST

Tercer módulo de la Fase 2 (Práctica 6). Opera sobre el flujo que reconoce el parser
LL(1) (`src/parser`): construye un **AST tipado** mediante Traducción Dirigida por la
Sintaxis y lo valida contra un **catálogo de metadatos** (tabla de símbolos). No accede a
disco ni a datos reales: solo metadatos del esquema. El diseño del SDT y del AST está en
[SDT.md](SDT.md).

## Componentes

- **`catalog.hpp` / `catalog.cpp`** — Tabla de símbolos: `DataType`
  (INT/FLOAT/VARCHAR/BOOLEAN), `Column`, `Table` y `Catalog` (consulta O(1) por
  `unordered_map`). Carga desde CSV (`loadCsv`) o esquema en memoria (`defaultSchema`).
- **`ast.hpp`** — Nodos del AST decorado (`AstNode`, `ProjItem`, `SelectStmt`).
- **`ast_builder.hpp` / `ast_builder.cpp`** — `buildAst()`: construye el AST a partir de
  la derivación LL(1) (SDT).
- **`semantic.hpp` / `semantic.cpp`** — `analyze()`: existencia de tabla, resolución de
  columnas, expansión de `*`, duplicados y verificación de tipos en WHERE;
  `printDecoratedAst()` imprime el árbol con los tipos inferidos.
- **`semantic_cli.cpp`** — ejecutable `semantic`.
- **`schema.csv`** — catálogo de prueba (3 tablas).

## Uso del CLI

```sh
cmake --build build --target semantic

# catalogo por defecto en memoria (estudiantes, cursos, matriculas)
echo "SELECT nombre, edad FROM estudiantes WHERE edad > 18 AND activo;" | ./build/semantic
echo "SELECT * FROM estudiantes;" | ./build/semantic          # expande *
./build/semantic consulta.sql                                 # desde archivo
./build/semantic --schema src/semantic/schema.csv consulta.sql  # catalogo desde CSV
```

Para una consulta válida imprime `CONSULTA ACEPTADA` y el **AST decorado**; para una
inválida, `CONSULTA RECHAZADA` con los errores (léxico → sintáctico → semántico) y su
línea/columna. Las columnas duplicadas en la proyección se reportan como advertencia sin
invalidar la consulta.

## Reglas semánticas

Resumen en [SDT.md](SDT.md) §3. En WHERE: `Int`/`Float` son numéricos con promoción;
`= <>` exigen tipos compatibles; `< > <= >=` solo numéricos; `AND`/`OR` solo `BOOLEAN`;
la condición completa debe ser `BOOLEAN`.

## Pruebas

- `tests/test_catalog.cpp` (objetivo CTest `catalog`) — tabla de símbolos.
- `tests/test_ast.cpp` (`ast`) — construcción del AST por SDT (precedencia/asociatividad).
- `tests/test_semantic.cpp` (`semantic`) — válidas, entidad inexistente, tipos
  incompatibles, promoción numérica y duplicados.

```sh
cmake --build build && ctest --test-dir build
```
