# Index: salida ASCII (`printTree`) y Graphviz (`exportDot`)

> Dueño: **P1**. Este archivo documenta el formato de las dos salidas que produce
> `TreePrinter` y la forma de verlas, como pide `open_v4.md` §2 y la tarjeta de P1.
> `printTree` es el formato obligatorio de la demo (visibilidad en consola); `exportDot` es
> prescindible (#1 del orden de recorte), un extra con Graphviz.

## 1. Salida ASCII: `printTree(const BTree&, std::ostream&)`

`printTree` recorre el árbol por la **API pública** (`rootPageID`, `readNode`, y los
accesores de `BTreeNode`: `isLeaf`, `keyCount`, `keyAt`, `childAt`, `nextLeaf`) — nunca
toca los internos del árbol. Dibuja el árbol en **pre-orden** con conexiones estilo `tree`:

```
interno p8 [6]
├── interno p3 [2 4]
│   ├── hoja    p1 [0 1]  -> hoja p2
│   ├── hoja    p2 [2 3]  -> hoja p4
│   └── hoja    p4 [4 5]  -> hoja p5
└── interno p7 [8]
    ├── hoja    p5 [6 7]  -> hoja p6
    └── hoja    p6 [8 9]
```

Reglas del formato:

- **Un nodo por línea**; el orden es el pre-orden: primero el nodo, luego cada subárbol hijo.
- **`├── `** precede a un hijo **no-último**; **`└── `** precede al **último** hijo de un nodo.
- La **continuación** del árbol bajo un hijo no-último se marca con **`│   `**; bajo el
  último hijo, con **espacios** (las ramas ya no continúan).
- Una **hoja** se rotula `hoja` (con 3 espacios de relleno, para que la `p` de la página
  quede en la misma columna que en `interno`); un **interno**, `interno`.
- Las **claves** van entre corchetes separadas por un espacio: `[10 20 30]`. Un nodo sin
  claves (la hoja raíz vacía de `open`) se imprime `<vacia>`.
- En las **hojas**, si `nextLeaf != 0` se muestra la flecha `-> hoja pN` al final de la
  línea: es la cadena de hojas del B+tree.
- **Árbol vacío:** la tarjeta exige "nunca 0 nodos"; `open` garantiza una hoja raíz, así que
  el árbol vacío se imprime como un solo nodo `hoja   pN <vacia>`. Solo si llamás a
  `printTree` sin `open` (raíz inexistente) imprime `(sin raiz: llama a BTree::open antes)`.

### Cómo verlo

La demo de P4 escribe esta salida en consola pasándole el árbol al `std::ostream` que
elija. Para reproducirlo solo (el árbol que sigue es un `page_size = 64`, 10 claves
`0..9`):

```
./sql_engine --demo 4          # menú de P4
```

o, desde un test:

```cpp
std::ostringstream out;
printTree(tree, out);
std::cout << out.str();
```

## 2. Graphviz: `exportDot(const BTree&, std::ostream&)`

`exportDot` comparte el mismo recorrido y la **misma mirada del árbol** que `printTree`
(mismas claves, mismo orden), pero emite un grafo **dirigido** en formato [DOT] con **tantos
nodos como páginas alcanzables** desde la raíz:

```
digraph BTree {
  p8 [label="interno: 6", shape=ellipse];
  p8 -> p3;
  p3 [label="interno: 2 4", shape=ellipse];
  p3 -> p1;
  p1 [label="hoja: 0 1", shape=box];
  p1 -> p2 [style=dotted];
  ...
}
```

Reglas del formato:

- **`digraph BTree { ... }`** es el envoltorio; cada página alcanzable tiene **un vértice**
  `p<PageID>` con `label="tipo: claves"` (mismas claves que en ASCII, sin el texto
  `-> pN`) y `shape`: `box` para hojas, `ellipse` para internos.
- Las **aristas** `pX -> pY` unen un interno con cada uno de sus hijos.
- El **encadenado de hojas** se dibuja como arista punteada `[style=dotted]` entre una hoja
  y su `nextLeaf`.

### Cómo renderizarlo

Necesitás Graphviz ([`dot`](https://graphviz.org/download/)). Con el `.dot` en un archivo:

```
dot -Tpng arbol.dot -o arbol.png      # PNG
dot -Tsvg arbol.dot -o arbol.svg      # SVG (se pega bien en docs/Markdown)
dot -Tpdf arbol.dot -o arbol.pdf      # PDF
```

Para producirlo en el programa:

```cpp
std::ofstream dot("arbol.dot");
exportDot(tree, dot);
dot.close();
// luego:  dot -Tsvg arbol.dot -o arbol.svg
```

> La demo obligatoria usa el ASCII (visible en consola sin dependencias); el `.dot` es un
> extra y no se instala Graphviz como requisito del proyecto.

[DOT]: https://graphviz.org/doc/info/lang.html