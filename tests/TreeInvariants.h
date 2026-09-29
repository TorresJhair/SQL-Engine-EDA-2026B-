// tests/TreeInvariants.h                      [P4]
#pragma once

#include "index/BTree.h"

#include <string>

struct InvariantReport { bool ok; std::string error; };

InvariantReport checkInvariants(const BTree&);
