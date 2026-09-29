# Índice: formato en bytes de la página de nodo

> Dueño: **P3**. Este archivo describe el layout congelado por `open_v4.md` §4 (regla 19):
> el header de 16 B **con `t` adentro**, el layout de hoja y de nodo interno, y el cálculo
> de `t = 204`. `BTreeNode.h`, `NodeData` y estas 16 bytes solo cambian con un Issue de las
> cuatro personas. Los cuerpos de las funciones son libres; el formato de aquí no.
>
> El lado opuesto —cómo se inserta, cómo se propaga el split y qué imprimen los logs— está
> en [`btree-insert.md`](btree-insert.md).

## 1. Header de 16 B

Todo nodo, hoja o interno, empieza con los mismos 16 bytes, escritos campo por campo en
**little-endian** (nunca con `memcpy` del struct, por el mismo motivo que
[`storage-tuple.md`](storage-tuple.md) §2):

```
 byte:   0        1   2     3          6    7          10    11  12    13  14  15
       ┌──────┬─────────┬───────────────┬───────────────┬─────────┬─────────────┐
       │isLeaf│ keyCount│     self      │   nextLeaf    │    t    │ reservado   │
       │ u8   │  u16    │     u32       │     u32       │  u16    │   3 B       │
       └──────┴─────────┴───────────────┴───────────────┴─────────┴─────────────┘
         1 B     2 B          4 B             4 B          2 B        3 B     = 16 B
```

| Bytes | Campo | Tipo | Valor |
|---|---|---|---|
| `0` | `isLeaf` | `u8` | `1` hoja, `0` interno. Cualquier otro valor → `Corrupt` |
| `1..2` | `keyCount` | `u16` | número de claves, hasta `2t − 1` |
| `3..6` | `self` | `u32` | la **propia** página del nodo (`PageID`) |
| `7..10` | `nextLeaf` | `u32` | siguiente hoja en la cadena, `0` = última. **Se escribe como `0` en los internos** |
| `11..12` | `t` | `u16` | el grado con el que se creó el nodo |
| `13..15` | reservado | `3 B` | siempre `00 00 00` |

Después del header viene el área de datos, que depende del tipo:

```
HOJA (page_size = 256, t = 12, 3 claves)                  INTERN (1 clave)

┌────────────┬──────────────────────┬────────────────┐   ┌────────────┬──────────────┬───────────────────┐
│ header 16 B│ claves: 4 B × keyCount│ rowIDs: 6 B × k│   │ header 16 B│ claves: 4 B × k│ hijos: 4 B × (k+1)│
└────────────┴──────────────────────┴────────────────┘   └────────────┴──────────────┴───────────────────┘
 16 + 4k + 6k = 16 + 10k                                   16 + 4k + 4(k+1)
```

Ejemplo real de una hoja con `t = 12`, `self = 1`, `nextLeaf = 0` y las claves `1000, 1001`
apuntando a `{page 5, slot 3}` y `{page 5, slot 4}`:

```
01 02 00  01 00 00 00  00 00 00 00  0C 00  00 00 00
│  └ keyCount=2       └ self=1     └ next=0  └ t=12  └ reservado
└ isLeaf=1

E8 03 00 00  E9 03 00 00        05 00 00 00 03 00   05 00 00 00 04 00
└ 1000 (LE)  └ 1001 (LE)        └ {5,3} (4+2 B)     └ {5,4}
 offset 16                          offset 24             offset 30   -> 36 B usados
```

Dos detalles del `RowID` que cambian la cuenta y están en [`storage-tuple.md`](storage-tuple.md)
§5: son **6 B en la página** (4 de `pageID` + 2 de `slotID` sin alinear) y 8 en memoria. El
serializador escribe los 6 bytes a mano; si alguien usara `sizeof(RowID)` en la fórmula de §4,
`t` saldría mal y el árbol se corrompería en silencio.

El `childCount` de un interno es `keyCount + 1`. La única excepción que `deserialize`
tolera es `keyCount == 0` → 0 hijos (el caso degradado que §3 explica por qué no puede
existir), porque una clave separa exactamente a dos conjuntos.

## 2. Por qué `t` vive dentro del header

`NodeData::t` está **en memoria y en la página** a propósito. Con él, `deserialize` puede
validar la página leyéndola sola, sin saber con qué `page_size` se escribió:

1. `t == computeT(page_size)` — si no cuadra, la página **no es de un nodo**: o es una
   página recién alocada llena de ceros (`t = 0`), o es la cabecera de una tupla del heap,
   o el archivo se abrió con otro `page_size` que el que usó el escritor. Devuelve
   `Status::Corrupt`, no basura y no un `assert`.
2. `keyCount <= 2t − 1` — un nodo con más claves de las que caben está corrupto, y aquí se
   detecta **antes** de leer el área de datos, no después de que un `for` se pase de rango.
3. El área de datos no se sale de la página (`16 + payload <= page_size`).

No hay campo `magic` en el header: esa es la decisión congelada. La corrupción se detecta
**por invariantes de estructura**, no por una firma; un nodo cuyos bytes sigan siendo
válidos pero que apunte a la página equivocada lo detecta `checkInvariants` (P4) y lo detecta
`open`, que exige `self == root_page_id` de la raíz.

## 3. `computeT`, `SLACK` y el `t = 204`

```cpp
static uint16_t computeT(size_t page_size);   // 204 @ 4096 B, 12 @ 256 B
```

Con `SLACK = 1` (un byte de margen, para que ninguna de las dos desigualdades quede en el
límite exacto):

```
Interno:  16 + 4·(2t−1) + 4·(2t) + 1  ≤  page_size   ->   16t + 13  ≤  page_size
Hoja:     16 +        10·(2t−1)      + 1  ≤  page_size   ->   20t +  7  ≤  page_size

             t_interno = (page_size − 13) / 16
             t_hoja    = (page_size −  7) / 20
             computeT  = min(t_interno, t_hoja)
```

