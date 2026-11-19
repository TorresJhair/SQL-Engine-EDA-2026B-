# Universidad Nacional de San Agustín de Arequipa
## Facultad de Ingeniería de Producción y Servicios
### Escuela Profesional de Ciencia de la Computación

---

**Curso:** Estructuras de Datos Avanzadas  
**Semestre:** 2026-B  
**Práctica de Laboratorio 5:** Analizador Sintáctico Predictivo LL(1) SQL  
**Fase:** Fase 2 — Parte 2 (Analizador Sintáctico)  
**Modalidad:** Grupal  
**Puntaje:** 20.0 puntos  

---

# INFORME TÉCNICO DE LABORATORIO: ANALIZADOR SINTÁCTICO PREDICTIVO LL(1)

---

## 1. Introducción y Contexto General

El presente proyecto práctico forma parte del desarrollo progresivo de un motor de base de datos relacional orientado a consultas de tipo `SELECT`.

En la fase anterior (Práctica 4), se diseñó e implementó el **analizador léxico** (`sql::Lexer`), encargado de reconocer la secuencia ordenada de componentes léxicos (`sql::Token`) y clasificar lexemas inválidos sin colapsar. La presente Práctica 5 se enfoca en el diseño formal e implementación en **C++17** de un **analizador sintáctico predictivo no recursivo LL(1)** (`sql::Parser`).

Este componente interactúa de forma no destructiva con el módulo léxico, consumiendo directamente su salida (`sql::LexResult`). Su propósito es determinar de forma unívoca si la sentencia cumple con la Gramática Libre de Contexto (CFG) formalizada por el equipo, simulando el proceso de derivación sintáctica mediante una pila explícita y la matriz de análisis $M[A, a]$, generando diagnósticos con ubicación precisa ante cualquier error sintáctico o léxico.

---

## 2. Arquitectura del Proyecto (Estructura de Archivos y Carpetas)

El proyecto está organizado de manera modular en **C++17**, estructurado para soportar el desarrollo progresivo de un motor de base de datos relacional (almacenamiento, indexación y front-end de procesamiento de consultas SQL):

```text
/ (Raíz del proyecto)
├── CMakeLists.txt                # Configuración global del sistema de construcción CMake y suites de CTest
├── README.md                     # Documentación general del repositorio, guías de compilación y ejecución
├── src/                          # Código fuente del motor de base de datos y front-end SQL
│   ├── storage/                  # Fase 1: Motor de almacenamiento físico y páginas ranuradas (Slotted Page)
│   ├── index/                    # Fase 1: Implementación del índice B+ Tree sobre disco
│   ├── lexer/                    # Fase 2.1: Analizador léxico SQL y modelo de tokens (Práctica 4)
│   ├── parser_ll1/               # Fase 2.2: Analizador sintáctico predictivo LL(1) no recursivo (Práctica 5)
│   ├── common/                   # Definiciones transversales, tipos y utilidades del sistema
│   ├── bench/                    # Benchmarks y métricas de rendimiento
│   └── main.cpp                  # Punto de entrada principal interactivo de la base de datos
└── tests/                        # Batería de pruebas unitarias y de integración automatizadas
```

### 2.1. Descripción de los Componentes Principales

1. **`src/` (Módulos del Motor y Front-End SQL):**
   - **`src/storage/`:** Implementa la capa de almacenamiento en disco: `SlottedPage` (gestión de registros de longitud variable en páginas de 4096 bytes), `PageManager` (paginación y control de archivos binarios), `Tuple` / `Value` (representación en memoria y serialización de tipos `INT`, `DOUBLE`, `VARCHAR`), y `HeapFile` (organización de registros desordenados con free-space map).
   - **`src/index/`:** Implementa el índice B+ Tree persistente (`BTree`, `BTreeNode`), ofreciendo búsquedas exactas e index scans en tiempo logarítmico $O(\log n)$ con manejo de splits de nodos y páginas en disco.
   - **`src/lexer/`:** Analizador léxico de consultas SQL (`lexer.cpp`, `token.cpp`, `lexer_cli.cpp`). Convierte la cadena SQL de entrada en un flujo estructurado de `sql::Token` (palabras reservadas, identificadores, literales, operadores, delimitadores y fin de archivo), registrando errores léxicos sin colapsar el programa.
   - **`src/parser_ll1/`:** Analizador sintáctico predictivo LL(1) no recursivo (`grammar.cpp`, `parser.cpp`, `parser_cli.cpp`). Contiene la definición formal de la gramática CFG en C++, la construcción automática de conjuntos FIRST, FOLLOW y la tabla $M[A, a]$, el algoritmo iterativo de pila de derivación, la CLI interactiva/por archivo y la suite de consultas de prueba (`examples/`).
   - **`src/common/` y `src/bench/`:** Estructuras de soporte (constantes, tipos de datos) y herramientas para medición de latencias y throughput.
   - **`src/main.cpp`:** Ejecutable principal del motor de base de datos integrando las diferentes capas.

2. **`tests/` (Suite de Pruebas Automatizadas):**
   - Contiene las suites de pruebas unitarias desarrolladas para verificar la corrección funcional y la preservación de invariantes de cada módulo:
     - `test_storage.cpp`: Pruebas de slotted page, serialización de tuplas y heap files.
     - `test_btree.cpp`: Invariantes del árbol B+, inserciones masivas, splits y recorridos ordenados.
     - `test_lexer.cpp` y `test_token.cpp`: Cobertura completa del analizador léxico, números válidos/inválidos, strings y palabras clave case-insensitive.
     - `test_parser.cpp`: Batería exhaustiva de pruebas sintácticas (consultas válidas, sentencias desordenadas, omisión de tabla/columnas, ausencia de `;`, errores léxicos integrados y validación de invariantes de la tabla LL(1)).

3. **`CMakeLists.txt` (Configuración Unificada de Construcción):**
   - Define los estándares de compilación en **C++17** con flags de optimización y advertencia estrictos (`-Wall -Wextra -Wpedantic`).
   - Compila la biblioteca estática `phase1_core` (almacenamiento e índice), los ejecutables modulares (`main`, `lexer`, `parser`) y registra las **13 suites de prueba** ejecutables de forma reproducible mediante `ctest`.

4. **`README.md` (Documentación del Repositorio):**
   - Manual integral del proyecto que detalla la arquitectura global, requisitos del entorno, pasos de configuración con CMake, comandos de compilación y ejecución de cada ejecutable (`main`, `lexer`, `parser`), y la guía para correr los tests unitarios.

---

## 3. Alcance del Subconjunto SQL Soportado

