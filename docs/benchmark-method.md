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

## Exportar a CSV y graficar

La opción 6 puede escribir sus propias filas en un CSV con `--csv RUTA`. Las dos curvas van
en el **mismo** archivo, separadas por la columna `page_size`:

```bash
./build/main --demo 6 --full --csv benchmark.csv
```

```
page_size,N,altura,heap_data_pages,heap_file_pages,index_reads,full_reads,ratio,build_ms,index_ms,full_ms
4096,20,1,1,2,2,1,0.5,0.051,0.030,0.121
4096,100000,2,764,765,3,764,254.7,5.915,0.042,111.758
256,100000,4,12500,12501,5,12500,2500.0,35.007,0.027,161.391
```

El CSV se escribe en el mismo bucle que imprime la tabla, con los mismos valores: no hay
parseo de texto ni una segunda medición, así que el archivo no puede divergir de la salida por
pantalla. Sin `--full`, el punto `N = 100 000` deja `index_ms` y `full_ms` vacíos, que es la
forma en CSV de lo que la tabla marca como `omitido`. Si el archivo no se puede abrir, el
benchmark avisa por `stderr` y continúa igual: el CSV es un extra, no un requisito.

`scripts/plot_benchmark.py` dibuja ese CSV con el eje X en `N` y dos métricas en el eje Y:
el tiempo de búsqueda en ms y las páginas leídas por búsqueda. Para cada métrica escribe **un
PNG por `page_size`**, o sea cuatro imágenes:

```bash
python3 scripts/plot_benchmark.py --csv benchmark.csv
# -> benchmark_timing_256.png   benchmark_timing_4096.png
#    benchmark_pages_256.png    benchmark_pages_4096.png

python3 scripts/plot_benchmark.py --csv benchmark.csv --metric pages  # solo paginas
python3 scripts/plot_benchmark.py --csv benchmark.csv --linear       # eje X lineal
python3 scripts/plot_benchmark.py --csv benchmark.csv --linear-y     # eje Y lineal
```

Están separados porque las curvas de las dos medidas en un mismo eje no se leen. El Index Scan
se mantiene cerca de 0.01 ms (o de 3 páginas) y el Full Table Scan llega a ~150 ms (o a 12 500
páginas): cuatro órdenes de magnitud, así que en un eje Y lineal lo que está en el piso queda
aplastado. Por eso el eje Y es **logarítmico** y los gráficos de una misma métrica usan **la
misma escala**, calculada sobre todas las filas, para que las dos curvas de página se comparen
a simple vista. El eje X también es logarítmico porque `N` va de 20 a 100 000.

Los gráficos de páginas y los de tiempos cuentan cosas distintas. Los conteos de páginas son
**deterministas** para estos tamaños y este esquema: el Index Scan se mantiene en 3–5 lecturas
mientras el Full Scan crece con `N`, y eso es el argumento algorítmico del índice. Los tiempos
dependen de la máquina y son orientativos. Además, como el benchmark siempre reporta los
conteos y solo omite los tiempos caros de `N = 100 000` sin `--full`, los gráficos de páginas
salen con los seis puntos **sin necesidad de `--full`**.

El script es el equivalente de `dot -Tpng tree.dot` para el benchmark: toma el archivo de datos
y produce las imágenes. Como con Graphviz, `matplotlib` es una dependencia opcional: si no está
instalado, el script dice cómo instalarla y sale con código 0 sin fallar. Los puntos con tiempos
vacíos se omiten del gráfico en lugar de dibujarse como cero.

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
