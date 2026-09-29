// tests/test_tuple.cpp        [P1]
// Serializado: [nFields:u8] + los campos, cada uno [tag:u8][len:u16][datos].
//
// El caso que mas importa es el VARCHAR vacio: fija que la tupla minima mide 4 B. De ahi
// depende que el puntero de 2 B de la cadena de libres tenga siempre lugar dentro de la
// region liberada, que es lo que hace seguro el cambio 43.
#include "TestHarness.h"

#include "common/Types.h"
#include "storage/Tuple.h"
#include "storage/Value.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

// operator<< para los enums de Types.h, que no tienen uno. CHECK_EQ imprime ambos
// valores cuando fallan, asi que sin esto el harness no compila al primer CHECK_EQ de
// un enum en vez de decir cual era el valor esperado.
std::ostream& operator<<(std::ostream& os, Status s) {
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

std::ostream& operator<<(std::ostream& os, ValueKind k) {
    return os << (k == ValueKind::Int ? "Int" : "VarChar");
}

namespace {

std::vector<uint8_t> ser(const Tuple& t, Status* st = nullptr) {
    std::vector<uint8_t> out;
    const Status          s = t.serializeTo(out);
    if (st) *st = s;
    return out;
}

// Lee una tupla y devuelve si el Status fue Ok.
bool de(const std::vector<uint8_t>& b, Tuple& out) {
    return Tuple::deserialize(b.data(), b.size(), out) == Status::Ok;
}

}  // namespace

// --- la tupla minima ------------------------------------------------------
TEST(tuple, varchar_vacio_da_4_bytes_exactos) {
    // ESTE es el test del proyecto. 1 B de nFields + 3 B de un VARCHAR vacio.
    // Con 4 B, toda region liberada tiene lugar para el puntero de 2 B, y por eso no
    // existe el caso length < 2.
    Tuple        t;
    t.append(Value(std::string("")));
    std::vector<uint8_t> b;
    CHECK(t.serializeTo(b) == Status::Ok);

    CHECK_EQ(b.size(), size_t(4));
    CHECK_EQ(t.serializedSize(), size_t(4));
    CHECK_EQ(int(b[0]), 1);   // nFields = 1
    CHECK_EQ(int(b[1]), 1);   // tag 1 = VARCHAR
    CHECK_EQ(int(b[2]), 0);   // len = 0, byte bajo
    CHECK_EQ(int(b[3]), 0);   // len = 0, byte alto
}

TEST(tuple, int_solo_no_alcanza_4_bytes) {
    // El INT son 7 B, asi que NO es la tupla minima. Sirve para ver que la minima la fija
    // el VARCHAR vacio y no cualquier campo.
    Tuple t;
    t.append(Value(1));
    CHECK_EQ(t.serializedSize(), size_t(8));  // 1 + 7
}

// --- serializedSize -------------------------------------------------------
TEST(tuple, serialized_size_es_1_mas_los_campos) {
    Tuple a;
    a.append(Value(1));
    CHECK_EQ(a.serializedSize(), size_t(8));

    Tuple b;
    b.append(Value(1));
    b.append(Value(std::string("nombre0000000000")));
    CHECK_EQ(b.serializedSize(), size_t(1 + 7 + 19));

    Tuple c;
    c.append(Value(std::string("")));
    c.append(Value(std::string("")));
    c.append(Value(0));
    CHECK_EQ(c.serializedSize(), size_t(1 + 3 + 3 + 7));
}

// --- la precondicion testeada --------------------------------------------
TEST(tuple, cero_campos_da_precondition_failed) {
    // Cambio 40: NO es un assert. Se comprueba con CHECK_EQ sobre el Status, sin fork y
    // sin depender de -DNDEBUG.
    Tuple                   vacia;
    std::vector<uint8_t>    out;
    CHECK(vacia.serializeTo(out) == Status::PreconditionFailed);
    CHECK_EQ(vacia.fieldCount(), size_t(0));
}

TEST(tuple, cero_campos_no_toca_el_buffer) {
    // Si se escribiera el nFields antes de fallar, out quedaria con 1 byte basura y quien
    // llama no podria distinguir "no se escribio nada" de "se escribio mal".
    std::vector<uint8_t> out;
    const std::vector<uint8_t> antes{9, 9, 9};

    Tuple t;
    t.append(Value(1));
    CHECK(t.serializeTo(out) == Status::Ok);
    const std::vector<uint8_t> con_datos = out;

    out.clear();
    Tuple vacia;
    CHECK(vacia.serializeTo(out) == Status::PreconditionFailed);
    CHECK_EQ(out.size(), size_t(0));

    // Y una tupla valida despues de un fallo sigue funcionando.
    CHECK(t.serializeTo(out) == Status::Ok);
    CHECK(out == con_datos);
    (void)antes;
}

TEST(tuple, el_status_de_serialize_to_es_ok_con_campos) {
    Tuple t;
    t.append(Value(1));
    Status s = Status::PageFull;
    ser(t, &s);
    CHECK(s == Status::Ok);
}

// --- bytes exactos --------------------------------------------------------
TEST(tuple, los_bytes_de_una_tupla_de_2_campos) {
    Tuple t;
    t.append(Value(42));
    t.append(Value(std::string("ab")));

    std::vector<uint8_t> b;
    CHECK(t.serializeTo(b) == Status::Ok);

    // 1 (nFields) + 7 (INT) + 5 (VARCHAR de 2) = 13
    CHECK_EQ(b.size(), size_t(13));
    CHECK_EQ(int(b[0]), 2);       // nFields = 2
    CHECK_EQ(int(b[1]), 0);       // tag INT
    CHECK_EQ(int(b[2]), 4);       // len = 4
    CHECK_EQ(int(b[3]), 0);
    CHECK_EQ(int(b[4]), 0x2A);    // 42
    CHECK_EQ(int(b[8]), 1);       // tag VARCHAR
    CHECK_EQ(int(b[9]), 2);       // len = 2
    CHECK_EQ(int(b[10]), 0);
    CHECK_EQ(int(b[11]), 'a');
    CHECK_EQ(int(b[12]), 'b');
}

// --- round trip -----------------------------------------------------------
TEST(tuple, round_trip_de_1_campo_varchar) {
    Tuple t;
    t.append(Value(std::string("nombre0000000000")));
    std::vector<uint8_t> b = ser(t);

    Tuple out;
    CHECK(de(b, out));
    CHECK_EQ(out.fieldCount(), size_t(1));
    CHECK_EQ(out.at(0).asVarChar(), std::string("nombre0000000000"));
}

TEST(tuple, round_trip_de_2_y_3_campos) {
    Tuple t;
    t.append(Value(42));
    t.append(Value(std::string("hola")));
    t.append(Value(-7));
    std::vector<uint8_t> b = ser(t);

    Tuple out;
    CHECK(de(b, out));
    CHECK_EQ(out.fieldCount(), size_t(3));
    CHECK_EQ(out.at(0).asInt(), 42);
    CHECK_EQ(out.at(1).asVarChar(), std::string("hola"));
    CHECK_EQ(out.at(2).asInt(), -7);
    CHECK_EQ(out.at(0).kind(), ValueKind::Int);
    CHECK_EQ(out.at(1).kind(), ValueKind::VarChar);
}

TEST(tuple, round_trip_byte_a_byte) {
    // No solo vuelven los valores: el buffer reconstruido tiene que ser IDENTICO. Un
    // round-trip que devuelve bien los valores pero ordena distinto los campos pasaria
    // con una comparacion de valores.
    Tuple t;
    t.append(Value(0));
    t.append(Value(std::string("")));
    t.append(Value(INT32_MIN));
    t.append(Value(std::string("x")));

    std::vector<uint8_t> b = ser(t);
    Tuple                out;
    CHECK(de(b, out));

    std::vector<uint8_t> b2 = ser(out);
    CHECK(b == b2);
    CHECK_EQ(b2.size(), b.size());
}

TEST(tuple, int_negativo_e_int_min) {
    const int32_t casos[] = {0, -1, 1, -1000, INT32_MAX, INT32_MIN};
    for (int32_t c : casos) {
        Tuple t;
        t.append(Value(c));
        std::vector<uint8_t> b = ser(t);
        Tuple                out;
        CHECK(de(b, out));
        CHECK_EQ(out.at(0).asInt(), c);
    }
}

TEST(tuple, varchar_vacio_que_es_la_tupla_minima) {
    Tuple t;
    t.append(Value(std::string("")));
    std::vector<uint8_t> b = ser(t);
    Tuple                out;
    CHECK(de(b, out));
    CHECK_EQ(out.fieldCount(), size_t(1));
    CHECK_EQ(out.at(0).asVarChar(), std::string(""));
}

// --- la cola se ignora (cambio 41) ---------------------------------------
TEST(tuple, la_cola_se_ignora_sin_error) {
    // Este es el caso del cambio 41: se borra una tupla de 27 B, se inserta una de 4 B que
    // reutiliza la region, y `lookup` devuelve los 27 B de la REGION. Los 23 de cola son
    // de la tupla vieja y no pueden dar Corrupt.
    std::vector<uint8_t> b = {1, 1, 0, 0};   // VARCHAR vacio = 4 B
    b.insert(b.end(), 23, 0xAB);              // 23 B de cola de la tupla anterior

    CHECK_EQ(b.size(), size_t(27));
    Tuple out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Ok);
    CHECK_EQ(out.fieldCount(), size_t(1));
    CHECK_EQ(out.at(0).asVarChar(), std::string(""));
}

