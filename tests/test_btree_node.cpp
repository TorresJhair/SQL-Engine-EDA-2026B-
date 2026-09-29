// tests/test_btree_node.cpp    [P3]
// computeT 204/12, serialize en el limite de 256 B, split, y Corrupt por
// invariantes.

#include "TestHarness.h"
#include "index/BTreeNode.h"

#include <climits>
#include <vector>

namespace suites {
void btree_node() {
    // La lista explicita de las suites. Los tests se registran con TEST()
    // abajo.
}
} // namespace suites

// 1. Verificacion de tipos de NodeData y constantes
TEST(btree_node, contrato_congelado_y_tipos) {
    static_assert(std::is_same_v<decltype(NodeData::t), uint16_t>);
    static_assert(std::is_same_v<decltype(NodeData::self), PageID>);
    static_assert(std::is_same_v<decltype(NodeData::next_leaf), PageID>);
    static_assert(std::is_same_v<decltype(NodeData::is_leaf), bool>);
    static_assert(
        std::is_same_v<decltype(NodeData::keys), std::vector<int32_t>>);
    static_assert(
        std::is_same_v<decltype(NodeData::children), std::vector<PageID>>);
    static_assert(
        std::is_same_v<decltype(NodeData::rowids), std::vector<RowID>>);
    static_assert(NODE_HEADER_SIZE == 16);

    NodeData nd;
    nd.is_leaf = true;
    nd.t = 12;
    nd.self = 1;
    nd.next_leaf = 2;
    nd.keys.push_back(100);
    nd.rowids.push_back(RowID{1, 0});
    nd.children.push_back(3);

    CHECK(nd.is_leaf);
    CHECK_EQ(nd.t, uint16_t(12));
    CHECK_EQ(nd.self, PageID(1));
    CHECK_EQ(nd.next_leaf, PageID(2));
    CHECK_EQ(nd.keys.size(), size_t(1));
    CHECK_EQ(nd.rowids.size(), size_t(1));
    CHECK_EQ(nd.children.size(), size_t(1));
}

// 2. computeT fija 204 @4096 B y 12 @256 B con SLACK = 1
TEST(btree_node, compute_t_fija_204_y_12) {
    CHECK_EQ(BTreeNode::computeT(4096), uint16_t(204));
    CHECK_EQ(BTreeNode::computeT(256), uint16_t(12));
}

// 3. data() y fromData() conservan el contenido del nodo, incluido t
TEST(btree_node, data_y_from_data_roundtrip) {
    BTreeNode node = BTreeNode::makeLeaf(10, 256);
    node.insertInNode(42, RowID{5, 2});
    node.setNextLeafPageID(11);

    NodeData d = node.data();
    CHECK(d.is_leaf);
    CHECK_EQ(d.t, uint16_t(12));
    CHECK_EQ(d.self, PageID(10));
    CHECK_EQ(d.next_leaf, PageID(11));
    CHECK_EQ(d.keys.size(), size_t(1));
    CHECK_EQ(d.keys[0], 42);
    CHECK_EQ(d.rowids.size(), size_t(1));
    CHECK_EQ(d.rowids[0].pageID, PageID(5));
    CHECK_EQ(d.rowids[0].slotID, SlotID(2));

    BTreeNode restored = BTreeNode::fromData(d, 256);
    CHECK(restored.isLeaf());
    CHECK_EQ(restored.t(), uint16_t(12));
    CHECK_EQ(restored.keyCount(), uint16_t(1));
    CHECK_EQ(restored.keyAt(0), 42);
    CHECK_EQ(restored.rowIDAt(0).pageID, PageID(5));
    CHECK_EQ(restored.rowIDAt(0).slotID, SlotID(2));
    CHECK_EQ(restored.nextLeaf(), PageID(11));
}

// 4. serialize devuelve Ok con 0, 1, t-1, t, 2t-1 claves y NodeOverflow con 2t
// claves
TEST(btree_node, serialize_limites_de_capacidad_y_overflow) {
    const size_t pageSize = 256;
    const uint16_t t = BTreeNode::computeT(pageSize); // 12
    CHECK_EQ(t, uint16_t(12));

    BTreeNode node = BTreeNode::makeLeaf(1, pageSize);
    Page page;

    // 0 claves
    CHECK_EQ(node.serialize(page), Status::Ok);

    // 1 clave
    node.insertInNode(10, RowID{1, 0});
    CHECK_EQ(node.serialize(page), Status::Ok);

    // Llegar hasta t - 1 claves (11 claves)
    for (int32_t k = 20; node.keyCount() < t - 1; k += 10) {
        node.insertInNode(k, RowID{1, static_cast<SlotID>(node.keyCount())});
    }
    CHECK_EQ(node.keyCount(), uint16_t(11));
    CHECK_EQ(node.serialize(page), Status::Ok);

    // t claves (12 claves)
    node.insertInNode(999, RowID{1, 11});
    CHECK_EQ(node.keyCount(), uint16_t(12));
    CHECK_EQ(node.serialize(page), Status::Ok);

    // Llegar hasta 2t - 1 claves (23 claves: maximo permitido en disco)
    for (int32_t k = 1000; node.keyCount() < 2 * t - 1; ++k) {
        node.insertInNode(k, RowID{1, static_cast<SlotID>(node.keyCount())});
    }
    CHECK_EQ(node.keyCount(), uint16_t(23));
    CHECK_EQ(node.serialize(page), Status::Ok);

    // 2t claves (24 claves: cabe en memoria, pero serialize debe rechazarlo con
    // NodeOverflow)
    node.insertInNode(2000, RowID{1, 23});
    CHECK_EQ(node.keyCount(), uint16_t(24));
    CHECK_EQ(node.serialize(page), Status::NodeOverflow);
}

