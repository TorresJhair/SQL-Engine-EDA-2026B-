# Informe técnico — Práctica 6: Analizador Semántico, Tabla de Símbolos y AST

> Documento base para redactar el Informe PDF del Lab 6. Reúne contexto, arquitectura,
> decisiones de diseño, el esquema SDT, las reglas semánticas, ejemplos de ejecución
> reales y los resultados de pruebas. Detalle formal del SDT y el diagrama de clases en
> [SDT.md](SDT.md); uso del módulo en [README.md](README.md).

## 1. Contexto y objetivo

Tercera parte de la Fase 2 del motor de base de datos para sentencias `SELECT`. Tras el
análisis léxico (P4) y el sintáctico predictivo LL(1) (P5), este módulo **verifica la
coherencia lógica** de la consulta contra el esquema de la base de datos **antes** de
cualquier ejecución. No se lee disco ni datos reales: se opera solo sobre **metadatos**.

Objetivos cumplidos:
- Una **Tabla de Símbolos / Catálogo** con metadatos de tablas, columnas y tipos.
- Construcción de un **AST tipado** mediante **Traducción Dirigida por la Sintaxis (SDT)**
  acoplada a las producciones LL(1) del Lab 5.
- Un **analizador semántico** que valida existencia de entidades, alcance de
  identificadores y compatibilidad de tipos, con reporte de errores preciso.

## 2. Arquitectura y flujo

```
  texto SQL
     │  Lexer (P4)            src/lexer
     ▼
  tokens ──► Parser LL(1) (P5)  src/parser   ──► derivación por la izquierda
                                                 (ids de producción + tokens)
     │                                                │
     │  (si no hay error léxico ni sintáctico)        ▼
     │                               buildAst()  src/semantic/ast_builder
     ▼                                                │
  AST  ◄───────────────────────────────────────────┘
     │  analyze(AST, Catálogo)   src/semantic/semantic
     ▼
  AST decorado con tipos  +  diagnósticos (errores / advertencias)
```

Regla de integración respetada: **el análisis semántico no inicia si la consulta tiene
errores léxicos o sintácticos**, y ningún error provoca un crash.

## 3. Componentes (código en `src/semantic/`)

| Archivo | Rol |
|---|---|
| `catalog.hpp/.cpp` | **Tabla de Símbolos**: `DataType {INT,FLOAT,VARCHAR,BOOLEAN}`, `Column` (nombre, ordinal, tipo, PK), `Table` (columnas + índice por nombre), `Catalog` (tablas en `unordered_map`, consulta O(1)). Carga desde CSV o esquema en memoria. |
| `ast.hpp` | Nodos del **AST decorado**: `AstNode` (con tipo inferido), `ProjItem`, `SelectStmt`. |
| `ast_builder.hpp/.cpp` | **SDT**: `buildAst()` construye el AST recorriendo la derivación LL(1). |
| `semantic.hpp/.cpp` | **Analizador semántico** `analyze()` + impresión del AST decorado. |
| `semantic_cli.cpp` | Ejecutable `semantic` (lex → parse → semántica). |
| `schema.csv` | Catálogo de prueba (3 tablas). |

Cambio mínimo en el parser (P5): `ParseResult` ahora expone `derivation` (ids de
producción) y `matched` (tokens emparejados) — dos `push_back` en el motor, sin romper las
pruebas sintácticas previas.

## 4. Tabla de Símbolos (esquema de prueba)

Esquema por defecto en memoria (y en `schema.csv`), 3 tablas con tipos variados:

| Tabla | Columnas (tipo) |
|---|---|
| `estudiantes` | id (INT, PK), nombre (VARCHAR), edad (INT), promedio (FLOAT), activo (BOOLEAN) |
| `cursos` | id (INT, PK), titulo (VARCHAR), creditos (INT) |
| `matriculas` | id (INT, PK), estudiante_id (INT), curso_id (INT), nota (FLOAT) |

Formato CSV: `tabla,columna,tipo[,pk]`. El ordinal de cada columna es su orden de
aparición. Consulta O(1) promedio por tabla y por columna.

## 5. Construcción del AST por SDT

El parser LL(1) genera una derivación por la izquierda. `buildAst` tiene una función por
no terminal que consume esa derivación y **sintetiza** el subárbol; las colas de las
listas/operadores (`disy_tail`, `conj_tail`, `suma_tail`, `prod_tail`) reciben el subárbol
izquierdo como atributo **heredado** para lograr **asociatividad por la izquierda**. Los
paréntesis de `primario → ( condicion )` no crean nodo. Cada nodo hereda la **posición
(línea/columna)** de su token. La tabla completa de atributos por producción y el diagrama
de clases del AST están en [SDT.md](SDT.md).

Precedencia resultante (de menor a mayor): `OR` < `AND` < comparación < `+ -` < `* /` <
negación unaria.

## 6. Reglas de validación semántica

1. **Existencia de tabla (FROM)**: la tabla debe estar en el catálogo.
2. **Resolución de identificadores (SELECT / WHERE)**: toda columna debe pertenecer a la
   tabla del FROM.
3. **Expansión de `*`**: `SELECT *` se reemplaza por la lista **ordenada por ordinal** de
   columnas, cada una con su tipo.
