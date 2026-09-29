// src/bench/Metrics.h                         [P2]
#pragma once

#include "common/Types.h"
#include "storage/PageManager.h"

#include <cstdint>

// Agrega los dos PageManager y el reloj. Lo usa Benchmark (P2), que es suyo.
class Metrics {
public:
    Metrics(const PageManager& heap_pm, const PageManager& index_pm);

    uint64_t heapPageReads() const;
    uint64_t indexPageReads() const;
    uint64_t totalPageReads() const;

    void   resetCounts();
    void   start();
    double elapsedMs() const;           // std::chrono::steady_clock
};