// 5. Round-trip serialize -> deserialize byte a byte (hoja y nodo interno)
TEST(btree_node, serialize_deserialize_roundtrip_hoja_e_interno) {
    const size_t pageSize = 256;
    Page page;

    // Hoja con claves extremas INT_MIN e INT_MAX
    {
        BTreeNode leaf = BTreeNode::makeLeaf(5, pageSize);
        leaf.setNextLeafPageID(6);
        leaf.insertInNode(INT_MIN, RowID{1, 0});
        leaf.insertInNode(0, RowID{1, 1});
        leaf.insertInNode(INT_MAX, RowID{1, 2});

        CHECK_EQ(leaf.serialize(page), Status::Ok);

        BTreeNode readLeaf;
        CHECK_EQ(BTreeNode::deserialize(page, readLeaf), Status::Ok);
        CHECK(readLeaf.isLeaf());
        CHECK_EQ(readLeaf.t(), uint16_t(12));
        CHECK_EQ(readLeaf.keyCount(), uint16_t(3));
        CHECK_EQ(readLeaf.keyAt(0), INT_MIN);
        CHECK_EQ(readLeaf.keyAt(1), 0);
        CHECK_EQ(readLeaf.keyAt(2), INT_MAX);
        CHECK_EQ(readLeaf.rowIDAt(0).pageID, PageID(1));
        CHECK_EQ(readLeaf.rowIDAt(2).slotID, SlotID(2));
        CHECK_EQ(readLeaf.nextLeaf(), PageID(6));
    }

    // Nodo interno
    {
        BTreeNode internal = BTreeNode::makeInternal(7, pageSize);
        internal.insertInNode(50, 100);  // primera clave con hijo derecho 100
        internal.insertInNode(150, 101); // segunda clave con hijo derecho 101

        CHECK_EQ(internal.serialize(page), Status::Ok);

        BTreeNode readInternal;
        CHECK_EQ(BTreeNode::deserialize(page, readInternal), Status::Ok);
        CHECK(!readInternal.isLeaf());
        CHECK_EQ(readInternal.t(), uint16_t(12));
        CHECK_EQ(readInternal.keyCount(), uint16_t(2));
        CHECK_EQ(readInternal.childCount(), uint32_t(3));
        CHECK_EQ(readInternal.keyAt(0), 50);
        CHECK_EQ(readInternal.keyAt(1), 150);
        CHECK_EQ(readInternal.nextLeaf(), PageID(0)); // interno siempre 0
    }
}

// 6. Deteccion de corrupcion en deserialize
TEST(btree_node, deserialize_detecta_corrupcion_por_invariantes) {
    const size_t pageSize = 256;
    BTreeNode node = BTreeNode::makeLeaf(1, pageSize);
    node.insertInNode(100, RowID{1, 0});

    Page validPage;
    CHECK_EQ(node.serialize(validPage), Status::Ok);

    BTreeNode out;

    // A. Pagina demasiado pequena (< 16 B)
    Page tooSmall(15, 0);
    CHECK_EQ(BTreeNode::deserialize(tooSmall, out), Status::Corrupt);

    // B. t corrupto en header (bytes 11-12)
    Page corruptT = validPage;
    corruptT[11] = 99; // t invalido (debe ser 12)
    corruptT[12] = 0;
    CHECK_EQ(BTreeNode::deserialize(corruptT, out), Status::Corrupt);

    // C. keyCount > 2t-1 (bytes 1-2)
    Page corruptKeyCount = validPage;
    corruptKeyCount[1] = 24; // 24 > 2t-1 (23)
    corruptKeyCount[2] = 0;
    CHECK_EQ(BTreeNode::deserialize(corruptKeyCount, out), Status::Corrupt);

    // D. isLeaf invalido (byte 0 distinto de 0 y 1)
    Page corruptIsLeaf = validPage;
    corruptIsLeaf[0] = 5;
    CHECK_EQ(BTreeNode::deserialize(corruptIsLeaf, out), Status::Corrupt);

    // E. Pagina todo ceros (como una pagina vacia sin formatear)
    Page allZeros(256, 0);
    CHECK_EQ(BTreeNode::deserialize(allZeros, out), Status::Corrupt);
}

