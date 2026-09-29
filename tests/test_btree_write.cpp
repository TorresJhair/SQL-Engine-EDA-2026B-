// tests/test_btree_write.cpp    [P3]
// open, findLeaf, insert, propagacion de Status, splits y el log bajo verbose.
// Esta suite esta repartida por fases en la rama btree-node: cada commit agrega los
// casos de la fase que implementa.
//
//   fase 1 (open/lectura)  -> casos 1-2
//   fase 2 (insert/split)  -> casos 3-10
//   fase 3 (log)           -> casos 11-13
//
// Lo que NO esta aqui y depende de P4: checkInvariants (tests/TreeInvariants.h) para
// cerrar el test de estres, y BTree::search para comprobar las 40 duplicadas "por la
// API publica". Hay un Issue abierto por eso; mientras tanto el contenido se comprueba
// recorriendo la cadena de hojas con readNode(), que es API de P3.

#include "TestHarness.h"
#include "bench/DataGen.h"
#include "index/BTree.h"
#include "storage/PageManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
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

// Recorre la cadena de hojas con readNode() (API de P3) y devuelve lo que hay dentro:
// el orden de la cadena ES el orden in-order del arbol. Es la comprobacion de contenido
// hasta que P4 entregue search().
struct LeafWalk {
    std::vector<int32_t> keys;
    std::vector<RowID>   rowids;
    size_t               leaves = 0;
};

// Captura std::cout durante un bloque: los logs salen por std::cout y sin esto el
// formato no se podria comprobar sin leer la terminal. RAII, para que un fallo en medio
// no deje la redireccion puesta y tape el reporte del harness.
struct CoutCapture {
    std::ostringstream buffer;
    std::streambuf*    previous = nullptr;

    void begin() { previous = std::cout.rdbuf(buffer.rdbuf()); }
    std::string end() {
        if (previous != nullptr) std::cout.rdbuf(previous);
        previous = nullptr;
        return buffer.str();
    }
    ~CoutCapture() {
        if (previous != nullptr) std::cout.rdbuf(previous);
    }
};

// RowID sin operator<< (solo existe == y <), asi que los RowID se comparan con CHECK.
// Ademas RowID{a, b} con comas dentro de CHECK_EQ partiria el macro en tres argumentos.
RowID rid(PageID page, SlotID slot) { return RowID{page, slot}; }

LeafWalk walkLeaves(const BTree& tree) {
    LeafWalk out;
    PageID current = tree.rootPageID();
    while (true) {                              // baja por la izquierda hasta una hoja
        const BTreeNode node = tree.readNode(current);
        if (node.isLeaf()) break;
        current = node.childAt(0);
    }
    while (current != 0) {                      // y sigue nextLeaf hasta el final (0)
        const BTreeNode leaf = tree.readNode(current);
        ++out.leaves;
        for (uint16_t i = 0; i < leaf.keyCount(); ++i) {
            out.keys.push_back(leaf.keyAt(i));
            out.rowids.push_back(leaf.rowIDAt(i));
        }
        current = leaf.nextLeaf();
    }
    return out;
}
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

// 3. insertar sin open() no es un segfault ni un assert: es un contrato roto.
TEST(btree_write, insert_sin_open_devuelve_precondition_failed) {
    BTree unopened;
    CHECK_EQ(unopened.rootPageID(), PageID(0)); // sin pm_ no hay raiz
    CHECK_EQ(unopened.height(), size_t(0));
    CHECK_EQ(unopened.insert(1, rid(1, 0)), Status::PreconditionFailed);
}

