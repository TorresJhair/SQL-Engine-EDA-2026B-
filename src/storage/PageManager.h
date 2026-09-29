// src/storage/PageManager.h                   [P2]
#pragma once

#include "common/Types.h"

#include <cstdint>
#include <string>

struct FileMeta {
    uint32_t magic, page_size, page_count, root_page_id, height;
    uint64_t record_count;
};

class PageManager {
public:
    // Crea el archivo si no existe. Si existe, valida magic y page_size:
    // si no cuadran devuelve Status::Corrupt (y no sigue operando).
    static Status open(const std::string& path, size_t page_size, PageManager& out);

    PageID allocate();                    // nunca devuelve 0
    Page   read(PageID) const;            // cuenta en pageReads()  -> mutable uint64_t
    void   write(PageID, const Page&);    // cuenta en pageWrites()
    void   flush();

    FileMeta readMeta() const;            // NO cuenta en los contadores
    void     writeMeta(const FileMeta&);  // NO cuenta en los contadores

    uint64_t pageReads() const;
    uint64_t pageWrites() const;
    size_t   pageSize() const;
    uint32_t pageCount() const;
    void     resetCounters() const;

private:
    std::string path_;
    size_t page_size_ = 0;
    uint32_t page_count_ = 0;
    FileMeta meta_{};
    mutable uint64_t page_reads_ = 0;
    mutable uint64_t page_writes_ = 0;
};
