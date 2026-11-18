#include "patrones.hpp"
#include "rutas.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace pf {

// ============================================================================
// 1. SINGLETON
// ============================================================================
MotorStream &MotorStream::getInstance() {
  static MotorStream instancia; // creada una sola vez, thread-safe desde C++11
  return instancia;
}

bool MotorStream::inicializar(const std::string &rutaCsv, bool usarCache,
                              unsigned numHilos) {
  if (inicializado_) return true;

  // El CSV se busca tal cual, en la raiz del proyecto y en ../ (rutas.hpp),
  // asi funciona aunque se ejecute desde la carpeta de compilacion.
  const std::string rutaFinal = resolverRuta(rutaCsv);
  if (!std::ifstream(rutaFinal, std::ios::binary)) return false;
  rutaCsvActiva_ = rutaFinal;

  const std::string rutaCache = rutaFinal + ".cache";
  std::ifstream f(rutaFinal, std::ios::binary | std::ios::ate);
  const uint64_t huella = f ? static_cast<uint64_t>(f.tellg()) : 0;

  EstadisticasIndice ei;
  bool cargado = false;
  if (usarCache && huella != 0) {
    cargado = indice_.cargarCache(rutaCache, catalogo_, huella, ei);
  }
  if (!cargado) {
    catalogo_.cargarDesdeCSV(rutaFinal);
    indice_.construir(catalogo_, numHilos); // tokenizacion en paralelo
    if (usarCache && huella != 0) {
      indice_.guardarCache(rutaCache, catalogo_, huella);
    }
  }

  arbolAnios_.construirDesdeCatalogo(catalogo_);
  buscador_ = std::make_unique<Buscador>(catalogo_, indice_);
  buscador_->setHilos(numHilos); // re-ranking por frase en paralelo
  recomendador_ = std::make_unique<Recomendador>(catalogo_, indice_);
  inicializado_ = true;
  return true;
}

// ============================================================================
// 2. BUILDER
// ============================================================================
ConsultaBuilder &ConsultaBuilder::texto(const std::string &t) {
  textoLibre_ = t;
  return *this;
}

ConsultaBuilder &ConsultaBuilder::director(const std::string &d) {
  director_ = d;
  return *this;
}

ConsultaBuilder &ConsultaBuilder::reparto(const std::string &r) {
  reparto_ = r;
  return *this;
}

ConsultaBuilder &ConsultaBuilder::genero(const std::string &g) {
  genero_ = g;
  return *this;
}

ConsultaBuilder &ConsultaBuilder::origen(const std::string &o) {
  origen_ = o;
  return *this;
}

ConsultaBuilder &ConsultaBuilder::anio(int a) {
  anio_ = a;
  return *this;
}

Consulta ConsultaBuilder::build(const Buscador &buscador) const {
  std::string cadenaConsulta;
  if (!textoLibre_.empty()) cadenaConsulta += textoLibre_ + " ";
  if (!director_.empty()) cadenaConsulta += "director:" + director_ + " ";
  if (!reparto_.empty()) cadenaConsulta += "reparto:" + reparto_ + " ";
  if (!genero_.empty()) cadenaConsulta += "genero:" + genero_ + " ";
  if (!origen_.empty()) cadenaConsulta += "origen:" + origen_ + " ";
  if (anio_ > 0) cadenaConsulta += "anio:" + std::to_string(anio_);

  if (cadenaConsulta.empty()) {
    Consulta vacia;
    vacia.valida = false;
    vacia.mensaje = "Ingresa al menos un filtro o palabra clave.";
    return vacia;
  }
  return buscador.interpretar(cadenaConsulta);
}

// ============================================================================
// 3. PROXY
// ============================================================================
PeliculaReal::PeliculaReal(std::string titulo, std::string url)
    : titulo_(std::move(titulo)), url_(std::move(url)) {}

std::string PeliculaReal::reproducir() {
  std::string info = "Reproduciendo ahora: \"" + titulo_ + "\"";
  if (!url_.empty()) info += "\nFuente Wiki/Stream: " + url_;
  return info;
}

ProxyPelicula::ProxyPelicula(const Pelicula &pelicula, int docId,
                             bool cuentaActiva, bool regionPermitida,
                             EstadoUsuario &estadoUsuario)
    : peliculaReal_(pelicula.titulo, pelicula.url), docId_(docId),
      cuentaActiva_(cuentaActiva), regionPermitida_(regionPermitida),
      estadoUsuario_(estadoUsuario) {}

std::string ProxyPelicula::reproducir() {
  if (!cuentaActiva_) {
    return "[PROXY BLOQUEADO] Tu suscripcion no esta activa. Activa tu cuenta para reproducir.";
  }
  if (!regionPermitida_) {
    return "[PROXY BLOQUEADO] Contenido restringido en tu region geografica.";
  }
  estadoUsuario_.marcarVisto(docId_);
  return "[PROXY AUTORIZADO] " + peliculaReal_.reproducir();
}

// ============================================================================
// 4. MEMENTO
// ============================================================================
MementoUsuario::MementoUsuario(std::vector<int> meGusta,
                               std::vector<int> verMasTarde)
    : meGusta_(std::move(meGusta)), verMasTarde_(std::move(verMasTarde)) {}

GestorEstadoUsuario::GestorEstadoUsuario(EstadoUsuario &estado)
    : estado_(estado) {}

void GestorEstadoUsuario::alternarMeGusta(int doc) {
  estado_.alternarMeGusta(doc);
}

void GestorEstadoUsuario::alternarVerMasTarde(int doc) {
  estado_.alternarVerMasTarde(doc);
}

MementoUsuario GestorEstadoUsuario::guardarEstado() const {
  return MementoUsuario(estado_.meGusta(), estado_.verMasTarde());
}

void GestorEstadoUsuario::restaurarEstado(const MementoUsuario &memento) {
  const auto &objetivoLikes = memento.getMeGusta();
  const auto actualesLikes = estado_.meGusta();
  for (int d : actualesLikes) {
    if (std::find(objetivoLikes.begin(), objetivoLikes.end(), d) ==
        objetivoLikes.end()) {
      estado_.alternarMeGusta(d);
    }
  }
  for (int d : objetivoLikes) {
    if (!estado_.tieneMeGusta(d)) {
      estado_.alternarMeGusta(d);
    }
  }

  const auto &objetivoVer = memento.getVerMasTarde();
  const auto actualesVer = estado_.verMasTarde();
  for (int d : actualesVer) {
    if (std::find(objetivoVer.begin(), objetivoVer.end(), d) ==
        objetivoVer.end()) {
      estado_.alternarVerMasTarde(d);
    }
  }
  for (int d : objetivoVer) {
    if (!estado_.tieneVerMasTarde(d)) {
      estado_.alternarVerMasTarde(d);
    }
  }
}

void HistorialUsuario::agregarEstado(const MementoUsuario &estado) {
  estados_.push_back(estado);
}

MementoUsuario HistorialUsuario::getEstadoAnterior() {
  if (estados_.empty()) return MementoUsuario({}, {});
  MementoUsuario anterior = estados_.back();
  estados_.pop_back();
  return anterior;
}

} // namespace pf