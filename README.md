# Motor de base de datos SQL — Fase 1: Storage Engine + B+ Tree

Heap de páginas con *slotted page* y un índice B+ tree sobre él. El árbol es de
altura mínima alcanzable y sus nodos caben enteros en una página de 4096 B.

## 1. Entorno y dependencias

| | |
|---|---|
| Lenguaje | C++17 |
| Build | CMake ≥ 3.16 |
| Compilador | gcc ≥ 9 o clang ≥ 10 |
| Dependencias externas | ninguna |

El proyecto **no usa ninguna librería de tests**: el harness (`TEST`, `CHECK`, `CHECK_EQ`) son
unos 100 renglones en `tests/TestHarness.h`. El repositorio compila sin red y sin
`git submodule`.

`matplotlib` es la única dependencia externa y es **opcional**: solo la necesita
`scripts/plot_benchmark.py` para dibujar el gráfico del benchmark. Ni el build, ni los tests,
ni el motor la requieren. Si no está instalada, el script avisa cómo instalarla y sale con
código 0 sin fallar.

Todos los que trabajen en el repo usan el `.clang-format` compartido (`BasedOnStyle: LLVM`,
`IndentWidth: 4`). Sin un archivo de formato único, los reformateos automáticos llenan los
diffs y tapan los cambios reales en la revisión.

## 2. Compilar y ejecutar

```bash
cmake -S . -B build
cmake --build build
./build/main
```

`Debug` es el tipo de build por defecto, porque mantiene vivos los `assert` de error de
programación. El **benchmark se mide en `RelWithDebInfo`** y eso se declara en la
presentación, sin excusas:

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel
```

Medir en un build optimizado no esconde errores de formato, y esa es la razón: el desborde de
un nodo devuelve un `Status::NodeOverflow`, no dispara un `assert`. `-DNDEBUG` borra los
`assert` y el desborde se sigue detectando igual.

El ejecutable ofrece un menú interactivo y acepta `--demo 0|1|2|3|4|5|6`,
`--page-size`, `--n`, `--full` y `--csv RUTA.csv`. Para ver todas las opciones:

```bash
./build/main --help
```

La opción 0 ejecuta las seis demos en orden. Las opciones 1-4 comparten los archivos
`data.db` e `index.db` durante una misma ejecución; por eso conviene usar el menú o
`--demo 0` para que cada opción encuentre los datos creados por la anterior. Al iniciar,
el programa elimina esos dos archivos del directorio de trabajo. Las opciones 5 y 6 usan
archivos temporales propios.

```bash
./build/main --demo 0                   # presentacion completa
./build/main --demo 5                   # splits con paginas de 256 B
./build/main --demo 6                   # ambas curvas del benchmark
./build/main --demo 6 --n 50000         # una N, en ambas curvas
./build/main --demo 6 --full            # tabla completa con repeticiones
./build/main --page-size 256 --demo 5   # la opcion 5 fuerza 256 B
```

`--page-size` ajusta las opciones 1-4 y selecciona una sola curva en la opción 6; la
opción 5 siempre usa 256 B. `--n` acota las tuplas que inserta la opción 1 y los valores de
N del benchmark; con `--n 0` (o sin el flag) la demo 1 inserta las 100 000 de la
presentación. `--full` muestra también el tiempo del punto de 100 000 filas. `--csv` guarda
las 12 filas del benchmark en un CSV y solo afecta a la opción 6.

### Generar la imagen de un B+ tree pequeño

La opción 4 escribe `tree.dot` y lo convierte con Graphviz. Para obtener un árbol que se
dibuje rápido hay que achicar los datos: con las 100 000 tuplas por defecto el índice tiene
764 hojas y la cadena `nextLeaf` hace que `dot` tarde minutos. Con `--n` chico se dibuja en
menos de dos segundos.

Un comando, y en el menú la secuencia **1 → 3 → 4**:

```bash
./build/main --n 300 --page-size 64
```

```
1. Storage   - insertar tuplas y mostrar una pagina      -> 1
2. Pager     - recorrer el heap y mostrar contadores     -> (opcional)
3. Bulk load - construir el indice desde el heap         -> 3
4. Arbol     - imprimir el B+ tree y exportar tree.dot   -> 4
q. Salir                                                 -> q
```

Salida: `tree.dot` siempre, más `tree.svg` y `tree.png` si Graphviz está instalado. Con esos
flags quedan 300 tuplas y un árbol de 5 niveles.

Las tres opciones tienen que ser **en la misma ejecución**: el programa borra `data.db` e
`index.db` al arrancar, así que `--demo 1`, `--demo 3` y `--demo 4` en procesos separados
deja el índice vacío. Con el menú o con `--demo 0` cada opción encuentra lo que creó la
anterior.

`--page-size` es la palanca más fuerte para el dibujo, porque define cuántas claves entran
por hoja (`computeT` en `src/index/BTreeNode.cpp`). Menos bytes por página significa nodos más
chicos, más páginas y más niveles:

| `--page-size` | claves por hoja | Altura con `--n 300` | Para qué |
|---:|---:|---:|---|
| 64 | 2 | 9 | ver splits y estructura profunda |
| 128 | 6 | 4 | árbol legible de varios niveles |
| 256 | 12 | 3 | recomendación para una imagen clara |
| 1024 | 50 | 2 | pocos nodos, muy legible |
| 4096 | 204 | 2 | las hojas caben casi todas juntas |

Para ver la **cadena `nextLeaf`** (las aristas punteadas entre hojas) conviene un
`--page-size` grande con `--n` chico: ahí hay pocas hojas y el encadenado se lee bien.

El `.dot` se genera siempre. Si Graphviz no está, el programa avisa y no falla; también podés
convertirlo a mano con cualquier formato:

```bash
dot -Tsvg tree.dot -o tree.svg      # o -Tpng, -Tpdf, -Tjpg
```

### Exportar el benchmark a CSV y generar los gráficos

Dos comandos. El primero escribe el CSV, el segundo lo dibuja:

```bash
# 1. Exportar los resultados a CSV (12 filas: 6 de page_size 4096 + 6 de 256)
./build/main --demo 6 --full --csv benchmark.csv