// 4. Insertar en una hoja raiz: las claves quedan no decrecientes en disco, con su RowID
//    correspondiente, y sobreviven a un reopen del archivo.
TEST(btree_write, insert_en_hoja_raiz_ordena_y_persiste) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    const int32_t input[] = {50, 10, 30, 20, 40};
    for (size_t i = 0; i < sizeof(input) / sizeof(input[0]); ++i) {
        CHECK_EQ(tree.insert(input[i], rid(7, static_cast<SlotID>(i))), Status::Ok);
    }
    CHECK_EQ(tree.height(), size_t(1)); // 5 claves: ni 2t-1 = 23, ni un split a la vista
    CHECK_EQ(pm.pageCount(), uint32_t(2));

    const LeafWalk walk = walkLeaves(tree);
    CHECK_EQ(walk.keys.size(), size_t(5));
    CHECK_EQ(walk.leaves, size_t(1));
    const int32_t esperadas[] = {10, 20, 30, 40, 50};
    const SlotID slots[] = {1, 3, 2, 4, 0}; // 50 entro primero (slot 0), 10 en el 1...
    for (size_t i = 0; i < 5; ++i) {
        CHECK_EQ(walk.keys[i], esperadas[i]);
        CHECK_EQ(walk.rowids[i].pageID, PageID(7));
        CHECK_EQ(walk.rowids[i].slotID, slots[i]); // el RowID viaja con su clave
    }

    // Reabro el archivo con un PageManager nuevo: lo que se leyo sale del disco, no de
    // la memoria del proceso
    pm.flush();
    PageManager reopened;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, reopened), Status::Ok);
    BTree again;
    CHECK_EQ(BTree::open(reopened, again), Status::Ok);
    const LeafWalk reread = walkLeaves(again);
    CHECK_EQ(reread.keys.size(), size_t(5));
    CHECK(std::equal(reread.keys.begin(), reread.keys.end(), esperadas,
                     esperadas + 5));
}

// 5. Duplicados: se acumulan tras las claves iguales, en orden de insercion, con su
//    RowID. (Las 40 copias que cruzan hojas estan en el caso 9.)
TEST(btree_write, insert_de_duplicados_acumula_tras_la_clave_igual) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    for (SlotID slot = 0; slot < 5; ++slot) {
        CHECK_EQ(tree.insert(7, rid(3, slot)), Status::Ok);
    }
    const LeafWalk walk = walkLeaves(tree);
    CHECK_EQ(walk.keys.size(), size_t(5));
    for (size_t i = 0; i < 5; ++i) {
        CHECK_EQ(walk.keys[i], int32_t(7));
        CHECK((walk.rowids[i] == rid(3, static_cast<SlotID>(i)))); // CHECK: RowID no tiene operator<<
    }
}

// 6. 2t-1 = 23 inserciones siguen cabiendo en la hoja raiz: la 23ava NO parte nada y no
//    se allocan paginas de mas.
TEST(btree_write, insert_hasta_2t_menos_1_sigue_siendo_hoja_raiz) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    const PageID root = tree.rootPageID();

    for (int32_t key = 1; key <= 23; ++key) {
        CHECK_EQ(tree.insert(key, rid(1, static_cast<SlotID>(key))), Status::Ok);
    }
    CHECK_EQ(tree.height(), size_t(1));
    CHECK_EQ(tree.rootPageID(), root);       // la raiz no cambio de pagina
    CHECK_EQ(pm.pageCount(), uint32_t(2));   // meta + raiz: cero splits
    const BTreeNode leaf = tree.readNode(root);
    CHECK(leaf.isLeaf());
    CHECK_EQ(leaf.keyCount(), uint16_t(23)); // 2t-1, el maximo que serialize acepta
    CHECK_EQ(leaf.nextLeaf(), PageID(0));
}

