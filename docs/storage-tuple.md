# Storage: formato de la tupla y de la slotted page

> Dueño: **P1**. Este archivo describe el formato binario que congeló y verifica `open_v4.md`
> §1, con los bytes reales que produce el código. P2, P3 y P4 programan contra este layout:
> si el formato de la tupla o de la página cambiara a mitad del proyecto, los cuatro `.cpp`
> se romperían el mismo día.

## 1. Formato de la tupla

Una tupla es `[nFields: u8]` seguido de sus campos, cada uno en formato

```
[tag: u8][len: u16][datos]
```

todo en **little-endian**. El `tag` distingue INT (`0`) de VARCHAR (`1`); el `len` dice cuántos
bytes ocupan los `datos` (4 para un INT, la longitud del texto para un VARCHAR).

El dataset del benchmark es la tupla mínima con la que se explica toda la capacidad del heap:
2 campos, `INT key` + `VARCHAR name` de 16 B.

```
                    ┌─ nFields = 2 ─┐   ┌ INT ──────────────┐   ┌ VARCHAR de 16 ───────────────────────┐
 byte:              0                 1  2  3  4  5  6  7     8  9 10  11  12                       27 (fin)
                     │                 │  │  │  │  │  │  │     │  │ │  │    │
                     ▼                 ▼  ▼  ▼  ▼  ▼  ▼  ▼     ▼  ▼ ▼  ▼    ▼
hex de la tupla:    02│ 00 04 00 D2 04 00 00│ 01 10 00 6E 6F 6D 62 72 65 5F 30 30 30 30 30 31 32 33 34
                     │  └ tag 0 (INT) ┘  └─ 1234 = 0x4D2 ─┘  │  └┘ └──── "nombre_000001234" (16 B) ──┘
                     │                     │  │  │  │  │  │     │  └ `len = 16` (0x0010)
                     └─ 1 B, conteo de campos                └─ tag 1 (VARCHAR)
```

Leída byte a byte:

| Bytes | Contenido | Valor |
|---|---|---|
| `0` | `nFields` | `2` (caben en un `u8`: máximo 255 campos) |
| `1` | tag | `0` = INT |
| `2..3` | `len` | `4` |
| `4..7` | datos | `D2 04 00 00` = `1234` (little-endian) |
| `8` | tag | `1` = VARCHAR |
| `9..10` | `len` | `0x0010` = `16` |
| `11..26` | datos | `nombre_000001234` |

El INT serializado ocupa `1 (tag) + 2 (len) + 4 (datos) = 7 B`; el VARCHAR de 16
`1 + 2 + 16 = 19 B`; la tupla completa `1 (nFields) + 7 + 19 = 27 B`. A eso hay que sumarle
los 4 B de su entrada en el directorio de la página, que es de donde sale el `31` que usa la
fórmula de capacidad de §3.

Dos decisiones del formato que no son arbitrarias:

- **El VARCHAR no lleva terminador `\0`.** La longitud va declarada en el `len`, no marcada al
  final con un byte centinela. Un texto de 16 B ocupa exactamente 16 B en la tupla, y ese
  byte de más o de menos es lo que hace que 131 tuplas entren o no en la página de 4096.
- **`nFields = 0` existe en memoria pero no llega a disco.** `Tuple::serializeTo` con cero
  campos devuelve `Status::PreconditionFailed` y no toca el buffer: una tupla vacía mediría
  `1 B`, y el puntero de 2 B de la cadena de libres no entraría en la región liberada. Es una
  precondición comprobable, no un `assert`, justamente porque así un test puede verificarla
  sin `fork` y sin depender de `-DNDEBUG`.

## 2. Por qué los bytes se escriben a mano y no con `memcpy`

`Value::serializeTo`, `Tuple::serializeTo` y `SlottedPage` escriben y leen los bytes campo por
campo, en little-endian, **nunca** con `memcpy` del struct. La razón es que en x86 la
comparación de bytes del test **no distinguiría** las dos versiones, y en una máquina
big-endian el resultado sí cambia:

