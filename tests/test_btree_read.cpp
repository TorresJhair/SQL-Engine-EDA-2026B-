// tests/test_btree_read.cpp
#include "TestHarness.h"
#include "index/BTree.h"
#include "storage/PageManager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

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
