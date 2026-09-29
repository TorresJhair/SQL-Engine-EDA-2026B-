// tests/test_value.cpp        [P1]
// Formato del valor: [tag:u8][len:u16][datos], little-endian.
//
// El round-trip NO alcanza para probar el formato: en una maquina big-endian un memcpy de
// un int32_t daria el mismo round-trip y un archivo que los demas no pueden leer. Por eso
// aca se comparan los BYTES contra un vector esperado, no solo los valores que vuelven.
#include "TestHarness.h"

#include "common/Types.h"
#include "storage/Value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> ser(const Value& v) {
    std::vector<uint8_t> out;
    v.serializeTo(out);
    return out;
}

}  // namespace

// --- tamano serializado ---------------------------------------------------
TEST(value, int_serialized_size_es_7) {
    // 3 B de cabecera + 4 B de datos.
    CHECK_EQ(Value(42).serializedSize(), size_t(7));
    CHECK_EQ(Value(INT32_MIN).serializedSize(), size_t(7));
    CHECK_EQ(Value(0).serializedSize(), size_t(7));
}

TEST(value, varchar_serialized_size_es_3_mas_len) {
    CHECK_EQ(Value(std::string("")).serializedSize(), size_t(3));
    CHECK_EQ(Value(std::string("a")).serializedSize(), size_t(4));
    // El del dataset del benchmark.
    CHECK_EQ(Value(std::string("nombre0000000000")).serializedSize(), size_t(19));
    CHECK_EQ(Value(std::string(200, 'x')).serializedSize(), size_t(203));
}

// --- bytes exactos --------------------------------------------------------
TEST(value, int_son_los_bytes_little_endian) {
    // 42 = 0x2A. En big-endian el primer byte seria 0x00, no 0x2A: por eso se comparan
    // los bytes y no el round-trip.
    // El len son 2 B: b[1] y b[2]. Los datos arrancan en b[3].
    const std::vector<uint8_t> b = ser(Value(42));
    CHECK_EQ(b.size(), size_t(7));
    CHECK_EQ(int(b[0]), 0);        // tag 0 = INT
    CHECK_EQ(int(b[1]), 4);        // len = 4, byte bajo
    CHECK_EQ(int(b[2]), 0);        // len = 4, byte alto
    CHECK_EQ(int(b[3]), 0x2A);     // 42 en el byte de menos peso
    CHECK_EQ(int(b[4]), 0);
    CHECK_EQ(int(b[5]), 0);
    CHECK_EQ(int(b[6]), 0);
}

TEST(value, int_negativo_usa_dos_complemento) {
    // -1 = 0xFFFFFFFF, los cuatro bytes a 0xFF.
    const std::vector<uint8_t> b = ser(Value(-1));
    CHECK_EQ(int(b[0]), 0);
    CHECK_EQ(int(b[1]), 4);
    CHECK_EQ(int(b[2]), 0);
    CHECK_EQ(int(b[3]), 0xFF);
    CHECK_EQ(int(b[4]), 0xFF);
    CHECK_EQ(int(b[5]), 0xFF);
    CHECK_EQ(int(b[6]), 0xFF);
}

TEST(value, varchar_vacio_son_3_bytes) {
    // El VARCHAR vacio es el caso que fija la tupla minima de 4 B: 1 de nFields + estos 3.
    const std::vector<uint8_t> b = ser(Value(std::string("")));
    CHECK_EQ(b.size(), size_t(3));
    CHECK_EQ(int(b[0]), 1);        // tag 1 = VARCHAR
    CHECK_EQ(int(b[1]), 0);        // len = 0
    CHECK_EQ(int(b[2]), 0);
}

TEST(value, varchar_no_lleva_terminador) {
    // "ab" son 2 bytes de datos, no 3: el len va en la cabecera, no al final.
    const std::vector<uint8_t> b = ser(Value(std::string("ab")));
    CHECK_EQ(b.size(), size_t(5));
    CHECK_EQ(int(b[0]), 1);
    CHECK_EQ(int(b[1]), 2);        // len = 2, byte bajo
    CHECK_EQ(int(b[2]), 0);        // len = 2, byte alto
    CHECK_EQ(int(b[3]), 'a');
    CHECK_EQ(int(b[4]), 'b');
}

