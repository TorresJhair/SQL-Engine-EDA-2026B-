// src/storage/SlottedPage.h                   [P1]  (puro, sin I/O)
#pragma once

#include "common/Types.h"

#include <cstdint>
#include <vector>

// Layout: [nSlots:u16][dataStart:u16][dataEnd:u16][freeHead:u16] + directorio
// de 4 B por slot ([offset:u16][length:u16]) + datos hacia atras desde el final.
// dataStart = fin del directorio = 8 + 4*nSlots; dataEnd = direccion mas baja ocupada.
// Slot LIBRE: el directorio conserva su length, y los 2 primeros bytes de la region
// liberada guardan el indice de directorio de la siguiente region libre (0xFFFF = fin de cadena).
// freeHead = indice de directorio del primer libre. Los dos punteros de la cadena son slotID y no
// offsets (cambio 43), igual que freeHead: asi el first-fit de insert la recorre en O(k) en vez de
// O(k*k) resolviendo offset -> slot en cada paso.
class SlottedPage {
public:
    static SlottedPage init(size_t page_size);
    static SlottedPage wrap(const Page&);

    // assert(bytes.size() >= 2): el unico productor real es Tuple::serializeTo (min. 4 B).
    Status insert(const std::vector<uint8_t>& bytes, SlotID& out_slot); // PageFull si no cabe

    // Devuelve la region COMPLETA segun el length del directorio, que al reutilizar sigue
    // siendo el de la region liberada: puede traer bytes de mas al final, y quien la llame
    // (Tuple::deserialize) tiene que ignorarlos. Solo hay length >= 2 en un slot vivo.
    Status lookup(SlotID, std::vector<uint8_t>& out) const;

    Status erase(SlotID);                     // NO compacta: enlaza la region liberada
                                              // en la cadena y no mueve un byte de datos

    size_t freeSpace() const;                 // (dataEnd - dataStart) + Σ length libres
    size_t slotCount() const;
    size_t freeSlotCount() const;             // recorre la cadena
    static size_t maxTupleSize(size_t page_size);
    Page   toPage() const;

private:
    Page page_;
};
