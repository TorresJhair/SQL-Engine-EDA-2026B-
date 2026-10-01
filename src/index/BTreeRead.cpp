// src/index/BTreeRead.cpp
// Lectura del arbol e indexScan.

#include "index/BTree.h"

#include <stdexcept>
#include <utility>

std::vector<RowID> BTree::search(int32_t key) const {
	std::vector<RowID> matches;
	if (pm_ == nullptr) return matches;

	// Keep the leaf read during the descent. Calling findLeaf() and then reading its
	// result again charged the first leaf twice, making an index scan cost height + 2
	// page reads instead of the documented height + 1 (including the heap fetch).
	PageID page_id = rootPageID();
	BTreeNode leaf = readNode(page_id);
	while (!leaf.isLeaf()) {
		if (leaf.childCount() == 0) {
			throw std::runtime_error("BTree::search: nodo interno sin hijos en pagina " +
			                         std::to_string(page_id));
		}
		const BTreeNode::SearchHit path = leaf.searchInNode(key);
		page_id = leaf.childAt(path.idx);
		leaf = readNode(page_id);
	}

	while (page_id != 0) {
		const BTreeNode::SearchHit hit = leaf.searchInNode(key);
		if (!hit.found) {
			if (hit.idx < leaf.keyCount()) break;
			page_id = leaf.nextLeaf();
			if (page_id != 0) leaf = readNode(page_id);
			continue;
		}

		for (uint16_t i = hit.idx; i < leaf.keyCount(); ++i) {
			const int32_t current_key = leaf.keyAt(i);
			if (current_key != key) return matches;
			matches.push_back(leaf.rowIDAt(i));
		}
		page_id = leaf.nextLeaf();
		if (page_id != 0) leaf = readNode(page_id);
	}
	return matches;
}

std::vector<Tuple> BTree::indexScan(int32_t key, HeapFile& heap) const {
	std::vector<Tuple> matches;
	for (const RowID& row_id : search(key)) {
		Tuple tuple;
		if (heap.get(row_id, tuple) != Status::Ok) {
			throw std::runtime_error("BTree::indexScan: RowID no resuelve a una tupla");
		}
		matches.push_back(std::move(tuple));
	}
	return matches;
}

size_t BTree::size() const {
	if (pm_ == nullptr) return 0;

	PageID page_id = rootPageID();
	BTreeNode node = readNode(page_id);
	while (!node.isLeaf()) {
		page_id = node.childAt(0);
		node = readNode(page_id);
	}

	size_t count = 0;
	while (page_id != 0) {
		count += node.keyCount();
		page_id = node.nextLeaf();
		if (page_id != 0) node = readNode(page_id);
	}
	return count;
}

Status BTree::bulkLoad(const std::vector<std::pair<int32_t, RowID>>& sorted) {
	if (pm_ == nullptr) return Status::PreconditionFailed;

	for (size_t entry_index = 1; entry_index < sorted.size(); ++entry_index) {
		if (sorted[entry_index].first < sorted[entry_index - 1].first) {
			return Status::PreconditionFailed;
		}
	}

	const BTreeNode old_root = readNode(rootPageID());
	if (!old_root.isLeaf() || old_root.keyCount() != 0) {
		return Status::PreconditionFailed;
	}
	if (sorted.empty()) return Status::Ok;

	struct ChildRef {
		PageID page_id;
		int32_t min_key;
	};

	const uint16_t degree = BTreeNode::computeT(pm_->pageSize());
	const size_t max_leaf_keys = 2 * static_cast<size_t>(degree) - 1;
	const size_t leaf_count = (sorted.size() + max_leaf_keys - 1) / max_leaf_keys;
	std::vector<PageID> leaf_pages(leaf_count);
	leaf_pages[0] = old_root.data().self;
	for (size_t leaf_index = 1; leaf_index < leaf_count; ++leaf_index) {
		leaf_pages[leaf_index] = pm_->allocate();
	}

	std::vector<ChildRef> level;
	level.reserve(leaf_count);
	const size_t base_leaf_keys = sorted.size() / leaf_count;
	const size_t extra_leaf_keys = sorted.size() % leaf_count;
	size_t entry_offset = 0;
	for (size_t leaf_index = 0; leaf_index < leaf_count; ++leaf_index) {
		const size_t entry_count =
			base_leaf_keys + (leaf_index < extra_leaf_keys ? 1 : 0);
		NodeData data;
		data.is_leaf = true;
		data.t = degree;
		data.self = leaf_pages[leaf_index];
		data.next_leaf =
			leaf_index + 1 < leaf_count ? leaf_pages[leaf_index + 1] : 0;
		data.keys.reserve(entry_count);
		data.rowids.reserve(entry_count);
		for (size_t key_index = 0; key_index < entry_count; ++key_index) {
			data.keys.push_back(sorted[entry_offset].first);
			data.rowids.push_back(sorted[entry_offset].second);
			++entry_offset;
		}

		const BTreeNode leaf = BTreeNode::fromData(std::move(data), pm_->pageSize());
		const Status status = writeNode(leaf);
		if (status != Status::Ok) return status;
		level.push_back(
			ChildRef{leaf_pages[leaf_index], sorted[entry_offset - entry_count].first});
	}

	const size_t max_children = 2 * static_cast<size_t>(degree);
	size_t new_height = 1;
	while (level.size() > 1) {
		const size_t parent_count = (level.size() + max_children - 1) / max_children;
		const size_t base_children = level.size() / parent_count;
		const size_t extra_children = level.size() % parent_count;
		std::vector<ChildRef> parents;
		parents.reserve(parent_count);
		size_t child_offset = 0;

		for (size_t parent_index = 0; parent_index < parent_count; ++parent_index) {
			const size_t child_count =
				base_children + (parent_index < extra_children ? 1 : 0);
			NodeData data;
			data.is_leaf = false;
			data.t = degree;
			data.self = pm_->allocate();
			data.next_leaf = 0;
			data.children.reserve(child_count);
			data.keys.reserve(child_count - 1);
			for (size_t child_index = 0; child_index < child_count; ++child_index) {
				data.children.push_back(level[child_offset + child_index].page_id);
				if (child_index > 0) {
					data.keys.push_back(level[child_offset + child_index].min_key);
				}
			}

			const PageID parent_page = data.self;
			const int32_t parent_min_key = level[child_offset].min_key;
			const BTreeNode parent =
				BTreeNode::fromData(std::move(data), pm_->pageSize());
			const Status status = writeNode(parent);
			if (status != Status::Ok) return status;
			parents.push_back(ChildRef{parent_page, parent_min_key});
			child_offset += child_count;
		}

		level = std::move(parents);
		++new_height;
	}

	FileMeta meta = pm_->readMeta();
	const size_t old_height = meta.height;
	meta.root_page_id = level.front().page_id;
	meta.height = static_cast<uint32_t>(new_height);
	pm_->writeMeta(meta);
	if (new_height > old_height) {
		logHeight(old_height, new_height, meta.root_page_id);
	}
	return Status::Ok;
}
