// pruebas.cpp — Pruebas unitarias y de integracion (sin dependencias externas).
#include <cstdlib>
#include <iostream>
#include <string>

#include "buscador.hpp"
#include "catalogo.hpp"
#include "csv.hpp"
#include "estado_usuario.hpp"
#include "indice.hpp"
#include "normalizador.hpp"
#include "recomendador.hpp"
#include "trie.hpp"

#include "arbol_avl.hpp"
#include "hilos.hpp"
#include "paralelo.hpp"
#include "patrones.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
int fallos = 0;
int total = 0;

void revisar(bool condicion, const std::string &nombre) {
  ++total;
  if (condicion) {
    std::cout << "  [ok]    " << nombre << "\n";
  } else {
    ++fallos;
    std::cout << "  [FALLA] " << nombre << "\n";
  }
}

template <typename T>
void igual(const T &obtenido, const T &esperado, const std::string &nombre) {
  ++total;
  if (obtenido == esperado) {
    std::cout << "  [ok]    " << nombre << "\n";
  } else {
    ++fallos;
    std::cout << "  [FALLA] " << nombre << " -> obtenido=" << obtenido
              << " esperado=" << esperado << "\n";
  }
}

// ---------------------------------------------------------------------------
void pruebasNormalizador() {
  std::cout << "\n== Normalizador ==\n";
  igual(pf::normalizar("El Capitán ÑOÑO"), std::string("el capitan nono"),
        "quita tildes y pasa a minusculas");
  igual(pf::normalizar("Wall-E 2008!"), std::string("wall e 2008 "),
        "los simbolos se vuelven espacios");
  igual(pf::normalizar("don't"), std::string("dont"),
        "el apostrofe no parte la palabra");
  igual(pf::limpiarTextoBruto("un barco.[12] Fin"), std::string("un barco. Fin"),
        "elimina referencias [12]");
  igual(pf::limpiarTextoBruto("linea uno\r\nlinea dos"),
        std::string("linea uno linea dos"), "elimina CRLF incrustados");
  const auto toks = pf::tokenizar("Le Bateau Fantôme (1968)");
  igual(toks.size(), size_t(4), "tokeniza 4 palabras");
  revisar(toks[2] == "fantome", "plegado de o con circunflejo");
  revisar(pf::esStopword("the") && !pf::esStopword("ship"), "stopwords");
}

// ---------------------------------------------------------------------------
void pruebasCsv() {
  std::cout << "\n== Lector CSV ==\n";
  pf::LectorCSV lector("tests/mini.csv");
  igual(lector.encabezados().size(), size_t(8), "8 columnas de encabezado");
  igual(lector.columna("Plot"), 7, "posicion de la columna Plot");
  std::vector<std::string> fila;
  revisar(lector.siguienteFila(fila), "lee la primera fila");
  igual(fila.size(), size_t(8), "la fila tiene 8 campos");
  igual(fila[1], std::string("Ghost Ship"), "campo Title");
  revisar(fila[4].find(',') != std::string::npos,
          "el campo entrecomillado con coma no se parte");
}

// ---------------------------------------------------------------------------
void pruebasTries() {
  std::cout << "\n== Tries ==\n";
  pf::TriePrefijos trie;
  const int idBarco = trie.insertar("barco");
  trie.insertar("barcaza");
  trie.insertar("bar");
  trie.insertar("cantina");
  igual(trie.insertar("barco"), idBarco, "insertar dos veces devuelve el mismo id");
  igual(trie.numTerminos(), size_t(4), "vocabulario de 4 terminos");
  igual(trie.buscar("barco"), idBarco, "busqueda exacta");
  igual(trie.buscar("barcos"), -1, "palabra inexistente");
  igual(trie.conPrefijo("bar", 100).size(), size_t(3), "3 palabras con prefijo bar");
  igual(trie.conPrefijo("zz", 100).size(), size_t(0), "prefijo sin resultados");

  pf::TrieSufijos sufijos;
  sufijos.insertarTermino("barco", 0);
  sufijos.insertarTermino("barcaza", 1);
  sufijos.insertarTermino("cantinabar", 2);
  sufijos.insertarTermino("nube", 3);
  igual(sufijos.terminosQueContienen("bar", 100).size(), size_t(3),
        "3 terminos contienen 'bar' (incluido al final de palabra)");
  igual(sufijos.terminosQueContienen("arc", 100).size(), size_t(2),
        "'arc' aparece en medio de dos terminos");
  igual(sufijos.terminosQueContienen("ube", 100).size(), size_t(1), "sufijo 'ube'");
  igual(sufijos.terminosQueContienen("xyz", 100).size(), size_t(0),
        "patron inexistente");
  igual(sufijos.terminosQueContienen("barco", 100)[0], 0,
        "la palabra completa se encuentra a si misma");
}

