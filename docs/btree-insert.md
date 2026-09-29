# Índice: inserción, propagación del split y logs

> Dueño: **P3**. Cómo entra una clave, cómo sube el split hasta la raíz, cuándo crece la
> altura, qué imprimen los logs y por qué las claves duplicadas no se pierden. El formato en
> bytes del nodo está en [`btree-node.md`](btree-node.md); este archivo es el lado del
> algoritmo.
>
> Todos los ejemplos usan `--page-size 256`, donde **`t = 12`** y `2t − 1 = 23` claves por
> nodo: es el tamaño en el que los splits se ven uno a uno.

## 1. Qué hace `insert` en una frase

Busca la hoja, mete la clave **en memoria**, y solo si no cabe en `2t − 1` parte el nodo
**antes** de tocar el disco y sube la clave promovida por una pila de ancestros hasta que
algún nodo quepa.

```
insert(key, rowid)
  │
  ├─ findLeaf(key, Bias::Right, &ancestores)      baja a la hoja, apila la ruta
  │
  ├─ leaf.insertInNode(key, rowid)                en memoria: entra hasta 2t claves
  │
  ├─ keyCount <= 2t-1 ?  ── sí ──► writeNode(leaf)             y termina (lo común)
  │
  └─ no: hoja con 2t claves, hay que partir
       split(allocate()) ──► write izq, write der ──► [SPLIT]
       └─ sube por ancestros (de abajo hacia arriba):
            padre.insertInNode(promovida, hijo_der)
            ├─ cabe  ──► writeNode(padre)         y termina
            └─ no     ──► split del padre ──► [SPLIT] ──► sigue subiendo
          si se llega al tope de la pila:
            raiz nueva ──► writeMeta(root, height+1) ──► [ALTURA]
```

Dos decisiones que no son arbitrarias:

- **El split pasa antes de la escritura.** `insertInNode` deja entrar hasta `2t` claves a
  propósito (el split necesita verlas todas juntas), y `serialize` devuelve
  `Status::NodeOverflow` si se le escapa una página con más de `2t − 1`. Como no se escribe
  nada desbordado, el error se detecta igual en `Release` con `-DNDEBUG`: no es un `assert`,
  es un `Status`.
- **La propagación es un bucle sobre una pila, no recursión.** La pila mide lo mismo que la
  altura del árbol, que es `O(log n)`; una recursión de `O(log n)` niveles habría sido
  exactamente lo mismo, pero el bucle deja el `Status` de cada escritura en un solo sitio.

`insert` devuelve `Status`: `Ok`, o el `Status` que devuelva `serialize`/`writeNode`. Devuelve
`PreconditionFailed` si se le pasa un árbol sin `open()`. **Nunca aborta.**

## 2. Propagación y crecimiento de altura, con las páginas del test

Con `page_size = 256`, la página 0 es `FileMeta`, la 1 se crea en `open()` como hoja raíz:

```
inserciones 1..23                     la 24 (t = 12, entra la 2t)
─────────────────────                 ─────────────────────────────────

   [ hoja raíz = página 1 ]                 [ interno = página 3 ]
     1 2 3 ... 23                             clave=13
                                             /        \
                                  [ página 1 ]        [ página 2 ]
                                    1..12               13..24
                                   next ──────────────► next = 0
```

```
[SPLIT]  hoja    page=1 -> izq=1 der=2  clave=13
[ALTURA] 1 -> 2  raiz=page 3
```

La hoja original **no se mueve**: se queda en la página 1 como mitad izquierda, y lo que
nace es la página 2 (derecha) y la página 3 (raíz nueva). `FileMeta` queda con
`root_page_id = 3` y `height = 2`, y eso es lo que hace que un `open()` posterior reabra el
mismo árbol en vez de crear otro.

El patrón se repite en cada nivel, y la altura solo crece en un punto: cuando la subida del
split llega a la raíz.

```
                    [ raíz nueva ]
                   /              \                    un solo insert puede partir
        [ interno lleno ]     [ hoja derecha ]         hoja + interno + crear raíz,
              /      \                                 es decir: 3 [SPLIT]/[ALTURA]
         [ hoja ]   [ hoja ]                           en una sola llamada a insert
```

La cascada completa se comprueba en `test_btree_write` detectando que **una inserción
allocó 2 páginas o más con `height >= 2`**: con la altura ya en 2, un `delta >= 2` de
`pageCount` solo puede venir de un split interno (1 página de la hoja + 1 del interno +
1 de la raíz nueva), así que la propagación se mide sin leer el log.