# 2. Dibujar los gráficos: eje X = N, eje Y = tiempo (ms) y páginas leídas
python3 scripts/plot_benchmark.py --csv benchmark.csv
```

El script escribe **cuatro PNG: dos métricas por `page_size`**:

```
benchmark_timing_256.png      Index Scan vs Full Table Scan, páginas de 256 B  (Y en ms)
benchmark_timing_4096.png     Index Scan vs Full Table Scan, páginas de 4096 B (Y en ms)
benchmark_pages_256.png       páginas leídas por búsqueda, 256 B
benchmark_pages_4096.png      páginas leídas por búsqueda, 4096 B
```

Todos quedan en el directorio de trabajo y están en `.gitignore`.

Están separados por `page_size` porque las curvas de las dos medidas en un mismo eje no se
leen: el Index Scan se mantiene en ~0.01 ms (o en 3 páginas) y el Full Table Scan llega a
~150 ms (o a 12 500 páginas), cuatro órdenes de magnitud, y lo que está en el piso queda
aplastado. Por eso el eje Y va en **logarítmico** y los gráficos de una misma métrica
**comparten la escala**, para que se comparen entre sí. Con `--linear-y` el eje Y pasa a
lineal.

Los dos juegos de gráficos cuentan cosas distintas:

| | Qué muestra | Cómo se comporta |
|---|---|---|
| `timing` | Tiempo de búsqueda en ms | Depende de la máquina, la caché y el compilador |
| `pages` | Páginas leídas por búsqueda | **Determinista**: el índice se mantiene en 3–5 páginas mientras el Full Scan crece con N |

Las dos curvas del benchmark van en el **mismo** CSV, separadas por la columna `page_size`.

`--full` solo hace falta para los **tiempos** de `N = 100 000`: sin esa opción el benchmark
omite esos dos tiempos (en el CSV quedan vacíos) y las curvas de tiempo terminan en 10 000.
Los **conteos de páginas siempre están**, así que los gráficos de `pages` salen completos
—inclusive el punto de 100 000— sin pagar la corrida cara.

Para medir tiempos representativos, usá el build `RelWithDebInfo` de arriba:

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-rel
./build-rel/main --demo 6 --full --csv benchmark.csv
python3 scripts/plot_benchmark.py --csv benchmark.csv
```

