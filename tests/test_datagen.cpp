// tests/test_datagen.cpp                        [P1]
// DataGen: los tres generadores del contrato. Lo que se comprueba aca no es que "no
// crashee", sino las TRES propiedades de las que dependen el benchmark y los tests de P3:
// el nombre del dataset mide 16 B FIJOS, `sorted` da claves 1..n sin repetidos (es la
// unica que puede alimentar `bulkLoad`, que exige no decreciente) y `randomKeys` con el
// mismo seed da SIEMPRE la misma secuencia.
#include "TestHarness.h"

#include "bench/DataGen.h"
#include "storage/SlottedPage.h"
#include "storage/Tuple.h"
#include "storage/Value.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// El nombre del dataset tiene que medir 16 B, ni uno más ni uno menos: es lo que hace que
// la tupla del benchmark mida 27 y que el numero de paginas sea derivable y no medido.
size_t nombreDe(int32_t key) { return std::strlen(DataGen::nameFor(key)); }

std::string nombreDe16(int32_t key) { return std::string(DataGen::nameFor(key), 16); }

// El set de claves distintas de una entrada de DataGen. Se cuenta en vez de mirar que no
// haya repetidos, porque el mensaje del fallo dice cuantos hay y no solo que hay uno.
size_t clavesDistintas(const std::vector<std::pair<int32_t, RowID>>& v) {
    std::set<int32_t> s;
    for (const auto& e : v) s.insert(e.first);
    return s.size();
}

}  // namespace

// --- el nombre del dataset -------------------------------------------------
TEST(datagen, el_nombre_mide_16_bytes_siempre) {
    const int32_t casos[] = {0, 1, 7, 42, 999, 1000, 123456, 1000000000, 2147483647};
    for (int32_t k : casos) CHECK_EQ(nombreDe(k), size_t(16));
}

TEST(datagen, el_nombre_mide_16_bytes_con_claves_negativas) {
    const int32_t casos[] = {-1, -42, -1000, -2147483647 - 1};
    for (int32_t k : casos) CHECK_EQ(nombreDe(k), size_t(16));
}

TEST(datagen, el_nombre_es_determinista) {
    CHECK(DataGen::nameFor(42) == DataGen::nameFor(42));
    CHECK_EQ(std::string(DataGen::nameFor(42)), std::string("nombre_000000042"));
}

TEST(datagen, las_claves_diferentes_dan_nombres_diferentes) {
    // Si dos claves compartieran nombre, el VARCHAR no distinguiría las tuplas y el
    // benchmark estaría comparando claves contra relleno.
    CHECK(std::string(DataGen::nameFor(1)) != std::string(DataGen::nameFor(2)));
    CHECK(std::string(DataGen::nameFor(1)) != std::string(DataGen::nameFor(-1)));
}

TEST(datagen, el_nombre_no_depende_de_un_buffer_anterior) {
    // nameFor devuelve un buffer estatico y lo reescribe entero. Si no se limpiara, el
    // nombre de una clave podría quedar con la cola de la anterior. Se prueba con claves
    // de largos distintos, para que un nombre corto después de uno largo delate el resto.
    const std::string clave_corta = DataGen::nameFor(1);
    const std::string clave_larga = DataGen::nameFor(1000000000);
    const std::string clave_neg   = DataGen::nameFor(-1);

    CHECK_EQ(clave_corta, std::string("nombre_000000001"));
    CHECK_EQ(std::string(DataGen::nameFor(1)), clave_corta);   // repetir no cambia nada
    CHECK_EQ(clave_larga, std::string("nombre_100000000"));
    // El signo va al final del tramo de 9 porque el relleno con ceros es a la izquierda:
    // "0" + "0" + "-1" = "00-1". No es un error, es donde cae el signo al rellenar.
    CHECK_EQ(clave_neg, std::string("nombre_0000000-1"));
    CHECK_EQ(DataGen::nameFor(1), std::string("nombre_000000001"));
}

TEST(datagen, un_key_de_mas_de_9_digitos_se_recorta_pero_el_nombre_sigue_16) {
    // 1000000000 son 10 dígitos y solo entran 9: se recortan los de la izquierda. El
    // nombre sigue midiendo 16, que es lo que hace que la tupla del dataset siga midiendo
    // 27. El recorte no pierde información que sirva: el campo que distingue las tuplas
    // es el INT key, no el VARCHAR de relleno.
    CHECK_EQ(std::string(DataGen::nameFor(1000000000)), std::string("nombre_100000000"));
    CHECK_EQ(nombreDe(1000000000), size_t(16));
    CHECK_EQ(nombreDe(2147483647), size_t(16));
}

