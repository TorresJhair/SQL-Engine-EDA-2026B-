#include "bench/Benchmark.h"
#include "bench/DataGen.h"
#include "index/BTree.h"
#include "index/TreePrinter.h"
#include "storage/HeapFile.h"
#include "storage/PageManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Options {
    size_t page_size = 4096;
    size_t n = 0;
    int demo = -1;
    bool page_size_set = false;
    bool full = false;
    std::string csv_path; // vacio = no exportar el benchmark a CSV
};

void printUsage(std::ostream& out) {
    out << "Uso: main [--demo 0|1|2|3|4|5|6] [--page-size BYTES] [--n N] [--full]\n"
           "            [--csv RUTA.csv]\n"
           "Sin --demo abre el menu interactivo. --demo 0 ejecuta las seis demos.\n"
           "--page-size aplica a las opciones 1-4; la demo 5 fuerza 256 B.\n"
           "--n acota las tuplas de la demo 1 y el N del benchmark; 0 = 100000.\n"
           "La demo 6 corre ambas curvas, salvo que se indique --page-size.\n"
           "--csv guarda las 12 filas de la demo 6 (ambas curvas, columna page_size)\n"
           "para graficarlas con scripts/plot_benchmark.py.\n";
}

bool parseSize(const std::string& text, size_t& value) {
    if (text.empty() || text.front() == '-') return false;
    try {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size() || parsed > static_cast<unsigned long long>(SIZE_MAX)) {
            return false;
        }
        value = static_cast<size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseOptions(int argc, char** argv, Options& options, bool& help_requested) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            help_requested = true;
            return true;
        }
        if (argument == "--full") {
            options.full = true;
            continue;
        }
        // --csv toma una RUTA, no un numero: se maneja antes del parseo numerico.
        if (argument == "--csv") {
            if (i + 1 >= argc) {
                std::cerr << "Falta la ruta del CSV para --csv\n";
                return false;
            }
            options.csv_path = argv[++i];
            continue;
        }
        if (argument != "--demo" && argument != "--page-size" && argument != "--n") {
            std::cerr << "Opcion desconocida: " << argument << '\n';
            return false;
        }
        if (i + 1 >= argc) {
            std::cerr << "Falta el valor para " << argument << '\n';
            return false;
        }

        const std::string value = argv[++i];
        size_t number = 0;
        if (!parseSize(value, number)) {
            std::cerr << "Valor numerico invalido para " << argument << ": " << value << '\n';
            return false;
        }
        if (argument == "--demo") {
            if (number > 6) {
                std::cerr << "--demo debe estar entre 0 y 6\n";
                return false;
            }
            options.demo = static_cast<int>(number);
        } else if (argument == "--page-size") {
            if (number < 47) {
                std::cerr << "--page-size debe ser al menos 47 B para crear nodos del arbol\n";
                return false;
            }
            options.page_size = number;
            options.page_size_set = true;
        } else {
            options.n = number;
        }
    }
    return true;
}

const char* statusName(Status status) {
    switch (status) {
        case Status::Ok: return "Ok";
        case Status::PageFull: return "PageFull";
        case Status::TupleTooLarge: return "TupleTooLarge";
        case Status::NotFound: return "NotFound";
        case Status::Corrupt: return "Corrupt";
        case Status::NodeOverflow: return "NodeOverflow";
        case Status::PreconditionFailed: return "PreconditionFailed";
    }
    return "Status desconocido";
}

bool reportStatus(const char* operation, Status status) {
    if (status == Status::Ok) return true;
    std::cerr << operation << " fallo: " << statusName(status) << '\n';
    return false;
}

bool resetDatabases() {
    for (const char* filename : {"data.db", "index.db"}) {
        std::error_code error;
        std::filesystem::remove(filename, error);
        if (error) {
            std::cerr << "No se pudo reiniciar " << filename << ": " << error.message() << '\n';
            return false;
        }
    }
    return true;
}

void printHexdump(const Page& page, size_t limit = 96) {
    const size_t count = std::min(page.size(), limit);
    for (size_t offset = 0; offset < count; offset += 16) {
        std::cout << std::setw(4) << std::setfill('0') << std::hex << offset << "  ";
        const size_t row_end = std::min(offset + 16, count);
        for (size_t byte = offset; byte < row_end; ++byte) {
            std::cout << std::setw(2) << static_cast<unsigned>(page[byte]) << ' ';
        }
        std::cout << '\n';
    }
    std::cout << std::dec << std::setfill(' ');
}

