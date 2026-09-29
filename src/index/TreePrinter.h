// src/index/TreePrinter.h                     [P1]
#pragma once

#include "index/BTree.h"

#include <ostream>

void printTree(const BTree&, std::ostream&);   // ASCII
void exportDot(const BTree&, std::ostream&);   // Graphviz (prescindible)