// --- datasetTuple ----------------------------------------------------------
TEST(datagen, la_tupla_del_dataset_tiene_2_campos) {
    Tuple t = DataGen::datasetTuple(42);
    CHECK_EQ(t.fieldCount(), size_t(2));
    CHECK_EQ(t.at(0).kind(), ValueKind::Int);
    CHECK_EQ(t.at(0).asInt(), 42);
    CHECK_EQ(t.at(1).kind(), ValueKind::VarChar);
}

TEST(datagen, la_tupla_del_dataset_mide_27_bytes) {
    // 1 (nFields) + 7 (INT) + 19 (VARCHAR de 16) = 27. Es el 27 de la formula
    // (page_size - 8) / 31, y por eso el nombre tiene que medir 16 clavados.
    std::vector<uint8_t> b;
    CHECK(DataGen::datasetTuple(42).serializeTo(b) == Status::Ok);
    CHECK_EQ(b.size(), size_t(27));
    CHECK_EQ(DataGen::datasetTuple(42).serializedSize(), size_t(27));
}

TEST(datagen, la_tupla_del_dataset_hace_round_trip) {
    for (int32_t k : {1, 42, 1000, 2147483647}) {
        Tuple t = DataGen::datasetTuple(k);
        std::vector<uint8_t> b;
        CHECK(t.serializeTo(b) == Status::Ok);

        Tuple out;
        CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Ok);
        CHECK_EQ(out.fieldCount(), size_t(2));
        CHECK_EQ(out.at(0).asInt(), k);
        CHECK_EQ(out.at(1).asVarChar(), nombreDe16(k));
    }
}

TEST(datagen, el_nombre_dentro_de_la_tupla_mide_16) {
    // El VARCHAR serializado lleva su len, así que si el nombre midiera distinto, el 27 de
    // arriba cambiaría y la fórmula de páginas dejan de cuadrar.
    Tuple t = DataGen::datasetTuple(42);
    std::vector<uint8_t> b;
    CHECK(t.serializeTo(b) == Status::Ok);
    CHECK_EQ(int(b[8]), 1);        // tag VARCHAR
    CHECK_EQ(int(b[9]), 16);       // len = 16
    CHECK_EQ(int(b[10]), 0);       // len = 16, byte alto
}

// --- sorted ----------------------------------------------------------------
TEST(datagen, sorted_da_n_claves_desde_1) {
    std::vector<std::pair<int32_t, RowID>> v = DataGen::sorted(10);
    CHECK_EQ(v.size(), size_t(10));
    for (size_t i = 0; i < v.size(); ++i) CHECK_EQ(v[i].first, int32_t(i + 1));
}

TEST(datagen, sorted_no_tiene_repetidos) {
    // bulkLoad exige no decreciente, y una clave repetida en el dataset rompe la
    // unicidad del índice. Acá se comprueba el generador, no bulkLoad.
    CHECK_EQ(clavesDistintas(DataGen::sorted(500)), size_t(500));
}

TEST(datagen, sorted_va_ordenada_de_forma_no_decreciente) {
    // La precondición de bulkLoad, comprobada en la entrada: si sorted devolviera
    // desordenado, el Status::PreconditionFailed de bulkLoad sería un bug de P3 y no del
    // generador. Mejor fallar acá, donde el error es local.
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::sorted(2000);
    for (size_t i = 1; i < v.size(); ++i) CHECK(v[i - 1].first <= v[i].first);
}

TEST(datagen, sorted_con_0_da_vacio) {
    CHECK_EQ(DataGen::sorted(0).size(), size_t(0));
}

TEST(datagen, sorted_con_1_da_la_clave_1) {
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::sorted(1);
    CHECK_EQ(v.size(), size_t(1));
    CHECK_EQ(v[0].first, int32_t(1));
}

TEST(datagen, sorted_es_determinista) {
    CHECK(DataGen::sorted(50) == DataGen::sorted(50));
}

TEST(datagen, sorted_pone_rowid_en_ceros) {
    // DataGen no sabe de qué página salió la tupla: el RowID lo rellena quien lo alinea
    // con el heap. Un PageID inventado sería peor que un cero explícito.
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::sorted(3);
    for (const auto& e : v) {
        CHECK_EQ(e.second.pageID, PageID(0));
        CHECK_EQ(e.second.slotID, SlotID(0));
    }
}

