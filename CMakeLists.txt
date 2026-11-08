cmake_minimum_required(VERSION 3.16)
project(phase1_btree)

# Debug por defecto al desarrollar: mantiene vivos los assert de error de programacion
# (asInt() sobre un VARCHAR, bytes.size() < 2). La tupla de 0 campos NO entra en la lista:
# devuelve Status::PreconditionFailed y se testea sin fork (cambio 40). El desborde de un nodo
# tampoco depende del build: serialize() devuelve Status::NodeOverflow, asi que se detecta con
# -DNDEBUG. Por eso el benchmark se puede (y se debe) medir en RelWithDebInfo.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)     # "listo" exige compilar sin warnings nuevos

# La implementacion vive en una libreria estatica y los dos ejecutables la linkean. Sin esto
# el target tests no tendria los .cpp de src/ y ningun test podria llamar a nada: se
# linkearia contra las DECLARACIONES de los headers y fallaria con "undefined reference".
# src/main.cpp queda AFUERA de la libreria porque trae su propio main().
add_library(phase1_core STATIC
  src/storage/Value.cpp  src/storage/Tuple.cpp      src/storage/SlottedPage.cpp
  src/storage/PageManager.cpp src/storage/HeapFile.cpp
  src/index/BTreeNode.cpp src/index/BTreeWrite.cpp src/index/BTreeRead.cpp
  src/index/TreePrinter.cpp
  src/bench/Metrics.cpp  src/bench/DataGen.cpp      src/bench/Benchmark.cpp
  src/lexer/token.cpp    src/lexer/lexer.cpp
  src/parser_ll1/grammar.cpp src/parser_ll1/parser.cpp
)
# Fuentes explicitas. NUNCA GLOB: anadir un archivo despues genera conflictos.

add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE phase1_core)

# CLI del analizador lexico (Practica 4): lee SQL de un archivo o de stdin e imprime
# los tokens. Trae su propio main(), por eso NO entra en phase1_core.
add_executable(lexer src/lexer/lexer_cli.cpp)
target_link_libraries(lexer PRIVATE phase1_core)

# CLI del analizador sintactico LL(1) (Practica 5): lee una consulta SQL de un
# archivo o de stdin, imprime la traza de la pila y acepta/rechaza la consulta.
# --grammar vuelca producciones, FIRST/FOLLOW y la tabla LL(1). Trae su propio
# main(), por eso NO entra en phase1_core.
add_executable(parser src/parser_ll1/parser_cli.cpp)
target_link_libraries(parser PRIVATE phase1_core)

add_executable(tests
  tests/test_main.cpp
  tests/test_value.cpp tests/test_tuple.cpp
  tests/test_storage.cpp tests/test_datagen.cpp
  tests/test_tree_printer.cpp
  tests/test_page_manager.cpp
  tests/test_btree_node.cpp tests/test_btree_write.cpp
  tests/test_btree_read.cpp tests/test_tree_invariants.cpp
)
# OJO: tests/ esta en la raiz, no en src/. Ver el arbol de arriba.
target_link_libraries(tests PRIVATE phase1_core)

# Tests del lexico (Practica 4). Cada uno trae su propio main() y su propio juego de
# macros CHECK, asi que son EJECUTABLES APARTE y no entran en el target 'tests' (que
# despacha por --suite con TestHarness.h). Se migraran a ese harness cuando convenga.
add_executable(test_lexer tests/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE phase1_core)
add_executable(test_token tests/test_token.cpp)
target_link_libraries(test_token PRIVATE phase1_core)
add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE phase1_core)

target_include_directories(phase1_core PUBLIC src)
target_include_directories(tests   PRIVATE tests)
enable_testing()
add_test(NAME value         COMMAND tests --suite value)
add_test(NAME tuple         COMMAND tests --suite tuple)
add_test(NAME datagen       COMMAND tests --suite datagen)
add_test(NAME storage       COMMAND tests --suite storage)
add_test(NAME tree_printer  COMMAND tests --suite tree_printer)
add_test(NAME page_manager  COMMAND tests --suite page_manager)
add_test(NAME btree_node    COMMAND tests --suite btree_node)
add_test(NAME btree_write   COMMAND tests --suite btree_write)
add_test(NAME btree_read    COMMAND tests --suite btree_read)
add_test(NAME invariants    COMMAND tests --suite invariants)
# Un add_test por suite: una suite roja se ve sola en el informe de CI.
add_test(NAME lexer         COMMAND test_lexer)
add_test(NAME token         COMMAND test_token)
add_test(NAME parser        COMMAND test_parser)
