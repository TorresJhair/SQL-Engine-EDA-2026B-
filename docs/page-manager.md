# PageManager y HeapFile

## Archivo paginado

`PageManager` guarda cada archivo como páginas de tamaño fijo. La página 0 contiene los
metadatos; las páginas de datos comienzan en el ID 1. Un archivo con `page_count = P` mide
`P * page_size` bytes, por lo que el total de páginas incluye la página de metadatos.
Los offsets se calculan como `page_id * page_size`.

Los primeros 28 bytes de la página 0 se codifican campo por campo en little-endian:

| Offset | Tamaño | Campo | Quién mantiene el valor |
|---:|---:|---|---|
| 0 | 4 B | `magic` (`PAGE`) | `PageManager` |
| 4 | 4 B | `page_size` | `PageManager` |
| 8 | 4 B | `page_count`, incluida la página 0 | `PageManager` |
| 12 | 4 B | `root_page_id` | `BTree` |
| 16 | 4 B | `height` del árbol | `BTree` |
| 20 | 8 B | `record_count` del heap | `HeapFile` |

El resto de la página de metadatos se inicializa en cero. Al abrir un archivo existente,
`PageManager` comprueba el magic, el tamaño de página, la longitud del archivo y que el
`page_count` guardado concuerde con las páginas físicas. Si los metadatos no son compatibles,
`open` devuelve `Status::Corrupt`.

`allocate()` asigna IDs desde 1, extiende el archivo y actualiza `page_count`. `read` y
`write` transfieren páginas completas; una escritura válida debe tener exactamente
`page_size` bytes. `flush()` persiste la copia actual de `FileMeta`. `readMeta()` y
`writeMeta()` no representan accesos a páginas de datos y no alteran los contadores.

## Contadores de acceso

`pageReads()` y `pageWrites()` cuentan llamadas lógicas exitosas de lectura y escritura de
páginas de datos. La página 0 no se cuenta. No hay caché en `PageManager`: cada `read` vuelve
a leer la página solicitada y cada `write` escribe la página completa. Estos contadores
describen el trabajo lógico del motor, no las operaciones físicas del dispositivo: el
sistema operativo puede mantener datos en su propia caché.

Por ejemplo, dos llamadas consecutivas a `HeapFile::get` con el mismo `RowID` hacen dos
lecturas lógicas. Durante una inserción, `HeapFile` lee la página del cursor para intentar
insertar ahí y escribe una página cuando la inserción termina con éxito. Si la página ya
está llena, asigna una nueva página y avanza el cursor.

## HeapFile y Full Table Scan

`HeapFile` lleva `lastInsertPageID`, un cursor a la última página usada. Cada inserción
examina solo esa página; cuando no cabe la nueva tupla, crea otra. Así no vuelve a recorrer
las páginas anteriores para hallar espacio. Una tupla válida genera una escritura de página
por inserción. El `record_count` se conserva en memoria y se copia a `FileMeta` en
`HeapFile::flush()`.

`get(RowID)` lee una página y busca el slot solicitado. `Scan` recorre de la página 1 hasta
la última página y devuelve todos los slots vivos; ignora los slots borrados y no lee la
página 0. Para el esquema del benchmark, la página slotted reserva 8 bytes de cabecera y 4
bytes por slot; una tupla de 27 bytes permite 131 tuplas por página de 4096 B y 8 por página
de 256 B. Por eso 100 000 filas ocupan 764 páginas de datos (765 páginas físicas contando
metadatos) a 4096 B, o 12 500 páginas de datos (12 501 físicas) a 256 B. El coste del full
scan crece linealmente con esas páginas de datos.
