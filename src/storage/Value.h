// src/storage/Value.h                         [P1]
#pragma once

#include "common/Types.h"

#include <cstdint>
#include <string>
#include <vector>

// Formato en bytes: [tag: u8][len: u16][datos: len], little-endian.
// tag 0 = INT (len 4) - tag 1 = VARCHAR (len arbitrario, sin terminador)
class Value {
public:
    Value(int32_t v);
    Value(std::string v);

    ValueKind          kind() const;
    int32_t            asInt() const;            // assert kind == Int
    const std::string& asVarChar() const;         // assert kind == VarChar
    size_t             serializedSize() const;    // 3 + len
    void               serializeTo(std::vector<uint8_t>& out) const;

    // Mismo estilo que Tuple::deserialize: recibe el fin del buffer y avanza p.
    // Si el len declarado se pasa de `end`, devuelve Status::Corrupt.
    static Status      deserialize(const uint8_t*& p, const uint8_t* end, Value& out);

private:
    ValueKind    kind_ = ValueKind::Int;
    int32_t      int_ = 0;
    std::string  str_;
};
