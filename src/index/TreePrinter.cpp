// src/index/TreePrinter.cpp                   [P1]
// printTree: arbol en ASCII con conexiones ├──/└──/│ (DFS pre-orden).
// exportDot: Graphviz con tantos nodos como paginas alcanzables.

#include "index/TreePrinter.h"

#include "index/BTree.h"
#include "index/BTreeNode.h"

#include "common/Types.h"

#include <sstream>
#include <string>

namespace {

// El formato de una linea que describe un nodo. printTree y exportDot necesitan la misma
// mirada del arbol (mismo orden, mismas claves), asi que esta funcion la comparten y la
// seccionizacion queda en un solo lugar.
std::string nodeLine(const BTreeNode& node, PageID page_id) {
    std::ostringstream line;
    line << (node.isLeaf() ? "hoja   " : "interno");
    if (node.keyCount() == 0) {
        line << " p" << page_id << " <vacia>";
    } else {
        line << " p" << page_id << " [";
        for (uint16_t i = 0; i < node.keyCount(); ++i) {
            if (i != 0) line << " ";
            line << node.keyAt(i);
        }
        line << "]";
    }
    return line.str();
}

// DFS pre-orden. Cada nivel aporta: un hijo no-ultimo se dibuja con ├── y la continuacion
// del arbol bajo el se marca con │; el ultimo se dibuja con └── y su arbol con espacios
// (las ramas ya no continuan). Devuelve cuantas paginas alcanzo, para exportDot.
size_t drawSubtree(const BTree& tree, PageID page_id,
                   const std::string& prefix, bool is_last, std::ostream& out) {
    const BTreeNode node = tree.readNode(page_id);

    const bool leaf = node.isLeaf();
    out << prefix << (is_last ? "└── " : "├── ") << nodeLine(node, page_id);
    if (leaf && node.nextLeaf() != 0) {
        out << "  -> hoja p" << node.nextLeaf();
    }
    out << "\n";

    if (leaf) return 1;

    const std::string continuation =
        prefix + (is_last ? "    " : "│   ");
    size_t reached = 1;
    const uint16_t children = node.keyCount() + 1;
    for (uint16_t i = 0; i < children; ++i) {
        reached += drawSubtree(tree, node.childAt(i), continuation,
                               (i + 1 == children), out);
    }
    return reached;
}

}  // namespace

void printTree(const BTree& tree, std::ostream& out) {
    // open() garantiza "nunca 0 nodos": la raiz siempre es una pagina != 0. El chequeo es
    // defensivo contra un BTree sin open(), donde rootPageID()==0 seria "sin arbol".
    const PageID root = tree.rootPageID();
    if (root == PageID(0)) {
        out << "(sin raiz: llama a BTree::open antes)\n";
        return;
    }

    const BTreeNode root_node = tree.readNode(root);
    out << nodeLine(root_node, root);
    if (root_node.isLeaf() && root_node.nextLeaf() != 0) {
        out << "  -> hoja p" << root_node.nextLeaf();
    }
    out << "\n";

    if (root_node.isLeaf()) return;

    const uint16_t children = root_node.keyCount() + 1;
    for (uint16_t i = 0; i < children; ++i) {
        drawSubtree(tree, root_node.childAt(i), "", (i + 1 == children), out);
    }
}