El subconjunto del lenguaje SQL abarca las sentencias fundamentales para consultas relacionales `SELECT`, integrando además expresiones de comparación, operadores lógicos y aritmética:

1. **Cláusula `SELECT`:**
   - Proyección total mediante el comodín `*`.
   - Proyección explícita mediante listas de uno o más identificadores de columnas separados por comas (ej. `SELECT nombre, edad, salario`).
2. **Cláusula `FROM`:**
   - Declaración de la tabla origen mediante un único `IDENTIFIER`.
3. **Cláusula `WHERE` (opcional):**
   - Comparaciones relacionales completas: `=`, `<>`, `!=`, `<`, `>`, `<=`, `>=`.
   - Operadores lógicos booleanos: `AND` y `OR`.
   - Soporte para literales tipados: enteros (`INTEGER_LITERAL`), decimales de punto fijo y notación científica (`DECIMAL_LITERAL`), y cadenas de texto delimitadas por comillas simples (`STRING_LITERAL`) con soporte para comillas escapadas `''`.
   - Soporte para expresiones aritméticas binarias (`+`, `-`, `*`, `/`) y operador unario de negación (`-`).
   - Agrupación explícita mediante paréntesis `(` y `)`.
4. **Delimitador explícito de fin de sentencia:**
   - Exigencia estricta del delimitador `;` al final de la consulta, seguido inmediatamente por el fin de archivo `$` (`END_OF_FILE`).

---

## 4. Modelo de Tokens (Lexer)

El analizador sintáctico consume de forma secuencial y no destructiva los tokens emitidos por `sql::Lexer` (Práctica 4). A continuación se formaliza el modelo de tokens completo implementado en `src/lexer/token.hpp`:

| Categoría | Tipos (`TokenType`) | Lexemas reconocidos |
|---|---|---|
| **Palabras reservadas** | `KEYWORD_SELECT`, `KEYWORD_FROM`, `KEYWORD_WHERE`, `KEYWORD_AND`, `KEYWORD_OR` | `SELECT`, `FROM`, `WHERE`, `AND`, `OR` (sin distinguir mayúsculas ni minúsculas) |
| **Identificadores y literales** | `IDENTIFIER`, `INTEGER_LITERAL`, `DECIMAL_LITERAL`, `STRING_LITERAL` | • `IDENTIFIER`: Nombres de tablas y columnas formados por letras ASCII, dígitos y guion bajo (`[a-zA-Z_][a-zA-Z0-9_]*`), rechazando palabras con caracteres no ASCII.<br>• `INTEGER_LITERAL`: Secuencias de dígitos decimales (ej. `42`, `007`).<br>• `DECIMAL_LITERAL`: Números con punto fraccionario y/o notación científica con signo de exponente (ej. `3.14`, `1e5`, `1e-10`, `2.5E+3`).<br>• `STRING_LITERAL`: Cadenas entre comillas simples con soporte de comilla escapada mediante `''` (ej. `'hola mundo'`, `'it''s'`). |
| **Operadores relacionales** | `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL` | `=`, `<>`, `!=`, `<`, `>`, `<=`, `>=` |
| **Operadores aritméticos** | `PLUS`, `MINUS`, `SLASH`, `STAR` | `+`, `-`, `/`, `*` (como operador aritmético de multiplicación dentro de expresiones) |
| **Delimitadores y puntuación** | `COMMA`, `SEMICOLON`, `STAR`, `LPAREN`, `RPAREN` | `,`, `;`, `*` (como comodín de proyección total en `SELECT`), `(`, `)` |
| **Fin de entrada** | `END_OF_FILE` | Fin de entrada / marcador centinela `$` |

---

## 5. Formalización de la Gramática Libre de Contexto (CFG)

Para construir un analizador sintáctico predictivo LL(1) determinista, la gramática debe satisfacer tres restricciones teóricas obligatorias:
1. **Ausencia de recursión a la izquierda** (directa o indirecta), evitando bucles infinitos en el algoritmo de expansión.
2. **Factorización a la izquierda**, eliminando prefijos comunes para evitar ambigüedades en la elección de producciones.
3. **Condición LL(1) estricta:** Para cualquier no terminal $A$ con alternativas $A \to \alpha_1 \mid \alpha_2 \mid \dots \mid \alpha_n$, sus conjuntos de selección deben ser mutuamente disjuntos dos a dos.

### 5.1. Eliminación de Recursión Izquierda y Factorización

- **Secuencias repetitivas:** Las listas y operadores asociativos por la izquierda se transformaron en **recursiones derechas** mediante símbolos de cola que derivan en $\varepsilon$ (`cols_tail`, `disy_tail`, `conj_tail`, `comp_tail`, `suma_tail`, `prod_tail`).
- **Factorización de la proyección:** Se factorizó la proyección aislando `STAR` (`*`) frente a `lista_cols`.
- **Determinismo del paréntesis `(`:** En SQL clásico, el token `(` introduce ambigüedad al poder iniciar una condición lógica agrupada `(a = 1 OR b = 2)` o una expresión aritmética `(a + 1) > 2`. Para mantener determinismo LL(1) puro sin requerir retroceso (*backtracking*), la gramática unifica la jerarquía haciendo que `primario → LPAREN condicion RPAREN` descienda a través de la cascada completa de precedencias:
  $$\text{Disy (OR)} \to \text{Conj (AND)} \to \text{Comp} \to \text{Suma} \to \text{Prod} \to \text{Unario} \to \text{Primario}$$
  Esto permite que `(a + 1)` descienda de forma natural hasta `suma`, resolviendo la ambigüedad y manteniendo la condición LL(1).

### 5.2. Gramática Formal en Notación BNF (39 Producciones)

Símbolo inicial: `consulta`. El símbolo `$` representa `END_OF_FILE` y $\varepsilon$ la producción vacía.