// 7. Split puro de hoja
TEST(btree_node, split_de_hoja_puro_copia_clave_promovida) {
    const size_t pageSize = 256;
    const uint16_t t = BTreeNode::computeT(pageSize); // 12
    BTreeNode leaf = BTreeNode::makeLeaf(1, pageSize);
    leaf.setNextLeafPageID(99);

    // Insertar 2t claves (24)
    for (int32_t i = 0; i < 2 * t; ++i) {
        leaf.insertInNode(i * 10, RowID{1, static_cast<SlotID>(i)});
    }
    CHECK_EQ(leaf.keyCount(), uint16_t(24));

    // Ejecutar split pasando el nuevo PageID asignado al hermano derecho
    // (PageID 2)
    SplitResult splitRes = leaf.split(2);

    // 1. Verificacion de clave promovida: se COPIA en hoja (es igual a
    // right.keyAt(0))
    CHECK_EQ(splitRes.promoted_key, splitRes.right.keyAt(0));
    CHECK_EQ(splitRes.promoted_key, 120); // i = 12 * 10 = 120

    // 2. Ambas hojas quedan con exactamente t claves (12)
    CHECK_EQ(leaf.keyCount(), uint16_t(12));
    CHECK_EQ(splitRes.right.keyCount(), uint16_t(12));

    // 3. right.self queda grabado con right_page_id
    CHECK_EQ(splitRes.right.data().self, PageID(2));

    // 4. Lista enlazada de hojas actualizada
    CHECK_EQ(leaf.nextLeaf(), PageID(2));
    CHECK_EQ(splitRes.right.nextLeaf(), PageID(99));

    // 5. Claves repartidas correctamente
    CHECK_EQ(leaf.keyAt(0), 0);
    CHECK_EQ(leaf.keyAt(11), 110);
    CHECK_EQ(splitRes.right.keyAt(0), 120);
    CHECK_EQ(splitRes.right.keyAt(11), 230);
}

// 8. Split puro de nodo interno
TEST(btree_node, split_de_nodo_interno_puro_sube_clave_mediana) {
    const size_t pageSize = 256;
    const uint16_t t = BTreeNode::computeT(pageSize); // 12
    BTreeNode internal = BTreeNode::makeInternal(1, pageSize);

    // Insertar 2t claves (24) y sus hijos
    for (int32_t i = 0; i < 2 * t; ++i) {
        internal.insertInNode(i * 10, static_cast<PageID>(100 + i));
    }
    CHECK_EQ(internal.keyCount(), uint16_t(24));
    CHECK_EQ(internal.childCount(), uint32_t(25));

    SplitResult splitRes = internal.split(3);

    // 1. Clave promovida: SUBE y se elimina de ambos nodos
    CHECK_EQ(splitRes.promoted_key, 120); // clave en index mid = 12

    // 2. Reparto de claves e hijos
    // Izquierdo: t claves (0..11), t+1 hijos (13)
    CHECK_EQ(internal.keyCount(), uint16_t(12));
    CHECK_EQ(internal.childCount(), uint32_t(13));
    CHECK_EQ(internal.keyAt(11), 110);

    // Derecho: t - 1 claves (11: 130..230), t hijos (12)
    CHECK_EQ(splitRes.right.keyCount(), uint16_t(11));
    CHECK_EQ(splitRes.right.childCount(), uint32_t(12));
    CHECK_EQ(splitRes.right.keyAt(0), 130);

    // 3. right.self grabado y nextLeaf en 0 para internos
    CHECK_EQ(splitRes.right.data().self, PageID(3));
    CHECK_EQ(splitRes.right.nextLeaf(), PageID(0));
    CHECK_EQ(internal.nextLeaf(), PageID(0));
}

// 9. Búsqueda y duplicados en hojas (upperBound inserta tras claves iguales)
TEST(btree_node, busqueda_e_insercion_de_duplicados) {
    BTreeNode leaf = BTreeNode::makeLeaf(1, 256);
    leaf.insertInNode(50, RowID{1, 0});
    leaf.insertInNode(50, RowID{1, 1});
    leaf.insertInNode(50, RowID{1, 2});
    leaf.insertInNode(20, RowID{1, 3});
    leaf.insertInNode(80, RowID{1, 4});

    CHECK_EQ(leaf.keyCount(), uint16_t(5));
    CHECK_EQ(leaf.keyAt(0), 20);
    CHECK_EQ(leaf.keyAt(1), 50);
    CHECK_EQ(leaf.keyAt(2), 50);
    CHECK_EQ(leaf.keyAt(3), 50);
    CHECK_EQ(leaf.keyAt(4), 80);

    // Validar orden de RowIDs en duplicados
    CHECK_EQ(leaf.rowIDAt(1).slotID, SlotID(0));
    CHECK_EQ(leaf.rowIDAt(2).slotID, SlotID(1));
    CHECK_EQ(leaf.rowIDAt(3).slotID, SlotID(2));

    // searchInNode devuelve lower_bound (primer indice con clave >= 50)
    auto hit = leaf.searchInNode(50);
    CHECK(hit.found);
    CHECK_EQ(hit.idx, uint16_t(1));

    // upperBound devuelve primer indice con clave > 50
    CHECK_EQ(leaf.upperBound(50), uint16_t(4));
}
