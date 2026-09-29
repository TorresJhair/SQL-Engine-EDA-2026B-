# Plan de trabajo — Fase 1: Storage Engine + B+ Tree (v4)

Motor de base de datos relacional, Fase 1 (Almacenamiento, tuplas e índices B/B+).
Especificado en `Trab1.md`. Reparto entre 4 integrantes (P1, P2, P3, P4).

Este documento **no tiene calendario**. El proyecto se hace con el tiempo que haya, así que
lo que importa son dos cosas: (a) que las decisiones de diseño estén cerradas para que nadie
programe contra una suposición distinta, y (b) que exista un **orden de recorte** explícito
para saber qué se suelta primero si el tiempo falta. Ver §5.

## Registro de cambios

Este documento se corrige a sí mismo: cada cambio que altera el cuerpo del plan queda numerado y
anotado abajo, con su motivo y en qué sección vive. Los últimos son las correcciones del punto de
cruce (39), de los asserts (40), de la reutilización de una región mayor con una tupla menor (41),
la firma de `Benchmark` (42) y la cadena de libres por índices de slot (43). Un cambio que solo
toca la redacción no se anota.

| # | Cambio | Dónde vive en este plan |
|---|---|---|
| 34 | `SlottedPage::erase` entra en la Fase 1 y sale del orden de recorte | §1 (decisión de borrado), §5 (orden de recorte), §6 (alcance #9), §10 (riesgo 15) |
| 35 | Cada curva del benchmark corre 6 valores de `N` desde 20, y se declara el punto de cruce | §1 (las dos curvas juntas), §6 (cómo se lee), §10 (riesgo 24) |
| 36 | `Benchmark.{h,cpp}` pasa a P2; P4 conserva la presentación | §2, §3, §3.1, §4, §6, §9, §11 |
| 37 | `serialize` devuelve `Status` y `insert`/`bulkLoad` propagan `Status` | §1 (contrato), §4, §7 (regla 20), §10 (riesgo 23) |
| 38 | `NodeData` y el header de 16 B quedan congelados en la rama `contract` | §4 (`CONGELADO`), §7 (regla 19), §8 (checklist), §10 (riesgo 22) |
| 39 | **El punto de cruce lo calcula el programa** (`Benchmark::crossover`) y se corrigen los valores: 263 a 4096 B, y 17 / 24 / 25 a 256 B | §1 (tabla del cruce, fila de P2), §6 (salida y "cómo se lee esto"), §8 (checklist), §10 (riesgo 24) |
| 40 | **Las precondiciones testeadas devuelven `Status::PreconditionFailed`**, no `assert`: tupla de 0 campos y las dos de `bulkLoad` | §1 (contrato de `bulkLoad`), §3 (tests de P1 y P4), §4 (`Status`, `Tuple`, `BTree`, `HeapFile`), §7 (reglas 20 y 21), §8 (checklist), §10 (riesgo 23) |
| 41 | **Reutilizar una región mayor con una tupla menor**: el `length` no baja, `lookup` devuelve la cola y `deserialize` la ignora | §1 (cadena de libres, decisión de borrado), §3 (tests de P1), §4 (`SlottedPage`, `Tuple`), §8 (checklist), §10 (riesgo 15) |
| 42 | **`Benchmark` tiene firma en el contrato**: `BenchmarkOptions`, `Band`, `run` y `crossover`, con `crossover` devolviendo las bandas del cruce y no tres campos sueltos | §4 (`Benchmark.h`), §7 (regla 19), §8 (checklist) |
| 43 | **La cadena de libres guarda índices de directorio, no offsets**: los dos punteros son `slotID` y `0xFFFF` sigue siendo el fin | §1 (cadena de libres), §4 (`SlottedPage`), §8 (checklist) |

---

## 1. Decisiones de diseño

Cerradas. Cualquiera que implemente contra una suposición distinta a esta tabla va a romper
la integración de alguien.

| Decisión | Valor | Razón |
|---|---|---|
| Estructura de índice | **B+ Tree**, implementado en las clases `BTree` / `BTreeNode` | Datos solo en hojas + hojas encadenadas → deja listo el range scan de la Fase 2 (`WHERE x BETWEEN`) y el índice no duplica datos. El enunciado permite "B o B+" y nombra las clases `BTree`/`BTreeNode`; se respetan los nombres y el README aclara que implementan un B+ |
| Página de datos | **Slotted page**: cabecera + directorio de slots + área de datos | Es lo estándar (PostgreSQL, SQLite). Append-only no permite responder "¿cuánto espacio queda?" y se rompe con `VARCHAR` de tamaño variable. El directorio además permite borrar sin cambiar los `RowID` de las demás tuplas |
| Tamaño de página | **4096 bytes** por defecto, en archivo real | El grado `t` solo se justifica si el nodo ocupa exactamente una página; y hace comparable el contador de páginas del benchmark. Acepta `--page-size 256` para demos |
| Archivos | **Dos**: `data.db` (heap) e `index.db` (nodos B+), cada uno con su propio `PageManager` | Contadores separados (`heap` vs `índice`), y `root_page_id` tiene un lugar natural (página 0 de `index.db`) sin que P2 tenga que conocer el árbol |
| Página 0 | **Metadatos** (`FileMeta`) en ambos archivos. Los IDs de datos y nodos empiezan en 1, así que `0` significa "sin página" (p. ej. `nextLeafPageID` de la última hoja) | Un solo criterio para "ausente" en todo el proyecto |
| Caché | **Sin caché / sin buffer pool LRU** | Cada acceso a página es un `read()`/`write()` al archivo. Un buffer pool haría que "páginas leídas" fuera un conteo lógico distinto del número de accesos que pide el algoritmo |
| Qué miden los contadores | **Accesos a página**, no I/O físico de disco | El sistema operativo mantiene su propio page cache, así que un `read()` puede servirse desde RAM. La métrica fiable es el **número de páginas tocadas**; el tiempo es orientativo |
| Modo de compilación | **`Debug` por defecto** al desarrollar; **el benchmark se mide en `RelWithDebInfo`** | El desborde de un nodo es un `Status::NodeOverflow`, no un assert: se detecta igual con `-DNDEBUG`, así que medir en un build optimizado no esconde errores de formato. `Debug` queda para que los asserts de error de programación sigan vivos mientras se escribe código |
| Margen en `computeT` | La fórmula reserva **`SLACK = 1` byte** | A 4096 B no cambia nada; a 256 B garantiza que el nodo más lleno quepa. Sin margen, `t = 12` da 246 B usados de 256 y el formato queda al borde |
| Clave de índice | `int32_t` fijo (4 B) | Evita el problema de tamaño variable de `VARCHAR` dentro del nodo. Se declara como alcance del equipo en la presentación |
| Árbol vacío | **Nunca hay 0 nodos.** Si `index.db` no tiene raíz válida, el `BTree` crea una **hoja raíz con 0 claves**, `nextLeaf = 0`, altura 1. Si la hay, la usa tal cual | Así `search`, `size`, `printTree` y el verificador de invariantes no necesitan un caso "sin raíz", y reabrir el archivo no destruye el árbol |
| Altura | Número de **niveles**: una hoja raíz = altura 1 | Un solo criterio para `height()`, el log y los tests |
| Ocupación mínima | Todo nodo **salvo la raíz** tiene entre `t-1` y `2t-1` claves (interno: entre `t` y `2t` hijos). La **raíz queda exenta**: hoja raíz con 0 a `2t-1` claves; raíz interna con al menos 2 hijos | Es la condición estándar del B+; hace explícito qué debe verificar el test invariante |
| Duplicados | Permitidos. **Invariante:** hijo izquierdo ≤ separador ≤ hijo derecho. `insert` desciende por la **derecha** en igualdad (`upperBound`) e inserta tras las claves iguales; `search` desciende por la **izquierda** (`lowerBound`) y recorre `nextLeaf` mientras la clave coincida | Con este par de reglas las copias de una clave pueden cruzar el borde de hojas sin que `search` pierda ninguna |
| `split()` | **Función pura, sin I/O ni asignación de páginas.** Recibe el `PageID` del nodo derecho ya asignado, deja `*this` como nodo izquierdo y devuelve `{right, promoted_key}` en memoria. La propagación en `BTree::insert` usa pila de ancestros y **bucle, no recursión** | Un solo camino de código visible para explicar "el split sube hasta la raíz". Permite testear `split` sin árbol ni archivos, y `split` no necesita el serializador para existir |
| Sobrecarga en memoria | `insertInNode` admite hasta `2t` entradas **en memoria**; `serialize` exige `≤ 2t-1` y devuelve `Status::NodeOverflow` si no se cumple | El nodo desbordado nunca llega a disco: se divide antes de escribirse, y si aun así se intentara, se devuelve el error en vez de escribir una página rota |
| `bulkLoad` | **Reparto uniforme por niveles** (ver abajo). Exige árbol vacío y entrada ordenada de forma no decreciente, y **devuelve `Status::PreconditionFailed` si no se cumple** (no es un assert: se testea sin `fork` y existe en el build del benchmark). Nodos lo más llenos posible | El reparto uniforme garantiza el mínimo de ocupación sin casos especiales para "la última hoja" |
| Logs | `[SPLIT]` y `[ALTURA]` se emiten **solo** desde `BTree::logSplit` y `BTree::logHeight` (definidos por P3). `bulkLoad` los llama, no los reimplementa | Un solo formato; si cambia, cambia en un solo lugar |
| Dependencias externas | **Cero** (los tests usan un mini-harness propio) | El repo compila en cualquier máquina sin red |
| Visualización | ASCII en consola. `exportDot` (Graphviz) es **prescindible**: es lo primero que se recorta | ASCII basta para la demo; Graphviz es una mejora, no un requisito |
| Aislamiento de los `.db` | `reset_databases()` se llama **una vez al inicio de cada ejecución**. Las opciones 1-4 comparten `data.db` e `index.db`; **las opciones 5 y 6 usan bases temporales propias** (ver §1 bis). Los tests usan rutas temporales | `bulkLoad` exige árbol vacío. Y la opción 4 (árbol) tiene que ver el árbol que dejó la 3 (bulk load). La 5 corre con 256 B y la 6 necesita dos índices distintos: si compartieran archivo, el `page_size` no cuadraría y el `bulkLoad` de la 6 devolvería `Status::PreconditionFailed` |
| **Política de `HeapFile`** | **Cursor de última página con espacio.** `insert` solo mira esa página; si no cabe, asigna una nueva y avanza el cursor. Nunca reexamina páginas anteriores. **Además escribe la página en cada `insert`** (no difiere la escritura al `flush`), así el total de escrituras es 1 por tupla insertada | Con escaneo lineal, insert es O(n²): 100 000 tuplas contra 765 páginas son 76 M lecturas. Escribir en cada insert hace que el contador de escrituras sea una cifra predecible y testeable (1 000 tuplas = 1 000 escrituras) |
| **Borrado de tuplas** | `SlottedPage::erase` **no compacta**: la región liberada se encadena en una lista de libres. El `length` original se conserva en el directorio y el puntero de la cadena vive en los 2 primeros bytes de la región liberada. `insert` reutiliza la primera región que le sirva; si no hay, escribe al final. `freeSpace` **suma** los `length` de los slots libres. Al reutilizar, el `length` **no baja**: una tupla menor deja cola, `lookup` devuelve la región entera y `Tuple::deserialize` ignora la cola (cambio 41) | Compactar movería los bytes de los slots siguientes y obligaría a reescribir sus offsets. Como el directorio se indexa por `slotID` y no por offset, **los `RowID` de las demás tuplas no cambian igual**. **Entra en la Fase 1** (§5): el formato ya lo incluye, así que sacarlo sería cambiarlo a mitad de camino |
| **Esquema del dataset** | 2 campos: `INT key` (la clave del índice) y `VARCHAR name` de **16 bytes fijos** para el benchmark. `key` = 1..N **sin repetidos** | Longitud fija hace el número de páginas reproducible y explicable en la presentación. Con `key` única, `search` devuelve exactamente 1 fila y la comparación con el Full Scan no se confunde. Los VARCHAR variables y los duplicados se cubren en los tests, no en el benchmark |
| **Tuplas por página** | No es una constante: es **`tuplesPerPage(pageSize) = (pageSize - 8) / 31`** y el programa la imprime. Da **131** con 4096 B y **8** con 256 B | Con un `131` fijo, la curva de 256 B mostraría un número de páginas del heap que no corresponde a su `page_size` |
| **Full Table Scan** | Recorre **todas** las páginas de datos, deserializa cada tupla y compara `key`. **No se detiene** en la primera coincidencia | Es lo que `Trab1.md` §2 dice que el índice evita. Parar en el primer match haría depender la comparación de dónde cae la clave, o sea, acomodarla al caso favorable |
| **Métrica del benchmark** | **Páginas de datos leídas por una búsqueda completa**: Index Scan = `altura` (el descenso, una página por nivel) + 1 (leer la tupla del heap) = **`altura + 1`**; Full Scan = **páginas de datos** del heap | `altura` es el número de niveles (línea de la tabla de arriba), así que el descenso toca exactamente `altura` páginas del índice. El tamaño total del índice crece linealmente con `n` también; lo O(log n) son los accesos por búsqueda. Medir el tamaño del índice no demuestra nada y se presta a que el jurado pregunte |
| **Qué cuenta el Full Scan** | **No** cuenta la página 0 de metadatos: se leyó una vez al abrir y `readMeta` no suma al contador. Con 100 000 tuplas el archivo tiene 765 páginas y **el escaneo lee 764** | Si se cuentan las dos cosas (765 contra 764) según el `page_size`, la comparación queda inflada sin querer. Se distinguen explícitamente en la pantalla: tamaño del archivo vs. lecturas |
| **Construcción del índice** | El benchmark construye con `bulkLoad`. El tiempo de construcción **se mide y se reporta aparte**, pero no entra en el tiempo de búsqueda | `bulkLoad` es lo que el enunciado pide demostrar. Medir solo la búsqueda es la comparación honesta |
| Convenciones | `RowID{pageID, slotID}`, `nextLeafPageID`, `rightPageID`, `FileMeta` | Se propagan a 4 módulos; mezcladas, rompen la integración |

### Formato binario de la tupla y de la slotted page (cambio 10)

Fijado byte a byte porque `HeapFile` (P2), `DataGen` (P1) y el benchmark (P2) dependen de estos
números. Todo en **little-endian**.

**Tupla:**

```
[nFields: u8]
y por cada campo:  [tag: u8][len: u16][datos: len bytes]
tag 0 = INT    -> len = 4, int32_t con signo
tag 1 = VARCHAR -> len = longitud arbitraria, sin terminador
```

**Regla de la tupla mínima: `nFields >= 1`** (devuelve `Status::PreconditionFailed`, no assert:
cambio 40). No es un capricho de estilo: una tupla sin campos mediría **1 B**, y el puntero de la
cadena de libres necesita **2 B** dentro de la región liberada (ver abajo). Con al menos un campo,
la tupla más pequeña posible es **4 B** —`[nFields:1]` + un `VARCHAR` vacío de `tag+len` = 3—, y
siempre hay lugar para el puntero. Por eso existe el test de `VARCHAR` vacío: fija ese 4 B.

**Página de datos (slotted page):**

```
offset 0:  [nSlots: u16][dataStart: u16][dataEnd: u16][freeHead: u16]      = 8 B
offset 8:  directorio de slots, 4 B por entrada, en orden de inserción:
             [offset: u16][length: u16]
           un slot libre conserva su length original; la cadena de libres
           avanza por los 2 primeros bytes de la region liberada:
             [indice de directorio de la siguiente region libre: u16]   (0xFFFF = fin de cadena)
datos:     desde dataEnd (inclusive) hasta el final de la pagina
```

`dataStart` es **el fin del directorio, `8 + 4 * nSlots`**: la dirección más baja a la que puede
llegar el área de datos. `dataEnd` es la dirección más baja **ocupada**, y solo baja. Al
deserializar se valida que `dataStart == 8 + 4 * nSlots`; si no, `Status::Corrupt`.

Los bytes de cada tupla se escriben **hacia atrás desde el final de la página**, así que
`dataEnd` solo baja. Consecuencias que hacen el diseño simple y sin sorpresas:

- `erase(slot)` **enlaza la región liberada** en la cadena de libres: copia el índice de directorio
  de la región siguiente en los 2 primeros bytes de la región liberada y actualiza `freeHead`.
  **No compacta**: no mueve un solo byte de los datos, y el `length` original se queda en el
  directorio, que es lo que permite saber cuánto espacio hay y cuánto mide.
- `insert` recorre la cadena de libres y reutiliza la **primera región cuyo `length` original
  alcance** (no están ordenadas por tamaño, así que recorre toda la cadena); si ninguna alcanza,
  escribe al final y baja `dataEnd`. Reutilizar una región no la divide: se ocupa entera.
- **Al reutilizar, el `length` del directorio NO baja** (cambio 41). Si entra una tupla de 4 B en una
  región liberada de 27 B, el directorio sigue diciendo 27. Dos consecuencias, y las dos son
  deliberadas:
  - **`lookup` devuelve 27 B, no 4**: los bytes de la cola son basura (los que dejaba la tupla vieja,
    porque `erase` no pone nada a cero) y `Tuple::deserialize` tiene que **ignorarlos sin dar
    error**: lee `[nFields]` y sus `nFields` campos, y se detiene ahí. Solo devuelve
    `Status::Corrupt` si un `len` **declarado** se pasa del final. Sin esta regla, reutilizar una
    región grande con una tupla pequeña daría `Corrupt` sobre datos válidos.
  - **`freeSpace()` baja en el `length` completo de la región (27), no en el tamaño de la tupla
    nueva (4).** El slot deja de estar libre y sale de la suma, así que la caída es de 27 aunque la
    tupla solo ocupe 4 B. Los 23 B de cola quedan **muertos**: no se cuentan como libres y no
    volverían a usarse hasta otro `erase` de ese mismo slot. Por eso `freeSpace()` vuelve
    exactamente al valor que tenía antes del `erase`. La fragmentación es el precio de no compactar,
    y el first-fit sin ordenar por tamaño la agrava. No es un bug: es lo que pasa cuando no se
    mueven bytes. Y `freeSpace()` **no sobredeclara**: los 23 B no se suman justamente porque no
    sirven para nada, ya que están dentro de una región ocupada.
- **El orden al reutilizar importa:** se **desenlaza la región de la cadena primero** y se escribe
  después. Los 2 primeros bytes de la región liberada guardan el índice de directorio de la
  siguiente, y la tupla nueva los pisa; si se escribiera antes de desenlazar, se perdería el resto
  de la cadena.
- `freeSpace()` = `(dataEnd - dataStart)` **+ Σ `length` de los slots libres del directorio**: el
  espacio total del que se puede escribir, contando lo reutilizable.
- **El `+4` que casi nadie se acuerda de sumar:** escribir al final también cuesta la **entrada de
  directorio nueva**, y eso empuja `dataStart` 4 B hacia arriba. O sea, para appendear hace falta
  `dataEnd - (dataStart + 4) >= len`, no solo `freeSpace() >= len`. Reutilizar una región libre, en
  cambio, no cuesta directorio (el slot ya existe). Consecuencia numérica que se testea: con 131
  tuplas de 27 B, `freeSpace()` da **27**, pero la 132.ª **no entra** (`27 < 27 + 4`) y devuelve
  `Status::PageFull`. Un `freeSpace() >= len` ingenuo aceptaría esa tupla y rompería la página.
- `SlottedPage::insert` lleva un assert `bytes.size() >= 2` (el único productor real es
  `Tuple::serializeTo`, que garantiza 4 B). Existe porque el contrato acepta bytes crudos: sin él,
  un llamador podría meter una región de 1 B y romper la cadena de libres.
- **La tupla más pequeña posible mide 4 B** (un `VARCHAR` vacío), así que toda región liberada
  tiene sitio para su puntero de 2 B. No existe el caso `length < 2`, y por eso el puntero puede
  vivir dentro de la región liberada y no aparte.
- Capacidad: `n` tuplas caben si `8 + 4n + Σlen ≤ page_size`. Con el esquema del benchmark, sin
  borrados, `n = (page_size - 8) / 31`.
- **Los `RowID` de las demás tuplas no cambian nunca**: el directorio se indexa por `slotID`, no
  por offset. Aunque un `erase` libertara espacio, ninguna otra tupla se mueve de posición lógica.

**Con el esquema del dataset:** tupla de 27 B (`1 + 7 + 19`), más 4 B de directorio = **31 B por tupla**.

```
131 tuplas por página de 4096 B     (8 + 31*131 = 4069 <= 4096;  132 no cabe: 4100 > 4096)
  8 tuplas por página de  256 B     (8 + 31*8   =  256 <= 256;    9 no cabe:  287 > 256)
```

`tuplesPerPage(pageSize) = (pageSize - 8) / 31` no es un número fijo: depende del `page_size`, y
el programa lo **imprime** en cada curva. De ahí sale, y se puede calcular a mano delante del
jurado, el número de páginas del heap: `ceil(N / tuplasPorPagina) + 1` (la `+1` es la página de
metadatos).

```
N =  1 000 ->   8 páginas de datos + 1 meta =  9      (4096 B)
N = 10 000 ->  77 páginas de datos + 1 meta = 78      (4096 B)
N = 100 000 -> 764 páginas de datos + 1 meta = 765     (4096 B)
N = 100 000 -> 12 500 páginas de datos + 1 meta       (256 B, 8 tuplas/página)
```

**El programa imprime esta fórmula junto a los números** (`31 B/tupla`, `131 tuplas/página`,
`765 = ceil(100000/131) + 1`), porque el bloque de P2 en la demo consiste justamente en explicar
de dónde sale ese número. Y aclara que de esas 765 páginas, **764 son de datos**: la 765ª es la de
metadatos y el escaneo no la cuenta.

Los VARCHAR de longitud variable, vacíos, y las tuplas mayores que una página se cubren en los
tests de P1, no en el dataset del benchmark.

### Cálculo de `t` (va al README y a la presentación)

Página 4096 B, header de nodo 16 B, **`RowID` de 6 B en disco** (`pageID` u32 + `slotID` u16, sin
padding: se serializa campo por campo en little-endian), puntero a hijo 4 B, `SLACK = 1`:

> Ojo, `sizeof(RowID)` en C++ es **8 B** por el padding del `struct`. El cálculo usa los **6 B
> serializados**, que es lo que ocupa en la página. Si alguien usa `sizeof` en la fórmula, `t`
> sale mal; por eso el serializador escribe los campos a mano.

```
Interno:  16 + 4*(2t) + 4*(2t-1) + 1 <= 4096   ->  16t + 13 <= 4096   ->  t <= 255
Hoja:     16 + 10*(2t-1)        + 1 <= 4096   ->  20t +  7 <= 4096   ->  t <= 204
```

La hoja es la restricción más estrecha -> **`t = 204`**.
Máximo 407 claves por nodo, 408 hijos por nodo interno.

`t` depende del tamaño de página, así que se calcula en tiempo de ejecución con
`BTreeNode::computeT(pageSize)` (mínimo entre `tHoja` y `tInterno`, ambas con `SLACK`) y se pasa
a cada nodo. Valores de referencia que los tests deben fijar:

| `pageSize` | `t` | Hoja más llena | Interno más llena |
|---|---|---|---|
| 4096 | 204 | 16 + 407×10 = **4086** B de 4096 | 16 + 408×4 + 407×4 = **3276** B de 4096 |
| 256 | 12 | 16 + 23×10 = **246** B de 256 | 16 + 24×4 + 23×4 = **204** B de 256 |

El `SLACK` es lo que separa "cabe" de "no cabe" cuando el margen es de un 4%, como en 256 B. Con
4096 B la restricción es la hoja; con 256 B también, y por eso `t = 12` y no 15 (el interno
aguantaría 15, la hoja no).

> **Para la demo en vivo:** con `t = 204` no ocurre ni un solo split hasta ~408 inserciones.
> Por eso la opción 5 de la demo usa `--page-size 256`, que da `t = 12` y hace visibles los splits
> uno a uno. El parámetro se aplica a ambos archivos (`data.db` e `index.db`), y por eso la
> opción 5 trabaja sobre **bases temporales propias** (§1 bis), no sobre las de la demo.
> Se documenta en el README como "tamaño de página reducido para fines de demostración".

### `bulkLoad`: reparto uniforme por niveles

```
Nivel de hojas:   n entradas -> k = ceil(n / (2t-1)) hojas.
                  Cada hoja recibe floor(n/k) o ceil(n/k) entradas.
Niveles internos: m hijos    -> j = ceil(m / 2t) nodos.
                  Cada nodo recibe floor(m/j) o ceil(m/j) hijos.
Se repite subiendo de nivel hasta que j = 1: ese nodo es la raíz.
Separador entre dos hijos consecutivos = clave mínima del subárbol derecho.
```

**Por qué cumple el mínimo (va a `docs/btree-insert.md`):**

- Hojas, k >= 2: `n >= (k-1)(2t-1) + 1`, luego `n/k >= 2t-1 - (2t-2)/k`, que es creciente en `k`; el mínimo está en `k = 2` y da `n/k >= t`, luego `floor(n/k) >= t` **claves por hoja**. El peor caso real es exactamente `t` (se alcanza en `n = 2t`: dos hojas de `t` claves).
- Internos, j >= 2: `m >= (j-1)*2t + 1`, luego `m/j >= 2t - (2t-1)/j`, también creciente en `j`; el mínimo está en `j = 2` y da `m/j >= t + 1/2`, luego `floor(m/j) >= t` **hijos**, y un nodo con `c` hijos tiene `c-1` claves, o sea **`>= t-1` claves**.
- Las dos cuentas cumplen la regla clásica del B-tree (**todo nodo salvo la raíz tiene al menos
  `t-1` claves**), y `bulkLoad` además deja las hojas con `t` claves o más: la holgura que deja
  es de al menos media página, que es lo que un `insert` posterior aprovecha antes de dividir.
- Si k = 1 hay una sola hoja y es la raíz (exenta). Si j = 1 ese nodo es la raíz (exenta, y tiene >= 2 hijos porque el nivel de abajo tenía >= 2 nodos).
- n = 0 -> hoja raíz vacía (árbol vacío, sin construir nada).
- Cada nivel construido llama a `logHeight` (de P3), así el log de `bulkLoad` tiene el mismo formato que el crecimiento por `insert`.

**Reglas de uso que se olvidan fácil y hay que escribir en el contrato:**

- `bulkLoad` **exige árbol vacío**: si no, devuelve `Status::PreconditionFailed` y no modifica nada.
  No se puede llamar dos veces sobre el mismo `BTree` sin reconstruirlo: la opción 6 del benchmark
  abre bases nuevas por eso.
- La entrada debe estar **ordenada de forma no decreciente**: si no, `Status::PreconditionFailed` y
  el árbol intacto. `DataGen` garantiza las dos cosas; nadie las arma a mano salvo los tests de caso
  negativo.
- `bulkLoad` **reconstruye desde cero** y escribe todas las páginas al final, así que su costo es
  lineal en `n` y no usa `split()` en ningún momento.

Con duplicados el reparto sigue siendo válido: al ser la entrada ordenada, el separador
(mínimo del subárbol derecho) cumple `izquierdo <= separador <= derecho`.

### El número de páginas del índice, y por qué hace falta la segunda curva

Cada curva corre **6 valores de `N`**: los tres del enunciado (1 000, 10 000, 100 000), más
500 y 5 000 para que la curva no salte de golpe, más **20**, que es el que está **por debajo del
punto de cruce**. Con `t = 204`, `2t-1 = 407` claves por hoja:

| N | Hojas | Nodos internos | Total páginas de índice | Altura |
|---|---|---|---|---|
| 20 | 1 | 0 (la hoja es la raíz) | 1 | 1 |
| 500 | 2 | 1 (raíz) | 3 | 2 |
| 1 000 | 3 | 1 (raíz) | 4 | 2 |
| 5 000 | 13 | 1 (raíz) | 14 | 2 |
| 10 000 | 25 | 1 (raíz) | 26 | 2 |
| 100 000 | 246 | 1 (raíz) | 247 | 2 |

Casi todos los tamaños caben en **un solo nodo interno**: la altura se queda en 2. La curva con
4096 B demuestra que el número de páginas de datos que toca una búsqueda **no crece** con `n`, que
es justo lo que importa, pero no muestra la forma logarítmica.

Con `--page-size 256` (`t = 12`, `2t-1 = 23`) la altura sí sube:

| N | Hojas | Nivel 2 | Nivel 3 | Raíz | Total | Altura |
|---|---|---|---|---|---|---|
| 20 | 1 | - | - | 0 | 1 | 1 |
| 500 | 22 | - | - | 1 | 23 | 2 |
| 1 000 | 44 | 2 | - | 1 | 47 | 3 |
| 5 000 | 218 | 10 | - | 1 | 229 | 3 |
| 10 000 | 435 | 19 | - | 1 | 455 | 3 |
| 100 000 | 4 348 | 182 | 8 | 1 | 4 539 | 4 |

Ahí se ve el crecimiento logarítmico: `1 000 -> 10 000 -> 100 000` multiplica `n` por 100, y
la altura sube de 3 a 4. **Las dos curvas van en la demo** (cambio 13).

#### Las dos curvas juntas: dónde el índice empieza a ganar

Esta es la tabla que se imprime en la opción 6, y la que se lee en voz alta. "Escaneo" son las
páginas de datos que toca un Full Table Scan; "índice" son las páginas de índice que desciende
una búsqueda **más 1** de datos, o sea `altura + 1`:

| N | 4096: escaneo | 4096: índice | 256: escaneo | 256: índice | ratio escaneo/índice 4096 | ratio escaneo/índice 256 |
|---|---|---|---|---|---|---|
| 20 | 1 | 2 | 3 | 2 | 0,5× | 1,5× |
| 500 | 4 | 3 | 63 | 3 | 1,3× | 21× |
| 1 000 | 8 | 3 | 125 | 4 | 2,7× | 31,3× |
| 5 000 | 39 | 3 | 625 | 4 | 13× | 156,3× |
| 10 000 | 77 | 3 | 1 250 | 4 | 25,7× | 312,5× |
| 100 000 | 764 | 3 | 12 500 | 5 | 254,7× | 2 500× |

Ojo con la columna de 256 B: son **páginas de datos**, no del archivo. El archivo de 100 000
tuplas a 256 B tiene 12 501 páginas (12 500 de datos + 1 de metadatos), y el escaneo lee 12 500.
Contar la de metadatos inflaría el escaneo en 1 lectura que no paga (riesgo 18).

**El punto de cruce, dicho con todas sus letras (cambio 35, corregido en el 39).** El índice
**pierde** mientras el heap entero cabe en una o dos páginas, porque ahí el escaneo ya lee 1 o 2 y
el índice necesita `altura + 1` ≥ 2. La cuenta fina es esta, y sale del programa:

| `page_size` | El escaneo gana | Empate | El índice gana | ¿Y después? |
|---|---|---|---|---|
| 4096 B | `N ≤ 131` (escaneo = 1, índice = 2) | `132–262` (2 contra 2) | **`desde N = 263`** (3 contra 2) | Gana siempre |
| 256 B | `N ≤ 8` (escaneo = 1, índice = 2) | `9–16` (2 contra 2) | `17–23` (3 contra 2) | **empate suelto en `N = 24`** (3 contra 3) y **gana desde `N = 25`** |

La trampa de este cálculo es la altura. Mientras el árbol es **una sola hoja** (`N ≤ 2t-1`, o sea
407 a 4096 B y 23 a 256 B) el índice cuesta **2**, no 3: `altura + 1` con altura 1. Toda la zona de
empate cae dentro de ese rango, así que usar 3 en vez de 2 desplaza el cruce y lo arruina. Por eso
`263` y no el `394` que dio una versión anterior de este plan.

Con 256 B aparece además una **isla de empate en `N = 24`**: el árbol acaba de crecer a altura 2 y el
índice sube a 3 justo cuando el escaneo todavía lee 3. No es un error de la tabla, es el comportamiento
real, y por eso el cruce se calcula recorriendo `N` de a uno en vez de suponiendo que las zonas son
un único intervalo.

**Los dos números los calcula el programa, no están escritos a mano.** `Benchmark::crossover(page_size,
tuplas_por_pagina)` recorre `N = 1 … 100 000`, clasifica cada valor en `escaneo | empate | índice` y
devuelve las transiciones; la opción 6 las imprime. El tope está justificado porque más allá del
último corte el escaneo crece 1 página cada `tuplas_por_pagina` mientras el índice solo sube 1 página
cada `2t-1 × 2t` claves: a 4096 B el escaneo ya va por 1268 páginas cuando el índice va por 4, así que
el índice no vuelve a perder (comprobado hasta `N = 1 000 000`). Un test fija los valores de esta
tabla contra la salida del programa, para que si la fórmula de `t` o la de `altura` cambian, el cruce
cambie con ella y no se quede escrito un número viejo.

Eso no se esconde: la fila de `N = 20` está puesta **a propósito** para que se vea que con 20
tuplas el escaneo gana. Una curva que empiece en `N = 1 000` y no muestre el cruce sería la forma
más fácil de que alguien lo ataque y no tuviera respuesta; con esa fila la respuesta es una
frase, y sale del propio output del programa.

### Metadatos de archivo (`FileMeta`, página 0)

```
magic: u32 | page_size: u32 | page_count: u32 |
root_page_id: u32 | height: u32 | record_count: u64
```

- `data.db` usa `page_size`, `page_count` y `record_count`.
- `index.db` usa `page_size`, `page_count`, `root_page_id` y `height`.
- Los campos que no aplican quedan en 0.
- `BTree` guarda `root_page_id` en memoria al abrir y solo llama a `writeMeta` cuando la raíz cambia (split de raíz o `bulkLoad`).
- `readMeta`/`writeMeta` **no** se cuentan en `pageReads`/`pageWrites`. Se documenta en `docs/benchmark-method.md`.
- **Dueño de cada campo:** `PageManager` mantiene `page_count` (es el que asigna) y lo graba en
  `writeMeta`. `HeapFile` mantiene `record_count` en memoria, lo incrementa en cada `insert` y lo
  graba en su `flush`. `BTree` mantiene `root_page_id` y `height`. Nadie más toca la página 0.
- **Al abrir un archivo existente**, `PageManager::open` valida el `magic` y el `page_size` del
  archivo contra el `page_size` pedido y devuelve `Status::Corrupt` si no cuadran. Sin esta
  validación, abrir un `data.db` de 4096 B pidiendo 256 B leería páginas desalineadas y
  produciría basura silenciosa (riesgo 20 de §10).

### §1 bis · La demo es una sola ejecución

Con `--demo 0` las seis opciones corren en **el mismo proceso**. Eso obliga a decidir qué
archivos usa cada una, porque `page_size` es un parámetro de construcción del `PageManager` y
`bulkLoad` exige árbol vacío:

| Opción | Archivos | `page_size` | Por qué |
|---|---|---|---|
| 1 Storage | `data.db` | el de la corrida | escribe en el heap real |
| 2 Pager | `data.db` | el de la corrida | tiene que ver el archivo que dejó la 1 |
| 3 Bulk load | `index.db` | el de la corrida | construye el índice real, sobre árbol vacío |
| 4 Árbol | `index.db` | el de la corrida | tiene que imprimir el árbol que dejó la 3 |
| 5 Split | **base temporal propia** | **256 B** | con 4096 B no hay ni un split: necesita otro `page_size` |
| 6 Benchmark | **base temporal propia** | 4096 y luego 256 | necesita dos índices distintos y medir su construcción |

Consecuencias que quedan escritas para que nadie las descubra en la demo:

- `reset_databases()` borra `data.db` e `index.db` **una vez**, al arrancar. Las opciones 5 y 6 no
  los tocan, así que el archivo real sigue intacto después de correrlas.
- La opción 3 se ejecuta **una vez por proceso**. Si el menú interactivo la repite, avisa con un
  mensaje claro (`"el índice ya fue construido en esta ejecución; use otra corrida"`) en vez de
  assertar.
- La opción 6 hace `reset` de su base temporal **entre curvas**, porque las dos necesitan un árbol
  vacío y con el `page_size` contrario.
- La opción 5 abre y cierra sus propios `PageManager`; no comparte estado con nadie. Por eso puede
  convivir con 256 B mientras el resto de la sesión sigue en 4096 B.
- Ningún `.db` se abre con dos `page_size` distintos en el mismo proceso.

**Por qué esta tabla es parte del plan y no un detalle de implementación:** sin ella, la demo de
12 minutos se cae en el minuto 8 (opción 5) o en el 10 (opción 6), y son los dos bloques que más
puntos valen.

---

## 2. La costura entre personas

`Trab1.md` §2.1 lista tres componentes. La costura del árbol cae exactamente entre el segundo y el tercero:

> **P3 = "Estructura del Árbol B" + "Operaciones de Balanceo"** · **P4 = "Búsqueda e Index Scan"**

Ningún algoritmo queda partido por la mitad: la promoción de la clave, el `split`, el
serializador del nodo y el bucle que lo sube a la raíz están enteros en P3.

**Detalle de propiedad de archivos:** `BTree` es una sola clase repartida en dos `.cpp` — P3
escribe `BTreeWrite.cpp`, P4 escribe `BTreeRead.cpp`, ambos sobre el mismo `BTree.h` que P3
posee. Sin esto los dos editan el mismo archivo y se pisan.

**Código compartido, con un solo dueño cada uno:**

| Pieza | Dueño | Quién la usa |
|---|---|---|
| `Value` / `Tuple` completos, con su formato en bytes | **P1** | `HeapFile` (P2), `indexScan` (P4), `DataGen` (P1) |
| `DataGen` (`sorted`, `randomKeys`, `datasetTuple`) | **P1** | `Benchmark` (P2), test de estrés (P3), heaps de los tests de integración (P4) |
| `Status` (incluido `Corrupt` y `NodeOverflow`) y `RowID` de `Types.h` | **P1** | los cuatro |
| `TestHarness.h` + `test_main.cpp` (macros, `--suite`) | **P1** | los cuatro |
| `BTreeNode` entero: `NodeData` (con `t`), accesores, `computeT`, `insertInNode`, `split`, `serialize`/`deserialize` | **P3** | `BTree` (P3) al leer y escribir; `bulkLoad` (P4) |
| `findLeaf(key, bias, ancestors*)` con la política de duplicados | **P3** | `insert` (P3) y `search` (P4) |
| `logSplit(...)`, `logHeight(...)` y la bandera `verbose_` | **P3** | `insert` (P3) y `bulkLoad` (P4) |
| `readNode()`, `rootPageID()`, `pages()` y los accesores de `BTreeNode` | **P3** | `TreePrinter` (P1), `Benchmark` (P2) y los tests |
| `Metrics` (agrega los dos `PageManager` y el `chrono`) | **P2** | `Benchmark` (P2), que es suyo; nadie más lo usa |
| `Benchmark.{h,cpp}` (las dos curvas, sobre base temporal) | **P2** | `main.cpp` (P4) lo invoca; **P4 lo presenta** en la opción 6 y responde la conclusión, P2 responde la metodología |
| `checkInvariants(const BTree&)` en `tests/TreeInvariants.h` | **P4** | tests de P3 y de P4 |
| `CMakeLists.txt` (con `Debug` por defecto) y `src/main.cpp` (menú) | **P4** | todo el proyecto |

**Lectura de nodos para la visualización:** P1 escribe `TreePrinter` leyendo el árbol solo a
través de `rootPageID()`, `readNode()` y los accesores de `BTreeNode` (`keyAt`, `childAt`,
`rowIDAt`, `nextLeaf`). No toca los internos del árbol.

---

## 3. Reparto de trabajo

| # | Módulo | Archivos exclusivos | Qué implementa |
|---|---|---|---|
| **P1** | **Formato binario + visualización** | `common/Types.h`<br>`storage/Value.{h,cpp}`<br>`storage/Tuple.{h,cpp}`<br>`storage/SlottedPage.{h,cpp}`<br>`index/TreePrinter.{h,cpp}`<br>`bench/DataGen.{h,cpp}`<br>`tests/TestHarness.h`<br>`tests/test_main.cpp` | `PageID`, `SlotID`, `Page`, `Status`, `RowID{pageID, slotID}` + `operator==` / `<`; `Value` (INT \| VARCHAR) con el formato `[tag][len][datos]` de §1; `Tuple` = `vector<Value>` serializado con `[nFields]`; slotted page de 8 B de cabecera + directorio de 4 B por slot y **cadena de slots libres**, con `insert`/`lookup`/`erase`/`freeSpace` **puros, sin I/O** y **sin compactar**; `printTree` en ASCII (y `exportDot`, prescindible); `DataGen` con las tres variantes del contrato de §4 (ordenadas, aleatorias, tupla del benchmark); **el mini-harness de tests y su `main()`** con el flag `--suite` |
| **P2** | **I/O y medición** | `storage/PageManager.{h,cpp}`<br>`storage/HeapFile.{h,cpp}`<br>`bench/Metrics.{h,cpp}`<br>`bench/Benchmark.{h,cpp}` | `allocate/read/write/flush` sobre archivo de `page_size` B; **validación de `magic` y `page_size` al abrir**; página 0 de metadatos con `readMeta`/`writeMeta`; **sin caché**; `read` es `const` con contadores `mutable`; contadores `pageReads`/`pageWrites`; `HeapFile::insert → RowID` con **cursor de última página con espacio**, **escribiendo la página en cada `insert`**, y `get(RowID)`; Full Table Scan (`Scan`) que recorre todas las páginas y deserializa cada tupla; `Metrics` que suma los dos `PageManager` y el `chrono`; **`Benchmark` con las dos curvas (4096 y 256) sobre base temporal propia**, la métrica de páginas de datos, `R` repeticiones con mediana y **`crossover(page_size, tuplas_por_pagina)`**, que recorre `N = 1 … 100 000` y devuelve las bandas `escaneo \| empate \| índice` del cruce |
| **P3** | **Nodo + balanceo** | `index/BTreeNode.{h,cpp}`<br>`index/BTree.h`<br>`index/BTreeWrite.cpp` | `NodeData` (la representación en memoria del nodo, **con `t` dentro**) con `data()`/`fromData()`; header fijo de 16 B `[isLeaf:1][keyCount:2][self:4][next:4][t:2][res:3]`; `computeT` con `SLACK`; `isLeaf/keyCount/childCount/keyAt/childAt/rowIDAt/nextLeaf/searchInNode/upperBound/insertInNode`; **`split(rightPageID)` puro → `{right, promotedKey}`** (que graba `right.self`); **`serialize`/`deserialize`** (nodo ⇄ `Page`, este último devuelve `Status` y valida `≤ 2t-1`); `findLeaf`; **`insert` con pila de ancestros y propagación en bucle**; persistencia de la raíz vía `FileMeta`; `height()`, `rootPageID()`, `readNode()`, `pages()`; **`logSplit`, `logHeight` y `setVerbose`** (formato único de logs) |
| **P4** | **Lectura + infra + integración** | `index/BTreeRead.cpp`<br>`tests/TreeInvariants.h`<br>`src/main.cpp`<br>`CMakeLists.txt`<br>`.gitignore`<br>`README.md` | `search → vector<RowID>` (con seguimiento de `nextLeaf` para duplicados); `bulkLoad` bottom-up con **reparto uniforme** (no llama a `split()`; llama a `logHeight` de P3); `indexScan → vector<Tuple>` (índice → RowID → `HeapFile::get`); `size()` recorriendo hojas; `checkInvariants`; **presenta** el benchmark de P2 (opción 6: las dos curvas y la conclusión) sin escribir su código; **menú de `main.cpp`** con las 6 opciones y el reparto de archivos de §1 bis; `CMakeLists.txt` con fuentes explícitas, `Debug` por defecto y `-Wall -Wextra`; `.gitignore`; ensambla el `README.md` |

### Tests — cada quien prueba su propio módulo

Todos los tests que construyen árboles verifican `checkInvariants` (de `tests/TreeInvariants.h`, P4) al final.

| Dueño | Archivo | Casos que debe cubrir |
|---|---|---|
| **P1** | `tests/test_storage.cpp` | VARCHAR vacío (**fija la tupla mínima de 4 B**, de la que depende el puntero de la cadena de libres) · **`Tuple` con 0 campos → `Status::PreconditionFailed`** (por eso existe la regla `nFields >= 1`; no es un assert, se comprueba sin `fork`) · VARCHAR de longitud máxima que aún cabe en una página vacía · tupla mayor que una página vacía → `Status::TupleTooLarge` · `INT` negativo y `INT_MIN` · página sin espacio → `Status::PageFull` · round-trip `serialize→deserialize` byte a byte · **`131` tuplas caben en una página de 4096 y la 132 da `Status::PageFull`**; con 256 B caben 8 y la 9 da `PageFull` (fija los dos números de §1) · **el `+4` del directorio**: tras 131 tuplas de 27 B, `freeSpace() == 27` y aun así un `insert` de 27 B da `Status::PageFull`, porque la entrada de directorio nueva empuja `dataStart` 4 B arriba (si el test usara `freeSpace() >= len`, aceptaría la tupla y rompería la página) · `erase` **no mueve un byte** de los datos de los demás slots: se compara la página antes y después, byte a byte, salvo los 4 B del directorio de ese slot y los 2 B del puntero dentro de la región liberada · `erase` dos veces el mismo slot → `Status::NotFound` · un `insert` posterior **reutiliza la región libre** (entera, sin dividirla) y el `RowID` de las demás tuplas sigue siendo válido · **reutilización de una región MAYOR por una tupla MENOR** (cambio 41): `erase` de 27 B, `insert` de 4 B que reutiliza → `lookup` devuelve los **27** B, `Tuple::deserialize` devuelve la tupla de 1 campo con `Ok` **ignorando la cola**, y la cola todavía contiene los bytes de la tupla vieja (`erase` no pone nada a cero) · **`freeSpace()` con los números exactos**, en la página llena que ya usa el resto del documento (131 tuplas de 27 B, `dataStart = 532`, `dataEnd = 559`): **27** antes del `erase`; **54** después del `erase` (sube los 27 del `length` de la región); y de vuelta a **27** tras el `insert` de 4 B que reutiliza —baja los 27 del `length` completo, no los 4 de la tupla— y los 23 B de cola no se cuentan como libres · un segundo `erase` + `insert` de 27 B recupera la región entera · `freeSpace` decrece al insertar, **crece al borrar** y `insert` reutiliza antes de bajar `dataEnd` · la cadena sobrevive a varios `erase`/`insert` intercalados: `freeSlotCount()` cuadra en cada paso · **`wrap` de una página con `dataStart != 8 + 4*nSlots` → `Status::Corrupt`** · `deserialize` con un `len` que se sale de la página → `Status::Corrupt` |
| **P1** | `tests/test_tree_printer.cpp` | `printTree` de árbol vacío (hoja raíz sin claves), de una sola hoja y de árbol de 3 niveles, comparado con salida esperada · `exportDot` produce un `.dot` con tantos nodos como páginas alcanzables |
| **P2** | `tests/test_page_manager.cpp` | Lectura de página recién escrita · los contadores cuadran con lo esperado · `readMeta`/`writeMeta` no alteran los contadores · relectura correcta tras `flush` y reapertura del archivo · el archivo crece al hacer `allocate` · `allocate` nunca devuelve 0 · sin caché propia: dos `get` consecutivos del mismo `RowID` cuentan 2 lecturas de página · **abrir un archivo existente con otro `page_size` o con `magic` corrupto → `Status::Corrupt`** · `HeapFile` pasa a una página nueva cuando la actual no tiene espacio · `HeapFile` rechaza una tupla que no cabe en una página vacía · el Full Table Scan visita todas las tuplas y `pageReads` = **número de páginas de datos** (764 con 100 000 tuplas, no 765: la de metadatos no se cuenta) · **el cursor no reexamina páginas anteriores**: insertar 1 000 tuplas produce `ceil(1000/131)+1` = 9 páginas y exactamente **1 000 escrituras de página** (una por `insert`), no 1 000 × 9 · `record_count` queda en 1 000 tras `flush` y se relee al reabrir |
| **P3** | `tests/test_btree_node.cpp`<br>`tests/test_btree_write.cpp` | `computeT(4096) == 204` y `computeT(256) == 12` · **`serialize` devuelve `Ok` con `page_size = 256` y `t = computeT(256)` para 0, 1, `t-1`, `t` y `2t-1` claves, y `NodeOverflow` para `2t`** (el mismo caso en `Release`: el desborde se detecta sin depender de `-DNDEBUG`) · nodo raíz = hoja · nodo en `2t-1` claves antes de insertar · overflow de exactamente una clave (`2t` entradas en memoria) · `split` de hoja (la clave promovida se **copia**) y de nodo interno (la clave del medio **sube**) · `split` es puro: no toca ningún archivo · `data()`/`fromData()` conservan el contenido del nodo, **incluido `t`** · round-trip `serialize→deserialize` byte a byte de hoja y de nodo interno, con 0, 1 y `2t-1` claves, y el `t` del header coincide con el del nodo · claves `INT_MIN` e `INT_MAX` · **`deserialize` de una página con `keyCount > 2t-1` o con `t` corrupto → `Status::Corrupt`** (no assert, no basura) · **`insert` propaga el `Status`**: una clave que desborda el nodo devuelve `NodeOverflow` en vez de abortar en silencio · `nextLeafPageID` válido en hoja y `0` en la última hoja y en nodo interno<br>· árbol recién creado = hoja raíz vacía, altura 1 · `2t-1` inserciones → sigue siendo hoja raíz · la inserción número `2t` → split exacto de la raíz: altura 2, dos hojas con `t` claves, `root_page_id` persistido · split que propaga 2+ niveles · **`split` graba `right.self = rightPageID`** · **claves duplicadas**: 40 copias de la misma clave con `page_size = 256` (cruzan varias hojas) y `search` devuelve las 40 · **estrés**: 5 000 inserciones aleatorias con `page_size = 256`, `checkInvariants` al final · el log: con `verbose` apagado no imprime nada; encendido aparecen `[SPLIT]` y `[ALTURA]` con el formato de `docs/btree-insert.md` |
| **P4** | `tests/test_btree_read.cpp`<br>`tests/test_tree_invariants.cpp` | Búsqueda en árbol vacío · clave ausente · `bulkLoad` con 0, con 1 y con n elementos · las n claves se recuperan tras `bulkLoad` · **bordes del reparto** con `page_size = 256` (`t = 12`): `n = 23` (una hoja), `n = 24` (2 hojas de 12), `n = 47` (`k(2t-1)+1` con k=2, 3 hojas), `n = 553` (25 hojas → 2 nodos internos y raíz nueva) · `bulkLoad` sobre árbol no vacío o con entrada desordenada → **`Status::PreconditionFailed`** (no un assert), y el árbol **queda intacto**: `size()` no cambia y `checkInvariants` sigue pasando, que es lo que distingue que se comprobó la condición correcta · **equivalencia `bulkLoad` vs `insert`**: mismo conjunto de `(clave, RowID)` en el mismo orden al recorrer las hojas y mismos resultados en `search` (la forma del árbol puede diferir) · `bulkLoad` con duplicados que cruzan hojas · **`bulkLoad` y LUEGO `insert`**: el caso que más rompe la frontera P3/P4, porque `bulkLoad` deja las hojas al 100% cuando `n` es múltiplo de `2t-1` y la primera inserción se encuentra un árbol sin una sola clave de holgura. Con `page_size = 256`, `bulkLoad` de **23** claves deja una **única hoja raíz llena** (`2t-1 = 23`) y **un solo `insert` fuerza el split de la raíz**: altura 1→2, `rootPageID()` cambia, el `nextLeaf` de la raíz vieja apunta a la hoja nueva, las 24 claves se encuentran, `checkInvariants` pasa. Segundo caso: `bulkLoad` de **46** (2 hojas llenas de 23) + **600 `insert`** en bloques de 7, para que las claves caigan en todas las hojas → altura 3, todas las hojas entre `t-1 = 11` y `2t-1 = 23` claves, `checkInvariants` después de cada bloque, las 646 claves se encuentran · `indexScan` devuelve **las mismas tuplas** que un Full Table Scan filtrado por esa clave, en el mismo orden · **integración**: cargar 1 000 tuplas en el heap, indexarlas y recuperar las 1 000 por clave<br>· **`checkInvariants`**: pasa con árbol de 0 claves (hoja raíz vacía), con 1 clave, con `2t-1` (raíz llena) y con `2t` (primer split) · **test negativo del verificador**: se construye a mano un árbol con un nodo por debajo de `t-1`, otro con un separador fuera de rango y otro con la cadena de hojas rota, y `checkInvariants` debe reportar fallo en los tres |

**Qué comprueba `checkInvariants` (documentado en el propio header):**

1. Claves ordenadas de forma no decreciente dentro de cada nodo y a lo largo de la cadena de hojas.
2. Separadores: hijo izquierdo <= separador <= hijo derecho, en todo el subárbol.
3. Todas las hojas a la misma profundidad, igual a `height()`.
4. Ocupación: ningún nodo con más de `2t-1` claves; todo nodo **que no sea la raíz** con al menos `t-1` claves; raíz interna con al menos 2 hijos. **La raíz hoja puede tener 0 claves.**
5. La cadena `nextLeaf` recorre todas las hojas de izquierda a derecha y termina en `0`; para un árbol de una sola hoja, `nextLeaf = 0` desde el inicio.
6. El total de entradas recorrido por la cadena coincide con `size()`.

### Documentación — cada quien escribe la de su módulo

| Dueño | Doc | Contenido |
|---|---|---|
| **P1** | `docs/storage-tuple.md`<br>`docs/visualization.md` | **Diagrama byte a byte de la tupla** (`[nFields][tag,len,datos]…`); **layout de la slotted page** (cabecera de 8 B, directorio de 4 B por slot, datos hacia atrás, cadena de slots libres) y por qué `erase` no mueve los demás `RowID`; por qué el `RowID` ocupa 6 B **en disco** y 8 B en memoria; la fórmula `(page_size - 8)/31` -> 131 con 4096 B y 8 con 256 B. Formato de la salida ASCII y cómo renderizar el `.dot` |
| **P2** | `docs/page-manager.md`<br>`docs/benchmark-method.md` | Formato del archivo y de la página de metadatos (`FileMeta`), y quién escribe cada campo; por qué no hay caché y qué miden realmente los contadores (accesos a página, no I/O físico); **por qué `HeapFile` lleva un cursor y qué costo tendría escanear**; por qué `Debug` por defecto; metodología de medición: qué se mide, cuántas repeticiones (`R`), que el tiempo de construcción del índice se reporta aparte, y por qué las 765 páginas del archivo son 764 lecturas |
| **P3** | `docs/btree-node.md`<br>`docs/btree-insert.md` | **Formato en bytes de la página de nodo** (header de 16 B con `t` adentro, claves, hijos o `RowID`s) y layout de nodo interno vs hoja; **cálculo de `t = 204`**, `computeT` y el papel de `SLACK`; **formato exacto de los logs** `[SPLIT]` y `[ALTURA]`; diagrama de propagación de split y crecimiento de altura; **regla de duplicados** e invariante; demostración de que `bulkLoad` cumple el mínimo |
| **P4** | `docs/benchmark-results.md` | **Recibe la tabla de resultados de P2** (no la mide) y le pone la **conclusión** en una línea: con 4096 B el índice no crece y con 256 B tampoco; documenta la fórmula de páginas de datos por búsqueda (`altura + 1`) y la descripción de `bulkLoad` con reparto uniforme |

Cada archivo de `docs/` tiene **un solo dueño**: nadie edita el archivo de otro, solo lo enlaza.
P4 **organiza** el `README.md` final pero no redacta la documentación técnica de los otros
tres: solo la integra.

### §3.1 · Tarjeta por persona

Lo que sigue es el resumen accionable de cada uno: qué entrega, qué congela, qué lo habilita, qué
no toca y a quién se lo pasa. Si algo no está en esta tarjeta, no es de nadie.

#### P1 — Formato binario y visualización

| | |
|---|---|
| **Entrega** | `Types.h`, `Value`, `Tuple`, `SlottedPage`, `DataGen`, `TreePrinter`, `TestHarness.h`, `test_main.cpp` |
| **Congela (no se toca sin avisar)** | El formato de la tupla, el layout de la slotted page, el esquema del dataset y la firma de `DataGen`. Los otros tres programan contra esto |
| **Habilita a** | P2 (lee y escribe con `Value`/`Tuple`/`SlottedPage`), P3 (`RowID` de `Types.h`), P2 (`DataGen` para su benchmark) y P4 (que lo usa en la opción 6) |
| **Tests que entrega** | `test_storage` (131/8 tuplas por página, `erase` sin mover bytes, `freeSpace`, `Corrupt`), `test_tree_printer` |
| **Doc** | `docs/storage-tuple.md`, `docs/visualization.md` |
| **Demo** | Arquitectura 0:00-1:10 · Demo 1 (opción 1) y Demo 4 (opción 4) |
| **Responde** | VARCHAR más largo que una página · por qué directorio y no append-only · por qué los bytes hacia atrás · por qué el `RowID` es de 6 B · **`erase` sin compactar: por qué los `RowID` de los demás no cambian y por qué el borrado entra aunque el enunciado no lo pida** |
| **Depende de** | Nada. Es la primera rama y la escribe primero |
| **No toca** | `PageManager`, `HeapFile`, `BTreeNode`, `BTree`, `main.cpp`, `CMakeLists.txt` |
| **Entrega a** | P2 por el formato de tupla; a P3 por `RowID`; a P4 por `DataGen` y el harness |

#### P2 — I/O y medición

| | |
|---|---|
| **Entrega** | `PageManager`, `HeapFile` (con cursor y `Scan`), `Metrics`, `Benchmark.{h,cpp}` |
| **Congela** | `FileMeta` y quién escribe cada campo; la semántica de los contadores (**accesos a página**, no I/O físico); `read` como `const`; la regla de "una escritura de página por `insert`"; **la métrica del benchmark** (páginas de datos leídas por búsqueda) y el protocolo de medición (`R` repeticiones, mediana, tiempo de construcción aparte) |
| **Habilita a** | Todos. Sin `PageManager` no hay I/O para nadie: es la pieza más bloqueante y la más pequeña |
| **Tests que entrega** | `test_page_manager` (contadores, validación de `magic`, cursor: 1 000 inserts = 9 páginas y 1 000 escrituras, Full Scan = 764 lecturas, **y las 6 curvas dan exactamente las páginas de las fórmulas de §1**) |
| **Doc** | `docs/page-manager.md`, `docs/benchmark-method.md` |
| **Demo** | Arquitectura 1:10-2:20 · Demo 2 (opción 2) |
| **Responde** | cómo saben que no leen dos veces la misma página · por qué no hay buffer pool · eso es I/O físico o page cache del SO · por qué dos archivos · cómo saben que son 765 páginas · **con qué build se midió y por qué `RelWithDebInfo`** · por qué el tiempo es orientativo y las páginas no |
| **Depende de** | P1 (formato de tupla, `RowID` y `DataGen`); **P3** para el `BTree` del índice, que es la última pieza que necesita |
| **No toca** | La implementación de `BTreeNode`/`BTree` (los usa vía `pages()` y por `BTree::open`), `main.cpp`, `CMakeLists.txt` |
| **Entrega a** | **P4 recibe `Benchmark` ya hecho y lo presenta** en la opción 6: P4 habla de la conclusión (el índice no crece), P2 contesta si preguntan por el método |

#### P3 — Nodo, split e inserción

| | |
|---|---|
| **Entrega** | `BTreeNode` completo (con `split`, serializador, `computeT`), `BTree.h`, `BTreeWrite.cpp` |
| **Congela** | El header de 16 B (con `t` adentro), el cálculo de `t`, **el formato de los logs**, la política de duplicados de `findLeaf(key, bias, ancestors*)` y la firma de `BTree.h` |
| **Habilita a** | P4 (lee con `readNode`/`rootPageID`/`pages`, escribe con `bulkLoad`, y necesita `logHeight` de P3) y P1 (`TreePrinter` lee el árbol por la API pública) |
| **Tests que entrega** | `test_btree_node` y `test_btree_write` (`computeT` 204/12, serializador a 256 B, split puro de hoja e interno, 40 duplicadas, estrés de 5 000) |
| **Doc** | `docs/btree-node.md`, `docs/btree-insert.md` |
| **Demo** | Arquitectura 2:20-3:35 · Demo 5 (opción 5, split paso a paso) |
| **Responde** | por qué `t = 204` · qué pasa si la raíz se llena · dónde se guarda la raíz · duplicados cruzando hojas · B o B+ · cómo se codifica el header de 16 B |
| **Depende de** | P1 (`RowID`, `Status`), P2 (`PageManager`) |
| **No toca** | `BTreeRead.cpp` (es de P4), `main.cpp`, `CMakeLists.txt`, los docs de otros |
| **Entrega a** | P4 la clase completa por header, no por archivo: si cambia una firma, cambia `BTree.h` y avisa |

#### P4 — Lectura, infraestructura e integración

| | |
|---|---|
| **Entrega** | `BTreeRead.cpp` (`search`, `bulkLoad`, `indexScan`, `size`), `TreeInvariants.h`, `main.cpp`, `CMakeLists.txt`, `.gitignore`, `README.md` |
| **Congela** | El **formato del menú y de `--demo`/`--page-size`/`--n`**, el **reparto de archivos por opción de §1 bis** y la forma de la tabla que se imprime. **La métrica y el protocolo de medición los congela P2**, no P4: P4 elige cómo se ve el resultado, P2 qué se mide |
| **Habilita a** | Nadie hacia adelante, pero **es el primero en tener `main.cpp` y `CMakeLists.txt` en verde con stubs**, que es lo que permite trabajar en paralelo |
| **Tests que entrega** | `test_btree_read` (bordes de `bulkLoad` 23/24/47/553, equivalencia con `insert`, `indexScan` vs Full Scan) y `test_tree_invariants` (incluido el **test negativo** del verificador) |
| **Doc** | `docs/benchmark-results.md`, que **escribe con los datos que le pasa P2** (no con su propia medición) + organiza el `README.md` con las secciones de los otros tres |
| **Demo** | Arquitectura 3:35-4:30 · Demo 3 (opción 3) y Demo 6 (opción 6, las dos curvas) |
| **Responde** | ¿`search` es de verdad O(log n)? · por qué `bulkLoad` no necesita `split` · cómo garantizan que `bulkLoad` no deja nodos casi vacíos · por qué el índice no crece con `n` · **qué concluye la curva (que el índice no crece), sin defender el método, que es de P2** |
| **Depende de** | P3 (`BTree.h`, `logHeight`, `readNode`), P1 (`DataGen`), **P2 (`Benchmark` y `Metrics`, listos cuando llega la opción 6)** |
| **No toca** | La implementación de los módulos de nadie. Si falta algo, lo pide; si el CMake cambia, lo cambia él |
| **Entrega a** | Todo el equipo: el `main` compilable, las bases de la demo y el README final |

**Revisión cruzada:** P3 ↔ P4 (ambos escriben el árbol), P1 ↔ P2 (ambos escriben storage).
Nadie aprueba su propio código. Un PR sin revisión cruzada no se mergea a `main`.

**Handoff:** cada módulo se entrega cuando su dueño dice "listo" según la definición de §5, y el
revisor cruzado responde en el PR. Si el PR toca el contrato de §4, el aviso va al canal del
equipo **antes** de abrirlo, no en la revisión.

---

## 4. Estructura del repositorio

```
/
├── CMakeLists.txt          # [P4] fuentes explicitas, nunca GLOB; Debug por defecto
├── .clang-format           # compartido por los cuatro, evita diffs de reformateo
├── README.md               # [P4]
├── .gitignore              # [P4] build/, *.db, *.dot, salidas/
├── src/
│   ├── main.cpp            # [P4] menu + --page-size + --demo + --n
│   ├── common/
│   │   └── Types.h         # [P1] PageID, SlotID, Page, RowID, Status
│   ├── storage/
│   │   ├── Value.{h,cpp}          # [P1]
│   │   ├── Tuple.{h,cpp}          # [P1]
│   │   ├── SlottedPage.{h,cpp}    # [P1]
│   │   ├── PageManager.{h,cpp}    # [P2]
│   │   └── HeapFile.{h,cpp}       # [P2]
│   ├── index/
│   │   ├── BTreeNode.{h,cpp}      # [P3] incluye serialize/deserialize
│   │   ├── BTree.h                # [P3]
│   │   ├── BTreeWrite.cpp         # [P3]
│   │   ├── BTreeRead.cpp          # [P4]
│   │   └── TreePrinter.{h,cpp}    # [P1]
│   └── bench/
│       ├── Metrics.{h,cpp}        # [P2]
│       ├── DataGen.{h,cpp}        # [P1]
│       └── Benchmark.{h,cpp}      # [P2]
├── tests/                  # nota: en la RAIZ, no dentro de src/
│   ├── TestHarness.h         # [P1] macros TEST/CHECK/CHECK_EQ, sin dependencias y sin fork()
│   ├── test_main.cpp         # [P1] main() y despacho por --suite
│   ├── TreeInvariants.h      # [P4] checkInvariants, lo usan P3 y P4
│   ├── test_storage.cpp          # [P1]
│   ├── test_tree_printer.cpp     # [P1]
│   ├── test_page_manager.cpp     # [P2]
│   ├── test_btree_node.cpp       # [P3]
│   ├── test_btree_write.cpp      # [P3]
│   ├── test_btree_read.cpp       # [P4]
│   └── test_tree_invariants.cpp  # [P4]
└── docs/
    ├── storage-tuple.md      # [P1]
    ├── visualization.md      # [P1]
    ├── page-manager.md       # [P2]
    ├── benchmark-method.md   # [P2]  (protocolo, escrito antes de medir)
    ├── benchmark-results.md  # [P4]  (tabla; la escribe con los datos de P2)
    ├── btree-node.md         # [P3]
    └── btree-insert.md       # [P3]
```

### `CMakeLists.txt` (P4): `Debug` por defecto

```cmake
cmake_minimum_required(VERSION 3.16)
project(phase1_btree)

# Debug por defecto al desarrollar: mantiene vivos los assert de error de programacion
# (asInt() sobre un VARCHAR, bytes.size() < 2). La tupla de 0 campos NO entra en la lista:
# devuelve Status::PreconditionFailed y se testea sin fork (cambio 40). El desborde de un nodo
# tampoco depende del build: serialize() devuelve Status::NodeOverflow, asi que se detecta con
# -DNDEBUG. Por eso el benchmark se puede (y se debe) medir en RelWithDebInfo.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)     # "listo" exige compilar sin warnings nuevos

add_executable(main src/main.cpp
  src/storage/Value.cpp  src/storage/Tuple.cpp      src/storage/SlottedPage.cpp
  src/storage/PageManager.cpp src/storage/HeapFile.cpp
  src/index/BTreeNode.cpp src/index/BTreeWrite.cpp src/index/BTreeRead.cpp
  src/index/TreePrinter.cpp
  src/bench/Metrics.cpp  src/bench/DataGen.cpp      src/bench/Benchmark.cpp
)
# Fuentes explicitas. NUNCA GLOB: anadir un archivo despues genera conflictos.

add_executable(tests
  tests/test_main.cpp
  tests/test_storage.cpp tests/test_tree_printer.cpp
  tests/test_page_manager.cpp
  tests/test_btree_node.cpp tests/test_btree_write.cpp
  tests/test_btree_read.cpp tests/test_tree_invariants.cpp
)
# OJO: tests/ esta en la raiz, no en src/. Ver el arbol de arriba.

target_include_directories(main  PRIVATE src)
target_include_directories(tests PRIVATE src tests)
enable_testing()
add_test(NAME storage       COMMAND tests --suite storage)
add_test(NAME tree_printer  COMMAND tests --suite tree_printer)
add_test(NAME page_manager  COMMAND tests --suite page_manager)
add_test(NAME btree_node    COMMAND tests --suite btree_node)
add_test(NAME btree_write   COMMAND tests --suite btree_write)
add_test(NAME btree_read    COMMAND tests --suite btree_read)
add_test(NAME invariants    COMMAND tests --suite invariants)
# Un add_test por suite: una suite roja se ve sola en el informe de CI.
```

**Toolchain fijado** (va en el README): C++17, CMake >= 3.16, `gcc >= 9` o `clang >= 10`.
Todos usan `.clang-format` del repo (estilo `BasedOnStyle: LLVM`, `IndentWidth: 4`).
Sin un archivo de formato compartido, los reformateos automáticos llenan los diffs y tapan
los cambios reales en la revisión.

**El harness y su `main()` son de P1** (van en la rama `contract`): define `TEST`, `CHECK` y
`CHECK_EQ`, nada más. `test_main.cpp` es el `main()` que registra las suites y despacha con
`--suite`. Así `ctest` puede correrlas de a una y un fallo queda atribuido a un dueño, no a "los
tests".

**Por qué no hay macro de "espera que esto aborte":** para testear un `assert` había que correr el
caso en un proceso hijo con `fork()` y comprobar que muere, lo que ataba la suite a POSIX. Con
`serialize` devolviendo `Status` (§4) el desborde se testea directo con `CHECK_EQ` y **el harness
no necesita `fork()`**: corre igual en Linux, macOS y Windows. La misma regla se aplicó a las
precondiciones de `bulkLoad` y a la tupla de 0 campos (cambio 40): devuelven
`Status::PreconditionFailed` y se testean igual. Los `assert` que quedan en el código son de error
de programación **dentro de una función ya llamada con el tipo equivocado** (`asInt()` sobre un
`VARCHAR`, `bytes.size() < 2` en `SlottedPage::insert`) y no se testean: probarlos exigiría volver
al `fork()`.

### `main.cpp` (P4): menú de 6 opciones

Las 6 opciones corresponden 1:1 con los bloques de la demo (§6), para que un integrante pueda
ensayar su bloque sin los demás.

```
1. Storage      - tuplas, slotted page, hexdump de una pagina
2. Pager        - crecimiento del archivo, contadores, Full Table Scan
3. Bulk load    - carga masiva del indice con log [ALTURA]   (una vez por ejecucion)
4. Arbol        - printTree en ASCII y .dot de Graphviz
5. Split        - split paso a paso, SIEMPRE con page_size 256
                  (abre su propia base temporal: ver §1 bis)
6. Benchmark    - Index Scan vs Full Table Scan, curvas 4096 y 256
                  (base temporal propia, reset entre curvas: ver §1 bis)
                  --full  = las 12 filas + R repeticiones (para el docs/)
                  --n N    = una sola corrida, para depurar
0. Presentacion completa (las 6, en orden)
q. Salir
```

```bash
./build/main                            # menu interactivo
./build/main --demo 0                   # presentacion completa (las 6, en orden)
./build/main --demo 6                   # solo el bloque 6 = las dos curvas
./build/main --demo 6 --n 50000         # una sola corrida de N, para depurar
./build/main --demo 6 --full             # las 12 filas con R repeticiones (para el docs/)
./build/main --page-size 256 --demo 5   # la opcion 5 ya fuerza 256 por su cuenta
```

`--page-size` fija el tamaño de página **de la corrida**. Las opciones 1-4 lo usan tal cual; la 5
y la 6 trabajan sobre bases temporales propias y pueden usar otro `page_size` sin chocar, porque
nunca comparten archivo con las demás (§1 bis).

**Regla del reset.** `reset_databases()` se llama **una vez, al inicio de la ejecución**: al abrir
la sesión interactiva, o al arrancar `--demo 0` / `--demo k`. Dentro de esa ejecución las
opciones 1-4 **comparten** `data.db` e `index.db`, que es lo que hace posible que la opción 4 vea
el árbol que dejó la 3. La 5 y la 6 no tocan esos archivos.

```cpp
// main.cpp: se llama UNA vez, al inicio de la ejecucion. No al entrar a cada opcion.
// Las opciones 5 y 6 crean y borran sus propias bases en temp_directory_path().
void reset_databases() {
    std::remove("data.db");
    std::remove("index.db");
}
```

Si el menú interactivo deja correr la opción 3 dos veces en la misma sesión, el programa avisa
(`"el indice ya fue construido en esta ejecucion: use otra corrida"`) en vez de assertar: un assert
en vivo frente al jurado es peor que un mensaje, y `bulkLoad` exige árbol vacío.

Los tests **nunca** tocan `data.db` ni `index.db` del repositorio: usan
`std::filesystem::temp_directory_path()` con un nombre único por test.

### Terminología del enunciado → código (va al `README.md`, lo redacta P4)

Los nombres del enunciado (`Trab1.md` §2.1 y §3) se respetan en el código. La tabla del README
evita que el jurado tenga que adivinar la correspondencia:

| Enunciado | En el código |
|---|---|
| Administrador de Páginas e Inodos | `PageManager` (páginas) + directorio de slots de `SlottedPage` (cumple el papel de inodo/ítem) + `RowID{pageID, slotID}` |
| Módulo de registros | `Tuple`, `SlottedPage`, `HeapFile` |
| Serializador de tuplas | `Value::serializeTo`, `Tuple::serializeTo`, `BTreeNode::serialize` |
| Asignación de RowIDs | `HeapFile::insert` |
| Clases `BTreeNode` y `BTree` | `BTreeNode`, `BTree` (implementan un **B+**: datos solo en hojas, hojas encadenadas) |
| `search(key)` / `insert(key, RowID)` | `BTree::search`, `BTree::insert` |
| División de nodos (*split*) | `BTreeNode::split` + propagación en `BTree::insert` |
| Carga masiva (*bulk load*) | `BTree::bulkLoad` |
| Control de capacidad | `BTreeNode::computeT` con `SLACK`, tope de `2t-1` claves con `Status::NodeOverflow` en `serialize` y `Status::Corrupt` en `deserialize` |
| Index Scan / Full Table Scan | `BTree::indexScan` / `HeapFile::Scan` |
| Grado `t` | `computeT(pageSize)` → 204 (4096 B), 12 (256 B) |
| Pruebas unitarias | 7 suites con el mini-harness de `tests/`, un `add_test` por suite |
| Comparativa de tiempo | `Benchmark` (P2) con `Metrics` (P2): páginas de datos leídas y tiempo orientativo |

### Contrato de interfaces

Cada dueño escribe el header de su módulo con cuerpos stub que **compilan**, para que los
cuatro puedan trabajar en paralelo desde el primer momento. `Types.h` lo escribe P1 primero,
porque todos dependen de él. Este bloque es normativo: si un cuerpo cambia, cambia aquí y se
avisa en el PR.

```cpp
// common/Types.h                          [P1]
using PageID = uint32_t;               // 0 = "ninguna pagina" (la pagina 0 es de metadatos)
using SlotID = uint16_t;
using Page   = std::vector<uint8_t>;   // exactamente page_size bytes
struct RowID {
    PageID pageID;
    SlotID slotID;
};
bool operator==(const RowID&, const RowID&);
bool operator<(const RowID&, const RowID&);
// PreconditionFailed = el que llama rompio el contrato documentado (tupla sin campos,
// bulkLoad sobre arbol no vacio o con entrada desordenada). NO es un assert: es un Status
// justamente para que un test pueda comprobarlo sin fork y sin depender de -DNDEBUG.
enum class Status { Ok, PageFull, TupleTooLarge, NotFound, Corrupt, NodeOverflow,
                    PreconditionFailed };
enum class ValueKind : uint8_t { Int = 0, VarChar = 1 };
// sizeof(RowID) == 8 por el padding del struct; en la pagina ocupa 6 (u32 + u16 sin alinear).
// El serializador escribe los campos a mano, nunca memcpy del struct.

// storage/Value.h                         [P1]
// Formato en bytes: [tag: u8][len: u16][datos: len], little-endian.
// tag 0 = INT (len 4) - tag 1 = VARCHAR (len arbitrario, sin terminador)
class Value {
public:
    Value(int32_t v);
    Value(std::string v);
    ValueKind          kind() const;
    int32_t            asInt() const;            // assert kind == Int
    const std::string& asVarChar() const;         // assert kind == VarChar
    size_t             serializedSize() const;    // 3 + len
    void               serializeTo(std::vector<uint8_t>& out) const;
    // Mismo estilo que Tuple::deserialize: recibe el fin del buffer y avanza p.
    // Si el len declarado se pasa de `end`, devuelve Status::Corrupt.
    static Status      deserialize(const uint8_t*& p, const uint8_t* end, Value& out);
};

// storage/Tuple.h                          [P1]
class Tuple {
public:
    void               append(const Value&);
    const Value&       at(size_t i) const;
    size_t             fieldCount() const;
    size_t             serializedSize() const;     // 1 + suma de los campos
    // Precondicion: fieldCount() >= 1, porque la tupla minima son 4 B (un VARCHAR vacio) y
    // el puntero de la cadena de libres necesita 2 B dentro de la region liberada.
    // Se devuelve Status::PreconditionFailed y NO se asserta: es la unica forma de que un
    // test lo compruebe sin fork y sin depender de -DNDEBUG (ver la regla de §7).
    Status             serializeTo(std::vector<uint8_t>& out) const;   // incluye [nFields]
    // Lee [nFields] y los nFields campos. **Ignora los bytes sobrantes al final** sin error:
    // al reutilizar una region liberada mas grande, `len` es el tamano de la region, no el
    // de la tupla. Devuelve Status::Corrupt solo si un len DECLARADO se pasa de `len`.
    static Status      deserialize(const uint8_t* data, size_t len, Tuple& out);
};

// storage/SlottedPage.h                   [P1]  (puro, sin I/O)
// Layout: [nSlots:u16][dataStart:u16][dataEnd:u16][freeHead:u16] + directorio
// de 4 B por slot ([offset:u16][length:u16]) + datos hacia atras desde el final.
// dataStart = fin del directorio = 8 + 4*nSlots; dataEnd = direccion mas baja ocupada.
// Slot LIBRE: el directorio conserva su length, y los 2 primeros bytes de la region
// liberada guardan el indice de directorio de la siguiente region libre (0xFFFF = fin de cadena).
// freeHead = indice de directorio del primer libre. Los dos punteros de la cadena son slotID y no
// offsets (cambio 43), igual que freeHead: asi el first-fit de insert la recorre en O(k) en vez de
// O(k*k) resolviendo offset -> slot en cada paso.
class SlottedPage {
public:
    static SlottedPage init(size_t page_size);
    static SlottedPage wrap(const Page&);
    // assert(bytes.size() >= 2): el unico productor real es Tuple::serializeTo (min. 4 B).
    Status insert(const std::vector<uint8_t>& bytes, SlotID& out_slot); // PageFull si no cabe
    // Devuelve la region COMPLETA segun el length del directorio, que al reutilizar sigue
    // siendo el de la region liberada: puede traer bytes de mas al final, y quien la llame
    // (Tuple::deserialize) tiene que ignorarlos. Solo hay length >= 2 en un slot vivo.
    Status lookup(SlotID, std::vector<uint8_t>& out) const;
    Status erase(SlotID);                     // NO compacta: enlaza la region liberada
                                              // en la cadena y no mueve un byte de datos
    size_t freeSpace() const;                 // (dataEnd - dataStart) + Σ length libres
    size_t slotCount() const;
    size_t freeSlotCount() const;             // recorre la cadena
    static size_t maxTupleSize(size_t page_size);
    Page   toPage() const;
};
// insert reutiliza la primera region libre que alcance (recorre toda la cadena: no estan
// ordenadas por tamaño) y la ocupa entera; si ninguna alcanza, baja dataEnd.
// OJO con el +4: appendear cuesta la entrada de directorio nueva, que empuja dataStart
// 4 B arriba. Se puede escribir al final solo si dataEnd - (dataStart + 4) >= bytes.size().
// Por eso con 131 tuplas de 27 B freeSpace() da 27 y aun asi la 132 da Status::PageFull.
// El mínimo serializado es 4 B (nFields=1 + un VARCHAR vacío), así que toda región
// liberada puede alojar su puntero de 2 B.
// REGLA (cambio 41): al reutilizar, el length del directorio NO baja. Una tupla de 4 B en
// una región de 27 deja 23 B de cola: lookup devuelve los 27 y Tuple::deserialize ignora la
// cola. freeSpace() baja 27 (el length completo, al salir el slot de la suma de libres),
// no 4: los 23 B de cola quedan muertos y no se cuentan como libres. Orden obligatorio:
// desenlazar de la cadena ANTES de escribir, porque la tupla nueva pisa los 2 bytes del
// puntero.

// storage/PageManager.h                   [P2]
struct FileMeta {
    uint32_t magic, page_size, page_count, root_page_id, height;
    uint64_t record_count;
};
class PageManager {
public:
    // Crea el archivo si no existe. Si existe, valida magic y page_size:
    // si no cuadran devuelve Status::Corrupt (y no sigue operando).
    static Status open(const std::string& path, size_t page_size, PageManager& out);
    PageID allocate();                    // nunca devuelve 0
    Page   read(PageID) const;            // cuenta en pageReads()  -> mutable uint64_t
    void   write(PageID, const Page&);    // cuenta en pageWrites()
    void   flush();
    FileMeta readMeta() const;            // NO cuenta en los contadores
    void     writeMeta(const FileMeta&);  // NO cuenta en los contadores
    uint64_t pageReads() const;
    uint64_t pageWrites() const;
    size_t   pageSize() const;
    uint32_t pageCount() const;
    void     resetCounters();
};

// storage/HeapFile.h                      [P2]
// CURSOR: insert solo mira lastInsertPageID_. Si no cabe, allocate() y avanza.
// Nunca reexamina paginas anteriores -> O(1) amortizado.
// ESCRIBE la pagina en cada insert (no difiere al flush): 1 tupla = 1 escritura.
class HeapFile {
public:
    explicit HeapFile(PageManager&);
    // TupleTooLarge si no cabe en pagina vacia; propaga PreconditionFailed si la tupla
    // tiene 0 campos (viene de Tuple::serializeTo, que ya devuelve Status).
    Status insert(const Tuple&, RowID& out);
    Status get(const RowID&, Tuple& out);
    class Scan {                               // Full Table Scan: recorre TODAS las paginas,
    public:                                    // deserializa cada tupla. La clave es el
                                               // campo 0 y la compara el que la usa, no Scan.
        Scan();
        bool next(Tuple& out, RowID& rid);     // NO se detiene en la primera coincidencia.
        void reset();
    };
    Scan scan();
    PageID  lastInsertPageID() const;           // visible para el test del cursor
    size_t  recordCount() const;
private:
    PageID lastInsertPageID_ = 0;               // el cursor
};
// Las tuplas por pagina NO son constante: size_t tuplesPerPage(size_t page_size);
//   = (page_size - 8) / 31 con el dataset del benchmark.  131 con 4096 B, 8 con 256 B.

// index/BTreeNode.h                       [P3]
struct NodeData {
    bool                 is_leaf;
    uint16_t             t;                     // el grado con el que se creo el nodo;
                                                // vive tambien en el header de la pagina,
                                                // asi deserialize puede validar 2t-1
    PageID               self;
    PageID               next_leaf;            // 0 = ninguna
    std::vector<int32_t> keys;
    std::vector<PageID>  children;             // solo interno: keys.size()+1 entradas
    std::vector<RowID>   rowids;               // solo hoja: keys.size() entradas
};
struct SplitResult;
class BTreeNode {
public:
    // header fijo de 16 B:
    // [isLeaf:1][keyCount:2][selfPageID:4][nextLeafPageID:4][t:2][reservado:3]
    static uint16_t computeT(size_t page_size);   // min(tHoja, tInterno), con SLACK=1
                                                   // 204 @4096, 12 @256
    static BTreeNode makeLeaf(PageID self, size_t page_size);
    static BTreeNode makeInternal(PageID self, size_t page_size);

    bool     isLeaf() const;
    uint16_t keyCount() const;
    uint16_t t() const;
    uint32_t childCount() const;                  // = keyCount + 1 (solo interno)
    int32_t  keyAt(uint16_t i) const;
    PageID   childAt(uint16_t i) const;           // interno
    RowID    rowIDAt(uint16_t i) const;           // hoja
    PageID   nextLeaf() const;                    // 0 = ultima hoja / nodo interno

    struct SearchHit { uint16_t idx; bool found; };
    SearchHit searchInNode(int32_t key) const;    // lowerBound + found
    uint16_t  upperBound(int32_t key) const;      // primera posicion con clave > key

    void insertInNode(int32_t key, PageID right_child);  // interno (hasta 2t en memoria)
    void insertInNode(int32_t key, const RowID&);        // hoja: tras las claves iguales

    // PURO: sin I/O ni asignacion de paginas. *this queda como izquierdo y
    // right.self queda grabado con right_page_id.
    SplitResult split(PageID right_page_id);
    // Status, NO assert: el desborde es un error esperado (insertInNode deja entrar
    // hasta 2t claves a proposito) y tiene que seguir detectandose en Release, donde
    // -DNDEBUG borra los asserts. NodeOverflow = keyCount > 2t-1, no se escribe nada.
    Status serialize(Page& out) const;
    // NO hay campo magic en el header: la corrupcion se detecta por invariantes.
    // Status::Corrupt, no assert ni basura, si: t != computeT(page_size) - o sea,
    // una pagina que no es de nodo (todo ceros, o el header de una tupla del heap) -,
    // isLeaf != 0 && isLeaf != 1, keyCount > 2t-1, o el area de datos se sale de la pagina.
    static Status deserialize(const Page&, BTreeNode& out);
    const NodeData& data() const;
    static BTreeNode fromData(NodeData, size_t page_size);
    void setNextLeafPageID(PageID);
};
// CONGELADO (regla 19): NodeData, este layout de 16 B y estas firmas solo cambian
// con Issue de las cuatro personas. Los cuerpos de BTreeNode son libres.
// El header NO es un struct que se copie: son 16 bytes que escribe a mano
// serialize() campo por campo en little-endian, asi que lo que se congela con
// static_assert es el TAMANO, no un offsetof:
//   static_assert(NODE_HEADER_SIZE == 16);            // isLeaf1+keyCount2+self4+next4+t2+res3
// Y sobre NodeData, que si es un struct de C++, los tipos:
//   static_assert(std::is_same_v<decltype(NodeData::t), uint16_t>);
//   static_assert(std::is_same_v<decltype(NodeData::keys), std::vector<int32_t>>);
// Si alguien cambia un tipo, el build se cae en la rama contract, no dos semanas
// despues en el de otra persona.
struct SplitResult { BTreeNode right; int32_t promoted_key; };
// hoja:    promoted_key = right.keyAt(0)  (se COPIA hacia arriba)
// interno: promoted_key = clave central   (SUBE, no queda en ningun hijo)

// index/BTree.h                           [P3 redacta, P4 implementa la parte de lectura]
enum class Bias { Left, Right };          // Left: search (lowerBound) - Right: insert (upperBound)
class BTree {
public:
    // Lee FileMeta. Si el archivo no tiene raiz valida, crea una hoja raiz con 0 claves
    // (nunca 0 nodos). Si la hay, la usa tal cual.
    static Status open(PageManager& index_pm, BTree& out);
    // insert y bulkLoad devuelven Status porque el serializador ya no aborta: si un nodo
    // no entra, hay que propagarlo en vez de perderlo. Quien los llama DEBE mirarlo.
    Status insert(int32_t key, const RowID&);                         // [P3] BTreeWrite.cpp
    // Precondiciones, como Status y NO como assert (mismo motivo que §7: sin fork no se
    // puede comprobar un assert, y con -DNDEBUG -- el build donde se mide -- no existe).
    // Devuelve PreconditionFailed y NO modifica el arbol si: la entrada no es no decreciente,
    // o el arbol no esta vacio. Los tests lo comprueban ademas con size() sin cambios.
    Status bulkLoad(const std::vector<std::pair<int32_t, RowID>>& sorted); // [P4]
    std::vector<RowID> search(int32_t key) const;                        // [P4]
    std::vector<Tuple> indexScan(int32_t key, HeapFile&) const;          // [P4] indexScan
    size_t size() const;                                                 // [P4] recorre hojas
    size_t height() const;                                               // [P3] niveles; hoja raiz = 1
    void   setVerbose(bool);                                             // [P3]
    PageID    rootPageID() const;                                        // [P3] para TreePrinter (P1)
    BTreeNode readNode(PageID) const;                                    // [P3] para TreePrinter (P1)
    const PageManager& pages() const;                                   // [P3] para Benchmark (P2)
private:
    PageID findLeaf(int32_t key, Bias, std::vector<PageID>* ancestors) const; // [P3]
    // UNICOS puntos donde se escribe el log. Ningun otro archivo imprime [SPLIT]/[ALTURA].
    void logSplit(const BTreeNode& left, const BTreeNode& right,
                  int32_t promoted_key) const;                           // [P3] bajo verbose_
    void logHeight(size_t old_h, size_t new_h, PageID new_root) const;   // [P3] bajo verbose_
    bool verbose_ = false;
};

// bench/Metrics.h                         [P2]
// Agrega los dos PageManager y el reloj. Lo usa Benchmark (P2), que es suyo.
class Metrics {
public:
    Metrics(const PageManager& heap_pm, const PageManager& index_pm);
    uint64_t heapPageReads() const;
    uint64_t indexPageReads() const;
    uint64_t totalPageReads() const;
    void     resetCounts();
    void     start();
    double   elapsedMs() const;           // std::chrono::steady_clock
};

// bench/Benchmark.h                       [P2]
// Las DOS curvas (4096 y luego 256) sobre base temporal propia, con reset entre
// curvas. Es lo UNICO que mide: main.cpp (P4) solo lo invoca y lo presenta.
// Benchmark NO lee argv: --page-size / --n / --full los parsea el menu (P4) y
// llegan en BenchmarkOptions. main.cpp no inventa ni un valor de medicion.
struct BenchmarkOptions {
    size_t page_size = 0;     // 0 = las dos curvas (4096 y 256); otro = esa sola
    size_t n         = 0;     // 0 = los 6 N canonicos (20, 500, 1e3, 5e3, 1e4, 1e5)
                              //     > 0 = una sola corrida, en LAS DOS curvas, para depurar
    bool   full      = false; // en vivo salen las 12 filas sin el tiempo de N = 100 000
                              // (el punto caro del riesgo 25); --full lo agrega, y con el
                              // las 12 filas salen con R repeticiones, que es la tabla de
                              // docs/benchmark-results.md
    size_t repeats   = 0;     // 0 = el protocolo de R: 5 hasta N = 1e4, 1 en N = 1e5
};
// Una banda contigua de N en la que gana lo mismo. La isla de empate de N = 24 a
// 256 B es una banda PROPIA, por eso esto es una lista y no tres campos sueltos.
struct Band { size_t from, to; enum class Winner { Scan, Tie, Index }; };
class Benchmark {
public:
    // Imprime el bloque de la opcion 6: 27 B/tupla y tuplas/pagina, las dos curvas
    // con altura y ratio, las paginas del archivo con ceil(N/tuplasPorPagina)+1, el
    // punto de cruce de cada curva y el tiempo de construccion del indice por
    // separado del tiempo de busqueda. El tiempo de construccion NO entra en el
    // tiempo de busqueda; R solo afecta al tiempo, nunca a los recuentos.
    // Status: bulkLoad exige arbol vacio y aca se construye una base nueva por
    // curva, asi que un PreconditionFailed es un bug de esta base, no del dato.
    static Status run(std::ostream& out, const BenchmarkOptions& opt = {});
    // Recorre N = 1..100000 y clasifica cada valor en escaneo | empate | indice.
    // NO esta escrito a mano: si cambian computeT o la formula de altura, el cruce
    // cambia con ellas. No construye ningun arbol: es aritmetica sobre
    // BTreeNode::computeT(page_size), y por eso no depende de P3 para correr.
    // El tope de 100000 esta justificado en §1.
    static std::vector<Band> crossover(size_t page_size, size_t tuples_per_page);
};

// bench/DataGen.h                         [P1]
// Dataset del benchmark: 2 campos, INT key + VARCHAR name de 16 bytes -> tupla de 27 B.
// sorted() es la unica que puede darle la entrada a bulkLoad (ordenada no decreciente).
class DataGen {
public:
    static Tuple        datasetTuple(int32_t key);   // (INT key, VARCHAR de 16 B)
    static const char*  nameFor(int32_t key);        // 16 bytes, deterministico
    static std::vector<std::pair<int32_t, RowID>>
                       sorted(size_t n);            // claves 1..n, en orden
    static std::vector<std::pair<int32_t, RowID>>
                       randomKeys(size_t n, size_t seed);  // para el test de estres
    static std::vector<Tuple>  heap(size_t n);       // n tuplas del dataset
};

// tests/TreeInvariants.h                  [P4]
struct InvariantReport { bool ok; std::string error; };
InvariantReport checkInvariants(const BTree&);

// tests/TestHarness.h + test_main.cpp     [P1]
// TEST(name) { ... }  CHECK(cond)  CHECK_EQ(a, b)
// Sin EXPECT_ASSERT_FAILS ni fork(): los errores esperados se comparan con CHECK_EQ
// sobre el Status, y los asserts que quedan son de error de programación.
// test_main.cpp registra las suites y las despacha con --suite storage|tree_printer|
// page_manager|btree_node|btree_write|btree_read|invariants (o "all" sin flag).

// index/TreePrinter.h                     [P1]
void printTree(const BTree&, std::ostream&);   // ASCII
void exportDot(const BTree&, std::ostream&);   // Graphviz (prescindible)
```

Formato de los logs (definido por P3 en `docs/btree-insert.md`, ejemplo):

```
[SPLIT]  hoja    page=12 -> izq=12 der=57  clave=4321
[SPLIT]  interno page=90 -> izq=90 der=91  clave=17800
[ALTURA] 2 -> 3  raiz=page 92
```

---

## 5. Orden de trabajo, "listo" y orden de recorte

No hay fechas. El proyecto se hace al ritmo que se pueda, así que esto define **qué se hace
primero por dependencia** (no por calendario) y **qué se suelta primero si el tiempo no alcanza**.

### Orden por dependencia

Las flechas son dependencias reales, no fechas. Varios de estos pasos se solapan porque los
headers con stub existen desde el principio.

```
Types.h (P1)
  |
  +-- Value / Tuple (P1) --> SlottedPage (P1) --+
  |                                             |
  +-- TestHarness + test_main (P1) --------------+--> (ctest corre verde desde el principio)
  |                                             |
  +-- PageManager (P2) --> HeapFile (P2) --> Metrics (P2) --+
  |                                             |
  +-- BTreeNode en memoria + computeT + split (P3) --------+
  |                                                        |
  +-- findLeaf (P3) --> serializador de nodo (P3) --> insert completo (P3) --+
  |                                                                      |
  +-- readNode / rootPageID (P3) --> TreePrinter (P1) ---------------------+
  |                                                                      |
  +-- DataGen (P1) --> search / indexScan (P4) --> checkInvariants (P4) ---+
                                              |                            |
                                              +--> bulkLoad (P4) ----------+
                                                          |
                                    Benchmark (P2) --> main.cpp (P4) --> README (P4)
```

**`Benchmark` es de P2 y va antes que el `main.cpp` de P4 en la última tanda:** la opción 6 del
menú necesita el benchmark, y P2 lo puede terminar cuando P3 ya cerró `insert` y P4 ya cerró
`search`. Que la presentación sea de P4 no significa que el código sea suyo: P2 mide, P4 enseña.
Si lo revés, P4 tocaría `Benchmark.{h,cpp}` para acomodar el `main` y se rompería el único dueño.

**Por qué `TestHarness` está en la primera tanda:** sin él, el primer PR de P1 llega sin forma de
demostrar que funciona, y el primer `ctest` en verde se atrasa hasta que P2 o P3 ya hayan
entregado algo. Con el harness en la rama `contract`, todos los que siguen agregan casos y ven
su nombre en la salida.

**Los tres cuellos de botella**, en orden de daño que hacen si se atascan:

1. **El serializador de nodo (P3).** De él dependen `insert` persistente, `bulkLoad`,
   `checkInvariants` y por lo tanto toda la demo. Si se atasca, lo que se recorta es Graphviz y
   después la segunda curva del benchmark, no el árbol.
2. **`PageManager` (P2).** Sin él no hay I/O. Es la pieza más pequeña y la más bloqueante: se
   escribe pronto y sus tests son los primeros en estar en verde.
3. **`main.cpp` (P4).** Un solo archivo del que cuelgan las 6 demos. Se escribe con las 6
   opciones ya presentes apuntando a stubs, y se va rellenando; nunca se deja para el final.

### Definición de "listo" (igual para los cuatro)

Un módulo está listo cuando:

- Compila con el `CMakeLists.txt` congelado, sin warnings nuevos (`-Wall -Wextra`).
- Sus tests pasan (`ctest`), incluidos los de `checkInvariants` si construye árboles.
- Su `docs/` está escrito.
- Tiene revisión cruzada de un compañero que no lo escribió.
- No quedan `TODO` ni `throw "por hacer"` en lo entregado.
- Si el cambio toca `CMakeLists.txt`, avisa antes: lo integra P4.
- Si el cambio toca el contrato de §4, avisó **antes** de abrir el PR, no en la revisión.

**Sobre los errores esperados y los tests. Regla: si un test tiene que comprobar una condición,
esa condición devuelve `Status`, no `assert`.** El motivo es concreto y no de gusto: para testear un
`assert` habría que correr el binario en un proceso hijo y mirar cómo muere, y este proyecto no usa
`fork` (decisión de §1 bis). Peor: el benchmark se mide en `RelWithDebInfo`, que compila con
`-DNDEBUG` y **borra todos los `assert`**. Una precondición que solo existe en `Debug` es una
precondición que desaparece justo en el build donde se mide, así que quedaría sin comprobar en la
presentación y sin testear en la suite. Por eso:

- **Devuelven `Status` y se testean con `CHECK_EQ`**: el desborde de un nodo (`NodeOverflow`), una
  página corrupta (`Corrupt`), una tupla que no cabe (`TupleTooLarge`, `PageFull`), una tupla de 0
  campos (`PreconditionFailed`) y las dos precondiciones de `bulkLoad` —árbol no vacío y entrada
  desordenada— (`PreconditionFailed`).
- **Siguen siendo `assert`**: solo lo que **ningún test puede provocar**, porque son errores de
  programación dentro de una función que ya se llamó con un tipo equivocado: `asInt()` sobre un
  `VARCHAR`, `asVarChar()` sobre un `INT` y `bytes.size() < 2` en `SlottedPage::insert` (la única
  fuente real de tuplas es `Tuple::serializeTo`, que ya garantiza 4 B). Son guards, no se testean, y
  **nunca se "arreglan" desactivándolos para que la suite pase**. Si uno dispara en la demo, es un
  bug real y se reporta.

El detalle que hace que los tests de `PreconditionFailed` no sean débiles: como un solo valor cubre
las tres precondiciones, cada test comprueba además que **el estado no cambió** (en `bulkLoad`, que
`size()` sigue igual y `checkInvariants` sigue en verde). Si la implementación comprobara la
condición equivocada, el `CHECK_EQ` solo no lo detectaría.

### Orden de recorte

Si el tiempo se acaba, se suelta **en este orden**. Lo que está más abajo no se suelta nunca.

| # | Qué se suelta | Por qué es lo primero |
|---|---|---|
| 1 | `exportDot` (Graphviz) | `printTree` en ASCII basta para la demo de P1 |
| 2 | El `[ALTURA]` de `bulkLoad` | El de `insert` se conserva, que es el que muestra el enunciado |
| 3 | La segunda curva (256) del benchmark | Queda la de 4096, que ya demuestra que el índice no crece |
| 4 | `Metrics` como clase | Se puede sumar en línea dentro de `Benchmark`; son 15 líneas |
| 5 | El `.dot` de Graphviz con el árbol grande | Con `printTree` en ASCII alcanza; el `.dot` se puede generar solo para un subárbol chico |
| 6 | El test de estrés de 5 000 inserciones | Se conservan los de `bulkLoad` + `insert` y 40 duplicadas, que son los de corrección |

**Nunca se recorta:** `split`, `insert`, el serializador de nodo, `search`, `bulkLoad`,
`SlottedPage::erase`, `checkInvariants`, los tests de los bordes de `bulkLoad` (23 / 24 / 47 / 553),
el test de `bulkLoad` seguido de `insert`, el Full Table Scan y la comparativa del benchmark. Son
los puntos de la rúbrica, o el formato ya está escrito y cambiarlo sale más caro que terminarlo.

**Por qué `erase` no está en esta lista aunque el enunciado no lo pida:** porque el formato de la
página ya lo incluye. `freeHead` está en el header, `freeSpace` suma las regiones libres y hay 5
tests que lo fijan. Recortarlo no sería "dejar de hacer una función": sería cambiar el formato de
la página después de haberlo documentado, y eso sí es caro. Se decidió **implementarlo** y sacarlo
del recorte, para que la decisión no quede reabierta a mitad del proyecto. Lo que sí queda fuera de
la Fase 1 es el borrado **de claves del índice** (`BTree::erase`), que es otra cosa.

**Si aun así no alcanza:** el plan B es entregar menos demos, no menos algoritmos. Se cae la
opción 4 (Graphviz) y la opción 5 se muestra grabada, y se conservan las que pide
`Trab1.md` §5.1.

**Sobre el build de la demo:** el binario por defecto es `Debug`, que es donde siguen vivos los
assert de error de programación. **El benchmark se mide en `RelWithDebInfo`** y se declara en la
diapositiva, sin excusas: el desborde de un nodo es un `Status`, no un assert, así que `-O2 -g -DNDEBUG`
no esconde ningún error de formato —los que sí escondería son los de error de programación, y para
eso está `Debug`. Presentar tiempos medidos en `-O0` y llamarlos "el benchmark" es regalarle un
argumento al jurado: si alguien pregunta por el `-O0`, ya quedó contestado.

---

## 6. Presentación (12 min)

La especificación pide primero **toda** la arquitectura y después **toda** la demostración.
Se reparte por fase, no por persona: así si un módulo falla, solo cae el bloque de su dueño.

### Fase 1 · Exposición de arquitectura — 4:30

| Tiempo | Quién | Qué explica |
|---|---|---|
| 0:00–1:10 | **P1** | Tupla binaria `[nFields][tag,len,datos]…`; por qué `VARCHAR` lleva longitud; la **página slotted**: cabecera de 8 B, directorio de slots, datos hacia atrás; cómo un `RowID{pageID, slotID}` resuelve a `(página, slot)`, y por qué `erase` no invalida los demás `RowID` |
| 1:10–2:20 | **P2** | `PageManager`: archivos con páginas de 4096 B y página 0 de metadatos; dos archivos (`data.db` e `index.db`). **Por qué no hay caché** y qué miden realmente los contadores: **accesos a página**, porque el SO tiene su propio page cache. **El cursor de `HeapFile`**: qué costo tendría escanear |
| 2:20–3:35 | **P3** | **Formato en bytes de la página de nodo**: header de 16 B (con `t` adentro), interno vs hoja. **El cálculo de `t = 204` hecho en vivo**, el papel de `SLACK`, y qué pasa cuando la raíz se llena |
| 3:35–4:30 | **P4** | El árbol completo: qué guarda cada nivel, el descenso de `search` O(log n), y que `bulkLoad` construye bottom-up con reparto uniforme |

### Fase 2 · Demostración en vivo — 7:30

| Tiempo | Quién | Opción de menú | Qué se ve en pantalla |
|---|---|---|---|
| 0:00–1:10 | **P1** | 1 · Storage | Inserción de 100 000 tuplas. `hexdump -C` de una página slotted con una tupla visible, y el `RowID` de esa tupla resolviendo a `(página, slot)` |
| 1:10–2:20 | **P2** | 2 · Pager | `ls -l data.db` creciendo. El contador de `pageReads` subiendo página a página en el Full Table Scan. **De dónde salen las 765 páginas**: el programa imprime `31 B/tupla` y `131 tuplas/página`, y `765 = ceil(100000/131) + 1` se calcula en vivo —y aclara que de esas, **764 son de datos**: las que lee el escaneo |
| 2:20–3:10 | **P4** | 3 · Bulk load | `bulkLoad` del índice con `--verbose`: log `[ALTURA]` en vivo |
| 3:10–4:00 | **P1** | 4 · Árbol | `printTree` en ASCII del árbol resultante y el `.dot` exportado y renderizado con Graphviz |
| 4:00–5:30 | **P3** | 5 · Split (256 B, base propia) | Repaso de un split paso a paso (→ `t = 12`): se ven los splits uno a uno. `hexdump` de una página interna |
| 5:30–7:30 | **P2** | 6 · Benchmark (base propia) | **Index Scan vs Full Table Scan** con **páginas de datos por búsqueda**, en las **dos curvas** (4096 y 256). Cierra con las decisiones de alcance |

> **Aclarar al presentar:** `bulkLoad` no llama a `split()`, por eso en la demo grande no aparece
> ningún `[SPLIT]`. Los splits se ven en la opción 5, que corre a 256 B sobre su propia base
> (§1 bis) e inserta de a una.
>
> **Aclarar también si preguntan por las dos bases:** la opción 5 y la 6 no usan `data.db` ni
> `index.db` porque necesitan otro `page_size` y porque `bulkLoad` exige árbol vacío. Se explica
> en 15 segundos, pero es la diferencia entre una demo que termina y una que se cae.

**Qué se mide en el benchmark** (va en `docs/benchmark-method.md`, dueño P2):

- **Páginas de datos leídas por una búsqueda completa**, que es la métrica principal:
  Index Scan = `altura` páginas del índice (una por nivel) + 1 del heap = **`altura + 1`**;
  Full Scan = **todas** las páginas de datos del heap.
- La página 0 de metadatos **no cuenta**: se leyó al abrir el archivo.
- El **tiempo** se reporta como dato orientativo, con una ejecución de calentamiento descartada y
  la **mediana de `R` repeticiones** (se usa `R` para no confundirla con el `N` del dataset).
- **`R` solo afecta al tiempo, nunca a los recuentos de páginas.** Las páginas son
  deterministas: dan igual en la primera corrida y en la quinta, porque el programa cuenta
  accesos, no mide duraciones. Eso es lo que hace viable la demo: `R = 5` para `N ≤ 10 000`,
  donde se muestra el tiempo, y **`R = 1` para `N = 100 000`**, que es el punto caro. La tabla
  completa sale con `--full` y es la que va al `docs/benchmark-results.md`; en vivo se muestran
  los 12 recuentos de páginas y el tiempo de los 5 puntos cortos.
- El **tiempo de construcción del índice** se mide aparte y se muestra aparte. No entra en el
  tiempo de búsqueda.
- El índice del benchmark se construye con `bulkLoad`, sobre una base temporal nueva para cada
  curva.
- **El build con el que se midió se declara en la diapositiva.**

**Lo que el programa imprime** (los números de páginas salen de las fórmulas de §1; los tiempos
dependen de la máquina, y por eso el ejemplo va marcado como ilustrativo):

```
Dataset del benchmark: 27 B de tupla (1 + 7 + 19) + 4 B de directorio = 31 B/tupla
4096 B -> (4096-8)/31 = 131 tuplas/pagina        256 B -> (256-8)/31 = 8 tuplas/pagina

Metrica: paginas de DATOS leidas por una busqueda completa
         Index Scan = altura del indice + 1 (la pagina del heap)
         Full Scan  = paginas de datos del heap

CURVA A - page_size 4096 (t = 204)
     N  |  altura | Index: págs | Full: págs | ratio
     20 |    1    |      2      |      1     |    0.5x   <-- el escaneo aun gana
    500 |    2    |      3      |      4     |    1.3x
   1000 |    2    |      3      |      8     |    2.7x
   5000 |    2    |      3      |     39     |   13.0x
  10000 |    2    |      3      |     77     |   25.7x
 100000 |    2    |      3      |    764     |  254.7x

CURVA B - page_size 256 (t = 12)
     N  |  altura | Index: págs | Full: págs | ratio
     20 |    1    |      2      |      3     |    1.5x
    500 |    2    |      3      |     63     |   21.0x
   1000 |    3    |      4      |    125     |   31.3x
   5000 |    3    |      4      |    625     |  156.3x
  10000 |    3    |      4      |   1250     |  312.5x
 100000 |    4    |      5      |  12500     | 2500.0x

Archivo data.db con 100 000 tuplas: 765 paginas = ceil(100000/131) + 1
   (765 incluye la de metadatos; el escaneo lee 764)
A 256 B el archivo tiene 12 501 paginas y el escaneo lee 12 500.
Punto de cruce (calculado, no escrito a mano):
  4096 B: el escaneo gana hasta N=131, empate 132-262, el indice gana desde N=263
  256 B : el escaneo gana hasta N=8, empate 9-16, el indice gana 17-23,
          empate suelto en N=24, el indice gana desde N=25
Construcción del índice (bulkLoad, no incluida arriba):   X ms
Medido con: RelWithDebInfo   [R=5 para N<=10000, R=1 para N=100000]
```

**Cómo se lee esto delante del jurado** (es el argumento, no los números sueltos):

- En la **curva A**, `n` crece 100× y una búsqueda del índice sigue tocando **3 páginas**: la raíz,
  la hoja y la tupla del heap. El Full Scan pasa de 8 a 764. Eso es la ganancia: **constante
  contra lineal**, y es la curva que corresponde al `page_size` por defecto.
- En la **curva B**, `n` crece 100× y la **altura sube de 3 a 4**: 100× más datos, +1 nivel como
  máximo. Ahí está la forma logarítmica explícita, y el log `[ALTURA]` la va mostrando mientras se
  construye.
- Cada curva empieza en `N = 20`, **a propósito**, para que se vea el punto de cruce: con 20
  tuplas el escaneo gana (1 o 3 páginas contra 2 del índice) y con 764 ya no. Si alguien pregunta
  "¿cuándo conviene el índice?", la respuesta sale de la tabla y del output: el escaneo gana hasta
  `N = 131` a 4096 B y hasta `N = 8` a 256 B, y a partir de ahí gana el índice. Decirlo es lo que
  hace creíble el resto de la curva.
- Las dos curvas cuentan lo mismo (páginas de datos leídas), así que el ratio es comparable entre
  ellas. No se mezclan páginas de metadatos con páginas de datos: si se mezclaran, el Full Scan
  ganaría una lectura que no paga.

Si alguien pregunta por qué el índice con 4096 B no crece: porque a `t = 204` un solo nodo
interno sostiene hasta 408 hojas, y 246 hojas es cómodo. Por eso la segunda curva usa 256 B.

### Decisiones de alcance que se mencionan al final

Ninguna está prohibida ni exigida por `Trab1.md`; se enuncian para que no parezca un descuido:

1. La clave de índice se trata como `INT` fijo de 4 B (evita el tamaño variable de `VARCHAR` dentro del nodo).
2. `search()` devuelve `vector<RowID>`, lo que soporta claves duplicadas (con regla explícita de descenso). El benchmark usa claves únicas.
3. Las hojas llevan `nextLeafPageID` — preparación para el range scan de la Fase 2.
4. Los nodos del índice se persisten como páginas en su propio archivo, no en memoria.
5. Las métricas de I/O son **accesos a página**, no I/O físico de disco.
6. Las clases se llaman `BTree`/`BTreeNode` como pide el enunciado, pero implementan un B+.
7. `SLACK = 1` en el cálculo de `t`: es un byte de margen deliberado, no un redondeo.
8. El dataset del benchmark usa `VARCHAR` de longitud fija para que el número de páginas sea reproducible; los tamaños variables se prueban en los tests.
9. **El borrado está a medias, y conviene decirlo así:** `SlottedPage::erase` (borrar una tupla del
   heap, sin compactar) **sí está implementado y testeado**, aunque el enunciado no lo pida, porque
   el formato de la página ya lo incluye y sacarlo después sería cambiar el formato. Lo que **no**
   está es `BTree::erase` (borrar una clave del índice), que sí sería alcance nuevo.
10. La métrica del benchmark son **páginas de datos leídas**, no tamaño del índice ni I/O físico.

### Preguntas del jurado y a quién le tocan

| Pregunta | Dueño |
|---|---|
| "¿Por qué `t = 204` y no otro?" | P3 |
| "¿Qué pasa si la raíz se llena?" | P3 |
| "¿Dónde se guarda la raíz cuando cambia?" | P3 |
| "¿Y si la clave está duplicada, y las copias cruzan varias hojas?" | P3 |
| "La clase se llama `BTree`, ¿es un B o un B+? ¿Por qué?" | P3 |
| "¿Cómo se codifica el header de 16 B en bytes?" | P3 |
| "¿Cómo saben que no leen dos veces la misma página?" | P2 |
| "¿Por qué no hay buffer pool?" | P2 |
| "¿Eso es I/O físico o viene del cache del SO?" | P2 |
| "¿Por qué dos archivos?" | P2 |
| "¿Cómo sabes que son 765 páginas sin contarlas?" | P2 |
| "¿Por qué el escaneo dice 764 y no 765?" | P2 |
| "¿Qué pasa con un `VARCHAR` más largo que una página?" | P1 |
| "¿Por qué directorio de slots y no append-only?" | P1 |
| "¿Por qué los bytes van hacia atrás?" | P1 |
| "¿Por qué el `RowID` son 6 B en la página y 8 en memoria?" | P1 |
| "`erase` no compacta: ¿no deja basura? ¿no invalida los `RowID`?" | P1 |
| "¿El borrado estaba en el enunciado? ¿Por qué lo implementaron?" | P1 |
| "¿`search` es de verdad O(log n)?" | P4 |
| "¿Por qué `bulkLoad` no necesita `split`?" | P4 |
| "¿Cómo garantizan que `bulkLoad` no deja nodos casi vacíos?" | P4 |
| "¿Por qué el índice no crece con `n`?" | P4 |
| "¿Por qué la opción 5 no usa el mismo `data.db`?" | P4 |

---

## 7. Reglas de integración

1. **Al empezar:** `Types.h` (P1) primero; luego los headers del contrato de §4 existen con cuerpos stub que **compilan**, y `main.cpp` (P4) con las 6 opciones apuntando a stubs. Nadie programa contra una suposición: si necesita un método que no está en el contrato, lo pide y se agrega, no lo inventa.
2. `PageManager` (P2) es la dependencia más temprana: **sin él no hay I/O para nadie**. Es la pieza más pequeña y la que primero tiene que estar en verde.
3. `findLeaf` (P3) es la pieza compartida entre `insert` y `search`. Si cambia la política de duplicados, cambian los dos: P3 avisa a P4.
4. CMake con **fuentes explícitas, no `GLOB`**, y `Debug` por defecto. Añadir un archivo significa editar el CMake, y eso genera conflictos.
5. `.clang-format` compartido y todos lo usan. Un reformateo automático dentro de un PR que cambia lógica se rechaza.
6. Ningún PR a `main` sin `ctest --output-on-failure` en verde.
7. **Revisión cruzada:** P3 ↔ P4 (ambos escriben el árbol), P1 ↔ P2 (ambos escriben storage). Nadie aprueba su propio código.
8. **Regla de revisión de logs:** `grep -rn "\[SPLIT\]\|\[ALTURA\]" src/` solo puede dar resultados en `BTreeWrite.cpp` (dentro de `logSplit`/`logHeight`). Si aparece en cualquier otro archivo, el PR se rechaza.
9. **Un archivo de `docs/` = un dueño.** Nadie edita el archivo de otro; se enlaza.
10. **`main.cpp` no contiene lógica de módulos.** Si un PR de P1/P2/P3 agrega código ahí, va al módulo correspondiente. P4 es el único que lo edita.
11. **Los `.db` solo se tocan desde las demos**, que los borran una vez al inicio de la ejecución. Ningún test escribe en `data.db` ni `index.db` del repositorio.
12. **Commits significativos y frecuentes, y de cada uno de los cuatro** — esto sí lo exige
    `Trab1.md` §4.2.2, y se evalúa el flujo de trabajo en Git. Un commit atómico y descriptivo
    (`"P3: split() puro, sin I/O, con sus tests"`), nunca `"cambios varios"`, y **nadie pasa
    semanas sin aparecer en el historial**: la rúbrica mira que el trabajo de los cuatro se vea.
13. Ramas: `contract` (la primera, con los stubs), `storage-format`, `storage-io`, `btree-node`, `btree-tree`. P4 integra a `main`.
14. P4 ensambla el `README.md` con las secciones de los otros tres, pero solo las organiza.
15. **El repo está en GitHub** (`Trab1.md` §4). P4 crea el repositorio, configura el remoto en la
    rama `contract` y da acceso de escritura a los cuatro. Si es privado, los cuatro quedan como
    colaboradores: cuatro personas con el mismo nivel, P4 solo mergea. Nadie trabaja en `main`
    directo.
16. **Un Issue por tarea, un PR por Issue.** El enunciado (§4.2.3) los deja "opcionales pero
    recomendados"; el equipo los adopta como regla propia porque con cuatro personas en paralelo
    es la única forma de que se sepa qué está haciendo cada quien. Un Issue por módulo de §3.1,
    con su dueño y su criterio de aceptación; el PR cierra el Issue y lo referencia. Así el
    historial se lee como avance contra la rúbrica y no como ruido.
17. `.gitignore` (P4) contiene `build/`, `*.db`, `*.dot`, `salidas/`, `*.o`. Nada de `.db` ni de
    salidas de demo llega al repo: si llega, el repositorio muestra datos de otra máquina y
    `data.db` desincronizado rompe la demo de cualquiera que lo clone.
18. **Nadie commitea sobre `main`.** Cada PR pasa por `ctest` en verde y por revisión cruzada
    (P3 ↔ P4, P1 ↔ P2). P4 es el único que mergea.
19. **`NodeData` y el header de 16 B quedan congelados desde la rama `contract`** (cambio 38).
    `NodeData` es la representación en memoria que tocan P1 (`TreePrinter`), P2 (`Benchmark`),
    P3 (todo) y P4 (`TreeInvariants`): si un campo se agrega o cambia de tipo **a mitad del
    proyecto**, los cuatro `.cpp` se rompen el mismo día y el PR se vuelve inmergeable.
    Regla operativa:
    - Los **cuerpos** de `BTreeNode`, `BTree`, `PageManager` y `SlottedPage` se pueden cambiar
      cuando quiera su dueño; son implementación.
    - Las **firmas** de §4 (`NodeData` como struct, `serialize`/`deserialize`, `fromData`,
      `data()`, `search`, `indexScan`, `bulkLoad`, y también `Benchmark::run` y
      `Benchmark::crossover`, que P4 invoca desde `main.cpp` y no puede acomodar por su cuenta)
      y el **layout de 16 B** solo se cambian con un
      Issue de las cuatro personas, antes de que arranque el trabajo de los demás.
    - Mientras tanto, si un cambio de firma es inevitable, se hace **agregando** (un campo nuevo al
      final de `NodeData` con valor por defecto, un parámetro con valor por defecto), nunca
      **modificando** lo que ya está.
    - Lo que lo protege son **`static_assert` dentro de `BTreeNode.h`** (`sizeof` del header = 16,
      `offsetof` de cada campo) y **una sección del test `btree_node` que relee los siete campos
      de `NodeData` con los tipos de §4**. No se crea una suite nueva: siguen siendo **7** (cambio 31).
20. **`serialize` devuelve `Status`, no aborta.** El desborde de un nodo (`keyCount > 2t-1`) es un
    caso de **entrada de datos**, no un error de programación: `insertInNode` deja entrar hasta
    `2t` claves a propósito, y quien llama decide. Con `assert` el fallo desaparecía en
    `RelWithDebInfo` (`-DNDEBUG`) y el benchmark se medía sobre un programa que en `Debug` se
    caía. La misma regla se aplicó después a las precondiciones de `bulkLoad` (árbol no vacío,
    entrada desordenada) y a la tupla de 0 campos: los tres devuelven `Status::PreconditionFailed`
    y tienen test. Lo que sí sigue siendo assert es el **error de programación dentro de una función
    ya llamada con el tipo equivocado**: `asInt()` sobre un `VARCHAR` y `bytes.size() < 2` en
    `SlottedPage::insert`.

21. **Regla general de las precondiciones: si un test tiene que comprobar una condición, esa
    condición devuelve `Status`, no `assert`** (cambio 40). Sin `fork` un `assert` no se puede
    testear, y con `-DNDEBUG` —el build en el que se mide el benchmark— desaparece. Afecta a
    `Tuple::serializeTo` con 0 campos y a las dos precondiciones de `bulkLoad`; los tres devuelven
    `Status::PreconditionFailed` sin tocar el estado. Cada test de esos comprueba además que el
    estado no cambió, porque un solo valor de `Status` para las tres condiciones no distingue por sí
    solo cuál se evaluó.

### Checklist de arranque

- [ ] Repositorio creado en GitHub, con remoto en la rama `contract` y los cuatro como colaboradores (P4)
- [ ] Un Issue por módulo de §3.1, con dueño y criterio de aceptación
- [ ] `.gitignore` con `build/`, `*.db`, `*.dot`, `salidas/`
- [ ] Historial con commits frecuentes y de los cuatro, y al menos un PR por módulo mergeado por P4
- [ ] `Types.h` con `Page`, `PageID`, `SlotID`, `Status` (incluido `Corrupt`), `RowID{pageID, slotID}` en la rama `contract`
- [ ] `Value` y `Tuple` en el contrato, con el formato `[nFields][tag,len,datos]`, little-endian y `deserialize` que devuelve `Status`
- [ ] `DataGen` en el contrato (`sorted`, `randomKeys`, `datasetTuple`), porque lo usan P3 y P4
- [ ] `TestHarness.h` y `test_main.cpp` (P1) con `TEST`/`CHECK`/`CHECK_EQ` y el despacho por `--suite`. **Sin `fork` y sin `EXPECT_ASSERT_FAILS`**: una precondición que un test tiene que comprobar devuelve `Status` (regla 21), porque `fork` en `ctest` es portable solo a medias y en macOS se comporta distinto
- [ ] `Status` incluye `PreconditionFailed` y las tres precondiciones testeadas lo devuelven sin assert: `Tuple::serializeTo` con 0 campos, `bulkLoad` sobre árbol no vacío y `bulkLoad` con entrada desordenada
- [ ] `Tuple::serializeTo` devuelve `Status` (no `void`) y `HeapFile::insert` lo propaga; ningún test depende de que un `assert` dispare
- [ ] Los headers con stubs compilables (`SlottedPage`, `PageManager`, `HeapFile`, `BTreeNode` con `NodeData` y `t`, `BTree`, `Metrics`, **`Benchmark`**, `TreePrinter`, `TreeInvariants`) en la rama `contract`
- [ ] **`NodeData`, el header de 16 B y las firmas de §4 congelados** (regla 19): `static_assert` de tamaño y `offsetof` en `BTreeNode.h`, la sección de contrato en el test `btree_node`, y el Issue de las cuatro personas abierto antes de empezar
- [ ] `main.cpp` (P4) con las 6 opciones de menú apuntando a stubs y el reparto de archivos de §1 bis
- [ ] `CMakeLists.txt` (P4) congelado, con fuentes explícitas, `Debug` por defecto, `-Wall -Wextra`, un `add_test` por suite y `tests/` en la raíz
- [ ] `.clang-format` en el repo, y todos usándolo
- [ ] `reset_databases()` llamado **una vez al inicio** de la ejecución; las opciones 5 y 6 sobre base temporal
- [ ] Los tests usan `temp_directory_path()`, nunca `data.db` / `index.db` del repo
- [ ] `PageManager` con `read` `const`, contadores de accesos, validación de `magic`/`page_size` y `readMeta`/`writeMeta` funcionando
- [ ] `computeT(4096) == 204` y `computeT(256) == 12` con test, y `SLACK` documentado
- [ ] Test de que `serialize` devuelve `Ok` con `page_size = 256` para 0, 1, `t-1`, `t` y `2t-1` claves, y `NodeOverflow` para `2t` — **en `Debug` y en `RelWithDebInfo`**, para probar que no depende del build
- [ ] Test de que caben 131 tuplas en una página de 4096 (y la 132 da `PageFull`), y 8 en una de 256 (y la 9)
- [ ] `HeapFile` con cursor: test de que 1 000 inserts dan 9 páginas y 1 000 escrituras, no 9 000
- [ ] `erase` sin compactar: test de que no mueve un byte de los datos de los demás slots, de que el slot se reutiliza, y de que `freeSpace` **crece** al borrar y el `insert` posterior no baja `dataEnd`
- [ ] **Cadena de libres por índices de slot** (cambio 43): `freeHead` y el puntero dentro de la región liberada son ambos `slotID`, con `0xFFFF` de fin — test de que al liberar **dos** slots y reutilizar solo el primero, el segundo **sigue en la cadena** (su `length` se sigue sumando en `freeSpace`) y se puede reutilizar después; y test de que una página llena de 27 B deja la cadena vacía con `freeHead == 0xFFFF`
- [ ] **Reutilización con sobrante** (cambio 41): test de que `erase` de 27 B seguido de `insert` de 4 B reutiliza la región entera, que `lookup` devuelve los 27 B, que `Tuple::deserialize` da la tupla de 1 campo con `Ok` **ignorando la cola**, y que **`freeSpace()` baja los 27 del `length` completo y no los 4 de la tupla** —con las 131 tuplas del caso de arriba: **27 → 54 → 27**— y que un segundo `erase` + `insert` de 27 B recupera la región entera
- [ ] `split(rightPageID)` puro: test que confirma que no toca ningún archivo y que graba `right.self`
- [ ] `deserialize` devuelve `Status::Corrupt` con `t`, `keyCount` o un offset alterado, y con una página de tuplas del heap pasada por nodo
- [ ] Regla de duplicados con test (40 copias cruzando hojas)
- [ ] `logSplit`/`logHeight` definidos por P3 con el formato documentado; `bulkLoad` solo los llama
- [ ] `checkInvariants` con los casos 0, 1, `2t-1`, `2t` y su test negativo
- [ ] `bulkLoad` con reparto uniforme y tests en los bordes (23, 24, 47, 553 con `page_size = 256`)
- [ ] Test de equivalencia `bulkLoad` vs `insert` escrito
- [ ] El benchmark mide **páginas de datos por búsqueda** (`altura + 1`), y el programa imprime `ceil(N/131)+1` al lado
- [ ] **El punto de cruce lo calcula `Benchmark::crossover`, no está escrito a mano** (cambio 39): test que fija 263 a 4096 B, y 17 / 24 / 25 a 256 B, contra la salida del programa
- [ ] El benchmark tiene las **dos curvas** (4096 y 256), cada una sobre base temporal propia
- [ ] El benchmark declara con qué build se midió
- [ ] **`--demo 6` cronometrado**: cabe en 2:00 con las 12 filas. Si no, se saca el punto de `N = 5 000` y se dice que la tabla completa está en `docs/benchmark-results.md` (riesgo 25)
- [ ] El protocolo de `R` escrito en `docs/benchmark-method.md`: `R = 5` hasta `N = 10 000`, `R = 1` en `N = 100 000`, y la nota de que las páginas no dependen de `R` porque son deterministas
- [ ] Cada quien redacta el `docs/` de su módulo (un archivo = un dueño)
- [ ] `README.md` con los 4 puntos exigidos: entorno/dependencias, compilar y ejecutar, correr tests, **por qué ese `t`**, más la tabla **enunciado → código** (§4)
- [ ] Nombres de clase y método coinciden con `Trab1.md` §3 (`BTreeNode`, `BTree`, `search`, `insert`, `RowID`)
- [ ] Salida de las 6 demos capturada en un `.txt` de respaldo, por si falla el proyector
- [ ] Presentación ensayada **por fases** (arquitectura completa, luego demo completa), no por persona
- [ ] **`--demo 0` corrido entero, de corrido, cronometrado**: es la prueba real de que §1 bis funciona

---

## 8. Verificación

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure     # suite completa en verde
./build/main                                  # menu de las 6 opciones
./build/main --demo 0                         # las 6, en orden
./build/main --demo 6                         # las dos curvas del benchmark
./build/main --demo 6 --n 50000               # una sola corrida, para depurar
./build/main --demo 6 --full                  # las 12 filas con R repeticiones
./build/main --page-size 256 --demo 5         # splits visibles uno a uno
./build/main --demo 6 | grep -A3 "Punto de cruce"   # el cruce, calculado
grep -rn "\[SPLIT\]\|\[ALTURA\]" src/         # solo debe aparecer en BTreeWrite.cpp
```

### Reparto de tiempo en la presentación

| | Arquitectura | Demo | Total |
|---|---|---|---|
| **P1** | 1:10 | 2:00 | 3:10 |
| **P2** | 1:10 | 3:10 | 4:20 |
| **P3** | 1:15 | 1:30 | 2:45 |
| **P4** | 0:55 | 0:50 | 1:45 |
| | **4:30** | **7:30** | **12:00** |

**P2 tiene el bloque de demo más largo a propósito** (cambio 36): la comparativa Index Scan vs
Full Table Scan son 5 de los 20 puntos, es la parte que más se rompe y además es **su** código, así
que la puede depurar hasta el último segundo. P4 se quedó con 0:50 de demo (el
`bulkLoad` con su log `[ALTURA]`) porque su bloque fuerte es el árbol completo en la fase de
arquitectura. P1 tiene 2:00 de demo porque muestra dos bloques (storage y árbol, porque escribió
`TreePrinter`).

**La carga no está repartida por igual y no tiene por qué estarlo:** P4 habla 9 minutos menos que
P2. Se acepta porque el bloque de P2 es el que más trabajo tiene delante (si el benchmark no
sale, se caen 5 puntos), y porque la nota de presentación es individual: **todos deben poder
responder preguntas de cualquier módulo**, así que la asimetría de tiempo no se traduce en
desigualdad de nota.

---

## 9. Mapeo a la rúbrica

| Criterio | Pts | Dónde se gana |
|---|---|---|
| Core B-Tree & Storage | 6.0 | P1 (formato) + P2 (I/O) + P3 (`split`, `insert`, serializador) + P4 (`search`, `bulkLoad`) |
| Demostración y Performance | 5.0 | P2 presenta el benchmark y fundamenta de dónde salen las 765 páginas; **P4** presenta la conclusión junto a las demos de lectura; P1 muestra el árbol |
| Repositorio en GitHub | 3.0 | Estructura de `docs/`, README de P4 con tabla enunciado → código, **commits frecuentes y de los cuatro** (regla 12, que es lo que pide §4.2.2), un PR por módulo con su Issue, remoto y permisos de los cuatro (reglas 15-18 de §7) |
| Pruebas Automatizadas | 3.0 | **7 suites** (`storage`, `tree_printer`, `page_manager`, `btree_node`, `btree_write`, `btree_read`, `invariants`), cada dueño el suyo, más el harness y el verificador compartidos, con `checkInvariants` en todo árbol que se construya |
| Presentación | 3.0 | Reparto por fases, con dueño asignado para cada pregunta |
| **Total** | **20.0** | |

**Desviación del enunciado que se declara en el README:** `Trab1.md` §4.1 escribe
`src/main.ext`; el repositorio usa `src/main.cpp`, que es lo que se compila en Linux y lo que
`Trab1.md` §4.1 quiere decir ("punto de entrada de prueba"). El resto de la estructura sugerida
(`src/storage/`, `src/index/`, `tests/`, `docs/`, `README.md`, `.gitignore`) se sigue tal cual.

---

## 10. Riesgos conocidos

| # | Riesgo | Sev. | Quién lo resuelve | Mitigación |
|---|---|---|---|---|
| 1 | `bulkLoad` viola el mínimo de ocupación | Alta — bug de correctitud | P4 | Reparto uniforme fijado en §1 con su demostración; tests en los bordes (23, 24, 47, 553) y `checkInvariants` |
| 2 | Los `.db` no se reinician entre corridas | **Alta — tumba la demo** | P4 | `reset_databases()` al inicio de cada ejecución; los tests usan rutas temporales; regla 11 de §7 |
| 3 | `page_size = 256` deja el nodo al límite | **Alta — corrupción silenciosa** | P3 (fórmula) · P4 (CMake) | `SLACK = 1` en `computeT`; `serialize` devuelve `Status::NodeOverflow` en vez de assertar, así que el desborde no depende del build; test de que `serialize` no reventa con 256 B |
| 4 | `insert` del heap es O(n²) si escanea páginas | Alta — la demo no termina | P2 | Cursor de última página con espacio, fijado en el contrato; test de que 1 000 inserts dan 9 páginas y 1 000 escrituras |
| 5 | Los números del benchmark no salen de nada si el esquema no está fijado | Alta — la demo queda en AssertionError | P1 (esquema) · P2 (fórmula) | Formato de tupla y de slotted page fijados byte a byte; `131 tuplas/página` con test; el programa imprime la fórmula |
| 6 | El Full Table Scan se detiene en la primera coincidencia y la comparativa queda inflada | Media — se ve en la pregunta del jurado | P2 | Semántica fijada: recorre todas. Se documenta en `docs/benchmark-method.md` |
| 7 | Se reporta el tamaño total del índice en vez de páginas de datos por búsqueda | Media — no demuestra O(log n) y es fácil de atacar | P2 | Métrica fijada: páginas de datos leídas por búsqueda; doble curva |
| 8 | Log `[ALTURA]` duplicado y divergente | Media | P3 | `logSplit`/`logHeight` en `BTree.h`; `bulkLoad` solo llama; regla 8 de §7 con `grep` |
| 9 | Test invariante falla con árboles de 0 y 1 nodo | Media — se descubre tarde | P4 | Árbol vacío = hoja raíz vacía, raíz exenta del mínimo; casos 0, 1, `2t-1`, `2t` y test negativo |
| 10 | Nombres de clase ≠ especificación | Media — riesgo de puntos | P4 | Clases `BTree`/`BTreeNode`/`RowID`; tabla enunciado → código en §4 y en el checklist |
| 11 | `main.cpp` es un punto único de falla | Media | P4 | Aceptado. Se escribe con las 6 opciones desde el principio y se va rellenando; nunca queda "pendiente" |
| 12 | El serializador de nodo se atrasa y arrastra a tres personas | Media | P3 | Es el primer cuello de botella de §5; si se atrasa, se recorta Graphviz y la segunda curva, nunca el árbol |
| 13 | La preparación de 100 000 tuplas en `-O0` tarda demasiado para la demo | Media | P2 | Se mide en `RelWithDebInfo` desde el principio y **se declara el build en la diapositiva** |
| 14 | Conflicto de reformateo automático tapa los cambios reales | Baja | Todos | `.clang-format` compartido; regla 5 de §7 |
| 15 | Se implementa `erase` y no hay nada que lo use | Baja — trabajo perdido | P1 | **Decidido: entra en la Fase 1** y sale del orden de recorte (§5), porque el formato de la página ya lo incluye. Se demuestra con el test de reutilización de región libre, incluido el caso de una región mayor reutilizada por una tupla menor (cambio 41), y su alcance se declara al final de la presentación |
| 16 | **`read`/`search` no compilan por el `const`**: los contadores se escriben dentro de un método `const` | **Alta — bloquea el arranque** | P2 | `read` es `const` y los contadores son `mutable`; está en el contrato de §4 antes de que nadie escriba código |
| 17 | **La demo se cae en el minuto 8**: la opción 5 necesita 256 B y la 6 un árbol vacío, sobre archivos que las opciones 1-4 ya usaron | **Alta — tumba la demo** | P4 | Tabla de reparto de archivos por opción (§1 bis): 1-4 sobre `data.db`/`index.db`, 5 y 6 sobre bases temporales. `--demo 0` se ensaya entero y cronometrado |
| 18 | Las cifras del benchmark no se sostienen si alguien cuenta páginas de metadatos junto con las de datos | Media — se ve en la pregunta del jurado | P2 · P4 | Métrica única declarada: **páginas de datos leídas**; `readMeta`/`writeMeta` no cuentan; 765 del archivo contra 764 leídas, mostrado explícitamente |
| 19 | Una página corrupta se deserializa como basura en vez de fallar | Media — fallo silencioso | P3 | `Status::Corrupt` en el contrato, con test que altera `t`, `keyCount` y un offset. No hay campo magic: se valida por invariantes (`t == computeT`, `isLeaf ∈ {0,1}`, `keyCount <= 2t-1`, datos dentro de la página), y el magic de archivo ya lo revisa `PageManager::open` |
| 20 | Abrir un `.db` con un `page_size` distinto al de sus páginas | Media — basura silenciosa | P2 | `PageManager::open` valida `magic` y `page_size`; test con `page_size` y `magic` corruptos |
| 21 | El `RowID` mide 8 B en memoria y se usa `sizeof` en el cálculo de `t` | Media — `t` mal calculado | P3 · P1 | El contrato dice explícitamente 6 B serializados / 8 B en memoria, y el serializador escribe campo por campo |
| 22 | **`NodeData` o el header de 16 B cambian a mitad del proyecto** | **Alta — bloquea a los cuatro** | P3 (dueño) | Congelados desde la rama `contract` (regla 19 de §7): cuerpos libres, firmas y layout solo con Issue de las cuatro. `static_assert` de tamaño y `offsetof` en el header, y una sección del test `btree_node` que relee los siete campos |
| 23 | Una precondición queda como `assert`, se compila con `NDEBUG` y el benchmark se mide sobre un programa que en `Debug` se cae | **Alta — invalidaría la medición** | P3 · P4 | `serialize` devuelve `Status::NodeOverflow` y `insert`/`bulkLoad` propagan `Status` (cambio 37). Las dos precondiciones de `bulkLoad` y la tupla de 0 campos también devuelven `Status::PreconditionFailed` y tienen test (cambio 40). Solo quedan asserts de error de programación dentro de una función ya llamada con el tipo equivocado, y el benchmark se mide en `RelWithDebInfo` declarándolo |
| 24 | La curva del benchmark empieza en `N = 1 000` y esconde el punto de cruce | Media — pregunta del jurado sin respuesta | P2 | Cada curva corre 6 valores desde `N = 20` (cambio 35) y el cruce **lo calcula el programa** con `Benchmark::crossover`, que recorre `N` de a uno y devuelve las bandas (cambio 39): a 4096 B el escaneo gana hasta `N = 131` y el índice desde `263`; a 256 B hasta `N = 8` y desde `25`, con la isla de empate en `N = 24`. Un test fija esos valores contra la salida, así que si cambia la fórmula de `t` o la de `altura` el cruce cambia con ella. Se dice en la presentación en vez de dejarlo implícito |
| 25 | **La opción 6 se pasa de los 2:00 de demo**: 12 corridas, y la de 256 B con 100 000 tuplas escribe un archivo de 12 501 páginas | **Alta — se come el tiempo de la presentación** | P2 | Los **recuentos de páginas son deterministas**, así que `R = 1` en `N = 100 000` no cambia ni un número de la tabla: solo se pierde el tiempo, que es orientativo. Se cronometra `--demo 6` antes de la presentación; si aun así se pasa de 2:00, cae el punto de `N = 5 000` (que es el que menos aporta) y se anuncia en voz alta que la tabla completa está en `docs/benchmark-results.md` |

---

## 11. Integración

P4 es el integrador por defecto: mergea a `main`, mantiene `CMakeLists.txt` y `main.cpp`, y
arma el `README.md` final con las secciones que los demás le pasan. No escribe el código de
nadie; si falta algo, lo pide.

**Quién revisa qué** (nadie aprueba su propio código):

| Artefacto | Autor | Revisor |
|---|---|---|
| `Types.h`, `Value`, `Tuple`, `SlottedPage`, `DataGen` | P1 | **P2** |
| `PageManager`, `HeapFile`, `Metrics`, `Benchmark` | P2 | **P1** (y **P4** para lo que se presenta) |
| `BTreeNode`, `BTree.h`, `BTreeWrite.cpp` | P3 | **P4** |
| `BTreeRead.cpp`, `TreeInvariants` | P4 | **P3** |
| `CMakeLists.txt`, `main.cpp`, `README.md`, `.gitignore` | P4 | **cualquiera de los tres** |
| Cambios del contrato de §4 | quien lo pide | **los tres** antes de abrir el PR |

### Lo que este documento NO cierra

Las decisiones de diseño están cerradas y los números están justificados. Lo que sigue no es
diseño, y cada punto necesita una decisión o una acción que no depende de este plan:

- [ ] **Nombres reales.** Todo el documento usa P1-P4. Falta pegarle un nombre a cada rol y
      reemplazar los roles en la primera pasada.
- [ ] **Repositorio en GitHub**, remoto y los cuatro como colaboradores (regla 15 de §7), más la
      rama `contract` con los stubs compilables y los `static_assert` de la regla 19.
- [ ] **Un Issue por módulo de §3.1**, con dueño y criterio de aceptación, y el Issue de las
      cuatro personas para cualquier cambio de firma.
- [ ] **Abrir la rama `contract` antes de repartir el trabajo.** Es la que decide quién rompe a
      quién el primer día: los stubs de §4 no son documentación, se compilan.
- [ ] **`TestHarness` y `DataGen` primero, no después.** `TestHarness.h` (P1) es la primera
      entrega y `DataGen` va en la rama `contract`, porque tres personas lo necesitan para tener
      un `ctest` en verde.
- [ ] **Medir el tiempo real de la demo y corregir §6.** Los 12 minutos están calculados con
      supuestos, no cronometrados. Si `--demo 0` tarda más de 14 minutos, se recortan demos
      según el orden de recorte de §5, no al revés.
- [ ] **Elegir el `seed` del dataset aleatorio** y dejarlo fijo en `DataGen`, para que el
      benchmark y los tests sean reproducibles entre máquinas.
- [ ] **Firmar el punto de corte de la Fase 2.** La rúbrica alcanza con las 6 demos; el range
      scan se puede dejar como "siguiente paso". Si el comité de la UNSA lo pide explícitamente,
      `nextLeafPageID` ya está en el header y el `search` ya lo sigue.

**Sobre el estado de este documento:** los cálculos de §1 están verificados a mano y son los
mismos que el programa imprime, así que las cifras de la presentación no deberían cambiar. Si
alguna no cuadra cuando corra el código, es el código el que se corrige y este documento se
actualiza con el mismo número: no se maquilla la curva para que cuadre con el texto.
