// src/bench/DataGen.h                         [P1]
#pragma once

#include "common/Types.h"
#include "storage/Tuple.h"

#include <cstdint>
#include <utility>
#include <vector>

// Dataset del benchmark: 2 campos, INT key + VARCHAR name de 16 bytes -> tupla de 27 B.
// sorted() es la unica que puede darle la entrada a bulkLoad (ordenada no decreciente).
class DataGen {
public:
    static Tuple        datasetTuple(int32_t key);   // (INT key, VARCHAR de 16 B)
    static const char*  nameFor(int32_t key);        // 16 bytes, deterministico
    static std::vector<std::pair<int32_t, RowID>>
                       sorted(size_t n);            // claves 1..n, en orden
    static std::vector<std::pair<int32_t, RowID>>
                       randomKeys(size_t n, size_t seed);  // para el test de estres
    static std::vector<Tuple>  heap(size_t n);       // n tuplas del dataset
};
