// tests/test_btree_read.cpp
#include "TestHarness.h"
#include "index/BTree.h"
#include "storage/PageManager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {
struct TempIndex {
    std::filesystem::path path;

    TempIndex() {
        static std::atomic<uint64_t> sequence{0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("sql_engine_btree_read_" + std::to_string(timestamp) + "_" +
                std::to_string(sequence.fetch_add(1)) + ".idx");
    }

    ~TempIndex() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

RowID rid(PageID page, SlotID slot) { return RowID{page, slot}; }

Tuple datasetTuple(int32_t key, const std::string& name) {
    Tuple tuple;
    tuple.append(Value(key));
    tuple.append(Value(name));
    return tuple;
}

std::vector<std::pair<int32_t, RowID>> ascendingEntries(size_t count) {
    std::vector<std::pair<int32_t, RowID>> entries;
    entries.reserve(count);
    for (size_t entry_index = 0; entry_index < count; ++entry_index) {
        entries.emplace_back(static_cast<int32_t>(entry_index),
                             rid(1, static_cast<SlotID>(entry_index)));
    }
    return entries;
}

std::vector<BTreeNode> leafChain(const BTree& tree) {
    PageID page_id = tree.rootPageID();
    BTreeNode node = tree.readNode(page_id);
    while (!node.isLeaf()) {
        page_id = node.childAt(0);
        node = tree.readNode(page_id);
    }

    std::vector<BTreeNode> leaves;
    while (page_id != 0) {
        node = tree.readNode(page_id);
        leaves.push_back(node);
        page_id = node.nextLeaf();
    }
    return leaves;
}
}  // namespace

namespace suites {
void btree_read() {
    // Los TEST() se registran solos; esta funcion mantiene explicita la suite.
}
}  // namespace suites

TEST(btree_read, search_en_arbol_vacio_y_claves_presentes_o_ausentes) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    CHECK(tree.search(1).empty());
    CHECK_EQ(tree.size(), size_t(0));

    CHECK_EQ(tree.insert(30, rid(4, 0)), Status::Ok);
    CHECK_EQ(tree.insert(10, rid(4, 1)), Status::Ok);
    CHECK_EQ(tree.insert(20, rid(4, 2)), Status::Ok);

    CHECK_EQ(tree.size(), size_t(3));
    CHECK(tree.search(5).empty());
    CHECK(tree.search(25).empty());
    CHECK(tree.search(35).empty());
    const std::vector<RowID> matches = tree.search(20);
    CHECK_EQ(matches.size(), size_t(1));
    CHECK((matches[0] == rid(4, 2)));
}

TEST(btree_read, search_encuentra_duplicados_que_cruzan_hojas) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    for (SlotID slot = 0; slot < 40; ++slot) {
        CHECK_EQ(tree.insert(4242, rid(9, slot)), Status::Ok);
    }

    const std::vector<RowID> matches = tree.search(4242);
    CHECK_EQ(matches.size(), size_t(40));
    CHECK_EQ(tree.size(), size_t(40));
    for (SlotID slot = 0; slot < 40; ++slot) {
        CHECK((matches[slot] == rid(9, slot)));
    }
    CHECK(tree.search(4243).empty());
}

TEST(btree_read, search_encuentra_clave_que_inicia_en_frontera_de_hoja) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    CHECK_EQ(tree.bulkLoad(ascendingEntries(24)), Status::Ok);

    const std::vector<RowID> matches = tree.search(12);
    CHECK_EQ(matches.size(), size_t(1));
    CHECK((matches[0] == rid(1, 12)));
}

TEST(btree_read, index_scan_coincide_con_full_scan_filtrado_en_orden) {
    TempIndex temp;
    const std::string heap_path = temp.path.string() + ".heap";
    struct HeapCleanup {
        std::string path;
        ~HeapCleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } heap_cleanup{heap_path};

    PageManager index_pm;
    PageManager heap_pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, index_pm), Status::Ok);
    CHECK_EQ(PageManager::open(heap_path, 256, heap_pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(index_pm, tree), Status::Ok);
    HeapFile heap(heap_pm);

    for (int32_t row = 0; row < 120; ++row) {
        const int32_t key = (row * 5) % 11;
        const Tuple tuple = datasetTuple(key, "row-" + std::to_string(row));
        RowID row_id{};
        CHECK_EQ(heap.insert(tuple, row_id), Status::Ok);
        CHECK_EQ(tree.insert(key, row_id), Status::Ok);
    }

    const int32_t target_key = 7;
    const std::vector<Tuple> indexed = tree.indexScan(target_key, heap);
    std::vector<Tuple> scanned;
    HeapFile::Scan scan = heap.scan();
    Tuple tuple;
    RowID row_id{};
    while (scan.next(tuple, row_id)) {
        if (tuple.at(0).asInt() == target_key) scanned.push_back(tuple);
    }

    CHECK_EQ(indexed.size(), scanned.size());
    for (size_t i = 0; i < indexed.size() && i < scanned.size(); ++i) {
        CHECK_EQ(indexed[i].at(0).asInt(), scanned[i].at(0).asInt());
        CHECK_EQ(indexed[i].at(1).asVarChar(), scanned[i].at(1).asVarChar());
    }
    CHECK(tree.indexScan(99, heap).empty());
}

