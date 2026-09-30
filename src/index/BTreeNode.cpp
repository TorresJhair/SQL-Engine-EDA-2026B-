// src/index/BTreeNode.cpp    [P3]
// computeT, serialize/deserialize y split del nodo de 16 B.

#include "index/BTreeNode.h"

#include <algorithm>
#include <cstring>

namespace {
inline void putU16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFU);
    p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
}

inline uint16_t getU16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8U);
}

inline void putU32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFU);
    p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
    p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
    p[3] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
}

inline uint32_t getU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}
} // namespace

uint16_t BTreeNode::computeT(size_t page_size) {
    // Formulas con SLACK = 1:
    // Interno: 16 + 4*(2t) + 4*(2t-1) + 1 <= pageSize  ->  16t + 13 <= pageSize
    // Hoja:    16 + 10*(2t-1)        + 1 <= pageSize  ->  20t +  7 <= pageSize
    if (page_size < 27) {
        return 2;
    }
    const uint16_t t_interno = static_cast<uint16_t>((page_size - 13) / 16);
    const uint16_t t_hoja = static_cast<uint16_t>((page_size - 7) / 20);
    return std::min(t_interno, t_hoja);
}

BTreeNode BTreeNode::makeLeaf(PageID self, size_t page_size) {
    BTreeNode node;
    node.page_size_ = page_size;
    node.d_.is_leaf = true;
    node.d_.t = computeT(page_size);
    node.d_.self = self;
    node.d_.next_leaf = 0;
    return node;
}

BTreeNode BTreeNode::makeInternal(PageID self, size_t page_size) {
    BTreeNode node;
    node.page_size_ = page_size;
    node.d_.is_leaf = false;
    node.d_.t = computeT(page_size);
    node.d_.self = self;
    node.d_.next_leaf = 0;
    return node;
}

bool BTreeNode::isLeaf() const { return d_.is_leaf; }

uint16_t BTreeNode::keyCount() const {
    return static_cast<uint16_t>(d_.keys.size());
}

uint16_t BTreeNode::t() const { return d_.t; }

uint32_t BTreeNode::childCount() const {
    return static_cast<uint32_t>(d_.children.size());
}

int32_t BTreeNode::keyAt(uint16_t i) const { return d_.keys[i]; }

PageID BTreeNode::childAt(uint16_t i) const { return d_.children[i]; }

RowID BTreeNode::rowIDAt(uint16_t i) const { return d_.rowids[i]; }

PageID BTreeNode::nextLeaf() const { return d_.next_leaf; }

void BTreeNode::setNextLeafPageID(PageID next) { d_.next_leaf = next; }

const NodeData &BTreeNode::data() const { return d_; }

BTreeNode BTreeNode::fromData(NodeData data, size_t page_size) {
    BTreeNode node;
    node.d_ = std::move(data);
    node.page_size_ = page_size;
    return node;
}

BTreeNode::SearchHit BTreeNode::searchInNode(int32_t key) const {
    const auto it = std::lower_bound(d_.keys.begin(), d_.keys.end(), key);
    const uint16_t idx =
        static_cast<uint16_t>(std::distance(d_.keys.begin(), it));
    const bool found = (it != d_.keys.end() && *it == key);
    return SearchHit{idx, found};
}

uint16_t BTreeNode::upperBound(int32_t key) const {
    const auto it = std::upper_bound(d_.keys.begin(), d_.keys.end(), key);
    return static_cast<uint16_t>(std::distance(d_.keys.begin(), it));
}

void BTreeNode::insertInNode(int32_t key, PageID right_child) {
    const auto it = std::upper_bound(d_.keys.begin(), d_.keys.end(), key);
    const auto idx = std::distance(d_.keys.begin(), it);
    d_.keys.insert(it, key);
    if (d_.children.empty()) {
        d_.children.push_back(0);
    }
    d_.children.insert(d_.children.begin() + idx + 1, right_child);
}

void BTreeNode::insertInNode(int32_t key, const RowID &rowid) {
    // En hoja se inserta tras las claves iguales para mantener el orden
    const uint16_t idx = upperBound(key);
    d_.keys.insert(d_.keys.begin() + idx, key);
    d_.rowids.insert(d_.rowids.begin() + idx, rowid);
}

SplitResult BTreeNode::split(PageID right_page_id) {
    SplitResult res;
    res.right.page_size_ = page_size_;
    res.right.d_.t = d_.t;
    res.right.d_.self = right_page_id;

    if (d_.is_leaf) {
        res.right.d_.is_leaf = true;
        // Hoja: se divide a la mitad (primeras t claves quedan en left, t
        // siguientes en right)
        const size_t mid = d_.t;
        res.promoted_key = d_.keys[mid];

        res.right.d_.keys.assign(d_.keys.begin() + mid, d_.keys.end());
        res.right.d_.rowids.assign(d_.rowids.begin() + mid, d_.rowids.end());

        d_.keys.erase(d_.keys.begin() + mid, d_.keys.end());
        d_.rowids.erase(d_.rowids.begin() + mid, d_.rowids.end());

        // Enlaces de la lista enlazada de hojas
        res.right.d_.next_leaf = d_.next_leaf;
        d_.next_leaf = right_page_id;
    } else {
        res.right.d_.is_leaf = false;
        res.right.d_.next_leaf = 0;

        // Nodo interno: la clave central SUBE (no queda en ningun hijo)
        const size_t mid = d_.t;
        res.promoted_key = d_.keys[mid];

        res.right.d_.keys.assign(d_.keys.begin() + mid + 1, d_.keys.end());
        res.right.d_.children.assign(d_.children.begin() + mid + 1,
                                     d_.children.end());

        d_.keys.erase(d_.keys.begin() + mid, d_.keys.end());
        d_.children.erase(d_.children.begin() + mid + 1, d_.children.end());
    }

    return res;
}