// ---------------------------------------------------------------------------
void pruebasCatalogoEIndice() {
  std::cout << "\n== Catalogo, indice y busqueda ==\n";
  pf::Catalogo catalogo;
  const pf::EstadisticasCarga ec = catalogo.cargarDesdeCSV("tests/mini.csv");
  igual(ec.filasLeidas, size_t(7), "7 filas de datos");
  igual(ec.descartadasDuplicadas, size_t(1), "descarta el duplicado exacto");
  igual(ec.descartadasSinopsisCorta, size_t(1), "descarta la sinopsis corta");
  igual(catalogo.tamano(), size_t(5), "quedan 5 peliculas utiles");

  // Reglas de limpieza.
  int idxBar = -1;
  for (size_t i = 0; i < catalogo.tamano(); ++i) {
    if (catalogo[i].tituloNorm == "the bar fight") idxBar = static_cast<int>(i);
  }
  revisar(idxBar >= 0, "encuentra The Bar Fight");
  revisar(catalogo[idxBar].director.empty(), "'Unknown' se traduce a director vacio");
  revisar(catalogo[idxBar].generos.empty(), "'unknown' se traduce a genero vacio");
  revisar(catalogo[0].sinopsis.find("[1]") == std::string::npos,
          "la sinopsis guardada no tiene referencias");

  pf::Indice indice;
  indice.construir(catalogo);
  revisar(indice.numDocumentos() == 5, "el indice tiene 5 documentos");
  revisar(indice.vocabulario().buscar("ship") >= 0, "'ship' esta en el vocabulario");

  pf::Buscador buscador(catalogo, indice);

  // 1) Palabra.
  auto r = buscador.buscar(buscador.interpretar("ship"), 10);
  revisar(!r.empty(), "buscar por palabra devuelve resultados");
  revisar(r.size() >= 3, "'ship' aparece en al menos 3 peliculas");

  // 2) Frase: semantica "y/o", pero las que tienen TODAS las palabras primero.
  r = buscador.buscar(buscador.interpretar("ghost ship"), 10);
  revisar(r.size() >= 3, "la frase devuelve tambien coincidencias parciales");
  revisar(catalogo[r[0].doc].tituloNorm == "ghost ship",
          "la coincidencia de frase completa va primero");

  // 3) Sub-palabra.
  r = buscador.buscar(buscador.interpretar("bar"), 10);
  bool hayBartender = false;
  for (const auto &x : r) {
    if (catalogo[x.doc].sinopsis.find("bartender") != std::string::npos)
      hayBartender = true;
  }
  revisar(hayBartender, "'bar' encuentra la pelicula que dice 'bartender'");

  // 4) Etiquetas.
  r = buscador.buscar(buscador.interpretar("director:sewell"), 10);
  revisar(r.size() == 1 && catalogo[r[0].doc].anio == 1952,
          "filtro por director");
  r = buscador.buscar(buscador.interpretar("genero:horror"), 10);
  revisar(r.size() == 1 && catalogo[r[0].doc].anio == 2002, "filtro por genero");
  r = buscador.buscar(buscador.interpretar("reparto:margulies"), 10);
  revisar(r.size() == 1, "filtro por reparto");
  r = buscador.buscar(buscador.interpretar("anio:1961"), 10);
  revisar(r.size() == 1, "filtro por anio");

  // 5) Acentos en la consulta y en los datos.
  r = buscador.buscar(buscador.interpretar("capitan"), 10);
  revisar(!r.empty(), "'capitan' encuentra 'capitán' (sin tilde en la consulta)");

  // 6) Consulta sin coincidencias.
  const pf::Consulta vacia = buscador.interpretar("qwertyasdf");
  revisar(!vacia.valida, "consulta sin coincidencias se marca invalida");

  // 7) Recomendaciones.
  pf::Recomendador recomendador(catalogo, indice);
  int docGhost1952 = -1;
  for (size_t i = 0; i < catalogo.tamano(); ++i) {
    if (catalogo[i].tituloNorm == "ghost ship" && catalogo[i].anio == 1952)
      docGhost1952 = static_cast<int>(i);
  }
  auto sim = recomendador.similaresA(docGhost1952, 3);
  revisar(!sim.empty(), "similaresA devuelve vecinos");
  revisar(!sim.empty() && sim[0].doc != docGhost1952,
          "no se recomienda a si misma");
  bool encontroLaOtraGhostShip = false;
  for (const auto &x : sim) {
    if (catalogo[x.doc].tituloNorm == "ghost ship") encontroLaOtraGhostShip = true;
  }
  revisar(encontroLaOtraGhostShip, "la otra Ghost Ship es la mas parecida");

  // 8) Estado del usuario y persistencia.
  pf::EstadoUsuario estado;
  revisar(estado.alternarMeGusta(docGhost1952), "alternar activa Me gusta");
  revisar(!estado.alternarMeGusta(docGhost1952), "alternar de nuevo lo desactiva");
  estado.alternarMeGusta(docGhost1952);
  estado.alternarVerMasTarde(0);
  revisar(estado.guardar("tests/estado_prueba.tmp", catalogo), "guarda el estado");
  pf::EstadoUsuario recargado;
  recargado.cargar("tests/estado_prueba.tmp", catalogo);
  igual(recargado.meGusta().size(), size_t(1), "recupera 1 Me gusta");
  igual(recargado.verMasTarde().size(), size_t(1), "recupera 1 Ver mas tarde");
  revisar(recargado.tieneMeGusta(docGhost1952), "el Me gusta apunta a la misma pelicula");
  std::remove("tests/estado_prueba.tmp");

  // 9) Cache binario: ida y vuelta.
  const std::string rutaCache = "tests/cache_prueba.tmp";
  revisar(indice.guardarCache(rutaCache, catalogo, 12345u), "escribe el cache");

  pf::Catalogo catalogo2;
  pf::Indice indice2;
  pf::EstadisticasIndice est2;
  revisar(indice2.cargarCache(rutaCache, catalogo2, 12345u, est2),
          "lee el cache");
  igual(catalogo2.tamano(), catalogo.tamano(), "mismo numero de peliculas");
  igual(catalogo2[0].titulo, catalogo[0].titulo, "mismo titulo tras el viaje");
  igual(catalogo2[0].sinopsis, catalogo[0].sinopsis, "misma sinopsis");
  igual(indice2.vocabulario().numTerminos(), indice.vocabulario().numTerminos(),
        "mismo vocabulario");

  pf::Buscador buscador2(catalogo2, indice2);
  auto r2 = buscador2.buscar(buscador2.interpretar("ghost ship"), 10);
  revisar(r2.size() == r.size() || !r2.empty(), "el cache permite buscar");
  auto rOrig = buscador.buscar(buscador.interpretar("ghost ship"), 10);
  bool mismoOrden = r2.size() == rOrig.size();
  for (size_t i = 0; mismoOrden && i < r2.size(); ++i) {
    if (catalogo2[r2[i].doc].tituloNorm != catalogo[rOrig[i].doc].tituloNorm)
      mismoOrden = false;
  }
  revisar(mismoOrden, "el ranking desde cache es identico al reconstruido");

  // La huella distinta invalida el cache.
  pf::Catalogo catalogo3;
  pf::Indice indice3;
  pf::EstadisticasIndice est3;
  revisar(!indice3.cargarCache(rutaCache, catalogo3, 99999u, est3),
          "una huella distinta invalida el cache");
  std::remove(rutaCache.c_str());
}


