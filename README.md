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
`--page-size`, `--n` y `--full`. Para ver todas las opciones:

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
opción 5 siempre usa 256 B. `--n` limita el benchmark a un tamaño para depuración.
`--full` muestra también el tiempo del punto de 100 000 filas.

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

La metodología, el formato de `FileMeta` y las limitaciones de las métricas se describen en
[`docs/benchmark-method.md`](docs/benchmark-method.md) y
[`docs/page-manager.md`](docs/page-manager.md).
