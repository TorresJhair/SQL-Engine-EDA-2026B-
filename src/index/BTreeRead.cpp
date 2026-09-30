// src/index/BTreeRead.cpp
// Lectura del arbol e indexScan.

#include "index/BTree.h"

std::vector<RowID> BTree::search(int32_t key) const {
	std::vector<RowID> matches;
	if (pm_ == nullptr) return matches;

	PageID page_id = findLeaf(key, Bias::Left, nullptr);
	while (page_id != 0) {
		const BTreeNode leaf = readNode(page_id);
		const BTreeNode::SearchHit hit = leaf.searchInNode(key);
		if (!hit.found) break;

		for (uint16_t i = hit.idx; i < leaf.keyCount(); ++i) {
			const int32_t current_key = leaf.keyAt(i);
			if (current_key != key) return matches;
			matches.push_back(leaf.rowIDAt(i));
		}
		page_id = leaf.nextLeaf();
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
