// hilos.hpp — Utilidad minima de paralelismo (std::thread, sin dependencias).
//
// paraleloPorBloques(n, hilos, f) parte el rango [0, n) en bloques contiguos y
// ejecuta f(inicio, fin) en un hilo por bloque. El hilo que llama procesa el
// primer bloque, asi que con hilos == 1 no se crea ningun hilo y el codigo es
// exactamente el secuencial.
//
// Regla de uso: cada hilo solo puede ESCRIBIR en las posiciones [inicio, fin)
// de las estructuras compartidas. Asi no hacen falta mutex ni atomicos.
#pragma once
#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace pf {

// Numero de hilos a usar: 0 significa "todos los nucleos disponibles".
inline unsigned resolverHilos(unsigned pedidos) {
  if (pedidos > 0) return pedidos;
  const unsigned hw = std::thread::hardware_concurrency();
  return hw == 0 ? 1u : hw;
}

template <typename Funcion>
void paraleloPorBloques(size_t n, unsigned hilos, Funcion &&f) {
  if (n == 0) return;
  hilos = resolverHilos(hilos);
  if (hilos > n) hilos = static_cast<unsigned>(n);
  if (hilos <= 1) {
    f(size_t{0}, n);
    return;
  }
  const size_t bloque = (n + hilos - 1) / hilos;
  std::vector<std::thread> trabajadores;
  trabajadores.reserve(hilos - 1);
  for (unsigned h = 1; h < hilos; ++h) {
    const size_t inicio = h * bloque;
    const size_t fin = std::min(inicio + bloque, n);
    if (inicio >= fin) break;
    trabajadores.emplace_back([&f, inicio, fin]() { f(inicio, fin); });
  }
  f(size_t{0}, std::min(bloque, n)); // el hilo actual hace el primer bloque
  for (auto &t : trabajadores) t.join();
}

} // namespace pf