TEST(tuple, la_cola_ignorada_no_afecta_al_tamanio_declarado) {
    // La tupla devuelta sigue midiendo 4: el length de la region no se infiltra en el
    // campo nFields ni en el len del VARCHAR.
    std::vector<uint8_t> b = {1, 1, 0, 0};
    b.insert(b.end(), 23, 0xAB);

    Tuple out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Ok);
    CHECK_EQ(out.serializedSize(), size_t(4));

    std::vector<uint8_t> b2 = ser(out);
    CHECK_EQ(b2.size(), size_t(4));
    CHECK_EQ(int(b2[0]), 1);
    CHECK_EQ(int(b2[1]), 1);
    CHECK_EQ(int(b2[2]), 0);
    CHECK_EQ(int(b2[3]), 0);
}

TEST(tuple, cola_con_el_mismo_tag_no_confunde) {
    // La cola es ruido, pero si la deserializacion leyera de mas, estos bytes falsearian
    // nFields. Con 4 + 23 el nFields tiene que seguir siendo 1.
    std::vector<uint8_t> b = {1, 1, 0, 0};
    b.insert(b.end(), {7, 250, 1, 0, 99});   // ruido con tags raros

    Tuple out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Ok);
    CHECK_EQ(out.fieldCount(), size_t(1));
}

