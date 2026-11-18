#include "paralelo.hpp"

#include <chrono>
#include <memory>

#include "hilos.hpp"

namespace pf {

namespace {
using Reloj = std::chrono::steady_clock;

double msDesde(Reloj::time_point t0) {
  return std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
}

const std::vector<std::string> &consultasDePrueba() {
  static const std::vector<std::string> consultas = {
      "ghost ship",   "space adventure", "murder mystery", "love story",
      "world war",    "time travel",     "secret agent",   "high school",
      "bank robbery", "alien invasion",  "family drama",   "serial killer",
      "lost island",  "treasure hunt",   "musical comedy", "courtroom trial"};
  return consultas;
}
} // namespace

bool mismosResultados(const std::vector<Resultado> &a,
                      const std::vector<Resultado> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].doc != b[i].doc) return false;
  }
  return true;
}

std::vector<std::vector<Resultado>> MotorParalelo::buscarLoteParalelo(
    const Catalogo &catalogo, const Indice &indice,
    const std::vector<std::string> &consultas, size_t limitePorConsulta,
    unsigned numHilos) {
  std::vector<std::vector<Resultado>> salida(consultas.size());

  // Cada hilo recibe un rango de consultas y crea SU PROPIO Buscador.
  // Solo escribe en salida[i] de su rango: no hay datos compartidos mutables.
  paraleloPorBloques(consultas.size(), numHilos, [&](size_t ini, size_t fin) {
    Buscador buscadorLocal(catalogo, indice);
    for (size_t i = ini; i < fin; ++i) {
      Consulta q = buscadorLocal.interpretar(consultas[i]);
      if (q.valida) salida[i] = buscadorLocal.buscar(q, limitePorConsulta);
    }
  });
  return salida;
}

ResultadoBenchmark MotorParalelo::ejecutarBenchmark(const Catalogo &catalogo,
                                                    const Indice &indice,
                                                    unsigned numHilos) {
  ResultadoBenchmark bench;
  bench.numHilos = resolverHilos(numHilos);

  // 1. INSERCION: construir el indice completo con 1 hilo y con N hilos.
  {
    auto secuencial = std::make_unique<Indice>();
    auto t0 = Reloj::now();
    secuencial->construir(catalogo, 1);
    bench.msInsercionSecuencial = msDesde(t0);

    auto paralelo = std::make_unique<Indice>();
    t0 = Reloj::now();
    paralelo->construir(catalogo, bench.numHilos);
    bench.msInsercionParalela = msDesde(t0);

    bench.terminosInsertados = secuencial->vocabulario().numTerminos();
    bool igual = secuencial->vocabulario().numTerminos() ==
                     paralelo->vocabulario().numTerminos() &&
                 secuencial->vocabulario().numNodos() ==
                     paralelo->vocabulario().numNodos() &&
                 secuencial->sufijos().numNodos() ==
                     paralelo->sufijos().numNodos();
    for (size_t id = 0; igual && id < bench.terminosInsertados; ++id) {
      const int t = static_cast<int>(id);
      igual = secuencial->vocabulario().termino(t) ==
                  paralelo->vocabulario().termino(t) &&
              secuencial->frecuenciaDocumental(t) ==
                  paralelo->frecuenciaDocumental(t);
    }
    bench.insercionIdentica = igual;
  }

  const auto &consultas = consultasDePrueba();
  bench.consultasEnLote = consultas.size();
  constexpr int kRepeticiones = 3;

  // 2. UNA CONSULTA a la vez: re-ranking por frase con 1 hilo vs N hilos.
  {
    Buscador sec(catalogo, indice);
    sec.setHilos(1);
    Buscador par(catalogo, indice);
    par.setHilos(bench.numHilos);

    std::vector<std::vector<Resultado>> rSec, rPar;
    auto t0 = Reloj::now();
    for (int rep = 0; rep < kRepeticiones; ++rep) {
      rSec.clear();
      for (const auto &c : consultas) rSec.push_back(sec.buscar(sec.interpretar(c), 50));
    }
    bench.msConsultaSecuencial =
        msDesde(t0) / (kRepeticiones * consultas.size());

    t0 = Reloj::now();
    for (int rep = 0; rep < kRepeticiones; ++rep) {
      rPar.clear();
      for (const auto &c : consultas) rPar.push_back(par.buscar(par.interpretar(c), 50));
    }
    bench.msConsultaParalela = msDesde(t0) / (kRepeticiones * consultas.size());

    bench.consultaIdentica = true;
    for (size_t i = 0; i < consultas.size(); ++i) {
      if (!mismosResultados(rSec[i], rPar[i])) bench.consultaIdentica = false;
    }
  }

  // 3. LOTE de consultas: una tras otra vs un Buscador por hilo.
  {
    std::vector<std::vector<Resultado>> rSec;
    auto t0 = Reloj::now();
    for (int rep = 0; rep < kRepeticiones; ++rep) {
      rSec = buscarLoteParalelo(catalogo, indice, consultas, 50, 1);
    }
    bench.msLoteSecuencial = msDesde(t0) / kRepeticiones;

    std::vector<std::vector<Resultado>> rPar;
    t0 = Reloj::now();
    for (int rep = 0; rep < kRepeticiones; ++rep) {
      rPar = buscarLoteParalelo(catalogo, indice, consultas, 50, bench.numHilos);
    }
    bench.msLoteParalelo = msDesde(t0) / kRepeticiones;

    bench.loteIdentico = rSec.size() == rPar.size();
    for (size_t i = 0; bench.loteIdentico && i < rSec.size(); ++i) {
      bench.loteIdentico = mismosResultados(rSec[i], rPar[i]);
    }
  }

  return bench;
}

} // namespace pf
