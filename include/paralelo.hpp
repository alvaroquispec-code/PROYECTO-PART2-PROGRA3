// paralelo.hpp — Programacion paralela para la insercion y busqueda de strings.
//
// Donde se usa paralelismo en el programa:
//
//  1. INSERCION (Indice::construir): Map-Reduce por lotes. N hilos normalizan
//     y tokenizan las peliculas (map) y un hilo inserta los tokens en el
//     TriePrefijos y en las listas de publicaciones en el orden original
//     (reduce). Resultado identico al secuencial.
//
//  2. BUSQUEDA dentro de una consulta (Buscador::reordenarPorFrase): la
//     ventana de 400 candidatos se reparte entre hilos para normalizar las
//     sinopsis y buscar la frase exacta.
//
//  3. BUSQUEDA de un lote de consultas (MotorParalelo::buscarLoteParalelo):
//     cada hilo tiene su PROPIO Buscador, porque Buscador reutiliza
//     acumuladores internos y no puede compartirse entre hilos.
//
// La utilidad base es paraleloPorBloques() en hilos.hpp.
#pragma once

#include <string>
#include <vector>

#include "buscador.hpp"
#include "catalogo.hpp"
#include "indice.hpp"

namespace pf {

struct ResultadoBenchmark {
  unsigned numHilos = 1;

  // 1. Construccion del indice (insercion de strings en los tries)
  size_t terminosInsertados = 0;
  double msInsercionSecuencial = 0.0;
  double msInsercionParalela = 0.0;
  bool insercionIdentica = false;

  // 2. Una consulta con re-ranking por frase (promedio por consulta)
  double msConsultaSecuencial = 0.0;
  double msConsultaParalela = 0.0;
  bool consultaIdentica = false;

  // 3. Lote de consultas
  size_t consultasEnLote = 0;
  double msLoteSecuencial = 0.0;
  double msLoteParalelo = 0.0;
  bool loteIdentico = false;
};

class MotorParalelo {
public:
  // Ejecuta varias consultas a la vez. Cada hilo crea su propio Buscador.
  // numHilos = 0 usa todos los nucleos. El resultado i corresponde a
  // consultas[i] y es identico al de una busqueda secuencial.
  static std::vector<std::vector<Resultado>>
  buscarLoteParalelo(const Catalogo &catalogo, const Indice &indice,
                     const std::vector<std::string> &consultas,
                     size_t limitePorConsulta = 20, unsigned numHilos = 0);

  // Mide 1 hilo vs N hilos en los tres puntos de arriba y verifica que los
  // resultados sean identicos. No toca ningun Buscador compartido, asi que se
  // puede ejecutar en segundo plano mientras la interfaz sigue funcionando.
  // Tarda unos segundos porque reconstruye el indice dos veces.
  static ResultadoBenchmark ejecutarBenchmark(const Catalogo &catalogo,
                                              const Indice &indice,
                                              unsigned numHilos = 0);
};

// Compara dos listas de resultados (mismos documentos en el mismo orden).
bool mismosResultados(const std::vector<Resultado> &a,
                      const std::vector<Resultado> &b);

} // namespace pf