// --- corrupcion -----------------------------------------------------------
TEST(tuple, buffer_vacio_da_corrupt) {
    Tuple out;
    CHECK(Tuple::deserialize(nullptr, 0, out) == Status::Corrupt);
}

TEST(tuple, nfields_0_da_una_tupla_vacia) {
    // NO es Corrupt, y esta distincion importa. La regla nFields >= 1 es de
    // serializeTo (no se puede ESCRIBIR una tupla de 0 campos, porque mediria 1 B y el
    // puntero de la cadena no entraria), pero un buffer con nFields = 0 se deserializa
    // sin error a una tupla vacia: no hay ningun campo declarado que pueda pasarse del
    // final, asi que no hay corrupcion que detectar.
    //
    // Que vuelva vacia NO la hace escribible: al re-serializar tiene que dar
    // PreconditionFailed, que es lo que se comprueba abajo. Read != write.
    const std::vector<uint8_t> b = {0};
    Tuple                     out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Ok);
    CHECK_EQ(out.fieldCount(), size_t(0));

    std::vector<uint8_t> b2;
    CHECK(out.serializeTo(b2) == Status::PreconditionFailed);
    CHECK_EQ(b2.size(), size_t(0));
}

TEST(tuple, nfields_0_sin_el_byte_del_conteo_da_corrupt) {
    // Si ni siquiera esta el byte de nFields, si hay corrupcion: el buffer no alcanza.
    const std::vector<uint8_t> b = {};
    Tuple                       out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Corrupt);
}

TEST(tuple, nfields_mayor_que_los_campos_presentes_da_corrupt) {
    // Dice 2 campos pero solo hay bytes para 1.
    const std::vector<uint8_t> b = {2, 1, 0, 0};
    Tuple                       out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Corrupt);
}

TEST(tuple, un_campo_con_len_que_excede_da_corrupt) {
    // nFields = 1, y el VARCHAR declara 500 B con 0 de dato.
    const std::vector<uint8_t> b = {1, 1, 0xF4, 0x01};
    Tuple                       out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Corrupt);
}

TEST(tuple, el_segundo_campo_corrupto_da_corrupt) {
    // El primero es valido y el segundo no: el Status tiene que propagarse.
    std::vector<uint8_t> b = {2, 0, 4, 0, 0, 0, 42, 1, 0xF4, 0x01};
    Tuple                out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Corrupt);
}

TEST(tuple, nfields_255_da_corrupt) {
    // 255 campos declarados y ningun byte de dato: no puede ser una tupla valida.
    const std::vector<uint8_t> b = {255};
    Tuple                       out;
    CHECK(Tuple::deserialize(b.data(), b.size(), out) == Status::Corrupt);
}

// --- append / at ----------------------------------------------------------
TEST(tuple, append_y_at) {
    Tuple t;
    t.append(Value(1));
    t.append(Value(std::string("a")));
    CHECK_EQ(t.fieldCount(), size_t(2));
    CHECK_EQ(t.at(0).asInt(), 1);
    CHECK_EQ(t.at(1).asVarChar(), std::string("a"));
}

TEST(tuple, una_tupla_deserializada_se_puede_anadir_mas) {
    // El cambio 41 necesita esto: deserializar una region con cola y seguir agregando.
    std::vector<uint8_t> b = {1, 1, 0, 0};
    b.insert(b.end(), 23, 0xAB);

    Tuple t;
    CHECK(Tuple::deserialize(b.data(), b.size(), t) == Status::Ok);
    t.append(Value(5));
    CHECK_EQ(t.fieldCount(), size_t(2));

    std::vector<uint8_t> b2 = ser(t);
    Tuple                out;
    CHECK(de(b2, out));
    CHECK_EQ(out.fieldCount(), size_t(2));
    CHECK_EQ(out.at(0).asVarChar(), std::string(""));
    CHECK_EQ(out.at(1).asInt(), 5);
}

namespace suites {
void tuple() {
    // Los TEST() de este archivo se registran solos; ver TestHarness.h.
}
}  // namespace suites
