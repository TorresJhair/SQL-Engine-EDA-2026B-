// src/index/BTree.h                           [P3 redacta, P4 implementa la parte de lectura]
#pragma once

#include "common/Types.h"
#include "index/BTreeNode.h"
#include "storage/HeapFile.h"
#include "storage/PageManager.h"
#include "storage/Tuple.h"

#include <cstdint>
#include <utility>
#include <vector>

enum class Bias { Left, Right };          // Left: search (lowerBound) - Right: insert (upperBound)

class BTree {
public:
    // Lee FileMeta. Si el archivo no tiene raiz valida, crea una hoja raiz con 0 claves
    // (nunca 0 nodos). Si la hay, la usa tal cual.
    //
    // Devuelve Status::Corrupt y NO modifica el archivo si la raiz referenciada por
    // FileMeta no es un nodo valido (pagina ilegible, t que no es computeT(page_size),
    // keyCount > 2t-1, o self que no apunta a su propia pagina).
    static Status open(PageManager& index_pm, BTree& out);

    // insert y bulkLoad devuelven Status porque el serializador ya no aborta: si un nodo
    // no entra, hay que propagarlo en vez de perderlo. Quien los llama DEBE mirarlo.
    Status insert(int32_t key, const RowID&);                         // [P3] BTreeWrite.cpp

    // Precondiciones, como Status y NO como assert (mismo motivo que §7: sin fork no se
    // puede comprobar un assert, y con -DNDEBUG -- el build donde se mide -- no existe).
    // Devuelve PreconditionFailed y NO modifica el arbol si: la entrada no es no decreciente,
    // o el arbol no esta vacio. Los tests lo comprueban ademas con size() sin cambios.
    Status bulkLoad(const std::vector<std::pair<int32_t, RowID>>& sorted); // [P4]

    std::vector<RowID> search(int32_t key) const;                         // [P4]
    std::vector<Tuple> indexScan(int32_t key, HeapFile&) const;          // [P4] indexScan
    size_t size() const;                                                 // [P4] recorre hojas
    size_t height() const;                                               // [P3] niveles; hoja raiz = 1
    void   setVerbose(bool);                                             // [P3]

    PageID    rootPageID() const;                                        // [P3] para TreePrinter (P1)
    // Lee y valida la pagina del nodo (BTreeNode::deserialize). Devuelve por valor: si la
    // pagina esta corrupta NO hay Status que devolver en esta firma, asi que lanza
    // std::runtime_error en vez de devolver basura. Los errores esperados de dominio
    // (overflow, precondicion) viajan en Status; Corrupt aqui es una pagina que nosotros
    // escribimos y que ya no cuadra.
    BTreeNode readNode(PageID) const;                                    // [P3] para TreePrinter (P1)
    const PageManager& pages() const;                                   // [P3] para Benchmark (P2)

private:
    PageID findLeaf(int32_t key, Bias, std::vector<PageID>* ancestors) const; // [P3]

    // Serializa el nodo y lo escribe en su propia pagina (self), propagando el Status del
    // serializador. Es el mismo camino que usa bulkLoad (P4) para persistir los nodos que
    // construye en memoria: no reimplementes la escritura de un nodo en otro archivo.
    Status writeNode(const BTreeNode&) const;                           // [P3] BTreeWrite.cpp

    // UNICOS puntos donde se escribe el log. Ningun otro archivo imprime [SPLIT]/[ALTURA].
    void logSplit(const BTreeNode& left, const BTreeNode& right,
                  int32_t promoted_key) const;                           // [P3] bajo verbose_
    void logHeight(size_t old_h, size_t new_h, PageID new_root) const;   // [P3] bajo verbose_

    // Estado del arbol, fijado por open(). El contrato congelado de la rama contract no
    // traia ni un puntero al archivo (solo verbose_), asi que sin esto open() no tiene
    // donde guardar la raiz ni readNode() donde leer. Se anadio en la rama btree-node y
    // se aviso a P4 en el Issue de la costura P3/P4.
    PageManager* pm_ = nullptr;

    bool verbose_ = false;
};