## 3. Formato exacto de los logs

Tres líneas, dos funciones, **un solo lugar** donde se escriben:

```
[SPLIT]  hoja    page=1 -> izq=1 der=2  clave=13
[SPLIT]  interno page=90 -> izq=90 der=91  clave=17800
[ALTURA] 1 -> 2  raiz=page 3
```

| Parte | Ejemplo | De dónde sale |
|---|---|---|
| `[SPLIT]` + **2 espacios** | `[SPLIT]  ` | fijo |
| tipo, alineado a la izquierda en **7 caracteres** | `hoja   ` / `interno` | `left.isLeaf()`; `hoja` ocupa 4 y se rellena con 3 espacios, más 1 espacio fijo → por eso hay **4** espacios entre `hoja` y `page=` |
| `page=` | `page=1` | la página que se partió (`left.self`) |
| ` -> ` `izq=` | `-> izq=1` | la mitad izquierda, que **se queda en la misma página** |
| `der=` | `der=2` | `right.self`, la página recién alocada |
| **2 espacios** `clave=` | `  clave=13` | la clave promovida (copia en hoja, subida en interno) |
| `[ALTURA]` + espacio | `[ALTURA] ` | fijo |
| `vieja -> nueva` | `1 -> 2` | `meta.height` antes y después |
| **2 espacios** `raiz=page ` | `  raiz=page 3` | `meta.root_page_id` nuevo |

Reglas del formato, que son contrato:

- **`logSplit` y `logHeight` son los únicos puntos del proyecto** donde se imprime `[SPLIT]`
  o `[ALTURA]`. Ningún otro `.cpp` imprime esas cadenas: si cambia el formato, cambia en
  `BTreeWrite.cpp` y acá, en ese orden.
- **`bulkLoad` (P4) llama a `logHeight`, no reimplementa el formato.** Así el log de la
  carga masiva tiene la misma cara que el de la inserción.
- **Sale por `std::cout` con `endl`**, no con `"\n"`: la demo opción 5 tiene que ver el
  split en el momento en que ocurre, y `endl` hace el flush.
- **Solo con `setVerbose(true)`.** El default es apagado: un árbol silencioso. Con `verbose`
  apagado no se imprime **ni un byte** aunque haya splits, y eso lo comprueba un test
  capturando `std::cout`.

## 4. Regla de duplicados

La regla completa es de `findLeaf` + `insertInNode`, y es la que hace que `search` pueda
devolver **todas** las copias de una clave:

**a) Bajar: el `Bias` decide hacia qué hijo se va.**

| | Función | Hijo elegido | Para qué |
|---|---|---|---|
| `Bias::Left` | `searchInNode` = `lowerBound` | primera separadora `>= key` → hijo izquierdo | llegar a la hoja **más a la izquierda** que puede tener la clave |
| `Bias::Right` | `upperBound` | última separadora `<= key` → hijo derecho | llegar a la hoja **más a la derecha** que puede tenerla |

`insert` usa `Bias::Right`; `search` (P4) usará `Bias::Left`.

**b) Dentro de la hoja: `insertInNode` inserta tras las claves iguales.** Las copias se
acumulan en orden de inserción y no se mezclan con la clave que sigue.

**c) El separador de una hoja es una COPIA de la primera clave del hijo derecho.** El hijo
izquierdo **puede** tener claves iguales al separador. La invariante de separadores es, por
eso, con desigualdades débiles en ambos lados:

```
todo lo que cuelga del hijo izquierdo  <=  separador  <=  todo lo que cuelga del hijo derecho
                     (puede haber iguales a la izquierda)        (siempre hay iguales a la derecha)
```

**Consecuencia para `search`:** bajar con `lowerBound` llega a la primera hoja que puede
tener la clave, y desde ahí basta con seguir `nextLeaf` mientras la clave siga siendo igual,
sumando los `RowID` de cada hoja. Si `insert` bajara con `lowerBound`, las copias nuevas
quedarían en las hojas **izquierdas** y esa misma búsqueda se encontraría solo las de la
derecha: la política de `insert` existe justamente para que la de `search` funcione.

**Traza de las 40 duplicadas del test** (misma clave, `page_size = 256`, `t = 12`):

```
copias  1..23   caben en la hoja raíz (2t−1 = 23)
copia    24     hoja llena → split: izq = 12 copias | der = 12 copias, promovida = la clave
copia    25     upperBound en la raíz (la separadora es igual a la clave) → hijo derecho
copia    36     esa hoja llegó a 23 → split de nuevo, la raíz toma un 2.º separador igual
copia    40     3 hojas: 12 + 12 + 16 copias, todas visibles por la cadena nextLeaf
```