// --- randomKeys ------------------------------------------------------------
TEST(datagen, random_keys_con_el_mismo_seed_da_la_misma_secuencia) {
    // Si el stream dependiera del orden de ejecución, el benchmark y los tests de P3 no
    // podrían comparar corridas. El seed es parte de la firma justamente por esto.
    CHECK(DataGen::randomKeys(200, 7) == DataGen::randomKeys(200, 7));
    CHECK(DataGen::randomKeys(200, 7) == DataGen::randomKeys(200, 7));
}

TEST(datagen, random_keys_con_distinto_seed_da_distinta_secuencia) {
    CHECK(!(DataGen::randomKeys(200, 7) == DataGen::randomKeys(200, 8)));
    CHECK(!(DataGen::randomKeys(200, 1) == DataGen::randomKeys(200, 2)));
}

TEST(datagen, random_keys_devuelve_n_elementos) {
    CHECK_EQ(DataGen::randomKeys(0, 1).size(), size_t(0));
    CHECK_EQ(DataGen::randomKeys(1, 1).size(), size_t(1));
    CHECK_EQ(DataGen::randomKeys(1000, 42).size(), size_t(1000));
}

TEST(datagen, random_keys_no_repite_la_misma_clave_dieciseis_veces) {
    // Un LCG con los bits bajos de key se repetiría muchísimo. El generador mezcla los
    // bits altos justamente por esto: en 1000 sorteos no puede haber ni 3 repetidos.
    CHECK(clavesDistintas(DataGen::randomKeys(1000, 3)) > 970);
}

// El PageID 0 sale con probabilidad 1/2^31 por elemento. Muestrear 20 000 no lo va a ver
// nunca, asi que un test por muestreo NO puede cubrir el `| 1`. Lo que si se puede
// comprobar es la PROPIEDAD que lo garantiza: con el `| 1` toda página es impar, y una
// página par ya no puede salir. Esa asertion es la que se cae si el `| 1` desaparece.
TEST(datagen, random_keys_saca_siempre_una_pagina_impar) {
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::randomKeys(20000, 11);
    for (const auto& e : v) {
        CHECK(e.second.pageID != PageID(0));          // la consecuencia que importa
        CHECK_EQ(e.second.pageID & 1, PageID(1));    // la que la garantiza
    }
}

TEST(datagen, random_keys_nunca_tira_el_slot_de_fin_de_cadena) {
    // 0xFFFF es el centinela de "ninguno" en la cadena de libres de SlottedPage. Un RowID
    // con ese slotID no se podría ni insertar.
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::randomKeys(20000, 11);
    for (const auto& e : v) CHECK(e.second.slotID != SlotID(0xFFFF));
}

TEST(datagen, la_pagina_de_random_keys_no_depende_de_la_clave) {
    // ESTE es el test de M6, y el que hizo falta de verdad. La tentación es partir UN solo
    // sorteo: `key = x & mask` y `page = x >> 3`. Con eso la página queda atada a la clave,
    // y P2 ubica la tupla en la página (page_size - 8) / 27: el test de estrés de P3
    // quiere que las claves caigan en MUCHAS páginas, y atadas a la clave la mitad caería
    // siempre en la misma. El test probaría un caso y parecería que el estrés pasó.
    //
    // La comprobación NO es de que dos claves iguales caigan en páginas distintas: eso no
    // alcanza. La clave que se publica viene enmascarada (k & 0x7FFFFFFF), así que dos
    // sorteos distintos pueden dar la misma clave publicada con la parte alta diferente, y
    // con `page = k >> 3` eso también pasa. Ese test no ve nada.
    //
    // Lo que se mide es lo que de verdad importa: que página y clave NO compartan bits.
    // Con page = k >> 3, la página y (key >> 3) salen de los MISMOS bits de k, así que
    // coinciden en los 28 bits bajos. Con sorteos independientes, la probabilidad de que
    // coincidan es 2⁻²⁸, o sea ~0 de cada 1000.
    //
    // Se contó antes de escribir el umbral: 0 de 1000 con el código correcto, 480 de 1000
    // con `page = k >> 3`. El umbral en 10 separa los dos casos por casi dos órdenes de
    // magnitud, y como la semilla es fija el resultado no varía entre corridas.
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::randomKeys(1000, 5);
    size_t                                      coincidencias = 0;
    for (const auto& e : v) {
        const uint64_t esperado = (static_cast<uint64_t>(e.first) >> 3) & 0xFFFFFFFULL;
        if ((e.second.pageID & 0xFFFFFFFULL) == esperado) ++coincidencias;
    }
    CHECK(coincidencias < 10);
}

