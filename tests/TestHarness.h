// tests/TestHarness.h         [P1] macros TEST/CHECK/CHECK_EQ, sin dependencias y sin fork()
#pragma once

#include "common/Types.h"

#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

// operator<< de los enums de Types.h, que no traen uno. CHECK_EQ imprime ambos valores
// cuando fallan, y sin esto el harness no compila al primer CHECK_EQ de un enum en vez de
// decir cual era el valor esperado. inline y NO en un .cpp compartido: un operador de
// este tipo en dos .cpp distintos seria una definicion multiple en el link.
inline std::ostream& operator<<(std::ostream& os, Status s) {
    switch (s) {
        case Status::Ok:                 return os << "Ok";
        case Status::PageFull:           return os << "PageFull";
        case Status::TupleTooLarge:      return os << "TupleTooLarge";
        case Status::NotFound:           return os << "NotFound";
        case Status::Corrupt:            return os << "Corrupt";
        case Status::NodeOverflow:       return os << "NodeOverflow";
        case Status::PreconditionFailed: return os << "PreconditionFailed";
    }
    return os << "?Status";
}

inline std::ostream& operator<<(std::ostream& os, ValueKind k) {
    return os << (k == ValueKind::Int ? "Int" : "VarChar");
}


// Sin EXPECT_ASSERT_FAILS ni fork(): los errores esperados se comparan con CHECK_EQ
// sobre el Status, y los asserts que quedan son de error de programacion.
// Probar un assert exigiria correr el caso en un proceso hijo con fork(), que ataba la
// suite a POSIX; con Status el desborde se testea directo y esto corre igual en Linux,
// macOS y Windows.

// --- Registro de tests -------------------------------------------------------
// Un vector global de punteros a funcion, llenado por los constructores estaticos que
// genera la macro TEST. Sin singletons ni globals por archivo: el orden de registro lo
// fija el orden de enlace, que es estable dentro de un ejecutable.
struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& testRegistry();

struct TestRegistrar {
    TestRegistrar(const char* suite, const char* name, void (*fn)()) {
        testRegistry().push_back(TestCase{suite, name, fn});
    }
};

struct TestState {
    int  checks  = 0;
    int  failed  = 0;
    bool current_failed = false;
    const char* current_name = "";
};

TestState& testState();

void testReportFailure(const char* file, int line, const std::string& what);

template <typename A, typename B>
void testCheckEq(const char* file, int line, const char* ta, const A& a,
                 const char* tb, const B& b) {
    testState().checks++;
    if (!(a == b)) {
        std::ostringstream os;
        os << ta << " == " << tb << "  (" << a << " vs " << b << ")";
        testReportFailure(file, line, os.str());
    }
}

// --- Macros ------------------------------------------------------------------
// TEST(suite, name) { ... }
// El cuerpo escribe CHECK(cond) y CHECK_EQ(a, b); el macro no necesita return.
#define TEST(suite_name, test_name)                                              \
    static void test_##suite_name##_##test_name();                               \
    static TestRegistrar reg_##suite_name##_##test_name(                        \
        #suite_name, #test_name, &test_##suite_name##_##test_name);             \
    static void test_##suite_name##_##test_name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        testState().checks++;                                                    \
        if (!(cond)) {                                                           \
            testReportFailure(__FILE__, __LINE__, "CHECK fallo: " #cond);        \
        }                                                                        \
    } while (0)

// CHECK_EQ(a, b) imprime ambos valores cuando fallan, que es lo que hace util el
// fallo: saber si es 131 o 132 lo cambia todo.
#define CHECK_EQ(a, b) testCheckEq(__FILE__, __LINE__, #a, (a), #b, (b))