// ===========================================================================
// PARTE 2: Arbol AVL, patrones de diseno y programacion paralela
// ===========================================================================
std::string leerArchivo(const std::string &ruta) {
  std::ifstream f(ruta, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

void pruebasArbolAVL() {
  std::cout << "\n== Arbol AVL ==\n";
  pf::ArbolAVLAnios avl;
  // Insertar en orden creciente es el peor caso de un BST sin balanceo
  // (altura 1000). El AVL debe quedar en altura logaritmica.
  for (int a = 1; a <= 1000; ++a) avl.insertar(a, a);
  igual(avl.totalNodos(), size_t(1000), "1000 anios distintos = 1000 nodos");
  revisar(avl.alturaTotal() <= 14,
          "insercion ordenada: altura " + std::to_string(avl.alturaTotal()) +
              " <= 1.44 log2(n)");

  auto r = avl.buscarPorRango(10, 20);
  igual(r.size(), size_t(11), "rango [10, 20] devuelve 11 documentos");
  bool enRango = true, ordenado = true;
  for (size_t i = 0; i < r.size(); ++i) {
    if (r[i] < 10 || r[i] > 20) enRango = false;
    if (i > 0 && r[i] < r[i - 1]) ordenado = false;
  }
  revisar(enRango, "todos los resultados estan dentro del rango");
  revisar(ordenado, "el recorrido en orden devuelve anios crecientes");
  igual(avl.buscarPorRango(20, 10).size(), size_t(11), "rango invertido se corrige");
  igual(avl.buscarPorRango(2000, 3000).size(), size_t(0), "rango vacio");

  avl.insertar(500, 9999); // anio repetido: mismo nodo, otro documento
  igual(avl.totalNodos(), size_t(1000), "un anio repetido no crea otro nodo");
  igual(avl.buscarPorRango(500, 500).size(), size_t(2), "el nodo guarda ambos docs");

  // Insercion decreciente tambien queda balanceada.
  pf::ArbolAVLAnios avl2;
  for (int a = 1000; a >= 1; --a) avl2.insertar(a, a);
  revisar(avl2.alturaTotal() <= 14, "insercion decreciente tambien balanceada");

  pf::Catalogo catalogo;
  catalogo.cargarDesdeCSV("tests/mini.csv");
  pf::ArbolAVLAnios avl3;
  avl3.construirDesdeCatalogo(catalogo);
  igual(avl3.totalNodos(), size_t(5), "mini.csv tiene 5 anios distintos");
  igual(avl3.buscarPorRango(1950, 1965).size(), size_t(2),
        "1950-1965: Ghost Ship (1952) y Sail a Crooked Ship (1961)");
}

void pruebasPatrones() {
  std::cout << "\n== Patrones de diseno ==\n";

  // --- Singleton ----------------------------------------------------------
  pf::MotorStream &m1 = pf::MotorStream::getInstance();
  pf::MotorStream &m2 = pf::MotorStream::getInstance();
  revisar(&m1 == &m2, "Singleton: getInstance() devuelve siempre el mismo objeto");
  revisar(m1.inicializar("tests/mini.csv", false, 2), "Singleton: inicializa el motor");
  igual(m2.catalogo().tamano(), size_t(5), "Singleton: la otra referencia ve el catalogo");
  revisar(m1.inicializar("otro/archivo.csv", false), "Singleton: no se reinicializa");
  igual(m1.arbolAnios().totalNodos(), size_t(5), "Singleton: construye tambien el AVL");

  const pf::Catalogo &cat = m1.catalogo();
  const pf::Buscador &bus = m1.buscador();
  int ghost1952 = -1;
  for (size_t i = 0; i < cat.tamano(); ++i) {
    if (cat[i].titulo == "Ghost Ship" && cat[i].anio == 1952) ghost1952 = static_cast<int>(i);
  }

  // --- Builder ------------------------------------------------------------
  pf::Consulta vacia = pf::ConsultaBuilder().build(bus);
  revisar(!vacia.valida, "Builder: sin filtros la consulta no es valida");

  pf::Consulta q = pf::ConsultaBuilder().texto("ship").director("sewell").build(bus);
  revisar(q.valida, "Builder: texto + director produce una consulta valida");
  auto r = bus.buscar(q, 10);
  revisar(!r.empty() && r[0].doc == ghost1952,
          "Builder: ship + director:sewell -> Ghost Ship (1952) primero");

  auto rAnio = bus.buscar(pf::ConsultaBuilder().anio(1961).build(bus), 10);
  revisar(rAnio.size() == 1 && cat[rAnio[0].doc].anio == 1961,
          "Builder: solo anio(1961) filtra por anio");

  auto rGen = bus.buscar(pf::ConsultaBuilder().genero("horror").build(bus), 10);
  revisar(rGen.size() == 1 && cat[rGen[0].doc].anio == 2002,
          "Builder: genero(horror) -> Ghost Ship (2002)");

  // --- Proxy --------------------------------------------------------------
  pf::EstadoUsuario estado;
  pf::ProxyPelicula sinCuenta(cat[ghost1952], ghost1952, false, true, estado);
  revisar(sinCuenta.reproducir().find("BLOQUEADO") != std::string::npos,
          "Proxy: sin suscripcion bloquea");
  pf::ProxyPelicula sinRegion(cat[ghost1952], ghost1952, true, false, estado);
  revisar(sinRegion.reproducir().find("BLOQUEADO") != std::string::npos,
          "Proxy: region no permitida bloquea");
  revisar(estado.vistas().empty(), "Proxy: un acceso bloqueado no se registra");
  pf::ProxyPelicula ok(cat[ghost1952], ghost1952, true, true, estado);
  pf::IPeliculaReproducible &comoInterfaz = ok; // el cliente usa la interfaz
  revisar(comoInterfaz.reproducir().find("AUTORIZADO") != std::string::npos,
          "Proxy: con permisos delega en la pelicula real");
  revisar(estado.vistas().size() == 1 && estado.vistas()[0] == ghost1952,
          "Proxy: registra la pelicula como vista (auditoria)");

  // --- Memento ------------------------------------------------------------
  pf::EstadoUsuario eu;
  pf::GestorEstadoUsuario gestor(eu);
  pf::HistorialUsuario historial;
  historial.agregarEstado(gestor.guardarEstado()); // estado 0: vacio
  gestor.alternarMeGusta(0);
  gestor.alternarVerMasTarde(1);
  historial.agregarEstado(gestor.guardarEstado()); // estado 1: like 0, ver 1
  gestor.alternarMeGusta(2);
  gestor.alternarVerMasTarde(1); // quita 1
  igual(historial.cantidad(), size_t(2), "Memento: el historial guarda 2 estados");

  gestor.restaurarEstado(historial.getEstadoAnterior());
  revisar(eu.meGusta() == std::vector<int>{0} && eu.verMasTarde() == std::vector<int>{1},
          "Memento: deshacer vuelve a {like 0, ver 1}");
  gestor.restaurarEstado(historial.getEstadoAnterior());
  revisar(eu.meGusta().empty() && eu.verMasTarde().empty(),
          "Memento: deshacer otra vez vuelve al estado vacio");
  revisar(!historial.hayHistorial(), "Memento: el historial queda vacio");
}

void pruebasParalelo() {
  std::cout << "\n== Programacion paralela ==\n";

  // paraleloPorBloques visita cada indice exactamente una vez.
  bool cubreTodo = true;
  for (size_t n : {size_t(0), size_t(1), size_t(7), size_t(1000)}) {
    for (unsigned h : {1u, 3u, 8u}) {
      std::vector<int> visitas(n, 0);
      pf::paraleloPorBloques(n, h, [&](size_t ini, size_t fin) {
        for (size_t i = ini; i < fin; ++i) ++visitas[i];
      });
      for (int v : visitas) if (v != 1) cubreTodo = false;
    }
  }
  revisar(cubreTodo, "paraleloPorBloques visita cada indice una sola vez");

  // El indice construido con 1 y con 4 hilos es identico.
  pf::Catalogo catalogo;
  catalogo.cargarDesdeCSV("tests/mini.csv");
  pf::Indice sec, par;
  sec.construir(catalogo, 1);
  par.construir(catalogo, 4);
  bool igualIndice = sec.vocabulario().numTerminos() == par.vocabulario().numTerminos() &&
                     sec.vocabulario().numNodos() == par.vocabulario().numNodos() &&
                     sec.sufijos().numNodos() == par.sufijos().numNodos();
  for (size_t t = 0; igualIndice && t < sec.vocabulario().numTerminos(); ++t) {
    const int id = static_cast<int>(t);
    igualIndice = sec.vocabulario().termino(id) == par.vocabulario().termino(id) &&
                  sec.frecuenciaDocumental(id) == par.frecuenciaDocumental(id);
  }
  for (size_t d = 0; igualIndice && d < sec.numDocumentos(); ++d) {
    const int doc = static_cast<int>(d);
    const auto &a = sec.perfil(doc);
    const auto &b = par.perfil(doc);
    igualIndice = a.size() == b.size() && sec.longitud(doc) == par.longitud(doc) &&
                  sec.normaPerfil(doc) == par.normaPerfil(doc);
    for (size_t k = 0; igualIndice && k < a.size(); ++k) {
      igualIndice = a[k].termino == b[k].termino && a[k].peso == b[k].peso;
    }
  }
  revisar(igualIndice, "indice con 1 hilo == indice con 4 hilos (tries, df, perfiles)");

  // El cache es determinista: el de 1 hilo y el de 4 hilos son identicos.
  const std::string c1 = "tests/cache_sec.tmp", c4 = "tests/cache_par.tmp";
  sec.guardarCache(c1, catalogo, 1u);
  par.guardarCache(c4, catalogo, 1u);
  const std::string b1 = leerArchivo(c1), b4 = leerArchivo(c4);
  revisar(!b1.empty() && b1 == b4, "cache de 1 hilo y de 4 hilos identico byte a byte");
  std::remove(c1.c_str());
  std::remove(c4.c_str());

  // Busqueda: re-ranking por frase con 1 y con 4 hilos da lo mismo.
  pf::Buscador b1h(catalogo, sec), b4h(catalogo, sec);
  b1h.setHilos(1);
  b4h.setHilos(4);
  const std::vector<std::string> consultas = {"ship", "ghost ship", "bar",
                                              "crooked ship", "sea", "shi"};
  bool igualBusqueda = true;
  for (const auto &c : consultas) {
    const auto x = b1h.buscar(b1h.interpretar(c), 10);
    const auto y = b4h.buscar(b4h.interpretar(c), 10);
    if (!pf::mismosResultados(x, y)) igualBusqueda = false;
    for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
      if (x[i].puntaje != y[i].puntaje) igualBusqueda = false;
    }
  }
  revisar(igualBusqueda, "busqueda con 1 hilo == con 4 hilos (orden y puntajes)");

  // El bonus de frase se aplica UNA sola vez (antes la GUI lo sumaba dos).
  const auto rg = b4h.buscar(b4h.interpretar("ghost ship"), 10);
  const auto rs = b4h.buscar(b4h.interpretar("ghost"), 10);
  revisar(!rg.empty() && rg[0].puntaje < 40.0, "el bonus de frase no se duplica");
  bool ordenado = true;
  for (size_t i = 1; i < rg.size(); ++i) {
    if (rg[i].puntaje > rg[i - 1].puntaje) ordenado = false;
  }
  revisar(ordenado && !rs.empty(), "los resultados quedan ordenados por puntaje");

  // Lote paralelo (un Buscador por hilo) == secuencial, repetido 20 veces.
  std::vector<std::string> lote;
  for (int i = 0; i < 20; ++i) lote.insert(lote.end(), consultas.begin(), consultas.end());
  const auto rPar = pf::MotorParalelo::buscarLoteParalelo(catalogo, sec, lote, 10, 4);
  bool igualLote = rPar.size() == lote.size();
  for (size_t i = 0; igualLote && i < lote.size(); ++i) {
    igualLote = pf::mismosResultados(rPar[i], b1h.buscar(b1h.interpretar(lote[i]), 10));
  }
  revisar(igualLote, "lote de 120 consultas en 4 hilos == secuencial");
}

} // namespace

int main() {
  std::cout << "Pruebas del proyecto final de Programacion III\n";
  pruebasNormalizador();
  pruebasCsv();
  pruebasTries();
  pruebasCatalogoEIndice();
  pruebasArbolAVL();
  pruebasPatrones();
  pruebasParalelo();
  std::cout << "\n----------------------------------------\n";
  std::cout << (total - fallos) << "/" << total << " pruebas pasaron.\n";
  return fallos == 0 ? 0 : 1;
}
