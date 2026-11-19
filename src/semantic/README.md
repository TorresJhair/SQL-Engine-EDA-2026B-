# Analizador semántico, catálogo y AST

Este módulo implementa la Práctica 6 de la Fase 2. Valida consultas `SELECT` contra un
catálogo de metadatos. Se integra con el lexer de `src/lexer/` y el parser predictivo LL(1)
de `src/parser_ll1/`.

## Resumen del informe

- **Análisis de la consulta:** el lexer produce tokens y el parser LL(1) valida la sintaxis.
- **Construcción del AST:** `buildAst()` aplica las reglas SDT a la derivación y los tokens
  reconocidos por el parser.
- **Tabla de símbolos:** el informe explica la carga desde el esquema predeterminado o un
  archivo CSV, y cómo se organizan tablas y columnas en `Catalog`.
- **Validación semántica:** `analyze()` resuelve tablas y columnas, expande `SELECT *` e
  infiere y comprueba tipos en `WHERE`.
- **Diagnósticos:** se describen errores con causa y posición, además de las advertencias
  por columnas duplicadas.
- **Diagramas y pruebas:** se presentan la arquitectura, el catálogo, el SDT, el AST y los
  casos y resultados de prueba.
- **Alcance:** el análisis usa metadatos.

## Flujo de procesamiento

1. `semantic_cli.cpp` carga el catálogo: usa `defaultSchema()` si no se indica un esquema,
   o `loadCsv()` si se proporciona `--schema <ruta>`.
2. El lexer convierte el SQL en tokens y el parser LL(1) valida la sintaxis y registra la
   derivación y los tokens reconocidos. Si una de esas etapas falla, el proceso se detiene.
3. `buildAst()` recorre la derivación y los tokens para construir `SelectStmt` y los nodos
   del AST. Los tipos empiezan como `Unknown`.
4. `analyze()` resuelve tablas y columnas contra el catálogo, expande `SELECT *`, verifica
   los tipos de `WHERE` y decora el AST. El CLI imprime el árbol si no hay errores; las
   columnas duplicadas se notifican como advertencias.

## Catálogo (tabla de símbolos)

`Catalog` almacena tablas en un `unordered_map`. Cada `Table` conserva las columnas en un
vector ordinal y un índice `by_name` para buscarlas por nombre. `Catalog::addColumn()` crea
la tabla cuando registra su primera columna, asigna el ordinal y mantiene ambos índices.
Las búsquedas de tabla y columna tienen costo promedio O(1).

El esquema de prueba de `defaultSchema()` está definido en código e incluye `estudiantes`,
`cursos` y `matriculas`. También se puede cargar otro esquema desde un CSV con filas
`tabla,columna,tipo[,pk]`; véase [`schema.csv`](schema.csv). Se soportan `INT`, `FLOAT`,
`VARCHAR` y `BOOLEAN`.

## Componentes

- `catalog.hpp` / `catalog.cpp`: tipos, metadatos, búsquedas y carga del esquema.
- `ast.hpp`: `SelectStmt`, `ProjItem` y `AstNode`.
- `ast_builder.hpp` / `ast_builder.cpp`: `buildAst()` y construcción del AST según el SDT.
- `semantic.hpp` / `semantic.cpp`: resolución de nombres, expansión de `*`, inferencia,
  diagnósticos e impresión del AST decorado.
- `semantic_cli.cpp`: integración del lexer, parser, catálogo y analizador.

## Compilar y ejecutar

Desde la raíz del repositorio:

```sh
cmake --build build --target semantic

# Usa el catálogo predeterminado definido en código.
echo "SELECT nombre, edad FROM estudiantes WHERE edad > 18 AND activo;" | ./build/semantic

# Expande * usando el orden de columnas del catálogo.
echo "SELECT * FROM estudiantes;" | ./build/semantic

# Lee el esquema desde un CSV y la consulta desde un archivo.
./build/semantic --schema src/semantic/schema.csv consulta.sql
```

El CLI acepta SQL desde un archivo o desde la entrada estándar. Si una consulta falla,
informa el tipo del diagnóstico y su línea y columna. Para ver las opciones:

```sh
./build/semantic --help
```

## Documentación y diagramas

- [`SDT.md`](SDT.md): atributos y ecuaciones semánticas, correspondencia con `buildAst()` y
  diseño de clases del AST.
- [`INFORME.md`](INFORME.md): informe de la práctica, reglas semánticas, resultados y
  capturas sugeridas.
- [`LAB_06_Analizador_Semantico_AST_EDA.pdf`](LAB_06_Analizador_Semantico_AST_EDA.pdf):
  informe PDF final.
- [`figures/arquitectura.png`](figures/arquitectura.png): flujo desde lexer hasta análisis
  semántico.
- [`figures/catalogo.png`](figures/catalogo.png): carga e indexación de la tabla de
  símbolos.
- [`figures/sdt.png`](figures/sdt.png) y [`figures/ast_clases.png`](figures/ast_clases.png):
  construcción del AST y estructura de sus clases.

## Pruebas

Las suites específicas del módulo son `catalog`, `ast` y `semantic`. Para compilar y
ejecutar todas las suites CTest desde la raíz:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```