4. **Duplicados en proyección**: **advertencia** (política del equipo: se advierte, la
   consulta sigue válida).
5. **Tipos en WHERE** (`Int`/`Float` numéricos, con promoción entre ellos):
   - `-` unario y `+ - * /`: operandos numéricos; resultado `Float` si alguno es `Float`.
   - `= <>`: tipos compatibles (numérico-numérico, o `Varchar`-`Varchar`, o
     `Boolean`-`Boolean`). Resultado `Boolean`.
   - `< > <= >=`: solo numérico-numérico. Resultado `Boolean`.
   - `AND` / `OR`: ambos operandos `Boolean`. Resultado `Boolean`.
   - La condición completa del WHERE debe evaluar a `Boolean`.

Cada violación genera un diagnóstico con **mensaje, línea y columna**.

## 7. Ejemplos de ejecución (salida real del CLI)

### 7.1. Consulta válida compleja → AST decorado
```
$ echo "SELECT nombre, promedio FROM estudiantes WHERE (edad >= 18 OR activo) AND promedio > 3.5;" | ./build/semantic
CONSULTA ACEPTADA. AST decorado:

SelectStmt
  proyeccion:
    nombre [VARCHAR]
    promedio [FLOAT]
  from: estudiantes
  where:
    Binary AND [BOOLEAN]
      Binary OR [BOOLEAN]
        Binary >= [BOOLEAN]
          Column edad [INT]
          IntLit 18 [INT]
        Column activo [BOOLEAN]
      Binary > [BOOLEAN]
        Column promedio [FLOAT]
        FloatLit 3.5 [FLOAT]
```

### 7.2. Expansión de `SELECT *`
```
$ echo "SELECT * FROM estudiantes;" | ./build/semantic
SelectStmt
  proyeccion (SELECT * expandido):
    id [INT]
    nombre [VARCHAR]
    edad [INT]
    promedio [FLOAT]
    activo [BOOLEAN]
  from: estudiantes
  where: (ninguno)
```

### 7.3. Error de entidad inexistente
```
$ echo "SELECT id FROM noexiste;" | ./build/semantic
[ERROR SEMANTICO] linea 1, columna 16: la tabla 'noexiste' no existe en el catalogo
CONSULTA RECHAZADA (semantica).

$ echo "SELECT x FROM estudiantes;" | ./build/semantic
[ERROR SEMANTICO] linea 1, columna 8: la columna 'x' no pertenece a la tabla 'estudiantes'
CONSULTA RECHAZADA (semantica).
```

### 7.4. Error de incompatibilidad de tipos
```
$ echo "SELECT id FROM estudiantes WHERE nombre > 100;" | ./build/semantic
[ERROR SEMANTICO] linea 1, columna 41: el orden '>' requiere operandos numericos, son VARCHAR y INT
CONSULTA RECHAZADA (semantica).

$ echo "SELECT id FROM estudiantes WHERE edad = 'veinte';" | ./build/semantic
[ERROR SEMANTICO] linea 1, columna 39: no se puede comparar INT con VARCHAR usando '='
CONSULTA RECHAZADA (semantica).
```

### 7.5. Advertencia por columna duplicada (sigue válida)
```
$ echo "SELECT id, id FROM cursos;" | ./build/semantic
[ADVERTENCIA SEMANTICA] linea 1, columna 12: columna 'id' repetida en la proyeccion
CONSULTA ACEPTADA. AST decorado:
...
```

## 8. Batería de pruebas

Tres suites CTest, todas en verde (parte del total de 16/16 del repositorio):

| Suite | Archivo | Checks | Cubre |
|---|---|---|---|
| `catalog` | `tests/test_catalog.cpp` | 16 | tabla de símbolos, carga CSV, tipos, ordinal, PK |
| `ast` | `tests/test_ast.cpp` | 11 | construcción del AST por SDT, precedencia, asociatividad, `*`, menos unario |
| `semantic` | `tests/test_semantic.cpp` | 17 | válidas, tabla/columna inexistente, tipos incompatibles, promoción INT/FLOAT, duplicados |

```sh
cmake --build build && ctest --test-dir build
# ... 100% tests passed, 0 tests failed out of 16
```

Casos mínimos exigidos por el enunciado y dónde se demuestran: consultas válidas
(§7.1–7.2, suite `semantic`), entidad inexistente (§7.3), incompatibilidad de tipos
(§7.4), impresión del AST decorado (§7.1–7.2).

## 9. Cómo compilar y ejecutar

```sh
cmake -S . -B build && cmake --build build
echo "SELECT nombre FROM estudiantes WHERE edad > 18;" | ./build/semantic
./build/semantic consulta.sql                                  # desde archivo
./build/semantic --schema src/semantic/schema.csv consulta.sql # catálogo desde CSV
```

## 10. Decisiones de diseño y límites

- **Módulo en `src/semantic/`** (coherente con `src/lexer` y `src/parser`); el enunciado
  menciona `/semantic_analyzer`.
- **Política de duplicados = advertencia** (no rechaza); documentada aquí y en SDT.md.
- **Catálogo**: CSV + esquema en memoria, sin dependencias externas (no JSON).
- **Alcance**: solo metadatos; no se accede a páginas de datos ni se ejecuta la consulta.
- El AST es un recognizer + decorador de tipos; no evalúa valores.