El test comprueba las 40 por la cadena de hojas (API de P3). El `CHECK` sobre
`BTree::search(k)` devuelve las 40 se agrega cuando P4 entregue `search` (Issue de la
costura P3/P4), igual que `checkInvariants` para cerrar el test de estrés de 5 000.

## 5. Por qué `bulkLoad` (P4) cumple el mínimo de ocupación

`bulkLoad` no llama a `split`: reparte las `n` claves **uniformemente** de abajo hacia
arriba. Ese reparto garantiza el mínimo de `t − 1` claves en todo nodo que no sea la raíz, y
la cuenta es aritmética pura.

**Hojas.** Con capacidad `2t − 1` por hoja, el número de hojas es
`L = ⌈n / (2t − 1)⌉`. Escribamos `n = q(2t − 1) + r` con `0 <= r < 2t − 1`:

- `r = 0` → `L = q` y todas las hojas quedan con exactamente `2t − 1` claves.
- `r > 0` → `L = q + 1` y las hojas quedan con `⌊n/L⌋` o `⌈n/L⌉`. Para `q >= 1`:

```
⌊n / (q+1)⌋ = ⌊ (q(2t−1) + r) / (q+1) ⌋
            = (2t−1) − ⌈ ((2t−1) − r) / (q+1) ⌉
            >= (2t−1) − ⌈ (2t−2) / 2 ⌉        (r >= 1 y q+1 >= 2)
            = (2t−1) − (t−1) = t   >=  t − 1   ∎
```

- `q = 0` (es decir `n < 2t − 1`) → `L = 1`: hay **una sola hoja, que es la raíz**, y la
  raíz está exenta del mínimo (una raíz hoja puede tener 0 claves).

**Internos.** El argumento es idéntico en cada nivel superior, con capacidad `2t` hijos: si
el nivel de abajo tiene `C` nodos, el siguiente tiene `⌈C / 2t⌉` padres y el reparto uniforme
da `⌊C / ⌈C/2t⌉⌋ >= t` hijos por padre —es decir `>= t − 1` claves separadoras— por la misma
identidad de arriba con `2t` en lugar de `2t − 1`. (Cuando `C <= 2t` hay un solo padre, que
es la raíz, y la raíz está exenta del mínimo.)

**Números concretos** (`t = 12`, mínimo `t − 1 = 11`), que son los que P4 testea:

| `n` | hojas `⌈n/23⌉` | claves por hoja | ¿>= 11? |
|---|---|---|---|
| 23 | 1 | 23 (raíz única) | exenta |
| 24 | 2 | 12, 12 | sí |
| 47 | 3 | 16, 16, 15 | sí |
| 553 | 25 | 22 u 23 | sí |

El peor caso no es `n` grande sino `n = k(2t−1) + 1`, justo después de llenar `k` hojas
completas: ahí `L = k+1` y el reparto da `t` claves por hoja, que sigue siendo `>= t − 1`.

**La precondición no es decorativa:** `bulkLoad` devuelve `PreconditionFailed` (no un
`assert`) si la entrada viene desordenada o el árbol no está vacío, y **no modifica el
árbol** —`size()` no cambia y `checkInvariants` sigue pasando—. Un bulk load que aceptara
entradas desordenadas produciría un árbol con separadores inválidos que ni `search` ni el
verificador podrían detectar de forma local.

## 6. Dónde se guarda la raíz, y qué no hace este árbol

- **La raíz vive en la página 0, en `FileMeta`**: `root_page_id` y `height`. `PageManager`
  la mantiene cacheada, así que `rootPageID()` y `height()` no cuestan una lectura de página
  ni mueven los contadores del benchmark. `open()` la lee; `insert` solo la reescribe cuando
  la raíz cambia (una vez por cada crecimiento de altura, no por cada split).
- **`open()` nunca deja 0 nodos.** Si `root_page_id == 0`, crea una hoja raíz con 0 claves:
  un árbol de 0 páginas no tiene hoja a donde bajar y `height()` devolvería 0.
- **La altura cuenta niveles, con la hoja raíz en 1.** Un árbol recién creado mide 1.
- **No hay `BTree::erase`** y no se planea: el borrado entra a medias en el proyecto
  (`SlottedPage::erase` sí existe y no compacta) y este archivo documenta un índice de solo
  inserción, que es lo que la rúbrica de Fase 1 evalúa.