También funciona desde el menú interactivo, porque `--csv` se lee al arrancar:

```bash
./build/main --csv benchmark.csv --full   # elegí la opción 6, después q
```

Opciones del script: `--metric time|pages|both` elige qué dibujar (por defecto, ambos),
`--out` y `--out-pages` cambian los prefijos de los PNG (se les agrega `_256.png` y
`_4096.png`), `--linear` usa eje X lineal, `--linear-y` eje Y lineal, y `--title "texto"`
agrega un texto al título.

### Instalar `matplotlib` (opcional)

En esta máquina ya está en los dos sitios, así que alcanza con `python3`:

```bash
python3 -c "import matplotlib; print(matplotlib.__version__)"   # 3.10.7 (system, apt)
.venv/bin/python -c "import matplotlib; print(matplotlib.__version__)"  # 3.11.2 (.venv)
```

El sistema la tiene por `apt` y el proyecto además trae su propio `.venv` (182 MB), así que
conviene usar `python3` a secas y dejar el `.venv` como respaldo. Si la borrás:

```bash
rm -rf .venv        # se regenera con: python3 -m venv .venv && .venv/bin/pip install matplotlib
```

Si en otra máquina no está, **Ojo: `pip install --user` no sirve**, porque el Python del
sistema está marcado como *externally managed* (PEP 668, `EXTERNALLY-MANAGED` en
`/usr/lib/python3.14/`) y pip lo rechaza. Las rutas que sí funcionan:

```bash
# Con sudo (pide password en esta máquina)
sudo apt install python3-matplotlib        # y después alcanza con python3

# Recomendado si no querés sudo: queda en ~/.local y no toca el sistema
pipx install matplotlib                   # si no tenés pipx: sudo apt install pipx

# Alternativa: entorno propio dentro del proyecto
python3 -m venv .venv && .venv/bin/pip install matplotlib
```

### Graphviz (opcional, para la imagen del árbol)

La opción 4 escribe siempre `tree.dot`, que es texto y no necesita nada. Para obtener
`tree.svg` y `tree.png` hace falta el binario `dot`:

```bash
dot -V                                   # graphviz version 14.1.2 (0)
dot -T? 2>&1 | tr ' ' '\n' | grep png    # si lista "png", el render PNG esta disponible
```

En esta máquina Graphviz 14.1.2 ya está instalado en `/usr/bin/dot` y genera **SVG, PNG, JPG
y PDF**, así que la opción 4 produce las dos imágenes sin configurar nada. Si en otra máquina
`dot` no está, el programa avisa, deja el `.dot` igual y sigue: el `.dot` siempre es válido y
se puede convertir a mano con `dot -Tsvg tree.dot -o tree.svg`.

## 3. Correr los tests

```bash
ctest --test-dir build --output-on-failure     # las 10 suites
ctest --test-dir build -R storage              # una sola
./build/tests --suite storage                  # igual, sin ctest
./build/tests                                  # todas ("all" si no se pasa --suite)
```

Hay un `add_test` por suite, así que una suite roja se ve sola en el informe en vez de
desaparecer dentro de un "los tests fallan". Las diez son `value`, `tuple`, `datagen`,
`storage`, `tree_printer`, `page_manager`, `btree_node`, `btree_write`, `btree_read` e
`invariants`.

Dos reglas del harness que no son arbitrarias:

- **No hay `fork()`.** Para comprobar que un `assert` aborta habría que correr el caso en un
  proceso hijo, que ataba la suite a POSIX. Como `serialize`, `bulkLoad` y la tupla de 0
  campos devuelven `Status`, el error esperado se comprueba directo con `CHECK_EQ`. Los
  `assert` que quedan son de error de programación dentro de una función ya llamada con el
  tipo equivocado, y no se testean.
- **Los tests usan `temp_directory_path()`.** Nunca `data.db` ni `index.db` del repo.

## 4. Por qué `t = 204`

El header de un nodo ocupa 16 B fijos, un `RowID` ocupa **6 B en la página**, un puntero a
hijo 4 B, y se deja `SLACK = 1` B de margen. Con `t` = máximo de claves por nodo:

```
Interno:  16 + 4·(2t) + 4·(2t−1) + 1  ≤  4096   ->   16t + 13 ≤ 4096   ->   t ≤ 255
Hoja:     16 +        10·(2t−1)      + 1  ≤  4096   ->   20t +  7 ≤ 4096   ->   t ≤ 204
```

La hoja es la restricción más estrecha, así que **`t = 204`**: 407 claves y 408 hijos por
nodo interno. Una hoja llena ocupa `16 + 407·10 = 4086` de 4096 B.

La trampa de este cálculo está en el `sizeof`:

```cpp
static_assert(sizeof(RowID) == 8);                    // en memoria, por el padding
static_assert(sizeof(PageID) + sizeof(SlotID) == 6);  // en la página, que es lo que cuenta
```

`sizeof(RowID)` da 8 por la alineación del `struct`, pero en la página el `RowID` son 4 + 2
bytes sin alinear. El serializador escribe los campos a mano en little-endian, nunca
`memcpy` del struct. Si alguien usa `sizeof(RowID)` en la fórmula de arriba, `t` sale mal y
el árbol se corrompe en silencio.

Con `--page-size 256` la fórmula da **`t = 12`**, que es lo que hace visibles los splits uno a
uno en la demo: con `t = 204` no ocurre un solo split hasta ~408 inserciones.

## 5. Estructura del enunciado que no seguimos

- El enunciado sugiere `src/main.ext`; el repositorio usa `src/main.cpp`, que es lo que
  compila en Linux.
- Las clases se llaman `BTree` y `BTreeNode` como pide el enunciado, pero implementan un **B+**
  tree: los datos viven solo en las hojas y las hojas están encadenadas.
- El borrado entra a medias: `SlottedPage::erase` sí está implementado y no compacta;
  `BTree::erase` no existe.

## 6. Terminología del enunciado → código

| En el enunciado | En el código | Notas |
|---|---|---|
| Administrador de páginas / inodos | `storage/PageManager.h` | `Page` es el bloque; el inodo es la página 0, que lleva el `FileMeta` |
| Archivo de datos | `storage/HeapFile.h` | |
| Nodo del árbol | `index/BTreeNode.h` | `NodeData` + header de 16 B |
| Árbol B | `index/BTree.h` | B+ real, no B |
| Buscar | `BTree::search` | `lowerBound`, devuelve **todos** los `RowID` de la clave |
| Recorrido del índice | `BTree::indexScan` | |
| Insertar | `BTree::insert` | |
| Carga masiva | `BTree::bulkLoad` | reparto uniforme por niveles |
| Register / tupla | `storage/Tuple.h` | |
| Verificación de invariantes | `tests/TreeInvariants.h` | `checkInvariants` |

## 7. Resultados del benchmark

La corrida de referencia se ejecutó en `RelWithDebInfo` con `--demo 6 --full` (GNU C++ 13.3).
Cada curva usa archivos temporales independientes. La altura es el número de niveles del
índice; las lecturas de índice incluyen la búsqueda y la lectura del RowID en el heap. El
Full Scan visita todas las páginas de datos. La página 0 de metadatos no se cuenta.

### Conteos de páginas

Los conteos son deterministas para estos tamaños y este esquema (`INT key` + `VARCHAR(16)`,
27 B por tupla). `páginas heap` muestra páginas de datos y, entre paréntesis, el total físico
incluida la metadata.

| page_size | N | altura | páginas heap (total) | lecturas índice | lecturas Full Scan | ratio Full/Index |
|---:|---:|---:|---:|---:|---:|---:|
| 4096 B | 20 | 1 | 1 (2) | 2 | 1 | 0.5x |
| 4096 B | 500 | 2 | 4 (5) | 3 | 4 | 1.3x |
| 4096 B | 1 000 | 2 | 8 (9) | 3 | 8 | 2.7x |
| 4096 B | 5 000 | 2 | 39 (40) | 3 | 39 | 13.0x |
| 4096 B | 10 000 | 2 | 77 (78) | 3 | 77 | 25.7x |
| 4096 B | 100 000 | 2 | 764 (765) | 3 | 764 | 254.7x |
| 256 B | 20 | 1 | 3 (4) | 2 | 3 | 1.5x |
| 256 B | 500 | 2 | 63 (64) | 3 | 63 | 21.0x |
| 256 B | 1 000 | 3 | 125 (126) | 4 | 125 | 31.2x |
| 256 B | 5 000 | 3 | 625 (626) | 4 | 625 | 156.2x |
| 256 B | 10 000 | 3 | 1 250 (1 251) | 4 | 1 250 | 312.5x |
| 256 B | 100 000 | 4 | 12 500 (12 501) | 5 | 12 500 | 2 500.0x |