- En x86 (little-endian), `memcpy(&int_, p, 4)` produce exactamente los mismos bytes que la
  escritura manual. Un test de round-trip pasaría igual, y hasta una comparación binaria de la
  tupla pasaría. El error no se vería **acá**: se vería meses después, cuando P2, P3 o P4
  leyeran el archivo.
- En una máquina big-endian, `memcpy` escribiría `[D2 04 00 00]` como `[00 00 04 D2]`, que es
  el archivo mal, y **ningún test local lo detectaría** porque los tests corren en la misma
  máquina que escribe.

El costo es real y se asume: tres funciones con el loop de bytes a mano en vez de una sola
utility, y los helpers `putU16LE`/`putU32LE` se repiten en `Value.cpp` y `SlottedPage.cpp`.
La alternativa es un header de helpers compartido, y **no existe** a propósito: `open_v4.md` §4
congela los 13 headers de contrato, y agregar uno más sería cambiarlo. En el PR quedó escrito
que la repetición es el precio de no tocar el contrato congelado.

## 3. Layout de la slotted page

```
 bytes 0..7 cabecera                     directorio (4 B por slot)               datos
┌──────────────────────────────┐  ┌────────────────────────────┐      ┌───────────────────┐
│ nSlots  dataStart  dataEnd    │  │ [offset][length]           │      │ ...  tupla_2  tupla_1 │
│ freeHead                     │  │ [offset][length]           │      │                     │
└──────────────────────────────┘  └────────────────────────────┘      └───────────────────┘
 8 B fijos                          crece hacia la derecha               crecen hacia atrás
                                 (cada slot ocupa 4 B)              desde el final de la página
```

La página se divide en tres zonas con una invariante simple: **el directorio crece hacia
adelante y los datos crecen hacia atrás desde el final**; se cruzan solo si la página está
llena.

- **Cabecera, 8 B:** `nSlots` (`u16`), `dataStart` (`u16`), `dataEnd` (`u16`), `freeHead`
  (`u16`). `dataStart` es el fin del directorio, `dataEnd` la dirección más baja ocupada por
  una tupla. Es exactamente el layout que el README promete: **8 B fijos**, no es un pointer
  gigante en una cabecera elástica.
- **Directorio, 4 B por slot:** `[offset: u16][length: u16]`. Un slot se identifica por su
  número, que es simplemente su posición en el directorio (`slotID`), así que `nSlots` es el
  número de entradas y `dataStart = 8 + 4·nSlots`. La entrada solo se mueve si cambia
  `insert`/`erase`.
- **Datos:** cada tupla ocupa `[offset, offset + length)` en los bytes de la página, contando
  desde el **final** hacia atrás.

Las tres zonas no se solapan mientras haya espacio: `dataEnd - dataStart` es el hueco libre
contiguo, y por eso la capacidad de una página vacía de 4096 B es `4096 - 8 = 4088` (el `+4`
de la entrada del directorio de la primera tupla la baja a 4084, ver §4).

### La cadena de slots libres

Un slot liberado **se enlaza en una cadena**, no se marca con un flag. La cabecera no tiene un
bit libre: el layout congelado no lo reservó, así que la *freesness* se define por
pertenencia a la cadena. El encadenado usa los 2 primeros bytes de la propia región liberada
para guardar el `slotID` del siguiente slot libre, y `0xFFFF` marca el final:

```
freeHead ──► [slot 3 libre]  ──►  [slot 1 libre]  ──►   0xFFFF
               ^ puntero: los 2 primeros bytes
                 de la región que antes era la tupla 3
```

El `length` de un slot libre **se conserva en el directorio** tal cual, y es lo que hace
posible el `freeSpace` y el `first-fit`:

- `freeSpace()` = `(dataEnd - dataStart)` + suma de los `length` de los libres: el hueco
  contiguo más lo que quedó atrapado entre tuplas vivas.