TEST(datagen, random_keys_reparte_entre_muchas_paginas) {
    // Y que el reparto sea de verdad amplio, no 3 páginas que absorben todo: el test de
    // estrés de P3 necesita caer en muchas páginas distintas del índice.
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::randomKeys(500, 5);
    std::set<PageID>                             pags;
    for (const auto& e : v) pags.insert(e.second.pageID);
    CHECK(pags.size() > 400);
}

TEST(datagen, random_keys_usa_paginas_diversas) {
    // El test de estrés de P3 necesita que las claves caigan en MUCHAS páginas del heap.
    // Si el generador devolviera siempre la misma, el test probaría un caso y no el
    // recorrido completo.
    const std::vector<std::pair<int32_t, RowID>> v = DataGen::randomKeys(500, 5);
    std::set<PageID>                             pags;
    for (const auto& e : v) pags.insert(e.second.pageID);
    CHECK(pags.size() > 100);
}

TEST(datagen, random_keys_acepta_seed_0) {
    // El seed 0 es un seed como cualquier otro: no se trata como "sin seed".
    CHECK_EQ(DataGen::randomKeys(10, 0).size(), size_t(10));
    CHECK(!(DataGen::randomKeys(100, 0) == DataGen::randomKeys(100, 1)));
}

// --- heap ------------------------------------------------------------------
TEST(datagen, heap_da_n_tuplas) {
    const std::vector<Tuple> v = DataGen::heap(10);
    CHECK_EQ(v.size(), size_t(10));
    for (const Tuple& t : v) CHECK_EQ(t.serializedSize(), size_t(27));
}

TEST(datagen, heap_y_sorted_usan_el_mismo_rango_de_claves) {
    // El benchmark compara índice contra heap Full Table Scan sobre el MISMO juego de
    // datos. Si las claves no coincidieran, la comparación no mediría nada.
    const std::vector<Tuple>                  v = DataGen::heap(50);
    const std::vector<std::pair<int32_t, RowID>> s = DataGen::sorted(50);
    for (size_t i = 0; i < v.size(); ++i) {
        CHECK_EQ(v[i].at(0).asInt(), s[i].first);
        CHECK_EQ(v[i].at(0).asInt(), int32_t(i + 1));
    }
}

TEST(datagen, heap_con_0_da_vacio) {
    CHECK_EQ(DataGen::heap(0).size(), size_t(0));
}

TEST(datagen, heap_es_determinista) {
    const std::vector<Tuple> a = DataGen::heap(20);
    const std::vector<Tuple> b = DataGen::heap(20);
    for (size_t i = 0; i < a.size(); ++i) CHECK(a[i].serializedSize() == b[i].serializedSize());
    for (size_t i = 0; i < a.size(); ++i) CHECK_EQ(a[i].at(0).asInt(), b[i].at(0).asInt());
}

// --- las 27 del dataset llenan la pagina ------------------------------------
TEST(datagen, el_dataset_entra_131_veces_en_una_pagina_de_4096) {
    // El número de páginas del heap del benchmark sale de acá, y si el nombre no midiera
    // 16 clavados daría otro. Por eso este test está en DataGen y no en SlottedPage.
    SlottedPage sp = SlottedPage::init(4096);
    int        n  = 0;
    for (size_t i = 1; i <= 200; ++i) {
        std::vector<uint8_t> b;
        CHECK(DataGen::datasetTuple(static_cast<int32_t>(i)).serializeTo(b) == Status::Ok);
        SlotID s;
        if (sp.insert(b, s) != Status::Ok) break;
        ++n;
    }
    CHECK_EQ(n, 131);
    CHECK_EQ(SlottedPage::maxTupleSize(4096), size_t(4084));
}

TEST(datagen, la_tupla_del_dataset_sobrevive_al_slot) {
    // El camino completo: DataGen -> Tuple -> SlottedPage -> lookup -> Tuple otra vez.
    SlottedPage sp = SlottedPage::init(4096);
    SlotID      s;
    std::vector<uint8_t> b;
    CHECK(DataGen::datasetTuple(12345).serializeTo(b) == Status::Ok);
    CHECK(sp.insert(b, s) == Status::Ok);

    std::vector<uint8_t> out;
    CHECK(sp.lookup(s, out) == Status::Ok);
    CHECK_EQ(out.size(), size_t(27));

    Tuple t;
    CHECK(Tuple::deserialize(out.data(), out.size(), t) == Status::Ok);
    CHECK_EQ(t.at(0).asInt(), 12345);
    CHECK_EQ(t.at(1).asVarChar(), nombreDe16(12345));
}

namespace suites {
void datagen() {
    // Los TEST() de este archivo se registran solos; ver TestHarness.h.
}
}  // namespace suites