bool demoStorage(const Options& options) {
    PageManager pm;
    if (!reportStatus("Abrir data.db", PageManager::open("data.db", options.page_size, pm))) {
        return false;
    }
    HeapFile heap(pm);
    // --n acota cuantas tuplas se insertan. El default es 100000, que es el dataset
    // de la presentacion; con un N chico el arbol queda con pocas hojas y la demo 4 lo
    // dibuja en vez de colgarse renderizando. Con N = 0 vale el default.
    const size_t row_count = options.n != 0 ? options.n : 100000;
    RowID first_row{};
    for (size_t i = 0; i < row_count; ++i) {
        const int32_t key = static_cast<int32_t>(i + 1);
        RowID row_id{};
        const Status status = heap.insert(DataGen::datasetTuple(key), row_id);
        if (!reportStatus("Insertar tupla", status)) return false;
        if (i == 0) first_row = row_id;
    }
    heap.flush();

    std::cout << "Storage: " << row_count << " tuplas insertadas; " << pm.pageCount()
              << " paginas en data.db.\n"
              << "Primera tupla: RowID{" << first_row.pageID << ", " << first_row.slotID
              << "}; pagina en hexadecimal:\n";
    printHexdump(pm.read(first_row.pageID));
    return true;
}

bool demoPager(const Options& options) {
    PageManager pm;
    if (!reportStatus("Abrir data.db", PageManager::open("data.db", options.page_size, pm))) {
        return false;
    }
    HeapFile heap(pm);
    HeapFile::Scan scan = heap.scan();
    Tuple tuple;
    RowID row_id{};
    size_t tuple_count = 0;
    while (scan.next(tuple, row_id)) ++tuple_count;

    std::cout << "Pager: " << tuple_count << " tuplas recorridas; " << pm.pageCount()
              << " paginas en el archivo, " << pm.pageReads() << " lecturas de pagina.\n";
    return true;
}

bool demoBulkLoad(const Options& options, bool& index_built) {
    if (index_built) {
        std::cout << "El indice ya fue construido en esta ejecucion; use otra corrida.\n";
        return true;
    }

    PageManager heap_pm;
    PageManager index_pm;
    if (!reportStatus("Abrir data.db", PageManager::open("data.db", options.page_size, heap_pm)) ||
        !reportStatus("Abrir index.db", PageManager::open("index.db", options.page_size, index_pm))) {
        return false;
    }

    HeapFile heap(heap_pm);
    HeapFile::Scan scan = heap.scan();
    std::vector<std::pair<int32_t, RowID>> entries;
    Tuple tuple;
    RowID row_id{};
    while (scan.next(tuple, row_id)) {
        entries.emplace_back(tuple.at(0).asInt(), row_id);
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& left, const auto& right) {
                         return left.first < right.first;
                     });

    BTree tree;
    if (!reportStatus("Abrir index.db", BTree::open(index_pm, tree))) return false;
    tree.setVerbose(true);
    if (!reportStatus("bulkLoad", tree.bulkLoad(entries))) return false;
    index_built = true;
    std::cout << "Bulk load: " << tree.size() << " entradas; altura " << tree.height()
              << "; raiz page " << tree.rootPageID() << ".\n";
    return true;
}

// Graphviz es una dependencia OPCIONAL: si el binario "dot" no esta instalado, el .dot
// sigue siendo valido y se avisa como convertirlo a mano en vez de fallar la demo.
bool graphvizAvailable() {
    return std::system("command -v dot >/dev/null 2>&1") == 0;
}

// Convierte tree.dot a imagen con el binario `dot` de Graphviz. Un formato no soportado por
// la instalacion (p.ej. png sin el plugin de render) NO es un fallo: se distingue del .dot
// mal formado leyendo su codigo de salida y su stderr.
bool renderDotImage(const std::string& dot_path, const std::string& format,
                    const std::string& image_path, bool& unsupported_format) {
    unsupported_format = false;
    const std::string command = "dot -T" + format + " \"" + dot_path + "\" -o \"" +
                                image_path + "\" 2>/dev/null";
    const int status = std::system(command.c_str());
    if (status == 0) return true;
    // Graphviz responde 0 incluso cuando el formato no existe, asi que la unica senal
    // fiable es que NO se haya escrito la imagen.
    unsupported_format = !std::filesystem::exists(image_path);
    return !unsupported_format;
}

