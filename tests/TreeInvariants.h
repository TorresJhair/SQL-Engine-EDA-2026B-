// tests/TreeInvariants.h                      [P4]
#pragma once

#include "index/BTree.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

struct InvariantReport { bool ok; std::string error; };

inline InvariantReport checkInvariants(const BTree& tree) {
	const PageID root_id = tree.rootPageID();
	if (root_id == 0) return {false, "root page ID is zero"};
	if (tree.height() == 0) return {false, "tree height is zero"};

	struct Summary {
		int32_t min_key;
		int32_t max_key;
		size_t entries;
		size_t leaf_depth;
	};

	try {
		const uint16_t expected_t =
			BTreeNode::computeT(tree.pages().pageSize());
		std::unordered_set<PageID> visited;
		std::vector<PageID> leaves;
		std::string error;

		std::function<bool(PageID, size_t, bool, Summary&)> visit =
			[&](PageID page_id, size_t depth, bool is_root,
				Summary& summary) -> bool {
				if (page_id == 0) {
					error = "reachable child has page ID zero";
					return false;
				}
				if (!visited.insert(page_id).second) {
					error = "page " + std::to_string(page_id) +
							" is reachable more than once";
					return false;
				}

				const BTreeNode node = tree.readNode(page_id);
				const NodeData& data = node.data();
				const size_t key_count = node.keyCount();
				const size_t min_keys = static_cast<size_t>(expected_t - 1);
				const size_t max_keys = 2 * static_cast<size_t>(expected_t) - 1;

				if (data.self != page_id) {
					error = "node self ID does not match page " +
							std::to_string(page_id);
					return false;
				}
				if (node.t() != expected_t) {
					error = "node degree does not match page size at page " +
							std::to_string(page_id);
					return false;
				}
				if (key_count > max_keys || (!is_root && key_count < min_keys)) {
					error = "key occupancy is invalid at page " +
							std::to_string(page_id);
					return false;
				}
				for (size_t i = 1; i < key_count; ++i) {
					if (node.keyAt(static_cast<uint16_t>(i - 1)) >
						node.keyAt(static_cast<uint16_t>(i))) {
						error = "keys are not ordered at page " +
								std::to_string(page_id);
						return false;
					}
				}

				if (node.isLeaf()) {
					if (!data.children.empty() || data.rowids.size() != key_count) {
						error = "leaf payload is inconsistent at page " +
								std::to_string(page_id);
						return false;
					}
					leaves.push_back(page_id);
					summary.entries = key_count;
					summary.leaf_depth = depth;
					if (key_count != 0) {
						summary.min_key = node.keyAt(0);
						summary.max_key = node.keyAt(
							static_cast<uint16_t>(key_count - 1));
					}
					return true;
				}

				if (!data.rowids.empty() ||
					node.childCount() != key_count + 1 ||
					(is_root && node.childCount() < 2)) {
					error = "internal node payload is inconsistent at page " +
							std::to_string(page_id);
					return false;
				}

				std::vector<Summary> children(node.childCount());
				for (uint32_t i = 0; i < node.childCount(); ++i) {
					if (!visit(node.childAt(static_cast<uint16_t>(i)), depth + 1,
							   false, children[i])) {
						return false;
					}
					if (i > 0) {
						const int32_t separator =
							node.keyAt(static_cast<uint16_t>(i - 1));
						if (children[i - 1].max_key > separator ||
							separator > children[i].min_key) {
							error = "separator bounds are invalid at page " +
									std::to_string(page_id);
							return false;
						}
						if (children[i].leaf_depth != children[0].leaf_depth) {
							error = "leaves are at different depths";
							return false;
						}
					}
				}

				summary.min_key = children.front().min_key;
				summary.max_key = children.back().max_key;
				summary.entries = 0;
				for (const Summary& child : children) {
					summary.entries += child.entries;
				}
				summary.leaf_depth = children.front().leaf_depth;
				return true;
			};

		Summary root_summary{};
		if (!visit(root_id, 1, true, root_summary)) return {false, error};
		if (root_summary.leaf_depth != tree.height()) {
			return {false, "tree height does not match leaf depth"};
		}

		for (size_t i = 0; i < leaves.size(); ++i) {
			const BTreeNode leaf = tree.readNode(leaves[i]);
			const PageID expected_next = i + 1 < leaves.size() ? leaves[i + 1] : 0;
			if (leaf.nextLeaf() != expected_next) {
				return {false, "leaf chain does not match tree order"};
			}
		}
		if (root_summary.entries != tree.size()) {
			return {false, "leaf entry count does not match tree size"};
		}
	} catch (const std::exception& exception) {
		return {false, exception.what()};
	}

	return {true, {}};
}