```text
( 1) consulta    → KEYWORD_SELECT proyeccion KEYWORD_FROM IDENTIFIER where_opt SEMICOLON
( 2) proyeccion  → STAR
( 3) proyeccion  → lista_cols
( 4) lista_cols  → IDENTIFIER cols_tail
( 5) cols_tail   → COMMA IDENTIFIER cols_tail
( 6) cols_tail   → ε
( 7) where_opt   → KEYWORD_WHERE condicion
( 8) where_opt   → ε
( 9) condicion   → disy
(10) disy        → conj disy_tail
(11) disy_tail   → KEYWORD_OR conj disy_tail
(12) disy_tail   → ε
(13) conj        → comp conj_tail
(14) conj_tail   → KEYWORD_AND comp conj_tail
(15) conj_tail   → ε
(16) comp        → suma comp_tail
(17) comp_tail   → op_comp suma
(18) comp_tail   → ε
(19) op_comp     → EQUAL
(20) op_comp     → NOT_EQUAL
(21) op_comp     → LESS
(22) op_comp     → GREATER
(23) op_comp     → LESS_EQUAL
(24) op_comp     → GREATER_EQUAL
(25) suma        → prod suma_tail
(26) suma_tail   → PLUS prod suma_tail
(27) suma_tail   → MINUS prod suma_tail
(28) suma_tail   → ε
(29) prod        → unario prod_tail
(30) prod_tail   → STAR unario prod_tail
(31) prod_tail   → SLASH unario prod_tail
(32) prod_tail   → ε
(33) unario      → MINUS unario
(34) unario      → primario
(35) primario    → IDENTIFIER
(36) primario    → INTEGER_LITERAL
(37) primario    → DECIMAL_LITERAL
(38) primario    → STRING_LITERAL
(39) primario    → LPAREN condicion RPAREN
```

---

## 6. Cálculo de Conjuntos FIRST y FOLLOW

Los conjuntos $\text{FIRST}$ y $\text{FOLLOW}$ fueron calculados algorítmicamente mediante el método de punto fijo implementado en `Grammar::computeFirst()` y `Grammar::computeFollow()` (`src/parser_ll1/grammar.cpp`).

### 6.1. Definiciones Matemáticas

- **$\text{FIRST}(\alpha)$:** Conjunto de terminales que inician las cadenas derivadas de $\alpha$. Si $\alpha \Rightarrow^* \varepsilon$, entonces $\varepsilon \in \text{FIRST}(\alpha)$.
- **$\text{FOLLOW}(A)$:** Conjunto de terminales que pueden aparecer inmediatamente a la derecha de $A$ en alguna forma sentencial derivada del símbolo inicial `consulta`. Para el símbolo inicial, $\$ \in \text{FOLLOW}(\text{consulta})$.

### 6.2. Tabla de Conjuntos FIRST y FOLLOW por No Terminal

| No Terminal ($A$) | Anulable ($\varepsilon$) | Conjunto $\text{FIRST}(A)$ | Conjunto $\text{FOLLOW}(A)$ |
|---|:---:|---|---|
| `consulta` | No | `{ SELECT }` | `{ $ }` |
| `proyeccion` | No | `{ STAR, IDENTIFIER }` | `{ FROM }` |
| `lista_cols` | No | `{ IDENTIFIER }` | `{ FROM }` |
| `cols_tail` | **Sí** | `{ COMMA, ε }` | `{ FROM }` |
| `where_opt` | **Sí** | `{ WHERE, ε }` | `{ SEMICOLON }` |
| `condicion` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ SEMICOLON, RPAREN }` |
| `disy` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ SEMICOLON, RPAREN }` |
| `disy_tail` | **Sí** | `{ OR, ε }` | `{ SEMICOLON, RPAREN }` |
| `conj` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ OR, SEMICOLON, RPAREN }` |
| `conj_tail` | **Sí** | `{ AND, ε }` | `{ OR, SEMICOLON, RPAREN }` |
| `comp` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ AND, OR, SEMICOLON, RPAREN }` |
| `comp_tail` | **Sí** | `{ EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL, ε }` | `{ AND, OR, SEMICOLON, RPAREN }` |
| `op_comp` | No | `{ EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL }` | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` |
| `suma` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ op_comp, AND, OR, SEMICOLON, RPAREN }` |
| `suma_tail` | **Sí** | `{ PLUS, MINUS, ε }` | `{ op_comp, AND, OR, SEMICOLON, RPAREN }` |
| `prod` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN }` |
| `prod_tail` | **Sí** | `{ STAR, SLASH, ε }` | `{ PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN }` |
| `unario` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN }` | `{ STAR, SLASH, PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN }` |
| `primario` | No | `{ IDENTIFIER, INTEGER, DECIMAL, STRING, LPAREN }` | `{ STAR, SLASH, PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN }` |

*(Donde `op_comp` representa `{ EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL }`).*

---

## 7. Demostración de la Condición LL(1) Estricta

Para que una gramática sea analizable mediante un autómata predictivo LL(1), el **conjunto de selección** de cada producción $A \to \alpha$, definido como:
$$\text{SELECT}(A \to \alpha) = \begin{cases} \text{FIRST}(\alpha) & \text{si } \alpha \not\Rightarrow^* \varepsilon \\ (\text{FIRST}(\alpha) \setminus \{\varepsilon\}) \cup \text{FOLLOW}(A) & \text{si } \alpha \Rightarrow^* \varepsilon \end{cases}$$
debe ser disjunto frente a las producciones alternativas del mismo no terminal.

### 7.1. Desglose de Conjuntos de Selección por No Terminal

1. **`proyeccion`:**
   - $\text{SELECT}(2: \text{proyeccion} \to \text{STAR}) = \{\text{STAR}\}$
   - $\text{SELECT}(3: \text{proyeccion} \to \text{lista\_cols}) = \{\text{IDENTIFIER}\}$
   - **Intersección:** $\{\text{STAR}\} \cap \{\text{IDENTIFIER}\} = \emptyset$ (Disjuntos).
2. **`cols_tail`:**
   - $\text{SELECT}(5: \text{cols\_tail} \to \text{COMMA \dots}) = \{\text{COMMA}\}$
   - $\text{SELECT}(6: \text{cols\_tail} \to \varepsilon) = \text{FOLLOW}(\text{cols\_tail}) = \{\text{KEYWORD\_FROM}\}$
   - **Intersección:** $\{\text{COMMA}\} \cap \{\text{KEYWORD\_FROM}\} = \emptyset$ (Disjuntos).
3. **`where_opt`:**
   - $\text{SELECT}(7: \text{where\_opt} \to \text{KEYWORD\_WHERE \dots}) = \{\text{KEYWORD\_WHERE}\}$
   - $\text{SELECT}(8: \text{where\_opt} \to \varepsilon) = \text{FOLLOW}(\text{where\_opt}) = \{\text{SEMICOLON}\}$
   - **Intersección:** $\{\text{KEYWORD\_WHERE}\} \cap \{\text{SEMICOLON}\} = \emptyset$ (Disjuntos).
4. **`disy_tail`:**
   - $\text{SELECT}(11: \text{disy\_tail} \to \text{KEYWORD\_OR \dots}) = \{\text{KEYWORD\_OR}\}$
   - $\text{SELECT}(12: \text{disy\_tail} \to \varepsilon) = \text{FOLLOW}(\text{disy\_tail}) = \{\text{SEMICOLON}, \text{RPAREN}\}$
   - **Intersección:** $\{\text{KEYWORD\_OR}\} \cap \{\text{SEMICOLON}, \text{RPAREN}\} = \emptyset$ (Disjuntos).
