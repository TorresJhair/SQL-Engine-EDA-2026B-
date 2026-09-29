// src/storage/Tuple.h                          [P1]
#pragma once

#include "common/Types.h"
#include "storage/Value.h"

#include <cstdint>
#include <vector>

// Serializado: [nFields: u8] + los campos, cada uno [tag: u8][len: u16][datos].
class Tuple {
public:
    void               append(const Value&);
    const Value&       at(size_t i) const;
    size_t             fieldCount() const;
    size_t             serializedSize() const;     // 1 + suma de los campos

    // Precondicion: fieldCount() >= 1, porque la tupla minima son 4 B (un VARCHAR vacio) y
    // el puntero de la cadena de libres necesita 2 B dentro de la region liberada.
    // Se devuelve Status::PreconditionFailed y NO se asserta: es la unica forma de que un
    // test lo compruebe sin fork y sin depender de -DNDEBUG (ver la regla de §7).
    Status             serializeTo(std::vector<uint8_t>& out) const;   // incluye [nFields]

    // Lee [nFields] y los nFields campos. **Ignora los bytes sobrantes al final** sin error:
    // al reutilizar una region liberada mas grande, `len` es el tamano de la region, no el
    // de la tupla. Devuelve Status::Corrupt solo si un len DECLARADO se pasa de `len`.
    static Status      deserialize(const uint8_t* data, size_t len, Tuple& out);

private:
    std::vector<Value> fields_;
};
