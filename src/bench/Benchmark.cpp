#include "bench/Benchmark.h"

#include "bench/DataGen.h"
#include "bench/Metrics.h"
#include "index/BTree.h"
#include "index/BTreeNode.h"
#include "storage/HeapFile.h"
#include "storage/PageManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if (values.size() % 2 != 0) return values[middle];
    return (values[middle - 1] + values[middle]) / 2.0;
}

const char* winnerName(Band::Winner winner) {
    switch (winner) {
        case Band::Winner::Scan: return "scan";
        case Band::Winner::Tie: return "empate";
        case Band::Winner::Index: return "indice";
    }
    return "?";
}

struct TempDirectory {
    std::filesystem::path path;
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
}  // namespace

std::vector<Band> Benchmark::crossover(size_t page_size, size_t tuples_per_page) {
    std::vector<Band> bands;
    if (tuples_per_page == 0) return bands;

    const size_t degree = BTreeNode::computeT(page_size);
    if (degree == 0) return bands;
    const size_t max_keys = 2 * degree - 1;
    const size_t max_children = 2 * degree;

    auto winnerFor = [&](size_t n) {
        size_t nodes = (n + max_keys - 1) / max_keys;
        size_t height = 1;
        while (nodes > 1) {
            nodes = (nodes + max_children - 1) / max_children;
            ++height;
        }
        const size_t scan_pages = (n + tuples_per_page - 1) / tuples_per_page;
        const size_t index_pages = height + 1;
        if (scan_pages < index_pages) return Band::Winner::Scan;
        if (scan_pages > index_pages) return Band::Winner::Index;
        return Band::Winner::Tie;
    };

    constexpr size_t kMaxN = 100000;
    Band::Winner current = winnerFor(1);
    size_t start = 1;
    for (size_t n = 2; n <= kMaxN; ++n) {
        const Band::Winner next = winnerFor(n);
        if (next != current) {
            bands.push_back({start, n - 1, current});
            start = n;
            current = next;
        }
    }
    bands.push_back({start, kMaxN, current});
    return bands;
}

