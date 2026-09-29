// tests/test_btree_write.cpp    [P3]
// open, findLeaf, insert, propagacion de Status, splits y el log bajo verbose.
// Esta suite esta repartida por fases en la rama btree-node: cada commit agrega los
// casos de la fase que implementa. Lo que hoy esta en verde es lo que se puede probar
// sin el resto del arbol:
//
//   fase 1 (open/lectura)   -> abajo
//   fase 2 (insert)         -> pendiente en este commit
//   fase 3 (split/log)      -> pendiente en este commit
//
// Lo que NO esta aqui y depende de P4: checkInvariants (tests/TreeInvariants.h) para el
// test de estres, y BTree::search para comprobar las 40 duplicadas "por la API publica".
// Hay un Issue abierto por eso; mientras tanto las duplicadas y el estres se comprueban
// recorriendo la cadena de hojas con readNode(), que es API de P3.

#include "TestHarness.h"
#include "index/BTree.h"
#include "storage/PageManager.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
// Base temporal propia por test, con el mismo patron que test_page_manager.cpp.
// Nunca data.db ni index.db del repo.
struct TempIndex {
    std::filesystem::path path;

    TempIndex() {
        static std::atomic<uint64_t> sequence{0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("sql_engine_btree_write_" + std::to_string(timestamp) + "_" +
                std::to_string(sequence.fetch_add(1)) + ".idx");
    }

    ~TempIndex() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
} // namespace

namespace suites {
void btree_write() {
    // Los TEST() se registran solos por inicializacion estatica (ver TestHarness.h);
    // esta funcion solo existe para que test_main.cpp tenga la lista explicita.
}
} // namespace suites

// 1. open crea una hoja raiz con 0 claves (nunca 0 nodos), y un segundo open del MISMO
//    archivo reutiliza esa raiz en vez de crear otra.
TEST(btree_write, open_crea_hoja_raiz_vacia_y_no_duplica_la_raiz) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    CHECK_EQ(pm.pageCount(), uint32_t(1)); // solo la pagina de metadatos

    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    CHECK_EQ(tree.height(), size_t(1));
    CHECK_EQ(tree.pages().pageSize(), size_t(256));

    const PageID root = tree.rootPageID();
    CHECK(root != 0); // "nunca 0 nodos": 0 significaria que no hay hoja a donde bajar

    const BTreeNode node = tree.readNode(root);
    CHECK(node.isLeaf());
    CHECK_EQ(node.keyCount(), uint16_t(0));
    CHECK_EQ(node.t(), uint16_t(12)); // computeT(256) == 12
    CHECK_EQ(node.data().self, root); // el header trae su propia pagina
    CHECK_EQ(node.nextLeaf(), PageID(0)); // hoja unica: nada que encadenar
    CHECK_EQ(pm.pageCount(), uint32_t(2)); // meta + raiz, sin paginas de mas

    // Reabrir sobre el MISMO PageManager: la raiz sale del FileMeta, no se vuelve a crear
    BTree again;
    CHECK_EQ(BTree::open(pm, again), Status::Ok);
    CHECK_EQ(again.rootPageID(), root);
    CHECK_EQ(again.height(), size_t(1));
    CHECK_EQ(pm.pageCount(), uint32_t(2));
}

// 2. open sobre un archivo con la raiz referenciada por FileMeta pero ilegible devuelve
//    Corrupt, no basura, y no modifica el archivo.
TEST(btree_write, open_con_raiz_corrupta_devuelve_corrupt_sin_escribir) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);

    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    const PageID root = tree.rootPageID();

    // Rollo la pagina de la raiz a ceros: isLeaf=0, keyCount=0, t=0 -> t != computeT(256)
    const Page zeros(256, 0);
    pm.write(root, zeros);
    const uint32_t pages_before = pm.pageCount();
    pm.flush();

    // Reabro con un PageManager NUEVO para que no conserve el estado en memoria
    PageManager reopened;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, reopened), Status::Ok);
    BTree broken;
    CHECK_EQ(BTree::open(reopened, broken), Status::Corrupt);
    CHECK_EQ(reopened.pageCount(), pages_before); // no se aprovisiono nada en el fallo

    // El archivo sigue teniendo su FileMeta: la raiz sigue siendo la misma pagina
    CHECK_EQ(reopened.readMeta().root_page_id, root);
    CHECK_EQ(reopened.readMeta().height, uint32_t(1));
}
