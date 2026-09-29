// src/main.cpp    [STUB de P4]
// Menu de 6 opciones, --page-size, --demo y --n.
//
// Cuerpo provisional de la rama contract. Cuando P4 escriba el menu real, este archivo
// pasa a ser suyo entero: hasta entonces solo announce que el ejecutable existe.
//
// P1 ya lo usa para probar el PR 2 (cmake + ctest verdes) y las demos 1 y 4 del PR 6, asi
// que cuando el menu este listo tiene que honorar --demo 0|1|4|5|6 y --page-size, tal
// como dice el plan.
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "phase1_btree (rama contract)\n";
    std::cout << "El menu de 6 opciones es de P4 y todavia no esta escrito.\n";

    for (int i = 1; i < argc; ++i) {
        std::cout << "  argumento recibido: " << argv[i] << "\n";
    }
    return 0;
}
