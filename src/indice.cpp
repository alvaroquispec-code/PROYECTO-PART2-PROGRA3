#include "indice.hpp"

#include "normalizador.hpp"

#include "binario.hpp"
#include "hilos.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace pf {
namespace {
// Cuantos terminos se conservan por pelicula para el perfil de similitud.
constexpr size_t kTerminosPorPerfil = 40;

// Cabecera del cache. La version sube cuando cambia el formato o cualquier
// parametro que altere el contenido del indice.
constexpr uint32_t kMagiaCache = 0x55544543u; // "UTEC"
constexpr uint32_t kVersionCache = 4; // 4: nodos adaptativos en los tries
} // namespace

double Indice::idfDesdeDf(double dfEntrada) const {
  double df = dfEntrada;
  const double N = static_cast<double>(numDocs_);
  if (df < 0.0) df = 0.0;
  if (df > N - 1.0) df = N - 1.0;
  return std::log(1.0 + (N - df + 0.5) / (df + 0.5));
}

double Indice::idf(int idTermino) const {
  return idfDesdeDf(static_cast<double>(publicaciones_[idTermino].size()));
}

EstadisticasIndice Indice::construir(const Catalogo &catalogo, unsigned numHilos) {
  using Reloj = std::chrono::steady_clock;
  const auto t0 = Reloj::now();

  numDocs_ = catalogo.tamano();
  longitudDoc_.assign(numDocs_, 0);
  publicaciones_.reserve(200000);

  // ultimoDoc_[t] evita usar una tabla hash por documento: si el ultimo
  // documento que toco el termino t es el actual, basta con actualizar la
  // ultima publicacion de su lista.
  std::vector<int32_t> ultimoDoc;
  ultimoDoc.reserve(200000);

  // -------------------------------------------------------------------------
  // PROGRAMACION PARALELA (Map-Reduce por lotes)
  //
  // MAP (paralelo): normalizar y tokenizar cada pelicula no depende de las
  // demas, asi que N hilos tokenizan un lote de peliculas a la vez. Cada hilo
  // escribe solo en tokensLote[i] de SUS documentos: no hay datos compartidos
  // ni locks.
  //
  // REDUCE (secuencial): la insercion en el TriePrefijos y en las listas de
  // publicaciones se hace en el orden original de las peliculas. Asi los ids
  // de termino, el indice y el cache son IDENTICOS a la version secuencial
  // (hay una prueba que lo verifica), sin importar cuantos hilos se usen.
  //
  // Se trabaja por lotes para no guardar los 13 M de tokens en memoria a la vez.
  // -------------------------------------------------------------------------
  struct CampoTokens {
    Campo campo;
    std::vector<std::string> tokens;
  };
  using TokensDoc = std::vector<CampoTokens>;
  constexpr size_t kLote = 4096;
  std::vector<TokensDoc> tokensLote(kLote);

  auto tokenizarDoc = [&catalogo](size_t d, TokensDoc &salida) {
    salida.clear();
    const Pelicula &p = catalogo[d];
    auto agregar = [&salida](const std::string &texto, Campo campo) {
      salida.push_back({campo, {}});
      tokenizarNormalizado(normalizar(texto), salida.back().tokens);
    };
    agregar(p.titulo, C_TITULO);
    agregar(p.sinopsis, C_SINOPSIS);
    if (!p.director.empty()) agregar(p.director, C_DIRECTOR);
    for (const auto &actor : p.reparto) agregar(actor, C_REPARTO);
    for (const auto &g : p.generos) agregar(g, C_GENERO);
    if (!p.origen.empty()) agregar(p.origen, C_ORIGEN);
  };

  auto indexar = [&](int doc, const std::vector<std::string> &tokens,
                     Campo campo) {
    for (const std::string &tok : tokens) {
      if (tok.size() > 30) continue; // basura tipografica
      const int id = vocab_.insertar(tok);
      if (static_cast<size_t>(id) >= publicaciones_.size()) {
        publicaciones_.resize(id + 1);
        ultimoDoc.resize(id + 1, -1);
      }
      if (ultimoDoc[id] != doc) {
        ultimoDoc[id] = doc;
        publicaciones_[id].push_back({doc, 0, 0, 0, 0, 0});
      }
      Publicacion &p = publicaciones_[id].back();
      p.campos |= static_cast<uint8_t>(campo);
      switch (campo) {
      case C_TITULO:
        if (p.tfTitulo < 65000) ++p.tfTitulo;
        break;
      case C_SINOPSIS:
        if (p.tfSinopsis < 65000) ++p.tfSinopsis;
        break;
      default:
        if (p.tfTag < 65000) ++p.tfTag;
        break;
      }
    }
    if (campo == C_SINOPSIS || campo == C_TITULO) {
      longitudDoc_[doc] += static_cast<uint32_t>(tokens.size());
    }
  };

  for (size_t base = 0; base < numDocs_; base += kLote) {
    const size_t enLote = std::min(kLote, numDocs_ - base);

    // MAP en paralelo: cada hilo tokeniza su rango [ini, fin) del lote.
    paraleloPorBloques(enLote, numHilos, [&](size_t ini, size_t fin) {
      for (size_t i = ini; i < fin; ++i) tokenizarDoc(base + i, tokensLote[i]);
    });

    // REDUCE secuencial, en el orden original.
    for (size_t i = 0; i < enLote; ++i) {
      const int doc = static_cast<int>(base + i);
      for (const CampoTokens &ct : tokensLote[i]) {
        indexar(doc, ct.tokens, ct.campo);
      }
    }
  }

  double suma = 0.0;
  for (uint32_t l : longitudDoc_) suma += l;
  longitudPromedio_ = numDocs_ ? suma / numDocs_ : 1.0;

  // El trie de sufijos se llena una sola vez, cuando el vocabulario ya es
  // definitivo: insertar sufijos de palabras repetidas seria trabajo perdido.
  for (size_t id = 0; id < vocab_.numTerminos(); ++id) {
    const std::string &t = vocab_.termino(static_cast<int>(id));
    if (t.size() <= 24) sufijos_.insertarTermino(t, static_cast<int>(id));
  }

  // Nodos adaptativos: los nodos con muchos hijos reciben una tabla de 36
  // para bajar en O(1) (ver trie.hpp).
  vocab_.densificar();
  sufijos_.densificar();

  construirPerfiles(numHilos);

  EstadisticasIndice est;
  rellenarEstadisticas(est);
  est.segundos = std::chrono::duration<double>(Reloj::now() - t0).count();
  return est;
}