5. **`conj_tail`:**
   - $\text{SELECT}(14: \text{conj\_tail} \to \text{KEYWORD\_AND \dots}) = \{\text{KEYWORD\_AND}\}$
   - $\text{SELECT}(15: \text{conj\_tail} \to \varepsilon) = \text{FOLLOW}(\text{conj\_tail}) = \{\text{KEYWORD\_OR}, \text{SEMICOLON}, \text{RPAREN}\}$
   - **Intersección:** $\{\text{KEYWORD\_AND}\} \cap \{\text{KEYWORD\_OR}, \text{SEMICOLON}, \text{RPAREN}\} = \emptyset$ (Disjuntos).
6. **`comp_tail`:**
   - $\text{SELECT}(17: \text{comp\_tail} \to \text{op\_comp suma}) = \{\text{EQUAL}, \text{NOT\_EQUAL}, \text{LESS}, \text{GREATER}, \text{LESS\_EQUAL}, \text{GREATER\_EQUAL}\}$
   - $\text{SELECT}(18: \text{comp\_tail} \to \varepsilon) = \{\text{KEYWORD\_AND}, \text{KEYWORD\_OR}, \text{SEMICOLON}, \text{RPAREN}\}$
   - **Intersección:** Vacía (Disjuntos).
7. **`suma_tail`:**
   - $\text{SELECT}(26: \text{suma\_tail} \to \text{PLUS \dots}) = \{\text{PLUS}\}$
   - $\text{SELECT}(27: \text{suma\_tail} \to \text{MINUS \dots}) = \{\text{MINUS}\}$
   - $\text{SELECT}(28: \text{suma\_tail} \to \varepsilon) = \text{FOLLOW}(\text{suma\_tail}) = \{\text{op\_comp}, \text{KEYWORD\_AND}, \text{KEYWORD\_OR}, \text{SEMICOLON}, \text{RPAREN}\}$
   - **Intersección:** Todos los pares son mutuamente disjuntos.
8. **`prod_tail`:**
   - $\text{SELECT}(30: \text{prod\_tail} \to \text{STAR \dots}) = \{\text{STAR}\}$
   - $\text{SELECT}(31: \text{prod\_tail} \to \text{SLASH \dots}) = \{\text{SLASH}\}$
   - $\text{SELECT}(32: \text{prod\_tail} \to \varepsilon) = \text{FOLLOW}(\text{prod\_tail}) = \{\text{PLUS}, \text{MINUS}, \text{op\_comp}, \text{KEYWORD\_AND}, \text{KEYWORD\_OR}, \text{SEMICOLON}, \text{RPAREN}\}$
   - **Intersección:** Todos los pares son mutuamente disjuntos.
9. **`unario`:**
   - $\text{SELECT}(33: \text{unario} \to \text{MINUS unario}) = \{\text{MINUS}\}$
   - $\text{SELECT}(34: \text{unario} \to \text{primario}) = \{\text{IDENTIFIER}, \text{INTEGER}, \text{DECIMAL}, \text{STRING}, \text{LPAREN}\}$
   - **Intersección:** $\{\text{MINUS}\} \cap \{\dots\} = \emptyset$ (Disjuntos).
10. **`primario`:**
    - Todas las alternativas inician con un terminal distinto (`IDENTIFIER`, `INTEGER_LITERAL`, `DECIMAL_LITERAL`, `STRING_LITERAL`, `LPAREN`). Intersección dos a dos vacía.

**Conclusión:**
$$\text{Total de conflictos LL(1)} = 0$$
La gramática diseñada es **estrictamente LL(1)**.

---

## 8. Tabla de Parseo LL(1) $M[A, a]$

La tabla de parseo $M[A, a]$ asigna a cada par $(\text{No Terminal } A, \text{Terminal } a)$ el identificador unívoco de producción a aplicar. Se implementa en `Grammar::buildTable()` mediante una estructura `std::map<NonTerminal, std::map<TokenType, int>> table_`.

### 8.1. Detalle de Entradas No Vacías de la Tabla $M[A, a]$

