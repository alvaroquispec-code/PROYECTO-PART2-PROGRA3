#pragma once

#include <memory>
#include <vector>

#include "catalogo.hpp"

namespace pf {

    class ArbolAVLAnios {
    private:
        struct Nodo {
            int anio;
            std::vector<int> docs; // IDs de peliculas estrenadas en ese anio
            int altura = 1;
            std::unique_ptr<Nodo> izq;
            std::unique_ptr<Nodo> der;

            Nodo(int a, int doc) : anio(a), docs{doc} {}
        };

        std::unique_ptr<Nodo> raiz_;
        size_t totalNodos_ = 0;

        static int altura(const std::unique_ptr<Nodo> &n);
        static void actualizarAltura(Nodo *n);
        static int factorBalance(const Nodo *n);

        static std::unique_ptr<Nodo> rotarDerecha(std::unique_ptr<Nodo> y);
        static std::unique_ptr<Nodo> rotarIzquierda(std::unique_ptr<Nodo> x);

        std::unique_ptr<Nodo> insertarRec(std::unique_ptr<Nodo> nodo, int anio,
                                          int doc);
        void buscarRangoRec(const Nodo *nodo, int minAnio, int maxAnio,
                            std::vector<int> &salida) const;

    public:
        void construirDesdeCatalogo(const Catalogo &catalogo);
        void insertar(int anio, int doc);
        std::vector<int> buscarPorRango(int minAnio, int maxAnio) const;

        int alturaTotal() const { return altura(raiz_); }
        size_t totalNodos() const { return totalNodos_; }
    };

} // namespace pf