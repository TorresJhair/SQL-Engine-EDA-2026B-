#include "TestHarness.h"
#include "TreeInvariants.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

namespace suites {
void invariants() {
}
}  // namespace suites

namespace {
struct TempIndex {
    std::filesystem::path path;

    TempIndex() {
        static std::atomic<uint64_t> sequence{0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("sql_engine_invariants_" + std::to_string(timestamp) + "_" +
                std::to_string(sequence.fetch_add(1)) + ".idx");
    }

    ~TempIndex() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
}  // namespace

TEST(invariants, acepta_arbol_vacio_y_arboles_con_varios_niveles) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);

    CHECK(checkInvariants(tree).ok);
    for (int32_t key = 0; key < 600; ++key) {
        CHECK_EQ(tree.insert(key / 17, RowID{1, static_cast<SlotID>(key)}),
                 Status::Ok);
    }
    const InvariantReport report = checkInvariants(tree);
    CHECK(report.ok);
    CHECK(report.error.empty());
}

TEST(invariants, rechaza_claves_desordenadas_en_una_hoja) {
    TempIndex temp;
    PageManager pm;
    CHECK_EQ(PageManager::open(temp.path.string(), 256, pm), Status::Ok);
    BTree tree;
    CHECK_EQ(BTree::open(pm, tree), Status::Ok);
    CHECK_EQ(tree.insert(10, RowID{1, 0}), Status::Ok);
    CHECK_EQ(tree.insert(20, RowID{1, 1}), Status::Ok);

    const PageID root_id = tree.rootPageID();
    NodeData data = tree.readNode(root_id).data();
    std::swap(data.keys[0], data.keys[1]);
    const BTreeNode corrupt = BTreeNode::fromData(std::move(data), pm.pageSize());
    Page page(pm.pageSize());
    CHECK_EQ(corrupt.serialize(page), Status::Ok);
    pm.write(root_id, page);

    const InvariantReport report = checkInvariants(tree);
    CHECK(!report.ok);
    CHECK(!report.error.empty());
}
