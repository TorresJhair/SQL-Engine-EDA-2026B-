# Analizador léxico SQL

Implementación en C++17 de un analizador léxico para un subconjunto de
sentencias SQL `SELECT`.

## Estructura

- `token.hpp` / `token.cpp`: modelo de tokens y nombres textuales.
- `lexer.hpp` / `lexer.cpp`: escáner léxico, estados de reconocimiento y manejo de errores.
- `lexer_cli.cpp`: programa principal del CLI léxico.
- `examples/`: entradas SQL válidas y con errores.
- `tests/test_lexer.cpp` / `tests/test_token.cpp`: pruebas unitarias en el directorio de tests del proyecto.

## Compilación y pruebas

El analizador léxico está integrado en `phase1_core` y se compila mediante CMake:

```bash
# Configurar y compilar el CLI del lexer y las suites de prueba
cmake -B build -S .
cmake --build build --target lexer test_lexer test_token

# Ejecutar pruebas unitarias automatizadas con CTest
ctest --test-dir build -V -R "lexer|token"
```

## Ejecución

El programa puede leer un archivo:

```bash
./build/lexer src/lexer/examples/valida.sql
```

También puede recibir la consulta por la entrada estándar:

```bash
echo "SELECT nombre FROM estudiantes;" | ./build/lexer
```

La salida muestra el tipo, lexema, línea y columna de cada token. Los errores
léxicos se escriben en la salida de error y el análisis continúa cuando es
posible.

## Reglas principales

- Palabras reservadas: `SELECT`, `FROM`, `WHERE`, `AND` y `OR`, sin distinguir
  mayúsculas de minúsculas.
- Identificadores ASCII formados por letras, dígitos y guion bajo; no pueden
  comenzar con un dígito. Si una palabra contiene caracteres no ASCII, como
  `diñosáurio`, se reporta completa como error y ninguna de sus partes genera
  tokens.
- Literales enteros, decimales y cadenas entre comillas simples. En una cadena,
  `''` representa una comilla simple.
  comenzar con un dígito.
- Literales enteros, decimales y números en notación científica (`42`, `3.14`,
  `1e5`, `1e-10`). Los operadores `+` y `-` se emiten como tokens separados;
  el signo de un exponente sigue formando parte del literal. Las cadenas se
  escriben entre comillas simples y `''` representa una comilla simple dentro
  de ellas.
- Operadores `=`, `<>`, `!=`, `<`, `>`, `<=` y `>=`.
- Signos `,`, `;`, `*`, `(` y `)`; operadores aritméticos `+`, `-` y `/`.
- Posiciones de línea y columna con base 1, además de un token final `EOF`.

Los errores no se convierten en tokens válidos. Entre los casos reportados
están los caracteres no admitidos, las cadenas sin cerrar, `!` aislado y los
números mal formados como `12.` o `12abc`.

## Uso desde otro módulo

```cpp
#include "lexer.hpp"

sql::LexResult result = sql::Lexer("SELECT a FROM tabla;").tokenize();
if (result.ok()) {
    for (const auto& token : result.tokens) {
        // token.type, token.lexeme, token.line, token.column
    }
}
```

## Códigos de salida

- `0`: análisis completado sin errores léxicos.
- `1`: se encontraron errores léxicos.
- `2`: uso incorrecto o no se pudo abrir el archivo indicado.

Para limpiar los binarios compilados:

```bash
cmake --build build --target clean
```
