#pragma once

#include <memory>
#include <string>
#include <vector>

#include "arbol_avl.hpp"
#include "buscador.hpp"
#include "catalogo.hpp"
#include "estado_usuario.hpp"
#include "indice.hpp"
#include "recomendador.hpp"

namespace pf {

// ============================================================================
// 1. PATRON DE CREACION: SINGLETON (Teoria 10)
// ============================================================================
// Una sola instancia del motor (catalogo + indice + arboles) en todo el
// programa: construirla cuesta segundos y cientos de MB, asi que no debe
// duplicarse por accidente. Se usa el "Meyers singleton": la variable estatica
// local se crea la primera vez que se llama a getInstance(), C++11 garantiza
// que esa inicializacion es segura entre hilos, y se destruye sola al salir.
class MotorStream {
private:

  Catalogo catalogo_;
  Indice indice_;
  ArbolAVLAnios arbolAnios_;
  std::unique_ptr<Buscador> buscador_;
  std::unique_ptr<Recomendador> recomendador_;
  bool inicializado_ = false;
  std::string rutaCsvActiva_;

  MotorStream() = default;

public:
  MotorStream(const MotorStream &) = delete;
  MotorStream &operator=(const MotorStream &) = delete;

  static MotorStream &getInstance();

  // Carga el CSV (o el cache binario) y construye todas las estructuras.
  // numHilos: hilos para construir el indice y re-rankear (0 = todos).
  bool inicializar(const std::string &rutaCsv, bool usarCache = true,
                   unsigned numHilos = 0);
  bool estaInicializado() const { return inicializado_; }
  const std::string &rutaCsvActiva() const { return rutaCsvActiva_; }

  const Catalogo &catalogo() const { return catalogo_; }
  const Indice &indice() const { return indice_; }
  const ArbolAVLAnios &arbolAnios() const { return arbolAnios_; }
  const Buscador &buscador() const { return *buscador_; }
  const Recomendador &recomendador() const { return *recomendador_; }
};

// ============================================================================
// 2. PATRON DE CREACION: BUILDER (Teoria 10)
// ============================================================================
// Arma paso a paso una consulta con varios filtros opcionales (texto,
// director, reparto, genero, origen, anio) sin un constructor de 6 parametros.
// build() la traduce al lenguaje de consulta del Buscador.
class ConsultaBuilder {
private:
  std::string textoLibre_;
  std::string director_;
  std::string reparto_;
  std::string genero_;
  std::string origen_;
  int anio_ = -1;

public:
  ConsultaBuilder &texto(const std::string &t);
  ConsultaBuilder &director(const std::string &d);
  ConsultaBuilder &reparto(const std::string &r);
  ConsultaBuilder &genero(const std::string &g);
  ConsultaBuilder &origen(const std::string &o);
  ConsultaBuilder &anio(int a);

  Consulta build(const Buscador &buscador) const;
};

// ============================================================================
// 3. PATRON DE ESTRUCTURA: PROXY (Teoria 11)
// ============================================================================
// ProxyPelicula tiene la misma interfaz que PeliculaReal, pero antes de
// delegar verifica la suscripcion y la region, y despues registra la pelicula
// como vista (auditoria). El cliente no sabe si habla con el proxy o con la
// pelicula real.
class IPeliculaReproducible {
public:
  virtual ~IPeliculaReproducible() = default;
  virtual std::string reproducir() = 0;
};

class PeliculaReal : public IPeliculaReproducible {
private:
  std::string titulo_;
  std::string url_;

public:
  PeliculaReal(std::string titulo, std::string url);
  std::string reproducir() override;
};

class ProxyPelicula : public IPeliculaReproducible {
private:
  PeliculaReal peliculaReal_;
  int docId_;
  bool cuentaActiva_;
  bool regionPermitida_;
  EstadoUsuario &estadoUsuario_;

public:
  ProxyPelicula(const Pelicula &pelicula, int docId, bool cuentaActiva,
                bool regionPermitida, EstadoUsuario &estadoUsuario);

  std::string reproducir() override;
};

// ============================================================================
// 4. PATRON DE COMPORTAMIENTO: MEMENTO (Teoria 11)
// ============================================================================
// Originador : GestorEstadoUsuario (crea y restaura snapshots)
// Memento    : MementoUsuario      (copia inmutable de Me gusta / Ver mas tarde)
// Cuidador   : HistorialUsuario    (pila de snapshots para Deshacer)
class MementoUsuario {
private:
  std::vector<int> meGusta_;
  std::vector<int> verMasTarde_;

public:
  MementoUsuario(std::vector<int> meGusta, std::vector<int> verMasTarde);

  const std::vector<int> &getMeGusta() const { return meGusta_; }
  const std::vector<int> &getVerMasTarde() const { return verMasTarde_; }
};

class GestorEstadoUsuario {
private:
  EstadoUsuario &estado_;

public:
  explicit GestorEstadoUsuario(EstadoUsuario &estado);

  void alternarMeGusta(int doc);
  void alternarVerMasTarde(int doc);

  MementoUsuario guardarEstado() const;
  void restaurarEstado(const MementoUsuario &memento);

  const EstadoUsuario &estado() const { return estado_; }
};

class HistorialUsuario {
private:
  std::vector<MementoUsuario> estados_;

public:
  void agregarEstado(const MementoUsuario &estado);
  bool hayHistorial() const { return !estados_.empty(); }
  size_t cantidad() const { return estados_.size(); }
  MementoUsuario getEstadoAnterior();
};

} // namespace pf