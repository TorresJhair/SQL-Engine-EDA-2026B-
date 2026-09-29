#include "TestHarness.h"

#include "bench/DataGen.h"
#include "bench/Metrics.h"
#include "storage/HeapFile.h"
#include "storage/PageManager.h"
#include "storage/Value.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct TempDirectory {
    std::filesystem::path path;

    TempDirectory() {
        static std::atomic<uint64_t> sequence{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("sql_engine_page_manager_" + std::to_string(timestamp) + "_" +
                std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void corruptFirstByte(const std::filesystem::path& path) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    const char zero = 0;
    file.seekp(0);
    file.write(&zero, 1);
    file.flush();
}
}  // namespace

TEST(page_manager, allocate_read_write_y_contadores) {
    TempDirectory temp;
    const auto path = (temp.path / "pages.db").string();
    PageManager pm;
    CHECK_EQ(PageManager::open(path, 256, pm), Status::Ok);
    CHECK_EQ(pm.pageSize(), size_t(256));
    CHECK_EQ(pm.pageCount(), uint32_t(1));
    CHECK(pm.readMeta().magic != 0);

    const PageID id = pm.allocate();
    CHECK_EQ(id, PageID(1));
    CHECK_EQ(pm.pageCount(), uint32_t(2));
    CHECK(std::filesystem::file_size(path) == 512);
    CHECK_EQ(pm.pageReads(), uint64_t(0));
    CHECK_EQ(pm.pageWrites(), uint64_t(0));

    const Page expected(256, 0x5A);
    pm.write(id, expected);
    CHECK_EQ(pm.pageWrites(), uint64_t(1));
    CHECK(pm.read(id) == expected);
    CHECK_EQ(pm.pageReads(), uint64_t(1));
    pm.flush();

    PageManager reopened;
    CHECK_EQ(PageManager::open(path, 256, reopened), Status::Ok);
    CHECK(reopened.read(id) == expected);
    CHECK_EQ(reopened.pageReads(), uint64_t(1));
}

TEST(page_manager, metrics_agrega_page_managers_y_reinicia_contadores) {
    TempDirectory temp;
    PageManager heap_pm;
    PageManager index_pm;
    CHECK_EQ(PageManager::open((temp.path / "heap.db").string(), 256, heap_pm), Status::Ok);
    CHECK_EQ(PageManager::open((temp.path / "index.db").string(), 256, index_pm), Status::Ok);

    const PageID heap_page = heap_pm.allocate();
    const PageID index_page = index_pm.allocate();
    const Page zeros(256, 0);
    heap_pm.write(heap_page, zeros);
    index_pm.write(index_page, zeros);
    heap_pm.read(heap_page);
    index_pm.read(index_page);
    index_pm.read(index_page);

    Metrics metrics(heap_pm, index_pm);
    CHECK_EQ(metrics.heapPageReads(), uint64_t(1));
    CHECK_EQ(metrics.indexPageReads(), uint64_t(2));
    CHECK_EQ(metrics.totalPageReads(), uint64_t(3));
    metrics.start();
    CHECK(metrics.elapsedMs() >= 0.0);
    metrics.resetCounts();
    CHECK_EQ(metrics.totalPageReads(), uint64_t(0));
    CHECK_EQ(heap_pm.pageWrites(), uint64_t(0));
    CHECK_EQ(index_pm.pageWrites(), uint64_t(0));
}

TEST(page_manager, readmeta_writemeta_y_flush_no_cuentan_accesos) {
    TempDirectory temp;
    const auto path = (temp.path / "meta.db").string();
    PageManager pm;
    CHECK_EQ(PageManager::open(path, 4096, pm), Status::Ok);

    FileMeta meta = pm.readMeta();
    meta.root_page_id = 0;
    meta.height = 0;
    meta.record_count = 73;
    pm.writeMeta(meta);
    CHECK_EQ(pm.readMeta().record_count, uint64_t(73));
    pm.flush();
    CHECK_EQ(pm.pageReads(), uint64_t(0));
    CHECK_EQ(pm.pageWrites(), uint64_t(0));

    PageManager reopened;
    CHECK_EQ(PageManager::open(path, 4096, reopened), Status::Ok);
    CHECK_EQ(reopened.readMeta().record_count, uint64_t(73));
}

TEST(page_manager, abrir_con_otro_page_size_da_corrupt) {
    TempDirectory temp;
    const auto path = (temp.path / "wrong-size.db").string();
    PageManager original;
    CHECK_EQ(PageManager::open(path, 4096, original), Status::Ok);
    PageManager wrong_size;
    CHECK_EQ(PageManager::open(path, 256, wrong_size), Status::Corrupt);
}

TEST(page_manager, magic_corrupto_da_corrupt) {
    TempDirectory temp;
    const auto path = temp.path / "wrong-magic.db";
    PageManager original;
    CHECK_EQ(PageManager::open(path.string(), 4096, original), Status::Ok);
    corruptFirstByte(path);

    PageManager corrupt;
    CHECK_EQ(PageManager::open(path.string(), 4096, corrupt), Status::Corrupt);
}

TEST(page_manager, heap_cursor_escribe_una_vez_por_insert_y_flush_persiste_cantidad) {
    TempDirectory temp;
    const auto path = (temp.path / "heap.db").string();
    PageManager pm;
    CHECK_EQ(PageManager::open(path, 4096, pm), Status::Ok);
    HeapFile heap(pm);

    RowID first{};
    for (int32_t key = 1; key <= 1000; ++key) {
        RowID rid{};
        CHECK_EQ(heap.insert(DataGen::datasetTuple(key), rid), Status::Ok);
        if (key == 1) first = rid;
    }
    CHECK_EQ(heap.recordCount(), size_t(1000));
    CHECK_EQ(heap.lastInsertPageID(), PageID(8));
    CHECK_EQ(pm.pageCount(), uint32_t(9));
    CHECK_EQ(pm.pageWrites(), uint64_t(1000));
    CHECK_EQ(pm.pageReads(), uint64_t(999));

    Tuple tuple;
    CHECK_EQ(heap.get(first, tuple), Status::Ok);
    CHECK_EQ(tuple.at(0).asInt(), int32_t(1));
    CHECK_EQ(heap.get(first, tuple), Status::Ok);
    CHECK_EQ(pm.pageReads(), uint64_t(1001));

    heap.flush();
    PageManager reopened_pm;
    CHECK_EQ(PageManager::open(path, 4096, reopened_pm), Status::Ok);
    HeapFile reopened_heap(reopened_pm);
    CHECK_EQ(reopened_heap.recordCount(), size_t(1000));
}

TEST(page_manager, scan_recorrer_todas_las_tuplas_y_paginas_de_datos) {
    TempDirectory temp;
    const auto path = (temp.path / "scan.db").string();
    PageManager pm;
    CHECK_EQ(PageManager::open(path, 4096, pm), Status::Ok);
    HeapFile heap(pm);
    constexpr int32_t kRecordCount = 100000;
    for (int32_t key = 1; key <= kRecordCount; ++key) {
        RowID rid{};
        CHECK_EQ(heap.insert(DataGen::datasetTuple(key), rid), Status::Ok);
    }
    pm.resetCounters();

    HeapFile::Scan scan = heap.scan();
    Tuple tuple;
    RowID rid{};
    size_t count = 0;
    int32_t previous_key = 0;
    while (scan.next(tuple, rid)) {
        const int32_t key = tuple.at(0).asInt();
        CHECK(key > previous_key);
        previous_key = key;
        ++count;
    }
    CHECK_EQ(count, size_t(kRecordCount));
    CHECK_EQ(pm.pageReads(), uint64_t(764));
}

TEST(page_manager, rechaza_tupla_que_no_cabe_en_una_pagina_vacia) {
    TempDirectory temp;
    const auto path = (temp.path / "too-large.db").string();
    PageManager pm;
    CHECK_EQ(PageManager::open(path, 256, pm), Status::Ok);
    HeapFile heap(pm);
    Tuple too_large;
    too_large.append(Value(std::string(245, 'x')));
    RowID rid{};
    CHECK_EQ(heap.insert(too_large, rid), Status::TupleTooLarge);
    CHECK_EQ(pm.pageCount(), uint32_t(1));
    CHECK_EQ(pm.pageWrites(), uint64_t(0));
}

namespace suites {
void page_manager() {
    // Los TEST() se registran automaticamente; ver TestHarness.h.
}
}  // namespace suites
