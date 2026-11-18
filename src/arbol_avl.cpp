#include "arbol_avl.hpp"

#include <algorithm>

namespace pf {

int ArbolAVLAnios::altura(const std::unique_ptr<Nodo> &n) {
  return n ? n->altura : 0;
}

void ArbolAVLAnios::actualizarAltura(Nodo *n) {
  n->altura = 1 + std::max(altura(n->izq), altura(n->der));
}

int ArbolAVLAnios::factorBalance(const Nodo *n) {
  return n ? altura(n->izq) - altura(n->der) : 0;
}

std::unique_ptr<ArbolAVLAnios::Nodo> ArbolAVLAnios::rotarDerecha(std::unique_ptr<Nodo> y) {
  auto x = std::move(y->izq);
  auto T2 = std::move(x->der);
  x->der = std::move(y);
  x->der->izq = std::move(T2);
  actualizarAltura(x->der.get());
  actualizarAltura(x.get());
  return x;
}

std::unique_ptr<ArbolAVLAnios::Nodo> ArbolAVLAnios::rotarIzquierda(std::unique_ptr<Nodo> x) {
  auto y = std::move(x->der);
  auto T2 = std::move(y->izq);
  y->izq = std::move(x);
  y->izq->der = std::move(T2);
  actualizarAltura(y->izq.get());
  actualizarAltura(y.get());
  return y;
}

std::unique_ptr<ArbolAVLAnios::Nodo>
ArbolAVLAnios::insertarRec(std::unique_ptr<Nodo> nodo, int anio, int doc) {
  if (!nodo) {
    ++totalNodos_;
    return std::make_unique<Nodo>(anio, doc);
  }

  if (anio < nodo->anio) {
    nodo->izq = insertarRec(std::move(nodo->izq), anio, doc);
  } else if (anio > nodo->anio) {
    nodo->der = insertarRec(std::move(nodo->der), anio, doc);
  } else {
    nodo->docs.push_back(doc);
    return nodo;
  }

  actualizarAltura(nodo.get());
  const int fb = factorBalance(nodo.get());

  // 4 casos de rotacion AVL
  if (fb > 1 && anio < nodo->izq->anio) return rotarDerecha(std::move(nodo));
  if (fb < -1 && anio > nodo->der->anio)
    return rotarIzquierda(std::move(nodo));
  if (fb > 1 && anio > nodo->izq->anio) {
    nodo->izq = rotarIzquierda(std::move(nodo->izq));
    return rotarDerecha(std::move(nodo));
  }
  if (fb < -1 && anio < nodo->der->anio) {
    nodo->der = rotarDerecha(std::move(nodo->der));
    return rotarIzquierda(std::move(nodo));
  }
  return nodo;
}

void ArbolAVLAnios::buscarRangoRec(const Nodo *nodo, int minAnio, int maxAnio,
                                   std::vector<int> &salida) const {
  if (!nodo) return;
  if (minAnio < nodo->anio) {
    buscarRangoRec(nodo->izq.get(), minAnio, maxAnio, salida);
  }
  if (nodo->anio >= minAnio && nodo->anio <= maxAnio) {
    salida.insert(salida.end(), nodo->docs.begin(), nodo->docs.end());
  }
  if (maxAnio > nodo->anio) {
    buscarRangoRec(nodo->der.get(), minAnio, maxAnio, salida);
  }
}

void ArbolAVLAnios::construirDesdeCatalogo(const Catalogo &catalogo) {
  raiz_.reset();
  totalNodos_ = 0;
  for (size_t i = 0; i < catalogo.tamano(); ++i) {
    if (catalogo[i].anio > 0) {
      insertar(catalogo[i].anio, static_cast<int>(i));
    }
  }
}

void ArbolAVLAnios::insertar(int anio, int doc) {
  raiz_ = insertarRec(std::move(raiz_), anio, doc);
}

std::vector<int> ArbolAVLAnios::buscarPorRango(int minAnio, int maxAnio) const {
  std::vector<int> salida;
  if (minAnio > maxAnio) std::swap(minAnio, maxAnio);
  buscarRangoRec(raiz_.get(), minAnio, maxAnio, salida);
  return salida;
}

} // namespace pf