TEST(btree_read, bulk_load_vacio_y_una_hoja) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    const PageID initial_root = tree.rootPageID();

    CHECK_EQ(tree.bulkLoad({}), Status::Ok);
    CHECK_EQ(tree.rootPageID(), initial_root);
    CHECK_EQ(tree.height(), size_t(1));
    CHECK_EQ(tree.size(), size_t(0));

    const auto entries = ascendingEntries(23);
    CHECK_EQ(tree.bulkLoad(entries), Status::Ok);
    CHECK_EQ(tree.rootPageID(), initial_root);
    CHECK_EQ(tree.height(), size_t(1));
    CHECK_EQ(tree.size(), size_t(23));
    const BTreeNode root = tree.readNode(tree.rootPageID());
    CHECK(root.isLeaf());
    CHECK_EQ(root.keyCount(), uint16_t(23));
}

TEST(btree_read, bulk_load_distribuye_uniformemente_24_y_47_claves) {
    {
        TempIndex temp;
        PageManager pm;
        CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
        BTree tree;
        CHECK_EQ(BTree::open(pm, tree), Status::Ok);
        CHECK_EQ(tree.bulkLoad(ascendingEntries(24)), Status::Ok);

        const std::vector<BTreeNode> leaves = leafChain(tree);
        CHECK_EQ(tree.height(), size_t(2));
        CHECK_EQ(leaves.size(), size_t(2));
        CHECK_EQ(leaves[0].keyCount(), uint16_t(12));
        CHECK_EQ(leaves[1].keyCount(), uint16_t(12));
    }
    {
        TempIndex temp;
        PageManager pm;
        CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
        BTree tree;
        CHECK_EQ(BTree::open(pm, tree), Status::Ok);
        CHECK_EQ(tree.bulkLoad(ascendingEntries(47)), Status::Ok);

        const std::vector<BTreeNode> leaves = leafChain(tree);
        const BTreeNode root = tree.readNode(tree.rootPageID());
        CHECK_EQ(tree.height(), size_t(2));
        CHECK_EQ(leaves.size(), size_t(3));
        CHECK_EQ(leaves[0].keyCount(), uint16_t(16));
        CHECK_EQ(leaves[1].keyCount(), uint16_t(16));
        CHECK_EQ(leaves[2].keyCount(), uint16_t(15));
        CHECK_EQ(root.keyAt(0), int32_t(16));
        CHECK_EQ(root.keyAt(1), int32_t(32));
    }
}

TEST(btree_read, bulk_load_construye_tres_niveles_con_ocupacion_minima) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    CHECK_EQ(tree.bulkLoad(ascendingEntries(553)), Status::Ok);

    const BTreeNode root = tree.readNode(tree.rootPageID());
    CHECK_EQ(tree.height(), size_t(3));
    CHECK_EQ(root.childCount(), uint32_t(2));
    const BTreeNode first_internal = tree.readNode(root.childAt(0));
    const BTreeNode second_internal = tree.readNode(root.childAt(1));
    CHECK_EQ(first_internal.childCount(), uint32_t(13));
    CHECK_EQ(second_internal.childCount(), uint32_t(12));

    const std::vector<BTreeNode> leaves = leafChain(tree);
    CHECK_EQ(leaves.size(), size_t(25));
    for (const BTreeNode& leaf : leaves) {
        CHECK(leaf.keyCount() == 22 || leaf.keyCount() == 23);
    }
    CHECK_EQ(tree.size(), size_t(553));
    CHECK_EQ(tree.search(0).size(), size_t(1));
    CHECK_EQ(tree.search(552).size(), size_t(1));
}

TEST(btree_read, bulk_load_rechaza_precondiciones_sin_mutar_el_arbol) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    const PageID initial_root = tree.rootPageID();
    const uint32_t initial_pages = pm.pageCount();
    const std::vector<std::pair<int32_t, RowID>> unordered = {
        {2, rid(3, 0)}, {1, rid(3, 1)}};

    CHECK_EQ(tree.bulkLoad(unordered), Status::PreconditionFailed);
    CHECK_EQ(tree.rootPageID(), initial_root);
    CHECK_EQ(tree.size(), size_t(0));
    CHECK_EQ(pm.pageCount(), initial_pages);

    CHECK_EQ(tree.insert(5, rid(3, 2)), Status::Ok);
    const uint32_t pages_after_insert = pm.pageCount();
    CHECK_EQ(tree.bulkLoad(ascendingEntries(2)), Status::PreconditionFailed);
    CHECK_EQ(tree.rootPageID(), initial_root);
    CHECK_EQ(tree.size(), size_t(1));
    CHECK_EQ(pm.pageCount(), pages_after_insert);
    const std::vector<RowID> existing = tree.search(5);
    CHECK_EQ(existing.size(), size_t(1));
    CHECK((existing[0] == rid(3, 2)));
}

TEST(btree_read, bulk_load_conserva_duplicados_entre_hojas) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    std::vector<std::pair<int32_t, RowID>> duplicates;
    for (SlotID slot = 0; slot < 40; ++slot) {
        duplicates.emplace_back(4242, rid(9, slot));
    }

    CHECK_EQ(tree.bulkLoad(duplicates), Status::Ok);
    const std::vector<RowID> matches = tree.search(4242);
    CHECK_EQ(matches.size(), size_t(40));
    for (SlotID slot = 0; slot < 40; ++slot) {
        CHECK((matches[slot] == rid(9, slot)));
    }
}
