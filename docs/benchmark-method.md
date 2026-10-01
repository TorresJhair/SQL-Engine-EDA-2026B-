# Metodología del benchmark

`Benchmark::run` compara una búsqueda completa por índice con un Full Table Scan sobre el
mismo conjunto de tuplas. Ejecuta dos curvas independientes, primero con páginas de 4096 B
y luego con páginas de 256 B. Cada curva crea sus propios archivos temporales de heap e
índice, de modo que no reutiliza datos ni páginas de la curva anterior.

## Datos y tamaños

El dataset es determinista y contiene dos campos: `INT key` y `VARCHAR(16)`. La tupla
serializada ocupa 27 bytes. Se ejecutan `N = 20, 500, 1 000, 5 000, 10 000, 100 000`.
Con la cabecera y directorio de `SlottedPage`, la capacidad es
`floor((page_size - 8) / 31)`: 131 tuplas por página de 4096 B y 8 por página de 256 B.

## Qué se mide

Para cada `N`, se construye el heap y se carga el B+ tree con `bulkLoad`. Se mide y se
reporta por separado el tiempo de construcción del índice. La búsqueda indexada consulta
la clave `N`, obtiene sus RowID y recupera la tupla desde el heap. El costo reportado en
páginas es `altura + 1`: una lectura de página por cada nivel del índice y una lectura de
la tupla en el heap. El Full Table Scan recorre el heap completo y compara cada clave; su
costo es `ceil(N / tuplesPerPage)` lecturas de páginas de datos.

La página 0 contiene metadatos: no se cuenta como página de datos leída. Por ello, para
100 000 filas y páginas de 4096 B, el archivo tiene 765 páginas físicas (una de metadatos y
764 de datos), pero el Full Scan registra 764 lecturas. Con páginas de 256 B, son 12 501
páginas físicas y 12 500 lecturas del scan.

Los contadores se reinician antes de cada búsqueda medida. `PageManager` no usa caché propia,
así que cada `read` de una página de datos aumenta el contador, aunque esos accesos lógicos
no necesariamente causen I/O físico: el sistema operativo puede servirlos desde su caché.
El ratio que imprime el programa es `lecturas_full / lecturas_indice`.

## Repeticiones y tiempos

Se repite cada búsqueda cinco veces para `N <= 10 000` y una vez para `N = 100 000`.
Cuando hay varias muestras se informa la mediana; a 100 000 se usa la única muestra. Las
repeticiones afectan los tiempos observados, pero no los conteos de páginas. El tiempo de
construcción del índice se mide una vez y permanece separado de las búsquedas.

Los tiempos dependen de la máquina, el compilador, el sistema de archivos y la caché del
sistema operativo. Para compararlos, configurar y declarar el build `RelWithDebInfo`:

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel
./build-rel/main --demo 6 --full
```

El build `Debug` es el predeterminado para desarrollar y conserva los `assert` que detectan
errores de programación. Para la medición se usa `RelWithDebInfo`: optimiza el ejecutable y
conserva símbolos de depuración. Los desbordamientos esperados se reportan con `Status`, por
lo que siguen comprobándose aunque el build defina `NDEBUG`.

`--full` incluye los tiempos de la corrida de 100 000 filas en la salida. Sin esa opción,
el benchmark conserva y reporta sus conteos, pero omite esos dos tiempos para acortar la
demostración.

## Punto de cruce

`Benchmark::crossover(page_size, tuples_per_page)` clasifica cada `N` entre 1 y 100 000
comparando las páginas del Full Scan con `altura + 1` del Index Scan. Devuelve bandas
contiguas de `Scan`, `Tie` e `Index`; las bandas salen de la fórmula y del grado calculado
para ese tamaño de página, no de límites escritos manualmente. Los valores esperados son:

- 4096 B: Scan en 1–131, empate en 132–262, Index en 263–100 000.
- 256 B: Scan en 1–8, empate en 9–16, Index en 17–23, empate en 24, Index en 25–100 000.

El cruce es una comparación de páginas leídas, mientras que los tiempos medidos son
orientativos. Ambas medidas se reportan para explicar tanto el costo algorítmico como el
comportamiento de esta ejecución concreta.
