// rutas.hpp — Encontrar los archivos del proyecto sin depender de la carpeta
// desde la que se ejecuta el programa.
//
// CLion y Visual Studio ejecutan los programas desde la carpeta de compilacion
// (cmake-build-debug/, build/Release/...), donde no estan data/ ni tests/. Para
// no obligar a configurar el "Working directory" a mano, CMake define
// PROYECTO_RAIZ con la ruta absoluta del repositorio, y aqui se usa como
// segundo lugar donde buscar.
#pragma once
#include <filesystem>
#include <string>
#include <system_error>

namespace pf {

// Raiz del repositorio. Con CMake es una ruta absoluta; con el Makefile (que se
// ejecuta desde la raiz) es simplemente ".".
inline std::string raizProyecto() {
#ifdef PROYECTO_RAIZ
  return PROYECTO_RAIZ;
#else
  return ".";
#endif
}

// Devuelve una ruta que exista para un archivo de LECTURA (el CSV): la ruta tal
// cual, o relativa a la raiz del proyecto, o relativa a "..". Si no existe en
// ningun lado, devuelve la original para que el error muestre lo que se pidio.
inline std::string resolverRuta(const std::string &ruta) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (ruta.empty() || fs::exists(ruta, ec)) return ruta;
  const fs::path p(ruta);
  if (p.is_absolute()) return ruta;
  for (const fs::path &base : {fs::path(raizProyecto()), fs::path("..")}) {
    const fs::path candidata = base / p;
    if (fs::exists(candidata, ec)) return candidata.string();
  }
  return ruta;
}

// Ruta para un archivo que el programa CREA (datos_usuario.txt): siempre en la
// raiz del proyecto, asi la consola y la interfaz grafica comparten el mismo
// estado aunque se ejecuten desde carpetas distintas.
inline std::string rutaEnRaiz(const std::string &nombre) {
  return (std::filesystem::path(raizProyecto()) / nombre).string();
}

// Si 'archivoDePrueba' no existe en la carpeta actual, se cambia a la raiz del
// proyecto. Lo usan las pruebas, que trabajan con rutas como tests/mini.csv.
inline void irARaizSiHaceFalta(const std::string &archivoDePrueba) {
  std::error_code ec;
  if (!std::filesystem::exists(archivoDePrueba, ec)) {
    std::filesystem::current_path(raizProyecto(), ec);
  }
}

} // namespace pf