// 7. La insercion numero 2t = 24 parte la raiz EXACTAMENTE: la hoja vieja se vuelve
//    nodo interno, nace una hoja derecha, sube la clave 13, la altura pasa de 1 a 2 y
//    la nueva raiz queda en FileMeta (sobrevive al reopen).
TEST(btree_write, insert_2t_parte_la_raiz_y_persiste_la_nueva) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    const PageID old_root = tree.rootPageID();

    for (int32_t key = 1; key <= 23; ++key) {
        CHECK_EQ(tree.insert(key, rid(1, static_cast<SlotID>(key))), Status::Ok);
    }
    CHECK_EQ(tree.height(), size_t(1));

    CHECK_EQ(tree.insert(24, rid(1, 24)), Status::Ok);
    CHECK_EQ(tree.height(), size_t(2));

    const PageID new_root = tree.rootPageID();
    CHECK(new_root != old_root); // crecio hacia arriba: hay raiz nueva

    // La hoja original NO se mueve: se queda en su pagina como mitad izquierda, y la
    // raiz nueva (pagina recien allocada) es el nodo interno con la clave promovida.
    const BTreeNode parent = tree.readNode(new_root);
    CHECK(!parent.isLeaf());
    CHECK_EQ(parent.keyCount(), uint16_t(1));
    CHECK_EQ(parent.keyAt(0), int32_t(13)); // 1..24 en memoria: keys[12] = 13, copiada
    CHECK_EQ(parent.childCount(), uint32_t(2));
    CHECK_EQ(parent.childAt(0), old_root); // la mitad izquierda se queda en su pagina

    const BTreeNode left = tree.readNode(parent.childAt(0));
    const BTreeNode right = tree.readNode(parent.childAt(1));
    CHECK(left.isLeaf());
    CHECK(right.isLeaf());
    CHECK_EQ(left.keyCount(), uint16_t(12)); // t claves de un lado, t del otro
    CHECK_EQ(right.keyCount(), uint16_t(12));
    CHECK_EQ(left.keyAt(0), int32_t(1));
    CHECK_EQ(left.keyAt(11), int32_t(12));
    CHECK_EQ(right.keyAt(0), int32_t(13)); // la promovida se COPIA: sigue en el hijo
    CHECK_EQ(right.keyAt(11), int32_t(24));
    CHECK_EQ(left.nextLeaf(), right.data().self); // la cadena se rehizo con la nueva hoja
    CHECK_EQ(right.nextLeaf(), PageID(0));         // ultima hoja

    const LeafWalk walk = walkLeaves(tree);
    CHECK_EQ(walk.keys.size(), size_t(24));
    CHECK_EQ(walk.leaves, size_t(2));
    for (size_t i = 0; i < walk.keys.size(); ++i) {
        CHECK_EQ(walk.keys[i], int32_t(i + 1));
    }

    // La raiz nueva esta en FileMeta, no en memoria
    pm.flush();
    PageManager reopened;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, reopened), Status::Ok);
    BTree again;
    CHECK_EQ(BTree::open(reopened, again), Status::Ok);
    CHECK_EQ(again.rootPageID(), new_root);
    CHECK_EQ(again.height(), size_t(2));
    CHECK_EQ(walkLeaves(again).keys.size(), size_t(24));
}

// 8. Propagacion: hay un instante en el que UNA insercion parte hoja y nodo interno a la
//    vez (2 paginas o mas allocadas en una sola llamada). Se detecta por los contadores
//    de PageManager, que son observables sin el log: con altura >= 2, delta >= 2 solo
//    puede venir de un split interno (1 de la hoja + 1 del split interno + 1 de la raiz).
TEST(btree_write, insert_propaga_el_split_por_dos_niveles) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    int32_t propagated_at = 0;
    for (int32_t key = 1; key <= 20000; ++key) {
        const uint32_t pages_before = tree.pages().pageCount();
        const size_t height_before = tree.height();
        CHECK_EQ(tree.insert(key, rid(1, static_cast<SlotID>(key % 4096))),
                 Status::Ok);
        const uint32_t delta = tree.pages().pageCount() - pages_before;
        if (height_before >= 2 && delta >= 2) {
            propagated_at = key;
            break;
        }
    }
    CHECK(propagated_at != 0);              // ocurrio, no es una suposicion
    CHECK_EQ(tree.height(), size_t(3));     // interno partido -> raiz nueva -> 2 -> 3

    // El arbol quedo entero: la cadena sigue devolviendo 1..propagated_at en orden
    const LeafWalk walk = walkLeaves(tree);
    CHECK_EQ(walk.keys.size(), size_t(static_cast<size_t>(propagated_at)));
    CHECK(walk.leaves > 1);
    for (size_t i = 0; i < walk.keys.size(); ++i) {
        CHECK_EQ(walk.keys[i], int32_t(i + 1));
    }
}

// 9. 40 copias de la MISMA clave con page_size = 256: cruzan varias hojas (caben 23 por
//    hoja) y las 40 aparecen en la cadena con su RowID en orden de insercion.
//    Caso que pide el enunciado para "search devuelve las 40": aqui se comprueba por la
//    cadena de hojas; el CHECK sobre search() se agrega cuando P4 lo entregue (Issue).
TEST(btree_write, insert_40_duplicadas_cruzan_varias_hojas) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    for (int32_t copy = 0; copy < 40; ++copy) {
        CHECK_EQ(tree.insert(4242, rid(9, static_cast<SlotID>(copy))), Status::Ok);
    }

    const LeafWalk walk = walkLeaves(tree);
    CHECK_EQ(walk.keys.size(), size_t(40));
    CHECK(walk.leaves >= 2); // 23 por hoja: 40 no caben en una
    for (size_t i = 0; i < 40; ++i) {
        CHECK_EQ(walk.keys[i], int32_t(4242));
        CHECK((walk.rowids[i] == rid(9, static_cast<SlotID>(i))));
    }
    CHECK(tree.height() >= 2);
}

