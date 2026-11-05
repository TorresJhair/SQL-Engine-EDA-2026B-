# Gramática SQL soportada

Esta es la gramática concreta que consume el parser. Los terminales corresponden a `sql::TokenType`; `ε` denota ausencia de tokens y `*` en la notación EBNF denota repetición cero o más veces.

```text
consulta       → KEYWORD_SELECT proyeccion KEYWORD_FROM IDENTIFIER
                 where_opt SEMICOLON END_OF_FILE

proyeccion     → STAR
               | lista_cols

lista_cols     → IDENTIFIER (COMMA IDENTIFIER)*

where_opt      → KEYWORD_WHERE condicion
               | ε

condicion      → disyuncion

disyuncion     → conjuncion (KEYWORD_OR conjuncion)*

conjuncion     → predicado (KEYWORD_AND predicado)*

predicado      → comparacion
               | LPAREN condicion RPAREN

comparacion    → expr op_comp expr

op_comp        → EQUAL | NOT_EQUAL | LESS | GREATER | LESS_EQUAL | GREATER_EQUAL

expr           → termino ((PLUS | MINUS) termino)*

termino        → factor ((STAR | SLASH) factor)*

factor         → IDENTIFIER
               | INTEGER_LITERAL
               | DECIMAL_LITERAL
               | STRING_LITERAL
               | LPAREN expr RPAREN
               | MINUS factor
```

## Precedencia y asociatividad

De mayor a menor precedencia: negación unaria, `*` y `/`, `+` y `-`, comparación, `AND`, `OR`. Los operadores binarios del mismo nivel se asocian de izquierda a derecha. Por ejemplo, `a - b - c` se representa como `(a - b) - c`, y `a = 1 OR b = 2 AND c = 3` como `(a = 1) OR ((b = 2) AND (c = 3))`.

## Paréntesis en condiciones y expresiones

`LPAREN` puede comenzar tanto un factor aritmético como una condición agrupada. Por ejemplo, `(a + 1) > 2` agrupa una expresión, mientras que `(a = 1 OR b = 2)` agrupa una condición. Por esta superposición, la gramática completa no es LL(1) estricta. El parser usa descenso recursivo por precedencia y, al analizar un predicado que empieza con `(`, intenta primero una comparación; si esa alternativa falla, retrocede al inicio y prueba una condición agrupada. Si ambas fallan, reporta el error que llegó más lejos.

## Límites

- Una entrada contiene exactamente una consulta y debe terminar en `;` seguido de EOF.
- La proyección permite `*` o una lista no vacía de identificadores; no admite alias ni expresiones.
- La tabla es un único identificador.
- Los operandos de comparación pueden ser identificadores, enteros, decimales, cadenas o expresiones aritméticas entre paréntesis.
- El parser reconoce sintaxis y construye AST. No resuelve nombres, valida tipos ni ejecuta consultas.
- El lexer acepta `!=` y `<>` y los representa con el mismo terminal `NOT_EQUAL`.