Status BTreeNode::serialize(Page &out) const {
    const size_t max_keys = static_cast<size_t>(2 * d_.t - 1);
    if (d_.keys.size() > max_keys) {
        return Status::NodeOverflow;
    }

    if (out.size() != page_size_) {
        out.assign(page_size_, 0);
    } else {
        std::fill(out.begin(), out.end(), 0);
    }

    // Header fijo de 16 B:
    // [isLeaf:1][keyCount:2][selfPageID:4][nextLeafPageID:4][t:2][reservado:3]
    out[0] = d_.is_leaf ? 1 : 0;
    putU16(&out[1], static_cast<uint16_t>(d_.keys.size()));
    putU32(&out[3], d_.self);
    putU32(&out[7], d_.is_leaf ? d_.next_leaf : 0);
    putU16(&out[11], d_.t);
    out[13] = 0;
    out[14] = 0;
    out[15] = 0;

    size_t offset = NODE_HEADER_SIZE;
    if (d_.is_leaf) {
        // Claves (4 B cada una)
        for (int32_t k : d_.keys) {
            putU32(&out[offset], static_cast<uint32_t>(k));
            offset += 4;
        }
        // RowIDs (6 B cada uno: PageID 4 B, SlotID 2 B)
        for (const RowID &rid : d_.rowids) {
            putU32(&out[offset], rid.pageID);
            offset += 4;
            putU16(&out[offset], rid.slotID);
            offset += 2;
        }
    } else {
        // Claves (4 B cada una)
        for (int32_t k : d_.keys) {
            putU32(&out[offset], static_cast<uint32_t>(k));
            offset += 4;
        }
        // Hijos (4 B cada uno)
        for (PageID ch : d_.children) {
            putU32(&out[offset], ch);
            offset += 4;
        }
    }

    return Status::Ok;
}

Status BTreeNode::deserialize(const Page &in, BTreeNode &out) {
    if (in.size() < NODE_HEADER_SIZE) {
        return Status::Corrupt;
    }

    const size_t page_size = in.size();
    const uint8_t is_leaf_byte = in[0];
    if (is_leaf_byte > 1) {
        return Status::Corrupt;
    }
    const bool is_leaf = (is_leaf_byte == 1);
    const uint16_t key_count = getU16(&in[1]);
    const PageID self = getU32(&in[3]);
    const PageID next_leaf = getU32(&in[7]);
    const uint16_t t_val = getU16(&in[11]);

    if (t_val == 0 || t_val != computeT(page_size)) {
        return Status::Corrupt;
    }
    if (key_count > 2 * t_val - 1) {
        return Status::Corrupt;
    }

    // Validacion de desborde de pagina
    size_t payload_size = 0;
    if (is_leaf) {
        payload_size =
            static_cast<size_t>(key_count) * 10; // 4 B clave + 6 B RowID
    } else {
        const size_t child_count =
            (key_count == 0 && in.size() >= NODE_HEADER_SIZE)
                ? 0
                : (static_cast<size_t>(key_count) + 1);
        payload_size = static_cast<size_t>(key_count) * 4 + child_count * 4;
    }

    if (NODE_HEADER_SIZE + payload_size > page_size) {
        return Status::Corrupt;
    }

    out.page_size_ = page_size;
    out.d_.is_leaf = is_leaf;
    out.d_.t = t_val;
    out.d_.self = self;
    out.d_.next_leaf = is_leaf ? next_leaf : 0;

    out.d_.keys.clear();
    out.d_.keys.reserve(key_count);
    out.d_.rowids.clear();
    out.d_.children.clear();

    size_t offset = NODE_HEADER_SIZE;
    if (is_leaf) {
        for (uint16_t i = 0; i < key_count; ++i) {
            out.d_.keys.push_back(static_cast<int32_t>(getU32(&in[offset])));
            offset += 4;
        }
        out.d_.rowids.reserve(key_count);
        for (uint16_t i = 0; i < key_count; ++i) {
            RowID rid;
            rid.pageID = getU32(&in[offset]);
            offset += 4;
            rid.slotID = getU16(&in[offset]);
            offset += 2;
            out.d_.rowids.push_back(rid);
        }
    } else {
        for (uint16_t i = 0; i < key_count; ++i) {
            out.d_.keys.push_back(static_cast<int32_t>(getU32(&in[offset])));
            offset += 4;
        }
        const size_t child_count = (key_count == 0) ? 0 : (key_count + 1);
        out.d_.children.reserve(child_count);
        for (size_t i = 0; i < child_count; ++i) {
            out.d_.children.push_back(getU32(&in[offset]));
            offset += 4;
        }
    }

    return Status::Ok;
}