Status Benchmark::run(std::ostream& out, const BenchmarkOptions& opt) {
    constexpr size_t kMaxN = 100000;
    const std::vector<size_t> canonical{20, 500, 1000, 5000, 10000, 100000};
    std::vector<size_t> page_sizes;
    if (opt.page_size == 0) {
        page_sizes = {4096, 256};
    } else if (opt.page_size == 4096 || opt.page_size == 256) {
        page_sizes = {opt.page_size};
    } else {
        return Status::PreconditionFailed;
    }
    const std::vector<size_t> sizes = opt.n == 0 ? canonical : std::vector<size_t>{opt.n};
    for (size_t n : sizes) {
        if (n == 0 || n > kMaxN) return Status::PreconditionFailed;
    }

    static std::atomic<uint64_t> sequence{0};
    const auto run_id = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef NDEBUG
    out << "Benchmark: tupla=27 B, esquema=INT key + VARCHAR(16), build con NDEBUG; "
           "configure RelWithDebInfo para medir\n";
#else
    out << "Benchmark: tupla=27 B, esquema=INT key + VARCHAR(16), build=Debug; "
           "configure RelWithDebInfo para medir\n";
#endif

    // El CSV opcional se escribe ACA, en el mismo bucle que imprime la tabla: asi
    // los numeros del archivo son los mismos que salen en pantalla. Si el archivo no
    // se puede abrir, el benchmark CONTINUA (el CSV es un extra, no un requisito).
    std::ofstream csv;
    if (!opt.csv_path.empty()) {
        csv.open(opt.csv_path);
        if (!csv) {
            std::cerr << "No se pudo abrir el CSV para escribir: " << opt.csv_path
                      << " (el benchmark sigue igual, sin exportar)\n";
        } else {
            csv << "page_size,N,altura,heap_data_pages,heap_file_pages,"
                   "index_reads,full_reads,ratio,build_ms,index_ms,full_ms\n";
        }
    }

    for (size_t page_size : page_sizes) {
        const size_t tuples_per_page = (page_size - 8) / 31;
        out << "\nCurva page_size=" << page_size << " B; tuplas/pagina=" << tuples_per_page << '\n';
        out << "N | altura | paginas heap (archivo) | lecturas indice | lecturas full | ratio | build ms | index ms | full ms\n";

        for (size_t n : sizes) {
            const size_t repeats = opt.repeats != 0 ? opt.repeats : (n <= 10000 ? 5 : 1);
            TempDirectory temp{std::filesystem::temp_directory_path() /
                               ("sql_engine_p2_" + std::to_string(run_id) + "_" +
                                std::to_string(sequence.fetch_add(1)))};
            std::filesystem::create_directories(temp.path);

            PageManager heap_pm;
            PageManager index_pm;
            Status status = PageManager::open((temp.path / "data.db").string(), page_size, heap_pm);
            if (status != Status::Ok) return status;
            status = PageManager::open((temp.path / "index.db").string(), page_size, index_pm);
            if (status != Status::Ok) return status;

            HeapFile heap(heap_pm);
            std::vector<std::pair<int32_t, RowID>> entries = DataGen::sorted(n);
            for (size_t i = 0; i < n; ++i) {
                RowID rid{};
                status = heap.insert(DataGen::datasetTuple(entries[i].first), rid);
                if (status != Status::Ok) return status;
                entries[i].second = rid;
            }
            heap.flush();

            BTree tree;
            status = BTree::open(index_pm, tree);
            if (status != Status::Ok) return status;
            const auto build_start = std::chrono::steady_clock::now();
            status = tree.bulkLoad(entries);
            const double build_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - build_start).count();
            if (status != Status::Ok) return status;

            Metrics metrics(heap_pm, index_pm);
            std::vector<double> index_times;
            std::vector<double> full_times;
            uint64_t index_reads = 0;
            uint64_t full_reads = 0;
            const int32_t key = static_cast<int32_t>(n);
            for (size_t repetition = 0; repetition < repeats; ++repetition) {
                metrics.resetCounts();
                metrics.start();
                const std::vector<Tuple> indexed = tree.indexScan(key, heap);
                index_times.push_back(metrics.elapsedMs());
                index_reads = metrics.totalPageReads();
                if (indexed.size() != 1 || indexed.front().at(0).asInt() != key) {
                    return Status::Corrupt;
                }

                metrics.resetCounts();
                metrics.start();
                HeapFile::Scan scan = heap.scan();
                Tuple tuple;
                RowID rid{};
                bool found = false;
                while (scan.next(tuple, rid)) {
                    if (tuple.at(0).asInt() == key) found = true;
                }
                full_times.push_back(metrics.elapsedMs());
                full_reads = metrics.totalPageReads();
                if (!found) return Status::Corrupt;
            }

            const size_t heap_data_pages = heap_pm.pageCount() - 1;
            const double ratio = index_reads == 0 ? 0.0 : static_cast<double>(full_reads) / index_reads;
            // Misma regla que la tabla: sin --full, N = 100 000 no publica tiempos. En
            // el CSV esos dos campos quedan VACIOS en vez de inventar un numero.
            const bool times_omitted = n == 100000 && !opt.full;
            if (csv) {
                csv << page_size << ',' << n << ',' << tree.height() << ','
                    << heap_data_pages << ',' << heap_pm.pageCount() << ','
                    << index_reads << ',' << full_reads << ','
                    << std::fixed << std::setprecision(1) << ratio << ','
                    << std::setprecision(3) << build_ms << ',';
                if (times_omitted) {
                    csv << ',' << '\n';
                } else {
                    csv << std::setprecision(3) << median(index_times) << ','
                        << median(full_times) << '\n';
                }
            }
            out << n << " | " << tree.height() << " | " << heap_data_pages << " ("
                << heap_pm.pageCount() << ") | " << index_reads << " | " << full_reads
                << " | " << std::fixed << std::setprecision(1) << ratio << "x | "
                << std::setprecision(3) << build_ms << " | ";
            if (times_omitted) {
                out << "omitido | omitido\n";
            } else {
                out << median(index_times) << " | " << median(full_times) << '\n';
            }
        }

        out << "Cruce calculado:\n";
        for (const Band& band : crossover(page_size, tuples_per_page)) {
            out << "  N=" << band.from;
            if (band.to != band.from) out << ".." << band.to;
            out << ": " << winnerName(band.winner) << '\n';
        }
    }
    return Status::Ok;
}