void Indice::rellenarEstadisticas(EstadisticasIndice &est) const {
  est.terminos = vocab_.numTerminos();
  est.publicaciones = 0;
  for (const auto &lp : publicaciones_) est.publicaciones += lp.size();
  est.nodosPrefijos = vocab_.numNodos();
  est.nodosSufijos = sufijos_.numNodos();
  size_t bytes = vocab_.bytesAproximados() + sufijos_.bytesAproximados();
  bytes += est.publicaciones * sizeof(Publicacion);
  for (const auto &per : perfiles_) {
    bytes += per.capacity() * sizeof(TerminoPeso);
  }
  est.megabytes = bytes / (1024 * 1024);
}

// ---------------------------------------------------------------------------
// Cache binario
// ---------------------------------------------------------------------------
bool Indice::guardarCache(const std::string &ruta, const Catalogo &catalogo,
                          uint64_t huellaCsv) const {
  EscritorBinario e;
  e.reservar(220u * 1024u * 1024u);
  e.pod(kMagiaCache);
  e.pod(kVersionCache);
  e.pod(huellaCsv);
  e.pod<uint32_t>(static_cast<uint32_t>(sizeof(Publicacion)));

  catalogo.escribir(e);
  vocab_.escribir(e);
  sufijos_.escribir(e);

  e.pod<uint64_t>(publicaciones_.size());
  for (const auto &lista : publicaciones_) e.vectorPod(lista);
  e.vectorPod(longitudDoc_);
  e.pod<uint64_t>(perfiles_.size());
  for (const auto &per : perfiles_) e.vectorPod(per);
  e.vectorPod(normas_);
  e.pod<uint64_t>(static_cast<uint64_t>(numDocs_));
  e.pod(longitudPromedio_);
  return e.volcarA(ruta);
}