### Tiempos de referencia

El build del índice se mide una vez, separado de la búsqueda. Para `N <= 10 000`, las
búsquedas usan cinco repeticiones y se reporta la mediana; para `N = 100 000`, una repetición.
Los tiempos varían con el equipo, el compilador y la caché del sistema operativo; los conteos
de páginas anteriores son la comparación reproducible.

| page_size | N | construcción índice (ms) | Index Scan mediana (ms) | Full Scan mediana (ms) |
|---:|---:|---:|---:|---:|
| 4096 B | 20 | 0.025 | 0.010 | 0.011 |
| 4096 B | 500 | 0.060 | 0.017 | 0.151 |
| 4096 B | 1 000 | 0.081 | 0.018 | 0.319 |
| 4096 B | 5 000 | 0.312 | 0.020 | 1.679 |
| 4096 B | 10 000 | 0.600 | 0.021 | 3.301 |
| 4096 B | 100 000 | 5.529 | 0.028 | 32.558 |
| 256 B | 20 | 0.017 | 0.010 | 0.019 |
| 256 B | 500 | 0.394 | 0.015 | 0.436 |
| 256 B | 1 000 | 0.804 | 0.021 | 0.871 |
| 256 B | 5 000 | 3.898 | 0.022 | 4.388 |
| 256 B | 10 000 | 7.515 | 0.024 | 8.613 |
| 256 B | 100 000 | 77.863 | 0.036 | 88.504 |

`Benchmark::crossover` calcula las bandas comparando páginas por búsqueda (`altura + 1`)
con páginas del Full Scan:

- **4096 B:** Scan para N=1–131, empate en 132–262, Index desde 263.
- **256 B:** Scan para N=1–8, empate en 9–16, Index en 17–23, empate en 24 e Index desde 25.

### Exportar a CSV y graficar

`./build/main --demo 6 --full --csv benchmark.csv` escribe esas mismas corridas en un CSV,
tomando los valores del **mismo bucle que imprime la tabla**: no hay parseo de texto ni una
segunda medición, así que el archivo no puede divergir de la salida por pantalla.

```
page_size,N,altura,heap_data_pages,heap_file_pages,index_reads,full_reads,ratio,build_ms,index_ms,full_ms
4096,100000,2,764,765,3,764,254.7,5.915,0.042,111.758
256,100000,4,12500,12501,5,12500,2500.0,35.007,0.027,161.391
```

Las dos curvas van en el mismo archivo y se distinguen por `page_size`. `build_ms` es el
tiempo de construcción del índice, separado del tiempo de búsqueda. Sin `--full`, la fila de
`N = 100 000` deja `index_ms` y `full_ms` vacíos, que es la forma en CSV del `omitido` de la
tabla. Si el archivo no se puede abrir, el benchmark avisa por `stderr` y sigue igual: el
CSV es un extra, no un requisito.

`scripts/plot_benchmark.py` convierte ese CSV en cuatro PNG: `benchmark_timing_256.png` y
`benchmark_timing_4096.png` con el tiempo de búsqueda en el eje Y, y
`benchmark_pages_256.png` y `benchmark_pages_4096.png` con las páginas leídas. El eje X es `N`
y el Y es logarítmico en ambas métricas, con escala compartida entre los dos gráficos de cada
una. La metodología, el formato del CSV y las instrucciones de instalación están en
[`docs/benchmark-method.md`](docs/benchmark-method.md) y en la
[sección 2](#exportar-el-benchmark-a-csv-y-generar-los-gráficos).

La metodología, el formato de `FileMeta` y las limitaciones de las métricas se describen en
[`docs/benchmark-method.md`](docs/benchmark-method.md) y
[`docs/page-manager.md`](docs/page-manager.md).