TEST(value, varchar_len_de_2_bytes_va_little_endian) {
    // Con 300 bytes, el len es 0x012C: en little-endian el byte bajo va primero.
    const std::vector<uint8_t> b = ser(Value(std::string(300, 'x')));
    CHECK_EQ(b.size(), size_t(303));
    CHECK_EQ(int(b[0]), 1);
    CHECK_EQ(int(b[1]), 300 & 0xFF);
    CHECK_EQ(int(b[2]), (300 >> 8) & 0xFF);
}

// --- round trip -----------------------------------------------------------
TEST(value, int_round_trip) {
    const int32_t casos[] = {0, 1, -1, 42, -42, 1000, -1000, INT32_MAX, INT32_MIN};
    for (int32_t c : casos) {
        const std::vector<uint8_t> b = ser(Value(c));
        const uint8_t*             p = b.data();
        const uint8_t*             end = b.data() + b.size();
        Value                      v(std::string("basura"));
        CHECK(v.deserialize(p, end, v) == Status::Ok);
        CHECK(v.kind() == ValueKind::Int);
        CHECK_EQ(v.asInt(), c);
        CHECK_EQ(p, end);  // el puntero quedo exacto, no quedo cola
    }
}

TEST(value, varchar_round_trip_incluye_el_vacio) {
    const std::string casos[] = {"", "a", "ab", "nombre0000000000", std::string(300, 'x')};
    for (const std::string& s : casos) {
        const std::vector<uint8_t> b = ser(Value(s));
        const uint8_t*             p = b.data();
        const uint8_t*             end = b.data() + b.size();
        Value                      v(0);
        CHECK(v.deserialize(p, end, v) == Status::Ok);
        CHECK(v.kind() == ValueKind::VarChar);
        CHECK_EQ(v.asVarChar(), s);
        CHECK_EQ(p, end);
    }
}

