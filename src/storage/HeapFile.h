// src/storage/HeapFile.h                      [P2]
#pragma once

#include "common/Types.h"
#include "storage/PageManager.h"
#include "storage/Tuple.h"

#include <cstdint>
#include <vector>

// CURSOR: insert solo mira lastInsertPageID_. Si no cabe, allocate() y avanza.
// Nunca reexamina paginas anteriores -> O(1) amortizado.
// ESCRIBE la pagina en cada insert (no difiere al flush): 1 tupla = 1 escritura.
class HeapFile {
public:
    explicit HeapFile(PageManager&);

    // TupleTooLarge si no cabe en pagina vacia; propaga PreconditionFailed si la tupla
    // tiene 0 campos (viene de Tuple::serializeTo, que ya devuelve Status).
    Status insert(const Tuple&, RowID& out);
    Status get(const RowID&, Tuple& out);
    void flush();                                  // persiste record_count en FileMeta

    class Scan {                               // Full Table Scan: recorre TODAS las paginas,
    public:                                    // deserializa cada tupla. La clave es el
                                               // campo 0 y la compara el que la usa, no Scan.
        Scan();
        bool next(Tuple& out, RowID& rid);     // NO se detiene en la primera coincidencia.
        void reset();

    private:
        friend class HeapFile;
        explicit Scan(PageManager*);
        PageManager* pm_ = nullptr;
        PageID page_id_ = 1;
        SlotID slot_id_ = 0;
        Page current_page_;
        bool page_loaded_ = false;
    };

    Scan   scan();
    PageID lastInsertPageID() const;           // visible para el test del cursor
    size_t recordCount() const;

private:
    PageManager& pm_;
    PageID lastInsertPageID_ = 0;               // el cursor
    uint64_t record_count_ = 0;
};

// Las tuplas por pagina NO son constante: size_t tuplesPerPage(size_t page_size);
//   = (page_size - 8) / 31 con el dataset del benchmark.  131 con 4096 B, 8 con 256 B.