bool demoTree(const Options& options) {
    PageManager index_pm;
    if (!reportStatus("Abrir index.db",
                      PageManager::open("index.db", options.page_size, index_pm))) {
        return false;
    }
    BTree tree;
    if (!reportStatus("Abrir arbol", BTree::open(index_pm, tree))) return false;
    printTree(tree, std::cout);

    std::ofstream dot_file("tree.dot");
    if (!dot_file) {
        std::cerr << "No se pudo crear tree.dot\n";
        return false;
    }
    exportDot(tree, dot_file);
    dot_file.close();
    std::cout << "Arbol exportado a tree.dot (" << tree.height() << " niveles).\n";

    // El .dot es texto; la imagen la produce Graphviz. Sin `dot` en el PATH el archivo
    // sigue siendo valido, asi que esto NO es un error de la demo.
    if (!graphvizAvailable()) {
        std::cout << "Graphviz no esta instalado: no se genero la imagen. "
                     "Para verla, instalalo (sudo apt install graphviz) o convertila "
                     "a mano:\n  dot -Tsvg tree.dot -o tree.svg\n";
        return true;
    }

    bool ok = true;
    for (const auto& entry : {std::pair<const char*, const char*>{"svg", "tree.svg"},
                              {"png", "tree.png"}}) {
        const std::string format = entry.first;
        const std::string image_path = entry.second;
        std::error_code ignored;
        std::filesystem::remove(image_path, ignored); // que un resto viejo no confunda
        bool unsupported = false;
        if (renderDotImage("tree.dot", format, image_path, unsupported)) {
            std::cout << "Imagen generada: " << image_path << '\n';
        } else if (unsupported) {
            std::cout << "Graphviz de esta instalacion no soporta -T" << format
                      << " (falta su plugin); se omite " << image_path << ".\n";
        } else {
            std::cerr << "Graphviz fallo al convertir tree.dot a " << format << '\n';
            ok = false;
        }
    }
    return ok;
}

struct TempIndexFile {
    std::filesystem::path path;
    ~TempIndexFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

bool demoSplit() {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    TempIndexFile temp{std::filesystem::temp_directory_path() /
                       ("sql_engine_demo_split_" + std::to_string(timestamp) + ".idx")};
    PageManager pm;
    if (!reportStatus("Abrir indice temporal",
                      PageManager::open(temp.path.string(), 256, pm))) {
        return false;
    }
    BTree tree;
    if (!reportStatus("Abrir arbol", BTree::open(pm, tree))) return false;
    tree.setVerbose(true);
    for (int32_t key = 1; key <= 50; ++key) {
        if (!reportStatus("Insertar clave", tree.insert(key, RowID{1, static_cast<SlotID>(key)}))) {
            return false;
        }
    }
    std::cout << "Split demo (page_size 256): " << tree.size() << " claves; altura "
              << tree.height() << ".\n";
    printTree(tree, std::cout);
    return true;
}

bool demoBenchmark(const Options& options) {
    BenchmarkOptions benchmark_options;
    benchmark_options.page_size = options.page_size_set ? options.page_size : 0;
    benchmark_options.n = options.n;
    benchmark_options.full = options.full;
    benchmark_options.csv_path = options.csv_path;
    return reportStatus("Benchmark", Benchmark::run(std::cout, benchmark_options));
}

bool runDemo(int choice, const Options& options, bool& index_built) {
    switch (choice) {
        case 1: return demoStorage(options);
        case 2: return demoPager(options);
        case 3: return demoBulkLoad(options, index_built);
        case 4: return demoTree(options);
        case 5: return demoSplit();
        case 6: return demoBenchmark(options);
        default: return false;
    }
}

bool runAllDemos(const Options& options, bool& index_built) {
    for (int choice = 1; choice <= 6; ++choice) {
        std::cout << "\n=== Demo " << choice << " ===\n";
        if (!runDemo(choice, options, index_built)) return false;
    }
    return true;
}

void printMenu() {
    std::cout << "\n1. Storage   - insertar tuplas y mostrar una pagina\n"
                 "2. Pager     - recorrer el heap y mostrar contadores\n"
                 "3. Bulk load - construir el indice desde el heap\n"
                 "4. Arbol     - imprimir el B+ tree y exportar tree.dot (y su imagen)\n"
                 "5. Split     - mostrar splits con paginas de 256 B\n"
                 "6. Benchmark - Index Scan vs Full Table Scan (--csv lo exporta)\n"
                 "0. Presentacion completa\n"
                 "q. Salir\n> ";
}

int runInteractive(const Options& options) {
    bool index_built = false;
    std::string choice;
    while (true) {
        printMenu();
        if (!std::getline(std::cin, choice) || choice == "q" || choice == "Q") return 0;
        if (choice == "0") {
            if (!runAllDemos(options, index_built)) return 1;
        } else if (choice.size() == 1 && choice[0] >= '1' && choice[0] <= '6') {
            if (!runDemo(choice[0] - '0', options, index_built)) return 1;
        } else {
            std::cout << "Opcion invalida.\n";
        }
    }
}
}  // namespace

int main(int argc, char** argv) {
    Options options;
    bool help_requested = false;
    if (!parseOptions(argc, argv, options, help_requested)) {
        printUsage(std::cerr);
        return 2;
    }
    if (help_requested) {
        printUsage(std::cout);
        return 0;
    }
    if (!resetDatabases()) return 1;

    bool index_built = false;
    if (options.demo == 0) return runAllDemos(options, index_built) ? 0 : 1;
    if (options.demo > 0) return runDemo(options.demo, options, index_built) ? 0 : 1;
    return runInteractive(options);
}
