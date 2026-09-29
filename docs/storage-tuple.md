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