// 10. Estres: 5 000 inserciones aleatorias (DataGen::randomKeys, PR propio de P1) y el
//     contenido recuperado por la cadena de hojas tiene que ser EXACTAMENTE el mismo
//     multiconjunto de (clave, RowID) que se inserto, en orden no decreciente.
//     checkInvariants() cierra el caso cuando P4 lo entregue (Issue).
TEST(btree_write, insert_estres_5000_aleatorias) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    const std::vector<std::pair<int32_t, RowID>> input =
        DataGen::randomKeys(5000, 20260929);
    for (const auto& entry : input) {
        CHECK_EQ(tree.insert(entry.first, entry.second), Status::Ok);
    }

    const LeafWalk walk = walkLeaves(tree);
    CHECK_EQ(walk.keys.size(), input.size());
    CHECK(tree.height() >= 2);
    CHECK(walk.leaves > 1);

    std::vector<std::pair<int32_t, RowID>> esperado = input;
    std::vector<std::pair<int32_t, RowID>> obtenido;
    obtenido.reserve(walk.keys.size());
    for (size_t i = 0; i < walk.keys.size(); ++i) {
        obtenido.emplace_back(walk.keys[i], walk.rowids[i]);
        if (i > 0) CHECK(walk.keys[i - 1] <= walk.keys[i]); // in-order, no decreciente
    }
    std::sort(esperado.begin(), esperado.end());
    std::sort(obtenido.begin(), obtenido.end());
    CHECK(esperado == obtenido); // mismo multiconjunto, claves y RowIDs
}

// 11. Con verbose apagado (el default) el arbol es silencioso: ni un byte de log aunque
//     se produzcan splits y crecimientos de altura.
TEST(btree_write, log_apagado_por_defecto_no_imprime_nada) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    CoutCapture capture;
    capture.begin();
    for (int32_t key = 1; key <= 40; ++key) { // 24 parte la raiz: hay split y [ALTURA]
        CHECK_EQ(tree.insert(key, rid(1, static_cast<SlotID>(key))), Status::Ok);
    }
    const std::string out = capture.end();

    CHECK_EQ(tree.height(), size_t(2)); // hubo splits, no los callados: no se imprimio nada
    CHECK_EQ(out, std::string());
}

// 12. Con verbose encendido, el formato es EXACTAMENTE el de docs/btree-insert.md. La
//     pagina 1 es la hoja raiz, la 2 la hoja derecha y la 3 la raiz nueva, asi que la
//     salida de las 24 inserciones primeras es determinista y se compara entera.
TEST(btree_write, log_encendido_imprime_split_y_altura_en_formato_exacto) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    tree.setVerbose(true);

    CoutCapture capture;
    capture.begin();
    for (int32_t key = 1; key <= 24; ++key) {
        CHECK_EQ(tree.insert(key, rid(1, static_cast<SlotID>(key))), Status::Ok);
    }
    const std::string out = capture.end();

    CHECK_EQ(out, std::string("[SPLIT]  hoja    page=1 -> izq=1 der=2  clave=13\n"
                              "[ALTURA] 1 -> 2  raiz=page 3\n"));
    // "hoja" se alinea a 7 caracteres: "interno" ocupa los 7 justos. Asi el campo de la
    // primera columna queda siempre en la misma columna de la terminal.
    CHECK(out.find("[SPLIT]  hoja    page=") != std::string::npos);
    CHECK(out.find("[ALTURA] 1 -> 2  raiz=page 3") != std::string::npos);
}

// 13. El split de un nodo INTERNO imprime "interno" en esa misma columna, y el
//     crecimiento a altura 3 viene con su [ALTURA]. Se llega ahi repitiendo el caso 8.
TEST(btree_write, log_encendido_tambien_imprime_el_split_interno) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    tree.setVerbose(true);

    CoutCapture capture;
    capture.begin();
    for (int32_t key = 1; key <= 20000 && tree.height() < 3; ++key) {
        CHECK_EQ(tree.insert(key, rid(1, static_cast<SlotID>(key % 4096))), Status::Ok);
    }
    const std::string out = capture.end();

    CHECK_EQ(tree.height(), size_t(3));
    CHECK(out.find("[SPLIT]  interno page=") != std::string::npos);
    CHECK(out.find("[ALTURA] 2 -> 3  raiz=page ") != std::string::npos);
    CHECK(out.find("[SPLIT]  hoja    page=") != std::string::npos); // la hoja parti antes
}
