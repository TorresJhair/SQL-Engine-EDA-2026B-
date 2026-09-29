# Motor de base de datos SQL — Fase 1: Storage Engine + B+ Tree

Heap de páginas con *slotted page* y un índice B+ tree sobre él. El árbol es de
altura mínima alcanzable y sus nodos caben enteros en una página de 4096 B.

> **Estado del repo.** Este README se escribe en la rama `contract`, que es la base común
> que comparten las cuatro personas del equipo. Las secciones 5 y 6 se completan a medida
> que cada dueño entregue su módulo.

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

El menú de 6 opciones es de P4. Cuando esté escrito va a entender `--demo 0|1|4|5|6`,
`--page-size` y `--n`.

## 3. Correr los tests

```bash
ctest --test-dir build --output-on-failure     # las 7 suites
ctest --test-dir build -R storage              # una sola
./build/tests --suite storage                  # igual, sin ctest
./build/tests                                  # todas ("all" si no se pasa --suite)
```

Hay un `add_test` por suite, así que una suite roja se ve sola en el informe en vez de
desaparecer dentro de un "los tests fallan". Las siete son `storage`, `tree_printer`,
`page_manager`, `btree_node`, `btree_write`, `btree_read` e `invariants`.

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

> **Pendiente — P2 y P4.** El protocolo de medición va en `docs/benchmark-method.md` (P2, se
> escribe **antes** de medir) y la tabla con los datos en `docs/benchmark-results.md` (P4).
>
> Criterio de aceptación: las dos curvas (4096 B y 256 B) sobre base temporal propia, los 6
> valores de N canónicos (20, 500, 1e3, 5e3, 1e4, 1e5), R repeticiones, altura y ratio de
> I/Os por curva, y el punto de cruce calculado por el propio programa
> (`Benchmark::crossover`) y no escrito a mano.
