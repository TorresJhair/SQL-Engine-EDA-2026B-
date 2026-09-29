// src/common/Types.h                          [P1]
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

using PageID = uint32_t;               // 0 = "ninguna pagina" (la pagina 0 es de metadatos)
using SlotID = uint16_t;
using Page   = std::vector<uint8_t>;   // exactamente page_size bytes

struct RowID {
    PageID pageID;
    SlotID slotID;
};

// Definidos INLINE y no declarados: son tres lineas, y si quedaran solo declarados el
// primer uso daria "undefined reference" en el link sin avisar en el build de los stubs.
// Inline en el header es ademas lo correcto para una funcion de este tamano, asi que no
// cuesta nada de binario.
inline bool operator==(const RowID& a, const RowID& b) {
    return a.pageID == b.pageID && a.slotID == b.slotID;
}

// El orden es por pagina y luego por slot: es el orden en que el Full Table Scan recorre
// el heap, y el benchmark lo usa para comparar las dos listas sin ordenarlas.
inline bool operator<(const RowID& a, const RowID& b) {
    if (a.pageID != b.pageID) return a.pageID < b.pageID;
    return a.slotID < b.slotID;
}

// PreconditionFailed = el que llama rompio el contrato documentado (tupla sin campos,
// bulkLoad sobre arbol no vacio o con entrada desordenada). NO es un assert: es un Status
// justamente para que un test pueda comprobarlo sin fork y sin depender de -DNDEBUG.
enum class Status { Ok, PageFull, TupleTooLarge, NotFound, Corrupt, NodeOverflow, PreconditionFailed };

enum class ValueKind : uint8_t { Int = 0, VarChar = 1 };

// sizeof(RowID) == 8 por el padding del struct; en la pagina ocupa 6 (u32 + u16 sin alinear).
// El serializador escribe los campos a mano, nunca memcpy del struct.
static_assert(sizeof(RowID) == 8, "RowID son 8 B en memoria por el padding del struct");
static_assert(sizeof(PageID) == 4, "PageID es u32");
static_assert(sizeof(SlotID) == 2, "SlotID es u16");
// Si estos dos fallan, el serialized size de un RowID en la pagina (6 B) tambien seria
// incorrecto y t saldria mal: el serialized size NO es sizeof(RowID).
static_assert(sizeof(PageID) + sizeof(SlotID) == 6, "RowID serializado ocupa 6 B, no sizeof(RowID)");