bool Indice::cargarCache(const std::string &ruta, Catalogo &catalogo,
                         uint64_t huellaCsv, EstadisticasIndice &est) {
  using Reloj = std::chrono::steady_clock;
  const auto t0 = Reloj::now();

  LectorBinario l;
  if (!l.abrir(ruta)) return false;

  uint32_t magia = 0, version = 0, tamPublicacion = 0;
  uint64_t huella = 0;
  if (!l.pod(magia) || !l.pod(version) || !l.pod(huella) || !l.pod(tamPublicacion))
    return false;
  if (magia != kMagiaCache || version != kVersionCache) return false;
  if (huella != huellaCsv) return false; // el CSV cambio
  if (tamPublicacion != sizeof(Publicacion)) return false; // otro compilador

  if (!catalogo.leer(l)) return false;
  if (!vocab_.leer(l) || !sufijos_.leer(l)) return false;

  uint64_t n = 0;
  if (!l.pod(n)) return false;
  publicaciones_.clear();
  publicaciones_.resize(static_cast<size_t>(n));
  for (uint64_t i = 0; i < n; ++i) {
    if (!l.vectorPod(publicaciones_[static_cast<size_t>(i)])) return false;
  }
  if (!l.vectorPod(longitudDoc_)) return false;

  if (!l.pod(n)) return false;
  perfiles_.clear();
  perfiles_.resize(static_cast<size_t>(n));
  for (uint64_t i = 0; i < n; ++i) {
    if (!l.vectorPod(perfiles_[static_cast<size_t>(i)])) return false;
  }
  if (!l.vectorPod(normas_)) return false;

  uint64_t docs = 0;
  if (!l.pod(docs) || !l.pod(longitudPromedio_)) return false;
  numDocs_ = static_cast<size_t>(docs);
  if (numDocs_ != catalogo.tamano() || longitudDoc_.size() != numDocs_) return false;

  l.liberar();
  rellenarEstadisticas(est);
  est.segundos = std::chrono::duration<double>(Reloj::now() - t0).count();
  return true;
}

void Indice::construirPerfiles(unsigned numHilos) {
  perfiles_.assign(numDocs_, {});
  normas_.assign(numDocs_, 0.0f);

  // Terminos ultra frecuentes no aportan a la similitud y encarecen todo.
  // El maximo con 4 evita que en catalogos muy chicos (pruebas unitarias) el
  // umbral valga 0 y se descarte el vocabulario completo.
  const size_t umbralFrecuencia = std::max<size_t>(numDocs_ / 8, 4);

  // Terminos utiles y su idf (se calcula una vez, secuencialmente).
  std::vector<std::pair<int32_t, float>> utiles;
  for (size_t id = 0; id < publicaciones_.size(); ++id) {
    const auto &lista = publicaciones_[id];
    if (lista.empty() || lista.size() > umbralFrecuencia) continue;
    const std::string &t = vocab_.termino(static_cast<int>(id));
    if (t.size() < 3 || esStopword(t)) continue;
    utiles.emplace_back(static_cast<int32_t>(id),
                        static_cast<float>(idf(static_cast<int>(id))));
  }

  // PROGRAMACION PARALELA: cada hilo se encarga de un rango de documentos
  // [ini, fin). Recorre las listas de publicaciones (ordenadas por doc) y con
  // busqueda binaria salta directo a sus documentos, asi que solo escribe en
  // los monticulos y perfiles de SU rango. Como cada documento ve los terminos
  // en el mismo orden que en la version secuencial, el resultado es identico.
  paraleloPorBloques(numDocs_, numHilos, [&](size_t ini, size_t fin) {
    std::vector<std::vector<std::pair<float, int32_t>>> monticulos(fin - ini);

    for (const auto &u : utiles) {
      const auto &lista = publicaciones_[u.first];
      auto it = std::lower_bound(
          lista.begin(), lista.end(), static_cast<int32_t>(ini),
          [](const Publicacion &p, int32_t d) { return p.doc < d; });
      for (; it != lista.end() && static_cast<size_t>(it->doc) < fin; ++it) {
        const Publicacion &p = *it;
        const float tf = 1.0f + 2.0f * p.tfTitulo + 1.0f * p.tfSinopsis +
                         3.0f * p.tfTag;
        const float peso = std::log(tf) * u.second;
        auto &h = monticulos[p.doc - ini];
        if (h.size() < kTerminosPorPerfil) {
          h.emplace_back(peso, u.first);
          std::push_heap(h.begin(), h.end(), std::greater<>());
        } else if (peso > h.front().first) {
          std::pop_heap(h.begin(), h.end(), std::greater<>());
          h.back() = {peso, u.first};
          std::push_heap(h.begin(), h.end(), std::greater<>());
        }
      }
    }

    for (size_t d = ini; d < fin; ++d) {
      auto &h = monticulos[d - ini];
      double norma2 = 0.0;
      perfiles_[d].reserve(h.size());
      for (const auto &par : h) {
        perfiles_[d].push_back({par.second, par.first});
        norma2 += static_cast<double>(par.first) * par.first;
      }
      std::sort(perfiles_[d].begin(), perfiles_[d].end(),
                [](const TerminoPeso &a, const TerminoPeso &b) {
                  return a.termino < b.termino;
                });
      normas_[d] = static_cast<float>(std::sqrt(norma2)) + 1e-6f;
    }
  });
}

} // namespace pf
