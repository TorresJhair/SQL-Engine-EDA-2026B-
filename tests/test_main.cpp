// tests/test_main.cpp         [P1] main() y despacho por --suite
//
// Registra las 7 suites y las despacha con --suite. Asi ctest puede correrlas de a una
// y un fallo queda atribuido a un dueño, no a "los tests".
#include "TestHarness.h"

#include <cstring>
#include <iostream>
#include <string>

// Cabeceras de las 7 suites: cada una tiene sus test_*.cpp.
namespace suites {
void value();          // [P1]
void tuple();          // [P1]
void storage();        // [P1]
void tree_printer();   // [P1]
void page_manager();   // [P2]
void btree_node();     // [P3]
void btree_write();    // [P3]
void btree_read();      // [P4]
void invariants();     // [P4]
}

std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> reg;
    return reg;
}

TestState& testState() {
    static TestState st;
    return st;
}

void testReportFailure(const char* file, int line, const std::string& what) {
    TestState& st = testState();
    st.failed++;
    st.current_failed = true;
    std::cout << "    FALLO " << st.current_name << "\n"
              << "      " << file << ":" << line << ": " << what << "\n";
}

namespace {

struct SuiteName {
    const char* name;
    void (*fn)();
};

const SuiteName kSuites[] = {
    {"value",        &suites::value},
    {"tuple",        &suites::tuple},
    {"storage",      &suites::storage},
    {"tree_printer", &suites::tree_printer},
    {"page_manager", &suites::page_manager},
    {"btree_node",   &suites::btree_node},
    {"btree_write",  &suites::btree_write},
    {"btree_read",   &suites::btree_read},
    {"invariants",   &suites::invariants},
};

void usage(const char* argv0) {
    std::cout << "uso: " << argv0 << " [--suite <nombre>|all]\n"
              << "suites:";
    for (const auto& s : kSuites) std::cout << " " << s.name;
    std::cout << " all\n";
}

}  // namespace

int main(int argc, char** argv) {
    // Llamar a las 7 suiteRegistration() engancha sus TEST al registro global.
    suites::value();
    suites::tuple();
    suites::storage();
    suites::tree_printer();
    suites::page_manager();
    suites::btree_node();
    suites::btree_write();
    suites::btree_read();
    suites::invariants();

    std::string want = "all";
    if (argc == 3 && std::strcmp(argv[1], "--suite") == 0) {
        want = argv[2];
    } else if (argc != 1) {
        usage(argv[0]);
        return 2;
    }

    if (want != "all") {
        bool existe = false;
        for (const auto& s : kSuites) existe = existe || (want == s.name);
        if (!existe) {
            std::cout << "suite desconocida: '" << want << "'\n";
            usage(argv[0]);
            return 2;
        }
    }

    TestState& st = testState();
    int corridos = 0;

    for (const auto& s : kSuites) {
        if (want != "all" && want != s.name) continue;

        std::cout << "== suite " << s.name << " ==\n";
        for (const auto& tc : testRegistry()) {
            if (std::strcmp(tc.suite, s.name) != 0) continue;

            st.current_failed = false;
            st.current_name   = tc.name;
            tc.fn();
            corridos++;
            if (!st.current_failed) std::cout << "  ok   " << tc.name << "\n";
        }
    }

    std::cout << "\n" << corridos << " tests, " << st.checks << " checks, "
              << st.failed << " fallos\n";
    return st.failed == 0 ? 0 : 1;
}
