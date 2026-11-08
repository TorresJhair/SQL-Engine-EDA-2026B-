# Gramática LL(1) del analizador sintáctico SQL

Este documento formaliza la gramática que reconoce el analizador predictivo
**LL(1) no recursivo** (Práctica 5). Los terminales son valores de
`sql::TokenType`; `ε` es la producción vacía y `$` (= `END_OF_FILE`) marca el fin
de la entrada. La gramática está **sin recursión izquierda**, **factorizada** y es
**LL(1) estricta** (la tabla se construye sin conflictos, ver más abajo).

## 1. Gramática (BNF)

```text
(1)  consulta    → KEYWORD_SELECT proyeccion KEYWORD_FROM IDENTIFIER where_opt SEMICOLON

(2)  proyeccion  → STAR
(3)  proyeccion  → lista_cols

(4)  lista_cols  → IDENTIFIER cols_tail
(5)  cols_tail   → COMMA IDENTIFIER cols_tail
(6)  cols_tail   → ε

(7)  where_opt   → KEYWORD_WHERE condicion
(8)  where_opt   → ε

(9)  condicion   → disy
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

El símbolo inicial es `consulta`. El motor inicia la pila con `$` en el fondo y
`consulta` encima; acepta cuando la pila queda vacía tras emparejar `$`.

### Diseño para evitar recursión izquierda y ambigüedad

- Las repeticiones EBNF (`(... )*` de columnas, `OR`, `AND`, `+/-`, `*//`) se
  expresan con **colas recursivas por la derecha** (`cols_tail`, `disy_tail`,
  `conj_tail`, `suma_tail`, `prod_tail`), que tienen una alternativa `ε`. Así no
  hay recursión izquierda.
- La cascada de precedencia (de menor a mayor): `OR` → `AND` → comparación →
  `+ -` → `* /` → negación unaria → primario.
- **Unificación de expresión y condición:** el `(` solía ser ambiguo (podía
  iniciar una expresión aritmética `(a+1)>2` o una condición agrupada
  `(a=1 OR b=2)`). Se resolvió haciendo que `primario → LPAREN condicion RPAREN`
  siempre abra una `condicion`, que desciende por toda la cascada. Con esto `(a+1)`
  también es derivable (baja hasta `suma`) y desaparece el conflicto: la gramática
  es LL(1). El reconocedor valida **sintaxis**, que es lo que exige la práctica.

## 2. Conjuntos FIRST y FOLLOW

Calculados por el código (`Grammar::computeFirst` / `computeFollow`). Se obtienen
en vivo con `./parser --grammar`.

| No terminal | FIRST | FOLLOW |
|---|---|---|
| consulta   | SELECT | $ |
| proyeccion | STAR, IDENTIFIER | FROM |
| lista_cols | IDENTIFIER | FROM |
| cols_tail  | COMMA, ε | FROM |
| where_opt  | WHERE, ε | SEMICOLON |
| condicion  | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | SEMICOLON, RPAREN |
| disy       | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | SEMICOLON, RPAREN |
| disy_tail  | OR, ε | SEMICOLON, RPAREN |
| conj       | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | OR, SEMICOLON, RPAREN |
| conj_tail  | AND, ε | OR, SEMICOLON, RPAREN |
| comp       | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | AND, OR, SEMICOLON, RPAREN |
| comp_tail  | EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL, ε | AND, OR, SEMICOLON, RPAREN |
| op_comp    | EQUAL, NOT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN |
| suma       | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | op_comp, AND, OR, SEMICOLON, RPAREN |
| suma_tail  | PLUS, MINUS, ε | op_comp, AND, OR, SEMICOLON, RPAREN |
| prod       | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN |
| prod_tail  | STAR, SLASH, ε | PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN |
| unario     | IDENTIFIER, INTEGER, DECIMAL, STRING, MINUS, LPAREN | STAR, SLASH, PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN |
| primario   | IDENTIFIER, INTEGER, DECIMAL, STRING, LPAREN | STAR, SLASH, PLUS, MINUS, op_comp, AND, OR, SEMICOLON, RPAREN |

(`op_comp` abrevia los seis operadores relacionales.)

## 3. Tabla de parseo LL(1) `M[A, a]`

La tabla se construye en `Grammar::buildTable()` a partir de FIRST/FOLLOW: para
`A → α`, la producción se coloca en `M[A, a]` para cada `a ∈ FIRST(α)`, y si `α`
es anulable, también para cada `a ∈ FOLLOW(A)`. Si una celda recibiera dos
producciones distintas sería un **conflicto LL(1)**; esta gramática no genera
ninguno. La tabla completa (cada celda con su número de producción) se imprime
con:

```sh
./parser --grammar
```

La condición LL(1) se cumple porque, para cada no terminal con varias
alternativas, los conjuntos de selección son disjuntos (p. ej. en `cols_tail`,
`FIRST(COMMA…) = {COMMA}` no se cruza con `FOLLOW(cols_tail) = {FROM}`).

## 4. Límites del subconjunto SQL

- Una entrada contiene exactamente una consulta terminada en `;` seguida de `$`.
- Proyección: `*` o lista no vacía de identificadores (sin alias ni expresiones).
- La tabla origen es un único `IDENTIFIER`.
- `WHERE` admite comparaciones (`=`, `<>`/`!=`, `<`, `>`, `<=`, `>=`) combinadas
  con `AND`/`OR` y paréntesis; los operandos pueden ser identificadores, enteros,
  decimales, cadenas y expresiones aritméticas (`+ - * /`, negación unaria).
- El analizador reconoce sintaxis (acepta/rechaza). No construye AST, no resuelve
  nombres, no valida tipos ni ejecuta la consulta.
- El lexer acepta `!=` y `<>` y los representa con el mismo terminal `NOT_EQUAL`.
