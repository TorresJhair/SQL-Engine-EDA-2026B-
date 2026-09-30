// src/index/BTreeWrite.cpp    [P3]
// Lado de escritura del arbol: open, findLeaf, insert con propagacion de Status y el
// log [SPLIT]/[ALTURA]. Es el UNICO .cpp donde se escribe el arbol: bulkLoad (P4) vive
// en BTreeRead.cpp y reutiliza writeNode()/readNode() declarados en BTree.h.
//
// Orden de implementacion de la rama btree-node (un commit por fase):
//   1. open / readNode / writeNode / rootPageID / height / pages / setVerbose  [listo]
//   2. findLeaf con la politica de duplicados, insert y propagacion del split   [listo]
//   3. logSplit / logHeight bajo verbose_                                      [listo]
//
// El formato exacto de estas dos lineas lo documenta docs/btree-insert.md [P3]: si cambia,
// cambia ahi y en estos dos lugares, en ese orden.

#include "index/BTree.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

Status BTree::open(PageManager& index_pm, BTree& out) {
    // Un nodo necesita t >= 2 para existir: con t = 1 el split de un nodo interno deja al
    // hijo derecho con 0 claves y 1 hijo, que deserialize NO representa (lee 0 hijos cuando
    // keyCount es 0) y el arbol se rompe en la siguiente lectura; con t = 0 serialize
    // calcula 2t-1 como size_t y deja de cortar. computeT baja de 2 en page_size <= 46
    // (la hoja pide 20t+7 y t=2 necesita 47), y PageManager admite desde 28, asi que el
    // hueco se alcanza con un --page-size chico. PreconditionFailed y NO Corrupt: el
    // archivo esta bien, lo que no sirve es el page_size con el que se abrio.
    if (BTreeNode::computeT(index_pm.pageSize()) < 2) return Status::PreconditionFailed;

    // Se abre "en su sitio": si algo falla, out queda como estaba (se restaura el
    // puntero), asi un out ya abierto no pierde su arbol por intentar abrir uno roto.
    // verbose_ NO se toca: es una preferencia de presentacion, no estado del arbol, y
    // setVerbose() llamado antes de open() tiene que seguir valiendo.
    PageManager* const previous_pm = out.pm_;
    out.pm_ = &index_pm;

    const FileMeta meta = index_pm.readMeta();

    if (meta.root_page_id != 0) {
        // Raiz presente: la valida y SOLO la lee. Si no es un nodo legible, Corrupt y el
        // arbol queda sin abrir: no se escribe nada en el camino de falla.
        if (meta.height == 0) {                          // raiz sin altura: meta inconsistente
            out.pm_ = previous_pm;
            return Status::Corrupt;
        }
        BTreeNode root;
        const Status s =
            BTreeNode::deserialize(index_pm.read(meta.root_page_id), root);
        if (s != Status::Ok || root.data().self != meta.root_page_id) {
            out.pm_ = previous_pm;
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

PageID BTree::findLeaf(int32_t key, Bias bias,
                       std::vector<PageID>* ancestors) const {
    // Baja de la raiz a la hoja guardando la ruta (raiz -> hoja). Los duplicados deciden
    // hacia que hijo se baja:
    //   Bias::Left  (search, P4) -> lowerBound: a la IZQUIERDA de la primera separadora
    //                               >= key, o sea la hoja mas a la izquierda que puede
    //                               contener la clave. Desde ahi search recorre nextLeaf.
    //   Bias::Right (insert)     -> upperBound: a la DERECHA de las separadoras <= key,
    //                               o sea la ultima hoja que puede contener la clave. Asi
    //                               las copias de una clave se acumulan juntas y no
    //                               quedan repartidas al reves de como se busca.
    if (ancestors != nullptr) ancestors->clear();

    PageID current = rootPageID();
    while (true) {
        const BTreeNode node = readNode(current);
        if (node.isLeaf()) return current;

        // Hoja interna sin hijos no puede existir: deserialize la permite (keyCount 0),
        // pero bajar por ella seria un childAt() fuera de rango, no un Status.
        if (node.childCount() == 0) {
            throw std::runtime_error("BTree::findLeaf: nodo interno sin hijos en pagina " +
                                     std::to_string(current));
        }
        if (ancestors != nullptr) ancestors->push_back(current);

        // idx queda siempre en [0, keyCount] = [0, childCount-1], para ambos bias.
        const uint16_t idx = (bias == Bias::Left) ? node.searchInNode(key).idx
                                                  : node.upperBound(key);
        current = node.childAt(idx);
    }
}

Status BTree::insert(int32_t key, const RowID& rowid) {
    if (pm_ == nullptr) return Status::PreconditionFailed;  // sin open() no hay arbol

    std::vector<PageID> ancestors;
    const PageID leaf_id = findLeaf(key, Bias::Right, &ancestors);
    BTreeNode node = readNode(leaf_id);
    node.insertInNode(key, rowid);

    // En memoria entra hasta 2t claves a proposito (insertInNode no corta): el serializador
    // es quien corta en 2t-1 con NodeOverflow. El split pasa ANTES de escribir, asi que
    // una pagina desbordada nunca llega a disco ni en Release.
    if (node.keyCount() <= 2 * node.t() - 1) return writeNode(node);

    PageID left_id = leaf_id;
    SplitResult split = node.split(pm_->allocate());   // split es puro: solo reparte claves
    Status s = writeNode(node);                        // izquierdo (ya con nextLeaf nuevo)
    if (s != Status::Ok) return s;
    s = writeNode(split.right);                        // right.self ya esta grabado por split
    if (s != Status::Ok) return s;
    logSplit(node, split.right, split.promoted_key);

    int32_t promoted = split.promoted_key;
    PageID right_page = split.right.data().self;

    // Sube por la pila de ancestros (de la hoja hacia la raiz) hasta que alguno quepa.
    // El bucle, no la recursion: es la misma altura que el arbol, que es log(n).
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        BTreeNode parent = readNode(*it);
        parent.insertInNode(promoted, right_page);
        if (parent.keyCount() <= 2 * parent.t() - 1) return writeNode(parent);

        left_id = parent.data().self;
        split = parent.split(pm_->allocate());         // interno: la clave SUBE, no queda
        s = writeNode(parent);
        if (s != Status::Ok) return s;
        s = writeNode(split.right);
        if (s != Status::Ok) return s;
        logSplit(parent, split.right, split.promoted_key);
        promoted = split.promoted_key;
        right_page = split.right.data().self;
    }

    // Se llego al tope de la pila: hace falta una raiz nueva (hoja que partido, o interno
    // que partido). Es el UNICO punto donde crece la altura del arbol.
    const PageID new_root_id = pm_->allocate();
    NodeData data;
    data.is_leaf = false;
    data.t = BTreeNode::computeT(pm_->pageSize());
    data.self = new_root_id;
    data.next_leaf = 0;
    data.keys.push_back(promoted);
    data.children.push_back(left_id);
    data.children.push_back(right_page);
    const BTreeNode new_root = BTreeNode::fromData(std::move(data), pm_->pageSize());
    s = writeNode(new_root);
    if (s != Status::Ok) return s;

    FileMeta meta = pm_->readMeta();                   // conserve record_count y page_count
    const size_t old_height = meta.height;
    meta.root_page_id = new_root_id;
    meta.height = old_height + 1;
    pm_->writeMeta(meta);
    logHeight(old_height, meta.height, new_root_id);
    return Status::Ok;
}

void BTree::logSplit(const BTreeNode& left, const BTreeNode& right,
                     int32_t promoted_key) const {
    // UNICO punto donde se imprime [SPLIT]. Sale por std::cout para que la demo (opcion 5)
    // lo muestre en el mismo canal que el resto de la salida, y con endl para que se vea
    // en vivo el instante en que el arbol crece.
    if (!verbose_) return;
    const PageID page = left.data().self;
    std::cout << "[SPLIT]  " << std::left << std::setw(7)
              << (left.isLeaf() ? "hoja" : "interno") << " page=" << page
              << " -> izq=" << page << " der=" << right.data().self
              << "  clave=" << promoted_key << std::endl;
}

void BTree::logHeight(size_t old_h, size_t new_h, PageID new_root) const {
    // UNICO punto donde se imprime [ALTURA]. Solo crece en insert (y en bulkLoad, P4,
    // que lo llama: no lo reimplementa).
    if (!verbose_) return;
    std::cout << "[ALTURA] " << old_h << " -> " << new_h << "  raiz=page " << new_root
              << std::endl;
}