| No Terminal ($A$) | Terminal de Entrada ($a$) | Producción Asignada ($M[A, a]$) |
|---|---|---|
| **`consulta`** | `KEYWORD_SELECT` | `(1) consulta -> KEYWORD_SELECT proyeccion KEYWORD_FROM IDENTIFIER where_opt SEMICOLON` |
| **`proyeccion`** | `STAR` | `(2) proyeccion -> STAR` |
| | `IDENTIFIER` | `(3) proyeccion -> lista_cols` |
| **`lista_cols`** | `IDENTIFIER` | `(4) lista_cols -> IDENTIFIER cols_tail` |
| **`cols_tail`** | `COMMA` | `(5) cols_tail -> COMMA IDENTIFIER cols_tail` |
| | `KEYWORD_FROM` | `(6) cols_tail -> epsilon` |
| **`where_opt`** | `KEYWORD_WHERE` | `(7) where_opt -> KEYWORD_WHERE condicion` |
| | `SEMICOLON` | `(8) where_opt -> epsilon` |
| **`condicion`** | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `MINUS`, `LPAREN` | `(9) condicion -> disy` |
| **`disy`** | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `MINUS`, `LPAREN` | `(10) disy -> conj disy_tail` |
| **`disy_tail`** | `KEYWORD_OR` | `(11) disy_tail -> KEYWORD_OR conj disy_tail` |
| | `SEMICOLON`, `RPAREN` | `(12) disy_tail -> epsilon` |
| **`conj`** | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `MINUS`, `LPAREN` | `(13) conj -> comp conj_tail` |
| **`conj_tail`** | `KEYWORD_AND` | `(14) conj_tail -> KEYWORD_AND comp conj_tail` |
| | `KEYWORD_OR`, `SEMICOLON`, `RPAREN` | `(15) conj_tail -> epsilon` |
| **`comp`** | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `MINUS`, `LPAREN` | `(16) comp -> suma comp_tail` |
| **`comp_tail`** | `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL` | `(17) comp_tail -> op_comp suma` |
| | `KEYWORD_AND`, `KEYWORD_OR`, `SEMICOLON`, `RPAREN` | `(18) comp_tail -> epsilon` |
| **`op_comp`** | `EQUAL` | `(19) op_comp -> EQUAL` |
| | `NOT_EQUAL` | `(20) op_comp -> NOT_EQUAL` |
| | `LESS` | `(21) op_comp -> LESS` |
| | `GREATER` | `(22) op_comp -> GREATER` |
| | `LESS_EQUAL` | `(23) op_comp -> LESS_EQUAL` |
| | `GREATER_EQUAL` | `(24) op_comp -> GREATER_EQUAL` |
| **`suma`** | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `MINUS`, `LPAREN` | `(25) suma -> prod suma_tail` |
| **`suma_tail`** | `PLUS` | `(26) suma_tail -> PLUS prod suma_tail` |
| | `MINUS` | `(27) suma_tail -> MINUS prod suma_tail` |
| | `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `;`, `)` | `(28) suma_tail -> epsilon` |
| **`prod`** | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `MINUS`, `LPAREN` | `(29) prod -> unario prod_tail` |
| **`prod_tail`** | `STAR` | `(30) prod_tail -> STAR unario prod_tail` |
| | `SLASH` | `(31) prod_tail -> SLASH unario prod_tail` |
| | `PLUS`, `MINUS`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `;`, `)` | `(32) prod_tail -> epsilon` |
| **`unario`** | `MINUS` | `(33) unario -> MINUS unario` |
| | `IDENTIFIER`, `INTEGER`, `DECIMAL`, `STRING`, `LPAREN` | `(34) unario -> primario` |
| **`primario`** | `IDENTIFIER` | `(35) primario -> IDENTIFIER` |
| | `INTEGER_LITERAL` | `(36) primario -> INTEGER_LITERAL` |
| | `DECIMAL_LITERAL` | `(37) primario -> DECIMAL_LITERAL` |
| | `STRING_LITERAL` | `(38) primario -> STRING_LITERAL` |
| | `LPAREN` | `(39) primario -> LPAREN condicion RPAREN` |

*(Total: 115 celdas no vacías computadas. La tabla completa se inspecciona con `./build/parser --grammar`).*

---

## 9. Motor Predictivo No Recursivo Basado en Pila

### 9.1. Arquitectura del Motor en C++
El motor está implementado en la clase `sql::Parser` (`src/parser_ll1/parser.cpp`). Opera de forma desacoplada consumiendo `sql::LexResult` de la Práctica 4. Utiliza una pila explícita `std::vector<Symbol> stack` donde el fondo contiene el centinela `$` (`TokenType::END_OF_FILE`) y encima el símbolo inicial `NonTerminal::Consulta`.

### 9.2. Pseudocódigo del Algoritmo Iterativo

```text
ALGORITMO: ParserPredictivoLL1(Tokens, Gramatica, Tabla M)
ENTRADA:   Vector de tokens generado por el Lexer finalizado en $ (EOF)
SALIDA:    ParseResult { accepted: bool, errors: list, trace: list }

INICIO:
    Si existen errores léxicos en el LexResult:
        Retornar Falso con los errores léxicos propagados

    Pila := [ $, consulta ]      // $ en el fondo, consulta en el tope
    ip   := 0                    // Puntero al token actual

    Mientras Pila NO esté vacía hacer:
        X := Tope(Pila)
        a := Tokens[ip]

        Si X es Terminal entonces:
            Si X == a.tipo entonces:
                RegistrarTraza(Pila, Tokens[ip..], "emparejar " + X)
                Desapilar(Pila)
                ip := ip + 1
            Sino:
                ReportarError("se esperaba " + X + ", se encontró " + a.lexema, a.linea, a.columna)
                Romper Bucle (salida limpia)
        Sino (X es No Terminal):
            id := M[X, a.tipo]
            Si id >= 0 entonces:
                regla := Gramatica.produccion(id)
                RegistrarTraza(Pila, Tokens[ip..], "expandir (" + id + ") " + regla)
                Desapilar(Pila)
                Para cada símbolo S en regla.rhs en orden INVERSO hacer:
                    Apilar(Pila, S)
            Sino:
                esperados := Gramatica.tokensEsperados(X)
                ReportarError("se esperaba uno de {" + esperados + "}, se encontró " + a.lexema, a.linea, a.columna)
                Romper Bucle (salida limpia)

    accepted := (errores esté vacío) Y (Pila esté vacía)
    Retornar ParseResult
