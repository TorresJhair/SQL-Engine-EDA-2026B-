// src/storage/Value.cpp                       [P1]
#include "storage/Value.h"

#include <cassert>
#include <cstring>

namespace {

// El formato es little-endian y NORMATIVO (lo consumen P2, P3 y P4), asi que los bytes se
// escriben y se leen a mano. memcpy de un int32_t daria el mismo resultado en x86, pero en
// una maquina big-endian daria el contrario: el round-trip pasaria igual y el archivo seria
// ilegible para los demas. Por eso nada de memcpy.
void putU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

uint16_t getU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

void putU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

uint32_t getU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

Value::Value(int32_t v) : kind_(ValueKind::Int), int_(v) {}

Value::Value(std::string v) : kind_(ValueKind::VarChar), str_(std::move(v)) {}

ValueKind Value::kind() const { return kind_; }

int32_t Value::asInt() const {
    // Assert de error de programacion: es un guard contra llamar asInt() sobre un VARCHAR
    // DENTRO de una funcion ya llamada con el tipo equivocado. No es una precondicion
    // testeada, asi que no se cambia por un Status (regla 21, §7).
    assert(kind_ == ValueKind::Int);
    return int_;
}

const std::string& Value::asVarChar() const {
    assert(kind_ == ValueKind::VarChar);
    return str_;
}

size_t Value::serializedSize() const {
    // 3 B de cabecera (tag:1 + len:2) + los datos.
    const size_t len = (kind_ == ValueKind::Int) ? 4 : str_.size();
    return 3 + len;
}

void Value::serializeTo(std::vector<uint8_t>& out) const {
    if (kind_ == ValueKind::Int) {
        out.push_back(0);                              // tag 0 = INT
        putU16LE(out, 4);                              // len = 4
        putU32LE(out, static_cast<uint32_t>(int_));
    } else {
        out.push_back(1);                              // tag 1 = VARCHAR
        // El VARCHAR no lleva terminador: el longitud va en el len, no al final.
        putU16LE(out, static_cast<uint16_t>(str_.size()));
        out.insert(out.end(), str_.begin(), str_.end());
    }
}

Status Value::deserialize(const uint8_t*& p, const uint8_t* end, Value& out) {
    // Corchete de 3 B (tag + len) como minimo, o no hay ni siquiera cabecera.
    if (end - p < 3) return Status::Corrupt;

    const uint8_t tag = p[0];
    const uint16_t len = getU16LE(p + 1);

    if (tag != 0 && tag != 1) return Status::Corrupt;

    // El len DECLARADO no puede pasarse del final del buffer. Esto es Corrupt y no un
    // assert justamente para que sea testeable sin fork y sin depender de -DNDEBUG.
    if (end - (p + 3) < static_cast<ptrdiff_t>(len)) return Status::Corrupt;

    if (tag == 0) {
        // INT: el len tiene que ser 4. Un INT con otro len es un archivo corrupto.
        if (len != 4) return Status::Corrupt;
        const uint32_t raw = getU32LE(p + 3);
        // static_cast de uint32_t a int32_t con los valores fuera de rango esta bien
        // definido (es el mismo patron de bits), asi que INT_MIN sobrevive el viaje.
        out = Value(static_cast<int32_t>(raw));
    } else {
        out = Value(std::string(reinterpret_cast<const char*>(p + 3), len));
    }

    // El puntero avanza SIEMPRE 3 + len, incluso con el VARCHAR vacio. Es lo que permite
    // que Tuple::deserialize encadene campos sin copiar.
    p += 3 + len;
    return Status::Ok;
}