- `insert` hace first-fit: recorre la cadena y usa la primera región libre que cumpla
  `length >= tupla`. Acá no hay `+4`: el costo de la entrada de directorio se pagó cuando se
  liberó la región, así que solo importa que la tupla entre. El `+4` sí reaparece al escribir
  al final, donde la entrada nueva empuja `dataStart` 4 B arriba.

El costo es explícito: `lookup`, `erase` y `freeSlotCount` tienen que recorrer la cadena para
saber si un slot está vivo. En el caso límite (página llena de tuplas de 4 B, 1 018 slots en
4096) la cadena entera se recorre en cada operación. Es O(k) igual que el first-fit del
`insert`, así que no cambia la complejidad del módulo, pero está anotado como el peor caso.

## 4. Por qué `erase` no compacta, y por qué no baja el `length`

`erase` enlaza la región liberada en la cadena y **no mueve un byte de datos**. Los `RowID` de
los demás slots no cambian: un `RowID` es `{pageID, slotID}`, y como los slots no se
reordenan ni los datos se comprimen, los `RowID` emitidos a P2, P3 y P4 siguen siendo válidos
después de borrar cualquier tupla. Esa es la propiedad que hace invisible el borrado para el
resto del sistema — el índice apunta a `RowID`s que no se invalidan.

La consecuencia que parece un bug y no lo es: **al reutilizar, el `length` del directorio no
baja**. Cuando se inserta en una región libre, el slot conserva `offset` y `length` de la
región *completa*, que puede ser más grande que la tupla nueva. El resto son los bytes de la
tupla vieja, y el que lee (P2, P3, P4 vía `Tuple::deserialize`) **los ignora al final**. Si
`deserialize` devolviera `Corrupt` por los bytes sobrantes, reutilizar una región de 27 B con
una tupla de 4 B daría error sobre datos válidos.

El `27 → 54 → 27` de los tests es exactamente eso: con 131 tuplas de 27 B en 4096,
`freeSpace()` da 27, se borra una (libera 27) y `freeSpace` pasa a `54`; al reutilizar la
región con la misma tupla de 27 B, vuelve a `27`. El `+4` del directorio aparece al medir si
entra la 132: `freeSpace` dice 27, pero la 132 necesita `27 + 4` de la entrada nueva, y no
entra.

## 5. `RowID`: 6 B en disco, 8 B en memoria

```cpp
static_assert(sizeof(RowID) == 8);                    // en memoria, por el padding
static_assert(sizeof(PageID) + sizeof(SlotID) == 6);  // en la página, que es lo que cuenta
```

`RowID` son `{PageID pageID; SlotID slotID}` con `PageID = uint32_t` y `SlotID = uint16_t`.
El struct ocupa **8 B en memoria** porque el compilador alinea `slotID` y deja 2 B de padding;
en la página son **6 B**, sin alinear: 4 del `pageID` más 2 del `slotID` lado a lado. El
serializador escribe los 6 bytes a mano y nunca `memcpy`ea el struct, por lo mismo que §2. Si
alguien usara `sizeof(RowID)` donde la página pide 6, `t` saldría mal y el árbol se corrompería
en silencio (el README ya lo advierte junto a su fórmula de `t`).

### De dónde sale la capacidad del heap

Cada tupla del dataset ocupa `27 B` y su entrada de directorio `4 B`, o sea **31 B por slot**.
Con la cabecera de `8 B`:

```
capacidad = (page_size - 8) / 31
     4096 → (4096 - 8) / 31 = 131 tuplas
      256 → (  256 - 8) / 31 =   8 tuplas
```

Son números **derivados de la fórmula, no medidos**: entrar o no la 132 (o la 9) es una
consecuencia del formato, y los tests la comprueban. Se eligió el VARCHAR de **largo fijo de
16 B** a propósito para que la capacidad sea reproducible y explicable en la presentación —
con VARCHAR variable dependería de los datos y habría que medirlo en vez de derivarlo.