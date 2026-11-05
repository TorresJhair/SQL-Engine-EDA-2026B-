# Plan detallado: parser para consultas `SELECT`

## Objetivo y límites

Implementar en `src/parser` un analizador sintáctico que consuma los tokens producidos por `sql::Lexer`, valide una consulta `SELECT` y construya un AST que pueda usar una fase de ejecución futura. Esta fase no abre archivos, consulta el heap ni ejecuta expresiones. Los literales se conservan en el AST; no se convierten ni se comparan valores.

Una entrada contiene exactamente una consulta, terminada en `;` y seguida por `END_OF_FILE`. Los errores léxicos impiden el análisis sintáctico exitoso.

## Gramática propuesta

La siguiente EBNF describe el lenguaje. Los nombres en mayúsculas representan `TokenType`; `ε` representa la producción vacía y `*` después de un grupo significa repetición cero o más veces.

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

La precedencia queda definida por los niveles de la gramática: primero la negación unaria, luego multiplicación/división, suma/resta, comparación, `AND` y finalmente `OR`. Las repeticiones de suma, producto, `AND` y `OR` se agrupan de izquierda a derecha al construir el AST.

## Estrategia de análisis

- Implementar un parser de descenso recursivo predictivo, con una rutina por producción o nivel de precedencia. Las listas y cadenas de operadores se procesan con bucles para evitar recursión izquierda.
- La mayoría de las decisiones se toman con el token actual: `STAR` o `IDENTIFIER` seleccionan la proyección; `WHERE` selecciona la condición opcional; y cada nivel de expresión consume únicamente sus operadores.
- Hay una ambigüedad de prefijo con `LPAREN`: puede iniciar un factor aritmético (`(a + 1) > 2`) o un predicado lógico agrupado (`(a = 1 OR b = 2)`). Por eso esta gramática extendida no es LL(1) estricto. En `predicado`, guardar la posición actual e intentar primero parsear una comparación; si falla antes de completar una comparación válida, restaurar la posición y parsear `LPAREN condicion RPAREN`. Conservar el error más avanzado para reportarlo si ambas alternativas fallan.
- Al final se exige `SEMICOLON` y luego `END_OF_FILE`; cualquier token extra produce error.
- El parser mantiene la posición del token actual. Cada error sintáctico registra mensaje, lexema esperado/observado y línea/columna del token observado. Para EOF, se informa la ubicación del token EOF que el lexer ya incluye.

## Resultado y AST

La interfaz pública del parser recibe un `LexResult` (o su secuencia de tokens y errores léxicos) y devuelve un resultado con:

- La consulta AST cuando el análisis termina sin errores.
- Uno o más diagnósticos con ubicación cuando existe error léxico o sintáctico; una consulta inválida no devuelve AST utilizable.

El AST debe representar:

- `SelectStatement`: proyección, identificador de tabla y condición opcional.
- Proyección como `*` o lista ordenada de nombres de columnas.
- Expresiones como identificadores, literales, negación unaria y operaciones binarias aritméticas.
- Condiciones como comparaciones y operaciones binarias lógicas (`AND`/`OR`). Los paréntesis solo controlan agrupación y no requieren nodo AST propio.
- Literales con su clase (entero, decimal o cadena) y lexema original. La cadena puede usar el lexema decodificado que entrega el lexer.

Los nodos de operaciones deben conservar operador e hijos, para que una futura fase pueda evaluar o traducir el árbol. No se valida que los identificadores existan en la tabla ni que los tipos de ambos lados de una operación sean compatibles.

## Lexer e integración

- Añadir `PLUS`, `MINUS` y `SLASH` a `TokenType`, sus nombres en `tokenTypeName` y el reconocimiento de `+`, `-` y `/` en el lexer.
- Mantener `STAR` para ambos usos: multiplicación dentro de una expresión y proyección completa en el contexto de `SELECT`.
- Dejar de consumir `+` o `-` como parte de los números. El lexer emitirá signo y literal por separado, de modo que `a-2` sea `IDENTIFIER, MINUS, INTEGER_LITERAL`; la gramática decidirá si el signo es binario o unario. La notación científica sigue aceptando signo dentro del exponente (`1e-3`), pues dicho signo pertenece al formato del literal.
- Añadir explícitamente las fuentes del parser a `phase1_core` en `CMakeLists.txt`; no usar `GLOB`.
- Actualizar `src/parser/README.md` con gramática, tokens, precedencia, estructura de AST, errores y la salvedad de agrupación con paréntesis.

## Pruebas y aceptación

- Aceptar proyección `*` y listas de columnas; consultas con y sin `WHERE`; enteros, decimales, cadenas, identificadores y operadores aritméticos.
- Verificar AST y asociatividad para `a - b - c`, `a / b * c`, precedencia aritmética y `a = 1 OR b = 2 AND c > 3`.
- Aceptar agrupación aritmética y lógica, incluyendo `(a + 1) > 2` y `(a = 1 OR b = 2) AND c > 0`.
- Rechazar proyección vacía, comas sin columna, operadores sin operando, comparaciones sin operador o lado derecho, paréntesis sin cerrar, `;` ausente y tokens después del `;`.
- Comprobar diagnósticos con línea/columna para errores al inicio, en medio de entrada y al llegar a EOF; verificar que los errores léxicos se propagan como fallo del parseo.
- Actualizar las pruebas del lexer que actualmente esperan números con signo unido y añadir casos para operadores aritméticos y notación científica.
- Registrar las pruebas nuevas en CMake/CTest siguiendo la organización existente, y compilar/ejecutar las suites pertinentes.

## Supuestos

- Se procesa una sentencia `SELECT` por entrada y la tabla se identifica con un único `IDENTIFIER`.
- La lista de proyección contiene identificadores, sin alias ni expresiones; `*` es la única proyección especial.
- Las condiciones admiten comparaciones combinadas mediante `AND`/`OR` y paréntesis lógicos.
- Se aceptan enteros, decimales y cadenas como operandos sintácticos. La semántica y ejecución quedan para otra fase.
