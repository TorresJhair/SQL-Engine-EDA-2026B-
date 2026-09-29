// src/index/BTreeNode.h                       [P3]
#pragma once

#include "common/Types.h"

#include <cstdint>
#include <vector>

// CONGELADO (regla 19): NodeData, este layout de 16 B y estas firmas solo cambian
// con Issue de las cuatro personas. Los cuerpos de BTreeNode son libres.
struct NodeData {
    bool                 is_leaf;
    uint16_t             t;                     // el grado con el que se creo el nodo;
                                                // vive tambien en el header de la pagina,
                                                // asi deserialize puede validar 2t-1
    PageID               self;
    PageID               next_leaf;            // 0 = ninguna
    std::vector<int32_t> keys;
    std::vector<PageID>  children;             // solo interno: keys.size()+1 entradas
    std::vector<RowID>   rowids;               // solo hoja: keys.size() entradas
};

// Lo que se congela con static_assert son los TIPOS: si alguien los cambia, el build
// se cae en la rama contract y no dos semanas despues en el de otra persona.
static_assert(std::is_same_v<decltype(NodeData::t), uint16_t>);
static_assert(std::is_same_v<decltype(NodeData::self), PageID>);
static_assert(std::is_same_v<decltype(NodeData::next_leaf), PageID>);
static_assert(std::is_same_v<decltype(NodeData::is_leaf), bool>);
static_assert(std::is_same_v<decltype(NodeData::keys), std::vector<int32_t>>);
static_assert(std::is_same_v<decltype(NodeData::children), std::vector<PageID>>);
static_assert(std::is_same_v<decltype(NodeData::rowids), std::vector<RowID>>);

// Header fijo de 16 B: [isLeaf:1][keyCount:2][selfPageID:4][nextLeafPageID:4][t:2][reservado:3]
// NO es un struct que se copie: son 16 bytes que escribe a mano serialize() campo por
// campo en little-endian, asi que lo que se congela es el TAMANO, no un offsetof.
constexpr size_t NODE_HEADER_SIZE = 16;       // isLeaf1+keyCount2+self4+next4+t2+res3
static_assert(NODE_HEADER_SIZE == 16);

struct SplitResult;

class BTreeNode {
public:
    static uint16_t computeT(size_t page_size);   // min(tHoja, tInterno), con SLACK=1
                                                   // 204 @4096, 12 @256
    static BTreeNode makeLeaf(PageID self, size_t page_size);
    static BTreeNode makeInternal(PageID self, size_t page_size);

    bool     isLeaf() const;
    uint16_t keyCount() const;
    uint16_t t() const;
    uint32_t childCount() const;                  // = keyCount + 1 (solo interno)
    int32_t  keyAt(uint16_t i) const;
    PageID   childAt(uint16_t i) const;           // interno
    RowID    rowIDAt(uint16_t i) const;           // hoja
    PageID   nextLeaf() const;                    // 0 = ultima hoja / nodo interno

    struct SearchHit { uint16_t idx; bool found; };
    SearchHit searchInNode(int32_t key) const;    // lowerBound + found
    uint16_t  upperBound(int32_t key) const;      // primera posicion con clave > key

    void insertInNode(int32_t key, PageID right_child);  // interno (hasta 2t en memoria)
    void insertInNode(int32_t key, const RowID&);        // hoja: tras las claves iguales

    // PURO: sin I/O ni asignacion de paginas. *this queda como izquierdo y
    // right.self queda grabado con right_page_id.
    SplitResult split(PageID right_page_id);

    // Status, NO assert: el desborde es un error esperado (insertInNode deja entrar
    // hasta 2t claves a proposito) y tiene que seguir detectandose en Release, donde
    // -DNDEBUG borra los asserts. NodeOverflow = keyCount > 2t-1, no se escribe nada.
    Status serialize(Page& out) const;

    // NO hay campo magic en el header: la corrupcion se detecta por invariantes.
    // Status::Corrupt, no assert ni basura, si: t != computeT(page_size) - o sea,
    // una pagina que no es de nodo (todo ceros, o el header de una tupla del heap) -,
    // isLeaf != 0 && isLeaf != 1, keyCount > 2t-1, o el area de datos se sale de la pagina.
    static Status deserialize(const Page&, BTreeNode& out);

    const NodeData& data() const;
    static BTreeNode fromData(NodeData, size_t page_size);
    void setNextLeafPageID(PageID);

private:
    NodeData d_;
    size_t   page_size_ = 0;
};

// hoja:    promoted_key = right.keyAt(0)  (se COPIA hacia arriba)
// interno: promoted_key = clave central   (SUBE, no queda en ningun hijo)
struct SplitResult { BTreeNode right; int32_t promoted_key; };
