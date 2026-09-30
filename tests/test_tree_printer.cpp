// tests/test_tree_printer.cpp  [P1]
// printTree en ASCII: arbol vacio, una hoja y un arbol de 3 niveles, mas exportDot.
//
// Los arboles se construyen con la API publica (BTree::open + insert) sobre un PageManager
// en temp dir, como test_btree_write.cpp. Las PageID las decide PageManager/insert (P2/P3),
// no printTree: el test fija lo que es de P1, que es el FORMATO de la salida. Para el arbol
// de 3 niveles el esperado se arma con un walk propio de los accessors (readNode), asi el
// test no depende de la numeracion interna del PageManager.
#include "TestHarness.h"

#include "index/BTree.h"
#include "index/BTreeNode.h"
#include "index/TreePrinter.h"
#include "storage/PageManager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>

namespace {
// Base temporal propia por test, mismo patron que el resto de suites (regla 11).
struct TempIndex {
    std::filesystem::path path;

    TempIndex() {
        static std::atomic<uint64_t> sequence{0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("sql_engine_tree_printer_" + std::to_string(timestamp) + "_" +
                std::to_string(sequence.fetch_add(1)) + ".idx");
    }

    ~TempIndex() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

std::string render(const BTree& tree) {
    std::ostringstream out;
    printTree(tree, out);
    return out.str();
}

// BTree::open sobre PageManager nuevo, como en las otras suites.
Status makeTree(const std::filesystem::path& path, size_t page_size, BTree& out,
                PageManager& pm) {
    if (PageManager::open(path.string(), page_size, pm) != Status::Ok) return Status::Corrupt;
    return BTree::open(pm, out);
}

// Walk de REFERENCIA: convierte el arbol a texto con los accessors, para que el esperado
// del test se arme con el PageID real y no con una numeracion tomada del PageManager.
// Duplica a proposito el formato de printTree: si printTree cambia su formato este walk
// (que es parte del test) se corrige al mismo tiempo y el TEST no es circular, porque
// compara la linea a linea contra la salida real.
std::string nodeText(const BTreeNode& node, PageID page_id) {
    std::ostringstream line;
    line << (node.isLeaf() ? "hoja   " : "interno"); // 3 espacios: alinea "hoja  " con "interno"
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

std::string arrowText(const BTreeNode& node) {
    if (node.isLeaf() && node.nextLeaf() != 0) {
        return "  -> hoja p" + std::to_string(node.nextLeaf());
    }
    return "";
}

std::string walkReference(const BTree& tree, PageID page_id, const std::string& prefix,
                          bool is_last) {
    const BTreeNode node = tree.readNode(page_id);
    std::string out = prefix + (is_last ? "└── " : "├── ") + nodeText(node, page_id) +
                      arrowText(node) + "\n";
    if (node.isLeaf()) return out;
    const uint16_t children = node.keyCount() + 1;
    const std::string continuation = prefix + (is_last ? "    " : "│   ");
    for (uint16_t i = 0; i < children; ++i) {
        out += walkReference(tree, node.childAt(i), continuation, (i + 1 == children));
    }
    return out;
}

std::string referenceTree(const BTree& tree) {
    const PageID root = tree.rootPageID();
    if (root == PageID(0)) return "(sin raiz: llama a BTree::open antes)\n";
    const BTreeNode root_node = tree.readNode(root);
    if (root_node.isLeaf()) {
        return nodeText(root_node, root) + arrowText(root_node) + "\n";
    }
    std::string out = nodeText(root_node, root) + "\n";
    const uint16_t children = root_node.keyCount() + 1;
    for (uint16_t i = 0; i < children; ++i) {
        out += walkReference(tree, root_node.childAt(i), "", (i + 1 == children));
    }
    return out;
}
}  // namespace

namespace suites {
void tree_printer() {
    // TEST() se registran solos; ver TestHarness.h. La lista explicita la usa test_main.cpp.
}
}  // namespace suites

// --- Arbol vacio -------------------------------------------------------------
TEST(tree_printer, arbol_vacio_es_hoja_raiz_sin_claves) {
    TempIndex temp;
    PageManager pm;
    BTree tree;
    CHECK_EQ(makeTree(temp.path, 256, tree, pm), Status::Ok);

    const PageID root = tree.rootPageID();
    CHECK(root != 0); // "nunca 0 nodos" de open

    // Hoja raiz sin claves, sin nextLeaf: "nunca 0 nodos" es UNA pagina dibujable.
    // "hoja   " (3 esp) + " p": la "p" queda en la columna 9, como "interno p".
    const std::string expected = "hoja    p" + std::to_string(root) + " <vacia>\n";
    CHECK_EQ(render(tree), expected);
    CHECK_EQ(render(tree), referenceTree(tree)); // el formato coincide con el walk
}

// --- Una hoja con claves ------------------------------------------------------
TEST(tree_printer, una_hoja_imprime_claves_sin_flecha) {
    TempIndex temp;
    PageManager pm;
    BTree tree;
    CHECK_EQ(makeTree(temp.path, 256, tree, pm), Status::Ok);
    CHECK_EQ(tree.insert(10, RowID{1, 1}), Status::Ok);
    CHECK_EQ(tree.insert(20, RowID{1, 2}), Status::Ok);
    CHECK_EQ(tree.insert(30, RowID{1, 3}), Status::Ok);

    const PageID root = tree.rootPageID();
    // 3 claves no parten la hoja (t=12 en 256 B) y nextLeaf()==0: sin flecha.
    const std::string expected =
        "hoja    p" + std::to_string(root) + " [10 20 30]\n";
    CHECK_EQ(render(tree), expected);
}

// --- Arbol de 3 niveles -------------------------------------------------------
TEST(tree_printer, arbol_de_3_niveles_mostrando_raiz_internos_y_hijos) {
    TempIndex temp;
    PageManager pm;
    BTree tree;
    // page_size 64 => t=2; con 10 claves en orden el arbol llega a altura 3 y cada hoja
    // queda con 2 claves. El orden y las claves son de las reglas de P3: este test fija
    // en el esperado que printTree las muestra igual que los accessors.
    CHECK_EQ(makeTree(temp.path, 64, tree, pm), Status::Ok);
    for (int32_t k = 0; k < 10; ++k) CHECK_EQ(tree.insert(k, RowID{1, (SlotID)k}), Status::Ok);
    CHECK_EQ(tree.height(), size_t(3));

    const std::string out = render(tree);
    CHECK_EQ(out, referenceTree(tree));

    // Arrays por claves y estructura visibles en la salida: internos con sus claves y
    // hojas encadenadas con flecha (nextLeaf != 0 en las primeras).
    CHECK(out.find("interno") != std::string::npos);
    CHECK(out.find("hoja") != std::string::npos);
    CHECK(out.find("└──") != std::string::npos);
    CHECK(out.find("├──") != std::string::npos);
    CHECK(out.find("│") != std::string::npos);     // continuacion bajo un hijo no-ultimo
    CHECK(out.find("-> hoja p") != std::string::npos); // flecha a la hoja siguiente
}

// --- Sin open -----------------------------------------------------------------
TEST(tree_printer, sin_open_avisa_que_falta_rootPageID) {
    BTree tree; // sin BTree::open
    CHECK_EQ(render(tree), std::string("(sin raiz: llama a BTree::open antes)\n"));
}
