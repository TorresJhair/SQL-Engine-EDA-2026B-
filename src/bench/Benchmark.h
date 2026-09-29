// src/bench/Benchmark.h                       [P2]
#pragma once

#include "common/Types.h"

#include <cstddef>
#include <ostream>
#include <vector>

// Las DOS curvas (4096 y luego 256) sobre base temporal propia, con reset entre
// curvas. Es lo UNICO que mide: main.cpp (P4) solo lo invoca y lo presenta.
// Benchmark NO lee argv: --page-size / --n / --full los parsea el menu (P4) y
// llegan en BenchmarkOptions. main.cpp no inventa ni un valor de medicion.
struct BenchmarkOptions {
    size_t page_size = 0;     // 0 = las dos curvas (4096 y 256); otro = esa sola
    size_t n         = 0;     // 0 = los 6 N canonicos (20, 500, 1e3, 5e3, 1e4, 1e5)
                              //     > 0 = una sola corrida, en LAS DOS curvas, para depurar
    bool   full      = false; // en vivo salen las 12 filas sin el tiempo de N = 100 000
                              // (el punto caro del riesgo 25); --full lo agrega, y con el
                              // las 12 filas salen con R repeticiones, que es la tabla de
                              // docs/benchmark-results.md
    size_t repeats   = 0;     // 0 = el protocolo de R: 5 hasta N = 1e4, 1 en N = 1e5
};

// Una banda contigua de N en la que gana lo mismo. La isla de empate de N = 24 a
// 256 B es una banda PROPIA, por eso esto es una lista y no tres campos sueltos.
struct Band { size_t from, to; enum class Winner { Scan, Tie, Index }; };

class Benchmark {
public:
    // Imprime el bloque de la opcion 6: 27 B/tupla y tuplas/pagina, las dos curvas
    // con altura y ratio, las paginas del archivo con ceil(N/tuplasPorPagina)+1, el
    // punto de cruce de cada curva y el tiempo de construccion del indice por
    // separado del tiempo de busqueda. El tiempo de construccion NO entra en el
    // tiempo de busqueda; R solo afecta al tiempo, nunca a los recuentos.
    // Status: bulkLoad exige arbol vacio y aca se construye una base nueva por
    // curva, asi que un PreconditionFailed es un bug de esta base, no del dato.
    static Status run(std::ostream& out, const BenchmarkOptions& opt = {});

    // Recorre N = 1..100000 y clasifica cada valor en escaneo | empate | indice.
    // NO esta escrito a mano: si cambian computeT o la formula de altura, el cruce
    // cambia con ellas. No construye ningun arbol: es aritmetica sobre
    // BTreeNode::computeT(page_size), y por eso no depende de P3 para correr.
    // El tope de 100000 esta justificado en §1.
    static std::vector<Band> crossover(size_t page_size, size_t tuples_per_page);
};