FIN
```

### 9.3. Análisis de Robustez: Estrategia de Rechazo sin Colapso (*No Crash / No Panic*)

La Sección 4.1 (Ítem 5) de `Lab_Parser.md` exige expresamente:
> *"Rechazar sentencias sintácticamente inválidas sin colapsar el programa (*crash* o *panic*)."*

#### 9.3.1. Desambiguación Conceptual: ¿Usa "Panic" el Analizador?
En ciencias de la computación existen dos significados radicalmente distintos para el término *panic*:

1. **Panic / Crash a nivel de Proceso (Colapso Fatal del Software):**
   - En sistemas operativos y lenguajes como C++, Rust o Go, un *panic* o *crash* ocurre cuando el programa se aborta violentamente ante una condición inesperada (mediante `abort()`, `std::terminate()`, un `assert(false)` fallido o una violación de acceso `SIGSEGV`).
   - **Cumplimiento en el proyecto:** El analizador **EVITA TOTALMENTE el crash o panic**: no contiene llamadas a `abort()`, `exit()`, `assert()` ni `throw` en su algoritmo de análisis. Cualquier desviación en la entrada del usuario se captura como un error sintáctico manejado y se devuelve una respuesta estructurada.
2. **Modo Pánico (*Panic-Mode Recovery*) en Teoría de Compiladores (Dragon Book):**
   - En compiladores para lenguajes completos con archivos de miles de líneas (GCC, Clang), el *Modo Pánico* consiste en que, al hallar un error, el parser descarta tokens de entrada uno tras otro hasta encontrar un símbolo de sincronización (como `;`) para intentar compilar la siguiente función o instrucción.
   - **¿Por qué no aplica el descarte en este motor SQL?**
     En un servidor relacional de sentencias individuales `SELECT`, el requisito explícito de la práctica es **determinar de forma unívoca la aceptación o rechazo** de la consulta. Descartar tokens a ciegas dentro de una sola consulta corta generaría derivaciones espurias y diagnósticos falsos. Por ello, el proyecto adopta la técnica de **Parada Determinista con Diagnóstico Exacto y Preservación de Traza** (*Immediate Controlled Failure with Exact Diagnostics*).

#### 9.3.2. Mecanismos Técnicos de Contención en `src/parser_ll1/parser.cpp`

1. **Salida Limpia mediante `break`:** Ante el primer token erróneo, el analizador no colapsa; registra el diagnóstico y rompe ordenadamente el bucle de pila (`break;`), permitiendo que `Parser::parse()` retorne el objeto `ParseResult` completo.
2. **Contención Segura de Celdas Vacías ($M[A, a] = -1$):** Cuando no existe producción para el par $(A, a)$, el método `Grammar::tableEntry(A, a)` devuelve `-1`. El parser comprueba `if (id < 0)` sin intentar acceder a memoria fuera de límites, invoca de forma segura `Grammar::expected(A)` para listar los tokens válidos y emite un mensaje enriquecido:
   ```text
   [ERROR SINTACTICO] linea 1, columna 15: se esperaba uno de {KEYWORD_FROM, COMMA}, se encontro 'WHERE' (KEYWORD_WHERE)
   ```
3. **Manejo Seguro de Desajustes de Terminales (*Mismatch*):** Si el terminal esperado en la cima no coincide con el token entrante, se extrae el nombre legible del token esperado y hallado, registrando posición y lexema sin desbordar la pila.
4. **Protección de Límites de Entrada (*Bounds Safety*):** El puntero de token está acotado por la función lambda:
   ```cpp
   const auto current = [&]() -> const Token& {
       return input_.tokens[std::min(pos, input_.tokens.size() - 1)];
   };
   ```
   Además, el constructor de `Parser` garantiza la presencia del token centinela `END_OF_FILE` (`$`) al final, evitando desbordamientos de lectura (*buffer over-read*) ante entradas vacías o truncadas.
5. **Preservación Integral de la Traza:** La secuencia de pasos ejecutados hasta el instante del error no se destruye, quedando disponible en `result.trace` para inspección forense o depuración.
6. **Aislamiento de Errores Léxicos:** Si `input_.errors` contiene fallos del escáner (caracteres no reconocidos, números mal formados), el parser no intenta derivar sobre tokens corruptos; inmediatamente traslada los errores con bandera `lexical = true` y finaliza con `accepted = false`.
7. **Códigos de Salida POSIX Estándar:** El CLI (`parser_cli.cpp`) mapea ordenadamente:
   - Código `0`: Consulta aceptada.
   - Código `1`: Consulta rechazada (sintáctica o léxicamente).
   - Código `2`: Error de I/O o argumentos de línea de comandos.
   Garantizando que el sistema operativo reciba una terminación limpia sin señales de fallo.

---

## 10. Simulación Paso a Paso de la Pila (Trazas Reales)

### 10.1. Consulta Válida Simple: `SELECT * FROM usuarios;`

Ejecución:
```bash
./build/parser src/parser_ll1/examples/01_valida_simple.sql
```

**Traza completa (9 pasos):**

| N | PILA (tope a la derecha) | ENTRADA RESTANTE | ACCIÓN REALIZADA |
|:---:|---|---|---|
| 1 | `$ consulta` | `SELECT * FROM usuarios ; $` | expandir (1) `consulta -> KEYWORD_SELECT proyeccion KEYWORD_FROM IDENTIFIER where_opt SEMICOLON` |
| 2 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM proyeccion KEYWORD_SELECT` | `SELECT * FROM usuarios ; $` | emparejar `KEYWORD_SELECT` |
| 3 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM proyeccion` | `* FROM usuarios ; $` | expandir (2) `proyeccion -> STAR` |
| 4 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM STAR` | `* FROM usuarios ; $` | emparejar `STAR` |
| 5 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM` | `FROM usuarios ; $` | emparejar `KEYWORD_FROM` |
| 6 | `$ SEMICOLON where_opt IDENTIFIER` | `usuarios ; $` | emparejar `IDENTIFIER` |
| 7 | `$ SEMICOLON where_opt` | `; $` | expandir (8) `where_opt -> epsilon` |
| 8 | `$ SEMICOLON` | `; $` | emparejar `SEMICOLON` |
| 9 | `$` | `$` | emparejar `$` |

**Veredicto:** `CONSULTA ACEPTADA: cumple la gramatica LL(1).` (Código 0).

---

### 10.2. Consulta Válida Compleja: `SELECT nombre, edad FROM estudiantes WHERE edad >= 18;`

Ejecución:
```bash
echo "SELECT nombre, edad FROM estudiantes WHERE edad >= 18;" | ./build/parser
```

**Traza completa (38 pasos exhaustivos):**

| N | PILA (tope a la derecha) | ENTRADA RESTANTE | ACCIÓN REALIZADA |
|:---:|---|---|---|
| 1 | `$ consulta` | `SELECT nombre , edad FROM estudiantes WHERE edad >= 18 ; $` | expandir (1) `consulta -> KEYWORD_SELECT proyeccion KEYWORD_FROM IDENTIFIER where_opt SEMICOLON` |
| 2 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM proyeccion KEYWORD_SELECT` | `SELECT nombre , edad FROM estudiantes WHERE edad >= 18 ; $` | emparejar `KEYWORD_SELECT` |
| 3 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM proyeccion` | `nombre , edad FROM estudiantes WHERE edad >= 18 ; $` | expandir (3) `proyeccion -> lista_cols` |
| 4 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM lista_cols` | `nombre , edad FROM estudiantes WHERE edad >= 18 ; $` | expandir (4) `lista_cols -> IDENTIFIER cols_tail` |
| 5 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM cols_tail IDENTIFIER` | `nombre , edad FROM estudiantes WHERE edad >= 18 ; $` | emparejar `IDENTIFIER` (`nombre`) |
| 6 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM cols_tail` | `, edad FROM estudiantes WHERE edad >= 18 ; $` | expandir (5) `cols_tail -> COMMA IDENTIFIER cols_tail` |
| 7 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM cols_tail IDENTIFIER COMMA` | `, edad FROM estudiantes WHERE edad >= 18 ; $` | emparejar `COMMA` (`,`) |
| 8 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM cols_tail IDENTIFIER` | `edad FROM estudiantes WHERE edad >= 18 ; $` | emparejar `IDENTIFIER` (`edad`) |
| 9 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM cols_tail` | `FROM estudiantes WHERE edad >= 18 ; $` | expandir (6) `cols_tail -> epsilon` |
| 10 | `$ SEMICOLON where_opt IDENTIFIER KEYWORD_FROM` | `FROM estudiantes WHERE edad >= 18 ; $` | emparejar `KEYWORD_FROM` |
| 11 | `$ SEMICOLON where_opt IDENTIFIER` | `estudiantes WHERE edad >= 18 ; $` | emparejar `IDENTIFIER` (`estudiantes`) |
| 12 | `$ SEMICOLON where_opt` | `WHERE edad >= 18 ; $` | expandir (7) `where_opt -> KEYWORD_WHERE condicion` |
| 13 | `$ SEMICOLON condicion KEYWORD_WHERE` | `WHERE edad >= 18 ; $` | emparejar `KEYWORD_WHERE` |
| 14 | `$ SEMICOLON condicion` | `edad >= 18 ; $` | expandir (9) `condicion -> disy` |
| 15 | `$ SEMICOLON disy` | `edad >= 18 ; $` | expandir (10) `disy -> conj disy_tail` |
| 16 | `$ SEMICOLON disy_tail conj` | `edad >= 18 ; $` | expandir (13) `conj -> comp conj_tail` |
| 17 | `$ SEMICOLON disy_tail conj_tail comp` | `edad >= 18 ; $` | expandir (16) `comp -> suma comp_tail` |
| 18 | `$ SEMICOLON disy_tail conj_tail comp_tail suma` | `edad >= 18 ; $` | expandir (25) `suma -> prod suma_tail` |
| 19 | `$ SEMICOLON disy_tail conj_tail comp_tail suma_tail prod` | `edad >= 18 ; $` | expandir (29) `prod -> unario prod_tail` |
| 20 | `$ SEMICOLON disy_tail conj_tail comp_tail suma_tail prod_tail unario` | `edad >= 18 ; $` | expandir (34) `unario -> primario` |
| 21 | `$ SEMICOLON disy_tail conj_tail comp_tail suma_tail prod_tail primario` | `edad >= 18 ; $` | expandir (35) `primario -> IDENTIFIER` |
| 22 | `$ SEMICOLON disy_tail conj_tail comp_tail suma_tail prod_tail IDENTIFIER` | `edad >= 18 ; $` | emparejar `IDENTIFIER` (`edad`) |
| 23 | `$ SEMICOLON disy_tail conj_tail comp_tail suma_tail prod_tail` | `>= 18 ; $` | expandir (32) `prod_tail -> epsilon` |
| 24 | `$ SEMICOLON disy_tail conj_tail comp_tail suma_tail` | `>= 18 ; $` | expandir (28) `suma_tail -> epsilon` |
| 25 | `$ SEMICOLON disy_tail conj_tail comp_tail` | `>= 18 ; $` | expandir (17) `comp_tail -> op_comp suma` |
| 26 | `$ SEMICOLON disy_tail conj_tail suma op_comp` | `>= 18 ; $` | expandir (24) `op_comp -> GREATER_EQUAL` |
| 27 | `$ SEMICOLON disy_tail conj_tail suma GREATER_EQUAL` | `>= 18 ; $` | emparejar `GREATER_EQUAL` (`>=`) |
| 28 | `$ SEMICOLON disy_tail conj_tail suma` | `18 ; $` | expandir (25) `suma -> prod suma_tail` |
| 29 | `$ SEMICOLON disy_tail conj_tail suma_tail prod` | `18 ; $` | expandir (29) `prod -> unario prod_tail` |
| 30 | `$ SEMICOLON disy_tail conj_tail suma_tail prod_tail unario` | `18 ; $` | expandir (34) `unario -> primario` |
| 31 | `$ SEMICOLON disy_tail conj_tail suma_tail prod_tail primario` | `18 ; $` | expandir (36) `primario -> INTEGER_LITERAL` |
| 32 | `$ SEMICOLON disy_tail conj_tail suma_tail prod_tail INTEGER_LITERAL` | `18 ; $` | emparejar `INTEGER_LITERAL` (`18`) |
| 33 | `$ SEMICOLON disy_tail conj_tail suma_tail prod_tail` | `; $` | expandir (32) `prod_tail -> epsilon` |
| 34 | `$ SEMICOLON disy_tail conj_tail suma_tail` | `; $` | expandir (28) `suma_tail -> epsilon` |
| 35 | `$ SEMICOLON disy_tail conj_tail` | `; $` | expandir (15) `conj_tail -> epsilon` |
| 36 | `$ SEMICOLON disy_tail` | `; $` | expandir (12) `disy_tail -> epsilon` |
| 37 | `$ SEMICOLON` | `; $` | emparejar `SEMICOLON` (`;`) |
| 38 | `$` | `$` | emparejar `$` |

**Veredicto:** `CONSULTA ACEPTADA: cumple la gramatica LL(1).` (Código 0).

---

## 11. Batería de Pruebas y Tratamiento de Errores

### 11.1. Validación de los 4 Tipos de Errores Sintácticos (Sección 6.4 de `Lab_Parser.md`)

#### Caso 1: Palabras clave fuera de orden (`WHERE` antes de `FROM`)
- **Entrada:** `SELECT nombre WHERE id = 1 FROM tabla;` (`src/parser_ll1/examples/03_error_palabra_desordenada.sql`)
- **Salida del Parser:**
  ```text
  CONSULTA RECHAZADA.
  [ERROR SINTACTICO] linea 1, columna 15: se esperaba uno de {KEYWORD_FROM, COMMA}, se encontro 'WHERE' (KEYWORD_WHERE)
  ```
- **Diagnóstico:** El motor detecta que tras el identificador de columna en `cols_tail` solo se admite una coma para otra columna o la cláusula `FROM`, rechazando inmediatamente con código 1.

#### Caso 2: Omisión de tokens obligatorios (nombre de la tabla origen)
- **Entrada:** `SELECT nombre FROM WHERE edad > 20;` (`src/parser_ll1/examples/04_error_omision_tabla.sql`)
- **Salida del Parser:**
  ```text
  CONSULTA RECHAZADA.
  [ERROR SINTACTICO] linea 1, columna 20: se esperaba IDENTIFIER, se encontro 'WHERE' (KEYWORD_WHERE)
  ```
- **Diagnóstico:** Tras emparejar `KEYWORD_FROM`, el tope de la pila exige el terminal `IDENTIFIER`. Al encontrar `KEYWORD_WHERE`, reporta desajuste exacto.

#### Caso 3: Omisión del delimitador final `;`
- **Entrada:** `SELECT a, b FROM t` (`src/parser_ll1/examples/05_error_sin_delimitador.sql`)
- **Salida del Parser:**
  ```text
  CONSULTA RECHAZADA.
  [ERROR SINTACTICO] linea 2, columna 1: se esperaba uno de {KEYWORD_WHERE, SEMICOLON}, se encontro fin de entrada
  ```
- **Diagnóstico:** Tras consumir la tabla, el motor evalúa `where_opt`. Al llegar al fin de entrada (`$`) sin encontrar `WHERE` ni `;`, reporta error en la posición final.

#### Caso 4: Expresiones mal formadas
- **Comparación sin lado derecho (`SELECT a FROM t WHERE a = ;`):**
  ```text
  [ERROR SINTACTICO] linea 1, columna 25: se esperaba uno de {IDENTIFIER, INTEGER_LITERAL, DECIMAL_LITERAL, STRING_LITERAL, MINUS, LPAREN}, se encontro ';' (SEMICOLON)
  ```
- **Operador aritmético sin operando (`SELECT a FROM t WHERE a + > 2;`):**
  ```text
  [ERROR SINTACTICO] linea 1, columna 27: se esperaba uno de {IDENTIFIER, INTEGER_LITERAL, DECIMAL_LITERAL, STRING_LITERAL, MINUS, LPAREN}, se encontro '>' (GREATER)
  ```
- **Paréntesis sin cerrar (`SELECT a FROM t WHERE (a = 1;`):**
  ```text
  [ERROR SINTACTICO] linea 1, columna 27: se esperaba RPAREN, se encontro ';' (SEMICOLON)
  ```

#### Caso 5: Propagación de errores léxicos
- **Entrada:** `SELECT a # b FROM tabla WHERE x = 12abc;` (`src/parser_ll1/examples/06_error_lexico.sql`)
- **Salida del Parser:**
  ```text
  CONSULTA RECHAZADA.
  [ERROR LEXICO] linea 1, columna 10: caracter no admitido
  [ERROR LEXICO] linea 1, columna 35: numero seguido de letras (identificador mal formado)
  ```
- **Diagnóstico:** Los errores léxicos impiden el inicio del análisis sintáctico y se reportan ordenadamente sin provocar abortos del programa.

---

### 11.2. Resumen General de Casos de Prueba

| Categoría | Consulta Evaluada | Resultado | Código |
|---|---|:---:|:---:|
| Válida simple | `SELECT * FROM usuarios;` | **ACEPTADA** | 0 |
| Válida compleja | `SELECT nombre, edad, salario FROM empleados WHERE (edad >= 18 AND salario > 2500.50) OR (antiguedad * 2 + 1) <= 10;` | **ACEPTADA** | 0 |
| Válida compleja | `SELECT a, b, c FROM t WHERE a <= b * 2 - 1 AND nombre = 'pepe';` | **ACEPTADA** | 0 |
| Válida científica | `SELECT x FROM t WHERE (a + 1) > -2.5e-3;` | **ACEPTADA** | 0 |
| Error orden | `SELECT nombre WHERE id = 1 FROM tabla;` | **RECHAZADA** | 1 |
| Error tabla | `SELECT nombre FROM WHERE edad > 20;` | **RECHAZADA** | 1 |
| Error delimitador | `SELECT a, b FROM t` | **RECHAZADA** | 1 |
| Error sintaxis | `SELECT FROM t;` | **RECHAZADA** | 1 |
| Error sintaxis | `SELECT a, FROM t;` | **RECHAZADA** | 1 |
| Error sintaxis | `SELECT *, a FROM t;` | **RECHAZADA** | 1 |
| Error sintaxis | `SELECT a FROM 123;` | **RECHAZADA** | 1 |
| Error sintaxis | `FROM t;` | **RECHAZADA** | 1 |
| Error léxico | `SELECT a # b FROM tabla WHERE x = 12abc;` | **RECHAZADA** | 1 |

---

### 11.3. Resultados de la Suite Automatizada CTest

```text
Test project /home/shiarowo/UNSA/EDA/Proyecto/build
      Start  1: value
 1/13 Test  #1: value ............................   Passed    0.01 sec
      Start  2: tuple
 2/13 Test  #2: tuple ............................   Passed    0.01 sec
      Start  3: datagen
 3/13 Test  #3: datagen ..........................   Passed    0.01 sec
      Start  4: storage
 4/13 Test  #4: storage ..........................   Passed    0.02 sec
      Start  5: tree_printer
 5/13 Test  #5: tree_printer .....................   Passed    0.01 sec
      Start  6: page_manager
 6/13 Test  #6: page_manager .....................   Passed    3.56 sec
      Start  7: btree_node
 7/13 Test  #7: btree_node .......................   Passed    0.00 sec
      Start  8: btree_write
 8/13 Test  #8: btree_write ......................   Passed    0.20 sec
      Start  9: btree_read
 9/13 Test  #9: btree_read .......................   Passed    0.18 sec
      Start 10: invariants
10/13 Test #10: invariants .......................   Passed    0.04 sec
      Start 11: lexer
11/13 Test #11: lexer ............................   Passed    0.00 sec
      Start 12: token
12/13 Test #12: token ............................   Passed    0.00 sec
      Start 13: parser
13/13 Test #13: parser ...........................   Passed    0.03 sec

100% tests passed, 0 tests failed out of 13
Total Test time (real) = 4.08 sec
```

---

## 12. Conclusiones

1. **Diseño Gramatical Exitoso:** Se diseñó y formalizó una CFG en BNF de 39 producciones libre de recursión izquierda y factorizada. La unificación de condiciones y expresiones a través de la cascada de precedencias resolvió la ambigüedad clásica del paréntesis `(`, demostrando matemáticamente que los conjuntos de selección son disjuntos ($\text{Total de conflictos} = 0$).
2. **Implementación Robusta del Algoritmo:** El analizador sintáctico predictivo no recursivo implementado en C++17 mediante una pila explícita (`std::vector<Symbol>`) refleja fielmente el comportamiento de un autómata a pila determinista.
3. **Ausencia Total de Crash/Panic:** Se garantiza que el software jamás aborte intempestivamente por errores de entrada del usuario. Los desajustes y celdas vacías se gestionan como eventos esperados en el flujo de control, emitiendo diagnósticos precisos y preservando la traza.
4. **Integración Modular y Desacoplada:** El analizador consume directamente la estructura `LexResult` de la Práctica 4 sin modificaciones destructivas, completando satisfactoriamente la Fase 2 del servidor relacional SQL.

---

## 13. Referencias Bibliográficas

1. Aho, A. V., Lam, M. S., Sethi, R., & Ullman, J. D. (2006). *Compilers: Principles, Techniques, and Tools* (2nd ed.). Addison-Wesley (Dragon Book).
2. Cooper, K. D., & Torczon, L. (2011). *Engineering a Compiler* (2nd ed.). Morgan Kaufmann.
3. Grune, D., & Jacobs, C. J. (2008). *Parsing Techniques: A Practical Guide* (2nd ed.). Springer.
4. ISO/IEC. (2017). *ISO International Standard ISO/IEC 14882:2017(E) – Programming Language C++*.
5. Silberschatz, A., Korth, H. F., & Sudarshan, S. (2019). *Database System Concepts* (7th ed.). McGraw-Hill.