La hoja es la restricción más estrecha, así que **`t = 204`** con 4096 B: 407 claves y 408
hijos por nodo interno.

```
page_size 4096 → t = 204   hoja llena: 16 + 407·10 = 4086 de 4096 B (10 B de sobra)
                           interno lleno: 16 + 407·4 + 408·4 = 3276 de 4096
page_size  256 → t =  12   hoja llena: 16 +  23·10 =  246 de  256 B
                           interno lleno: 16 +  23·4 +  24·4 =  204 de  256
```

Con `--page-size 256` la fórmula da `t = 12`, que es lo que hace visibles los splits uno a
uno en la demo: con `t = 204` no ocurre un solo split hasta ~408 inserciones.

El árbol **necesita `t >= 2`**, y eso es un límite de `page_size`, no una preferencia:

- Con `t = 1` el split de un nodo interno deja al hijo derecho con **0 claves y 1 hijo**,
  y `deserialize` lee 0 hijos cuando `keyCount == 0`: la página se reescribe sin ese hijo
  y el árbol se rompe en la siguiente lectura.
- Con `t = 0` (`page_size = 28`, que `PageManager` sí acepta) `serialize` calcula `2t − 1`
  como `size_t` y deja de cortar.

La hoja pide `20t + 7`: **`t = 2` recién cabe en 47 B**. Por eso `BTree::open` comprueba
`computeT(page_size) >= 2` antes de crear nada y devuelve `Status::PreconditionFailed` si no
se cumple —que es un contrato roto del que llama, no un archivo corrupto—.

## 4. `serialize` / `deserialize`: qué devuelven y cuándo

Ambos devuelven `Status`, nunca abortan:

| Condición | `serialize` | `deserialize` |
|---|---|---|
| `keyCount <= 2t − 1` y cabe en la página | `Ok`, escribe los `page_size` bytes | — |
| `keyCount > 2t − 1` | **`NodeOverflow`** y **no escribe nada** | **`Corrupt`** |
| `isLeaf` distinto de 0 y de 1 | — | `Corrupt` |
| `t != computeT(page_size)` (incluye `t == 0`) | — | `Corrupt` |
| `16 + payload > page_size` | — | `Corrupt` |

`NodeOverflow` es un **error esperado, no un error de programación**: `insertInNode` deja
entrar hasta `2t` claves en memoria a propósito (el split necesita verlas todas juntas), y
`insert` parte el nodo **antes** de llamar a `serialize`. Por eso el desborde se sigue
detectando en `Release`, donde `-DNDEBUG` borra los `assert`, y por eso el benchmark se puede
medir en `RelWithDebInfo` sin esconder errores de formato.

`data()` / `fromData()` son el intercambio con `NodeData` en memoria y conservan **todo**,
incluido `t` y `next_leaf`; son ida y vuelta puros, sin disco.

## 5. `split`: puro, y con dos semánticas distintas

```cpp
SplitResult split(PageID right_page_id);   // { BTreeNode right; int32_t promoted_key; }
```

`split` **no hace I/O y no alocan páginas**: solo reparte claves en memoria y graba
`right.self = right_page_id`. El que llama (es `BTree::insert`) es el que alocan, escriben
y deciden el orden. Se llama siempre con el nodo en el estado de desborde, **`2t` claves**:

| | Clave promovida | Izquierdo | Derecho |
|---|---|---|---|
| **Hoja** | `keys[t]` → se **copia** (sigue estando en el hijo derecho) | `t` claves | `t` claves |
| **Interno** | `keys[t]` → **sube** y se **borra** de ambos | `t` claves, `t+1` hijos | `t−1` claves, `t` hijos |

```
        ...                    [ ... | s | ... ]                    ...            [ ... s ... ]
                                  sube s                                  s NO queda en ninguno
hoja:    izq = keys[0..t-1]     derecho = keys[t..2t-1] (con s adentro)
interno: izq = keys[0..t-1]     s = keys[t]           derecho = keys[t+1..2t-1]
```

La asimetría de la hoja no es un descuido: `search` (P4) recorre la cadena `nextLeaf` desde
la primera hoja que puede contener la clave, y para eso la última copia de una clave tiene
que quedar **dentro** de la hoja derecha, que es la que queda enlazada después. Ver la regla
de duplicados en [`btree-insert.md`](btree-insert.md) §4.

El enlazado de hojas también lo arregla `split`:

```
antes:   ... -> izq -> X
después: ... -> izq -> derecho -> X        (izq.next = derecho; derecho.next = X)
```

`split` **no** toca `nextLeaf` de un nodo interno: los internos lo llevan en `0` siempre
(eso es lo que serializa el header, §1).

## 6. Qué no hay, y por qué

- **No hay `BTree::erase`.** `SlottedPage::erase` existe y no compacta
  ([`storage-tuple.md`](storage-tuple.md) §4), pero el índice no borra: la rúbrica pide
  búsqueda, inserción, split y bulk load, y un B+ tree sin borrado es una estructura de
  solo inserción **consistente**, no a medias.
- **No hay `memcpy` de `NodeData` ni de `RowID`.** Mismo argumento que §2 de
  `storage-tuple.md`: en x86 el test de round-trip no distinguiría las versiones, y el error
  se vería en otra máquina o en el cálculo de `t`.
- **No hay caché de nodos en memoria.** Cada `readNode` es una lectura de página del
  `PageManager` y cuenta en `pageReads()`, que es exactamente lo que el benchmark de P2 mide.
  Meter un buffer pool acá cambiaría lo que el benchmark reporta como I/Os.