// --- corrupcion -----------------------------------------------------------
TEST(value, len_declarado_mayor_que_el_buffer_da_corrupt) {
    // 3 bytes de cabecera que declaran len = 500, y ni un byte de datos.
    const std::vector<uint8_t> b = {0, 0xF4, 0x01};
    const uint8_t*             p = b.data();
    Value                      v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

TEST(value, tag_invalido_da_corrupt) {
    // tag 7 no es ni INT ni VARCHAR.
    const std::vector<uint8_t> b = {7, 0, 0};
    const uint8_t*             p = b.data();
    Value                      v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

TEST(value, int_con_len_distinto_de_4_da_corrupt) {
    // Un INT declara len = 3: el archivo esta corrupto.
    const std::vector<uint8_t> b = {0, 3, 0, 1, 2, 3};
    const uint8_t*             p = b.data();
    Value                      v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

TEST(value, buffer_menor_que_la_cabecera_da_corrupt) {
    const std::vector<uint8_t> b = {0, 4};  // ni siquiera los 3 B
    const uint8_t*             p = b.data();
    Value                      v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

TEST(value, buffer_vacio_da_corrupt) {
    const uint8_t* p = nullptr;
    Value          v(0);
    CHECK(v.deserialize(p, p, v) == Status::Corrupt);
}

// MUTACION 4: quitar este chequeo hace que el round-trip IGUAL pase, porque leer fuera del
// buffer no rompe nada en una maquina normal. Por eso el test tiene que mirar el Status, no
// el valor devuelto: si se devuelve Ok con un len de 500 sobre un buffer de 3 B, elStatus
// ya dice que se leyo de la nada.
TEST(value, len_que_se_pasa_por_1_da_corrupt) {
    // len = 1 sobre un buffer con 0 B de datos: falta 1 byte, no 500.
    const std::vector<uint8_t> b = {0, 1, 0};
    const uint8_t*             p = b.data();
    Value                      v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

TEST(value, len_que_se_pasa_por_1_no_altera_el_puntero) {
    // Cuando devuelve Corrupt, el puntero NO avanza: quien llama no debe saltarse un
    // campo que no se leyo.
    const std::vector<uint8_t> b = {0, 1, 0};
    const uint8_t*             p = b.data();
    Value                      v(0);
    (void)v.deserialize(p, b.data() + b.size(), v);
    CHECK_EQ(p, b.data());
}

TEST(value, varchar_con_len_1_y_sin_datos_da_corrupt) {
    // Mismo caso por el lado del VARCHAR, para que el chequeo no dependa del tag.
    const std::vector<uint8_t> b = {1, 1, 0};
    const uint8_t*             p = b.data();
    Value                      v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

TEST(value, len_que_excede_por_1_en_un_buffer_lleno_da_corrupt) {
    // Primero un INT completo (7 B), despues un VARCHAR que declara 1 B de dato pero no
    // lo trae. El len excede al buffer en exactamente 1.
    std::vector<uint8_t> b;
    Value(42).serializeTo(b);
    b.push_back(1);  // tag VARCHAR
    b.push_back(1);  // len = 1, byte bajo
    b.push_back(0);  // len = 1, byte alto
                       // y NO hay byte de dato

    // Se lee el INT primero: Ok, y el puntero queda en los 7 B.
    const uint8_t* p = b.data();
    Value          w(0);
    CHECK(w.deserialize(p, b.data() + b.size(), w) == Status::Ok);
    CHECK_EQ(w.asInt(), 42);
    CHECK_EQ(p - b.data(), ptrdiff_t(7));

    // Ahora el VARCHAR: falta 1 byte de dato, asi que Corrupt.
    Value v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Corrupt);
}

// MUTACION 5: el VARCHAR con terminador hace que el puntero quede un byte corrido. Con el
// ultimo campo de una cadena eso NO se ve, porque el assert de kind_ revienta antes. Con
// un INT despues, si, porque el INT se lee desde el terminador.
TEST(value, tras_un_varchar_se_lee_bien_el_siguiente_campo) {
    std::vector<uint8_t> b;
    Value(std::string("")).serializeTo(b);   // el vacio es el caso limite
    Value(7).serializeTo(b);

    const uint8_t* p = b.data();
    const uint8_t* end = b.data() + b.size();
    Value          v(0), w(0);

    CHECK(v.deserialize(p, end, v) == Status::Ok);
    CHECK_EQ(v.asVarChar(), std::string(""));
    CHECK(w.deserialize(p, end, w) == Status::Ok);
    CHECK_EQ(w.asInt(), 7);   // si el vacio hubiera avanzado 4, esto seria otra cosa
    CHECK_EQ(p, end);
}

TEST(value, el_tamanio_declarado_cuadra_con_lo_serializado) {
    // Si el terminador se colara en el buffer, serializedSize() y el avance del puntero
    // dejarian de cuadrar. Este test ata las dos cosas.
    const std::vector<uint8_t> b = ser(Value(std::string("ab")));
    CHECK_EQ(b.size(), Value(std::string("ab")).serializedSize());

    const uint8_t* p = b.data();
    Value          v(0);
    CHECK(v.deserialize(p, b.data() + b.size(), v) == Status::Ok);
    CHECK_EQ(p - b.data(), ptrdiff_t(b.size()));
}

// --- el puntero avanza ---------------------------------------------------
TEST(value, el_puntero_avanza_para_encadenar) {
    // Tres values seguidas en un mismo buffer. Esto es lo que usa Tuple::deserialize, y si
    // el puntero no avanza 3+len la segunda se leeria de la posicion equivocada.
    std::vector<uint8_t> b;
    Value(7).serializeTo(b);
    Value(std::string("hola")).serializeTo(b);
    Value(-3).serializeTo(b);

    const uint8_t* p = b.data();
    const uint8_t* end = b.data() + b.size();

    Value a(0), c(0), d(0);
    CHECK(a.deserialize(p, end, a) == Status::Ok);
    CHECK_EQ(a.asInt(), 7);
    CHECK(c.deserialize(p, end, c) == Status::Ok);
    CHECK_EQ(c.asVarChar(), std::string("hola"));
    CHECK(d.deserialize(p, end, d) == Status::Ok);
    CHECK_EQ(d.asInt(), -3);
    CHECK_EQ(p, end);
}

TEST(value, el_varchar_vacio_tambien_avanza_3) {
    // Con un VARCHAR vacio en el medio: el puntero avanza 3, no 0. Si no, la lectura
    // siguiente se quedaria en el mismo lugar.
    std::vector<uint8_t> b;
    Value(std::string("")).serializeTo(b);
    Value(9).serializeTo(b);

    const uint8_t* p = b.data();
    const uint8_t* end = b.data() + b.size();
    Value          v(std::string("x")), w(0);

    CHECK(v.deserialize(p, end, v) == Status::Ok);
    CHECK_EQ(v.asVarChar(), std::string(""));
    CHECK(w.deserialize(p, end, w) == Status::Ok);
    CHECK_EQ(w.asInt(), 9);
    CHECK_EQ(p, end);
}

namespace suites {
void value() {
    // Los TEST() de este archivo se registran solos; ver TestHarness.h.
}
}  // namespace suites
