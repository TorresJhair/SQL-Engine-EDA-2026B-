// src/bench/DataGen.cpp                         [P1]
#include "bench/DataGen.h"

#include "storage/Value.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// El dataset del benchmark: 2 campos, INT key + VARCHAR name de 16 B, o sea 27 B por
// tupla y 31 B con su entrada de directorio. El VARCHAR es de LARGO FIJO a proposito:
// hace que el numero de paginas del heap sea reproducible y explicable en la presentacion
// ((page_size - 8) / 31 -> 131 con 4096, 8 con 256). Con VARCHAR variable el numero
// dependeria de los datos y habria que medirlo en vez de derivarlo.

// El nombre tiene que ser de EXACTAMENTE 16 B. snprintf escribe el \0 final dentro de los
// 16, asi que despues se copia name[0..15] fuera del buffer: si se devolviera el char*
// del array local, seria un puntero a memoria muerta.
char g_name[16 + 1];

}  // namespace

const char* DataGen::nameFor(int32_t key) {
    // "nombre_" son 7 B, así que del key entran 9 en los 16 del nombre, con ceros a la
    // izquierda. key = 1 da "nombre_000000001".
    //
    // Un key que no cabe en 9 dígitos (INT32_MIN son 11 con el signo) NO se trunca ni se
    // devuelve nullptr: el nombre es RELLENO y el campo que distingue las tuplas es el
    // INT key, así que dos keys enormes pueden compartir nombre sin que nada se confunda.
    // nameFor nunca devuelve nullptr justamente para que datasetTuple no tenga que
    // defenderse de un caso que no puede romper nada.
    char digits[12] = {0};
    std::snprintf(digits, sizeof(digits), "%lld", static_cast<long long>(key));
    const int n = std::min<int>(9, static_cast<int>(std::strlen(digits)));

    std::memcpy(g_name, "nombre_", 7);
    for (int i = 0; i < 9; ++i) g_name[7 + i] = (i < 9 - n) ? '0' : digits[i - (9 - n)];
    g_name[16] = '\0';
    return g_name;
}

Tuple DataGen::datasetTuple(int32_t key) {
    Tuple t;
    t.append(Value(key));
    t.append(Value(std::string(nameFor(key), 16)));
    return t;
}

std::vector<std::pair<int32_t, RowID>> DataGen::sorted(size_t n) {
    // Claves 1..n, en orden, sin repetidos: es la unica que puede darle la entrada a
    // bulkLoad, que exige no decreciente.
    //
    // El RowID va en {0,0} porque DataGen todavia no sabe de que pagina salio la tupla.
    // Lo rellena quien lo consume, alinear la entrada con el heap. Poner un PageID
    // inventado seria peor que un cero explicito.
    std::vector<std::pair<int32_t, RowID>> v;
    v.reserve(n);
    for (size_t i = 1; i <= n; ++i) v.emplace_back(static_cast<int32_t>(i), RowID{0, 0});
    return v;
}

std::vector<std::pair<int32_t, RowID>> DataGen::randomKeys(size_t n, size_t seed) {
    // PRNG propio y NO <random>: mt19937 NO garantiza el mismo stream entre
    // implementaciones de la libreria estandar, y el benchmark tiene que dar el mismo
    // numero en dos maquinas distintas. Ademas <random> trae una dependencia que el plan
    // no quiere.
    //
    // Es un LCG de 64 bits con los parametros de Knuth. Los bits BAJOS de un LCG son su
    // parte debil (ciclo corto), asi que se leen los bits 47..17.
    //
    // El seed es PARTE de la firma y no un global: el benchmark y los tests necesitan la
    // MISMA secuencia sin depender del orden en que se ejecuten.
    std::vector<std::pair<int32_t, RowID>> v;
    v.reserve(n);

    uint64_t state = static_cast<uint64_t>(seed) * 6364136223846793005ULL + 1442695040888963407ULL;
    auto next = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 17;
    };

    for (size_t i = 0; i < n; ++i) {
        // DOS sorteos, no uno partido. Con un solo sorteo y `page = x >> 3` la pagina
        // seria una funcion de la clave: P2 ubica la tupla en la pagina (page_size - 8) /
        // 27 y el test de estres de P3 justamente quiere que las claves caigan en
        // MUCHAS paginas. Con page ligada a la clave, la mitad de las claves cairia
        // siempre en la misma pagina y el test probaria un caso solo.
        const uint64_t k    = next();
        const uint64_t page = next();

        // El 0 se vuelve 1: el PageID 0 es la pagina de metadatos y "ninguna pagina", y
        // una clave con RowID de la pagina 0 daria por no encontrada en toda busqueda.
        v.emplace_back(static_cast<int32_t>(k & 0x7FFFFFFFULL),
                       RowID{static_cast<PageID>((page & 0x7FFFFFFFULL) | 1), 0});
    }
    return v;
}

std::vector<Tuple> DataGen::heap(size_t n) {
    // n tuplas del dataset, con keys 1..n. `sorted` y `heap` usan el mismo rango a
    // proposito: el benchmark compara indice y heap sobre el MISMO logical de datos, y si
    // las claves no coincidieran la comparacion no mediria nada.
    std::vector<Tuple> v;
    v.reserve(n);
    for (size_t i = 1; i <= n; ++i) v.push_back(datasetTuple(static_cast<int32_t>(i)));
    return v;
}
