#include "bench/Metrics.h"

Metrics::Metrics(const PageManager& heap_pm, const PageManager& index_pm)
    : heap_pm_(heap_pm), index_pm_(index_pm) {}

uint64_t Metrics::heapPageReads() const { return heap_pm_.pageReads(); }
uint64_t Metrics::indexPageReads() const { return index_pm_.pageReads(); }
uint64_t Metrics::totalPageReads() const { return heapPageReads() + indexPageReads(); }

void Metrics::resetCounts() {
    heap_pm_.resetCounters();
    index_pm_.resetCounters();
}

void Metrics::start() { started_ = std::chrono::steady_clock::now(); }

double Metrics::elapsedMs() const {
    const auto elapsed = std::chrono::steady_clock::now() - started_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}
