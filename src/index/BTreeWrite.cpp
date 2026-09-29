// src/index/BTreeWrite.cpp    [P3]
// Lado de escritura del arbol: open, findLeaf, insert con propagacion de Status y el
// log [SPLIT]/[ALTURA]. Es el UNICO .cpp donde se escribe el arbol: bulkLoad (P4) vive
// en BTreeRead.cpp y reutiliza writeNode()/readNode() declarados en BTree.h.
//
// Orden de implementacion de la rama btree-node (un commit por fase):
//   1. open / readNode / writeNode / rootPageID / height / pages / setVerbose
//   2. findLeaf con la politica de duplicados y insert dentro de una hoja
//   3. split + propagacion hasta la raiz, con logSplit / logHeight

#include "index/BTree.h"

#include <stdexcept>
#include <string>

Status BTree::open(PageManager& index_pm, BTree& out) {
    // Se abre "en su sitio": si algo falla, out queda como estaba (se restaura el puntero),
    // asi un out ya abierto no pierde su arbol por intentar abrir uno roto.
    PageManager* const previous_pm = out.pm_;
    const bool previous_verbose = out.verbose_;
    out.pm_ = &index_pm;
    out.verbose_ = false;

    const FileMeta meta = index_pm.readMeta();

    if (meta.root_page_id != 0) {
        // Raiz presente: la valida y SOLO la lee. Si no es un nodo legible, Corrupt y el
        // arbol queda sin abrir: no se escribe nada en el camino de falla.
        if (meta.height == 0) {                          // raiz sin altura: meta inconsistente
            out.pm_ = previous_pm;
            out.verbose_ = previous_verbose;
            return Status::Corrupt;
        }
        BTreeNode root;
        const Status s =
            BTreeNode::deserialize(index_pm.read(meta.root_page_id), root);
        if (s != Status::Ok || root.data().self != meta.root_page_id) {
            out.pm_ = previous_pm;
            out.verbose_ = previous_verbose;
            return s != Status::Ok ? s : Status::Corrupt;
        }
        return Status::Ok;
    }

    // Sin raiz valida: hoja raiz con 0 claves. Nunca 0 nodos, porque buscar en un arbol
    // de 0 paginas no tiene hoja a donde bajar y height() devolveria 0.
    const PageID root_id = index_pm.allocate();         // page_count primero: allocate
                                                        // hace writeMeta con root todavia 0
    const BTreeNode root = BTreeNode::makeLeaf(root_id, index_pm.pageSize());
    const Status s = out.writeNode(root);               // out.pm_ ya apunta al index_pm
    if (s != Status::Ok) {                              // hoja vacia: no puede fallar
        out.pm_ = previous_pm;
        out.verbose_ = previous_verbose;
        return s;
    }

    FileMeta updated = index_pm.readMeta();             // conserve record_count y page_count
    updated.root_page_id = root_id;
    updated.height = 1;
    index_pm.writeMeta(updated);
    return Status::Ok;
}

size_t BTree::height() const {
    return pm_ == nullptr ? 0 : static_cast<size_t>(pm_->readMeta().height);
}

PageID BTree::rootPageID() const {
    // La raiz vive en FileMeta y PageManager la tiene cacheada: leerla no es una lectura
    // de pagina ni mueve los contadores del benchmark.
    return pm_ == nullptr ? 0 : pm_->readMeta().root_page_id;
}

const PageManager& BTree::pages() const {
    if (pm_ == nullptr) throw std::logic_error("BTree::pages(): open() no fue llamado");
    return *pm_;
}

void BTree::setVerbose(bool on) { verbose_ = on; }

BTreeNode BTree::readNode(PageID page_id) const {
    const Page page = pm_->read(page_id);
    BTreeNode node;
    const Status s = BTreeNode::deserialize(page, node);
    if (s != Status::Ok) {
        throw std::runtime_error("BTree::readNode: pagina " + std::to_string(page_id) +
                                 " corrupta (deserialize -> " +
                                 std::string(s == Status::Corrupt ? "Corrupt" : "error") + ")");
    }
    return node;
}

Status BTree::writeNode(const BTreeNode& node) const {
    Page page(pm_->pageSize());
    const Status s = node.serialize(page);              // NodeOverflow si > 2t-1, sin abortar
    if (s != Status::Ok) return s;
    pm_->write(node.data().self, page);
    return Status::Ok;
}
