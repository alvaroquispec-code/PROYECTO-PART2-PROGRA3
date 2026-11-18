// gui_main.cpp — Interfaz grafica de UTEC Stream (Dear ImGui + GLFW + OpenGL 3).
//
// Uso:  streaming_gui [ruta_csv] [ruta_estado]
//   ruta_csv     por defecto data/wiki_movie_plots_deduped.csv (tambien se
//                busca en ../ para poder ejecutarlo desde build/)
//   ruta_estado  por defecto datos_usuario.txt
//
// Pestanas:
//   Inicio            Ver mas tarde + recomendaciones por los Me gusta
//   Buscar            palabra / frase / sub-palabra + etiquetas (Builder),
//                     autocompletado (TriePrefijos), rango de anios (AVL),
//                     resultados de 5 en 5
//   Mis listas        Me gusta y Ver mas tarde
//   Rendimiento       benchmark 1 hilo vs N hilos (en segundo plano)
//
// Patrones usados aqui: Singleton (MotorStream), Builder (ConsultaBuilder),
// Proxy (ProxyPelicula) y Memento (boton Deshacer).
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <vector>

#include "hilos.hpp"
#include "normalizador.hpp"
#include "paralelo.hpp"
#include "patrones.hpp"

namespace {

constexpr int kPorPagina = 5; // el enunciado pide 5 resultados por pagina

const ImVec4 kAzul(0.30f, 0.85f, 1.00f, 1.0f);
const ImVec4 kAmarillo(0.95f, 0.80f, 0.30f, 1.0f);
const ImVec4 kVerde(0.40f, 0.90f, 0.50f, 1.0f);
const ImVec4 kRosa(1.00f, 0.45f, 0.55f, 1.0f);
const ImVec4 kRojo(1.00f, 0.35f, 0.35f, 1.0f);

std::string unirLista(const std::vector<std::string> &v, size_t maxItems = 8) {
  std::string out;
  for (size_t i = 0; i < v.size() && i < maxItems; ++i) {
    if (i > 0) out += ", ";
    out += v[i];
  }
  if (v.size() > maxItems) {
    out += " (+" + std::to_string(v.size() - maxItems) + ")";
  }
  return out;
}

template <typename T> bool listo(const std::future<T> &f) {
  return f.valid() &&
         f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void dibujarFrame(GLFWwindow *ventana) {
  ImGui::Render();
  int w = 0, h = 0;
  glfwGetFramebufferSize(ventana, &w, &h);
  glViewport(0, 0, w, h);
  glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(ventana);
}

} // namespace

int main(int argc, char **argv) {
  const std::string rutaCsv =
      argc > 1 ? argv[1] : "data/wiki_movie_plots_deduped.csv";
  const std::string rutaEstado = argc > 2 ? argv[2] : "datos_usuario.txt";

  // --- Ventana y contexto de ImGui ------------------------------------------
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  GLFWwindow *ventana = glfwCreateWindow(
      1320, 840, "UTEC Stream - CS2013 Programacion III", nullptr, nullptr);
  if (!ventana) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ventana);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetIO().IniFilename = nullptr; // no ensuciar la carpeta con imgui.ini
  ImGui_ImplGlfw_InitForOpenGL(ventana, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // --- PATRON SINGLETON: el motor se carga en segundo plano ------------------
  // Asi la ventana responde y muestra "Cargando..." mientras se construyen los
  // arboles. El hilo principal no toca el motor hasta que la carga termina.
  pf::MotorStream &motor = pf::MotorStream::getInstance();
  std::future<bool> carga = std::async(std::launch::async, [&]() {
    return motor.inicializar(rutaCsv, true, 0);
  });
  enum class Fase { Cargando, Lista, Error } fase = Fase::Cargando;
  const auto inicioCarga = std::chrono::steady_clock::now();

  // --- Estado del usuario + PATRON MEMENTO -----------------------------------
  pf::EstadoUsuario estadoUsuario;
  pf::GestorEstadoUsuario gestorEstado(estadoUsuario);
  pf::HistorialUsuario historial;

  // --- Estado de la interfaz -------------------------------------------------
  int docSeleccionado = -1;

  // Busqueda
  char bufTexto[256] = "";
  char bufDirector[128] = "";
  char bufReparto[128] = "";
  char bufGenero[128] = "";
  char bufOrigen[128] = "";
  int filtroAnio = 0;
  bool usarRangoAvl = false;
  int rangoAvl[2] = {1990, 2017};
  std::vector<pf::Resultado> resultados;
  std::vector<std::string> sugerencias;
  std::string mensajeBusqueda = "Escribe una palabra, frase o sub-palabra.";
  double msBusqueda = 0.0;
  int paginaBusqueda = 0;

  // Inicio (recomendaciones cacheadas: solo se recalculan si cambian los Like)
  std::vector<pf::Resultado> recomendaciones;
  bool recomendacionesSucias = true;
  int paginaRecs = 0;

  // Ficha
  int similaresDe = -1;
  std::vector<pf::Resultado> similares;
  bool cuentaActiva = true;
  bool regionPermitida = true;
  std::string mensajeProxy;

  // Benchmark
  std::future<pf::ResultadoBenchmark> benchEnCurso;
  pf::ResultadoBenchmark ultimoBench;
  bool hayBench = false;

  auto guardarEstado = [&]() {
    estadoUsuario.guardar(rutaEstado, motor.catalogo());
    recomendacionesSucias = true;
  };

  auto seleccionar = [&](int doc) {
    if (doc != docSeleccionado) mensajeProxy.clear();
    docSeleccionado = doc;
  };

  // --- PATRON BUILDER + ARBOL AVL --------------------------------------------
  auto ejecutarBusqueda = [&]() {
    const auto t0 = std::chrono::steady_clock::now();
    sugerencias.clear();
    paginaBusqueda = 0;

    pf::ConsultaBuilder builder;
    if (bufTexto[0]) builder.texto(bufTexto);
    if (bufDirector[0]) builder.director(bufDirector);
    if (bufReparto[0]) builder.reparto(bufReparto);
    if (bufGenero[0]) builder.genero(bufGenero);
    if (bufOrigen[0]) builder.origen(bufOrigen);
    if (filtroAnio > 0) builder.anio(filtroAnio);
    pf::Consulta q = builder.build(motor.buscador());

    const bool hayTexto = q.valida;
    if (hayTexto) {
      // El buscador ya aplica el re-ranking por frase en paralelo.
      resultados = motor.buscador().buscar(q, 500);
    } else {
      resultados.clear();
    }

    if (usarRangoAvl) {
      // Consulta de rango en el AVL: O(log n + k)
      const auto docs =
          motor.arbolAnios().buscarPorRango(rangoAvl[0], rangoAvl[1]);
      if (hayTexto) {
        std::vector<char> enRango(motor.catalogo().tamano(), 0);
        for (int d : docs) enRango[d] = 1;
        resultados.erase(std::remove_if(resultados.begin(), resultados.end(),
                                        [&](const pf::Resultado &r) {
                                          return !enRango[r.doc];
                                        }),
                         resultados.end());
      } else if (q.terminos.empty() && q.anio < 0) {
        // Sin texto: listar el rango, del anio mas reciente al mas antiguo.
        for (int d : docs) resultados.push_back({d, 0.0, "rango de anios (AVL)"});
        std::stable_sort(resultados.begin(), resultados.end(),
                         [&](const pf::Resultado &a, const pf::Resultado &b) {
                           return motor.catalogo()[a.doc].anio >
                                  motor.catalogo()[b.doc].anio;
                         });
      }
    }

    msBusqueda = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
    if (!resultados.empty()) {
      mensajeBusqueda = std::to_string(resultados.size()) +
                        (resultados.size() >= 400 ? " mejores resultados"
                                                  : " resultados");
    } else if (!hayTexto && !usarRangoAvl) {
      mensajeBusqueda = q.mensaje;
    } else {
      mensajeBusqueda = "Sin resultados.";
    }
  };

  // --- Lista paginada de 5 en 5 ----------------------------------------------
  auto listaPaginada = [&](const char *id, const std::vector<pf::Resultado> &lista,
                           int &pagina, bool mostrarPuntaje) {
    const int total = static_cast<int>(lista.size());
    const int paginas = std::max(1, (total + kPorPagina - 1) / kPorPagina);
    pagina = std::clamp(pagina, 0, paginas - 1);
    ImGui::PushID(id);
    const int desde = pagina * kPorPagina;
    for (int i = desde; i < std::min(total, desde + kPorPagina); ++i) {
      const auto &r = lista[i];
      const auto &p = motor.catalogo()[r.doc];
      std::string etiqueta = std::to_string(i + 1) + ". " + p.titulo;
      if (p.anio > 0) etiqueta += " (" + std::to_string(p.anio) + ")";
      if (estadoUsuario.tieneMeGusta(r.doc)) etiqueta += "  [Me gusta]";
      if (estadoUsuario.tieneVerMasTarde(r.doc)) etiqueta += "  [Ver mas tarde]";
      etiqueta += "##" + std::to_string(r.doc);
      if (ImGui::Selectable(etiqueta.c_str(), docSeleccionado == r.doc)) {
        seleccionar(r.doc);
      }
      if (!r.explicacion.empty()) {
        if (mostrarPuntaje) {
          ImGui::TextDisabled("     puntaje %.2f | %s", r.puntaje,
                              r.explicacion.c_str());
        } else {
          ImGui::TextDisabled("     %s", r.explicacion.c_str());
        }
      }
    }
    if (total > kPorPagina) {
      ImGui::Spacing();
      ImGui::BeginDisabled(pagina == 0);
      if (ImGui::Button("< Anteriores 5")) --pagina;
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::Text("Pagina %d de %d", pagina + 1, paginas);
      ImGui::SameLine();
      ImGui::BeginDisabled(pagina + 1 >= paginas);
      if (ImGui::Button("Siguientes 5 >")) ++pagina;
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  };

  // --- Ficha de la pelicula (se muestra en todas las pestanas) ---------------
  auto dibujarFicha = [&]() {
    if (docSeleccionado < 0 ||
        docSeleccionado >= static_cast<int>(motor.catalogo().tamano())) {
      ImGui::TextDisabled("Selecciona una pelicula para ver su ficha.");
      return;
    }
    const int doc = docSeleccionado;
    const pf::Pelicula &p = motor.catalogo()[doc];

    ImGui::PushStyleColor(ImGuiCol_Text, kAmarillo);
    ImGui::TextWrapped("%s (%d)", p.titulo.c_str(), p.anio);
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (!p.director.empty()) ImGui::TextWrapped("Director: %s", p.director.c_str());
    if (!p.generos.empty())
      ImGui::TextWrapped("Generos:  %s", unirLista(p.generos).c_str());
    if (!p.reparto.empty())
      ImGui::TextWrapped("Reparto:  %s", unirLista(p.reparto, 6).c_str());
    if (!p.origen.empty()) ImGui::TextWrapped("Origen:   %s", p.origen.c_str());
    ImGui::Spacing();

    // Me gusta / Ver mas tarde. Antes de cada cambio se guarda un Memento.
    const bool esLike = estadoUsuario.tieneMeGusta(doc);
    if (ImGui::Button(esLike ? "Quitar Me gusta" : "Me gusta")) {
      historial.agregarEstado(gestorEstado.guardarEstado());
      gestorEstado.alternarMeGusta(doc);
      guardarEstado();
    }
    ImGui::SameLine();
    const bool esVer = estadoUsuario.tieneVerMasTarde(doc);
    if (ImGui::Button(esVer ? "Quitar de Ver mas tarde" : "Ver mas tarde")) {
      historial.agregarEstado(gestorEstado.guardarEstado());
      gestorEstado.alternarVerMasTarde(doc);
      guardarEstado();
    }

    // PATRON PROXY
    ImGui::SameLine();
    if (ImGui::Button("Reproducir (Proxy)")) {
      pf::ProxyPelicula proxy(p, doc, cuentaActiva, regionPermitida,
                              estadoUsuario);
      mensajeProxy = proxy.reproducir();
      estadoUsuario.guardar(rutaEstado, motor.catalogo());
    }
    ImGui::Checkbox("Suscripcion activa", &cuentaActiva);
    ImGui::SameLine();
    ImGui::Checkbox("Region permitida", &regionPermitida);
    if (!mensajeProxy.empty()) {
      const bool bloqueado = mensajeProxy.find("BLOQUEADO") != std::string::npos;
      ImGui::PushStyleColor(ImGuiCol_Text, bloqueado ? kRojo : kVerde);
      ImGui::TextWrapped("%s", mensajeProxy.c_str());
      ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::TextColored(kAmarillo, "SINOPSIS");
    const float altoSimilares = 7.5f * ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("sinopsis",
                      ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y -
                                                     altoSimilares)),
                      true);
    ImGui::TextWrapped("%s", p.sinopsis.c_str());
    ImGui::EndChild();

    // Similares a esta pelicula (se calcula una vez por seleccion)
    if (similaresDe != doc) {
      similares = motor.recomendador().similaresA(doc, 5);
      similaresDe = doc;
    }
    ImGui::TextColored(kAzul, "PELICULAS SIMILARES");
    for (const auto &r : similares) {
      const auto &s = motor.catalogo()[r.doc];
      std::string etiqueta = s.titulo + " (" + std::to_string(s.anio) +
                             ")##sim" + std::to_string(r.doc);
      if (ImGui::Selectable(etiqueta.c_str())) seleccionar(r.doc);
    }
  };

  // Dos paneles: izquierda (contenido de la pestana) | derecha (ficha)
  auto dosPaneles = [&](const char *id, auto &&izquierda) {
    ImGui::PushID(id);
    const float ancho = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("izq", ImVec2(ancho * 0.52f, 0), true);
    izquierda();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("der", ImVec2(0, 0), true);
    dibujarFicha();
    ImGui::EndChild();
    ImGui::PopID();
  };

  // ===========================================================================
  // Bucle principal
  // ===========================================================================
  while (!glfwWindowShouldClose(ventana)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("UTEC_Stream", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // --- Pantalla de carga ---------------------------------------------------
    if (fase == Fase::Cargando && listo(carga)) {
      if (carga.get()) {
        fase = Fase::Lista;
        estadoUsuario.cargar(rutaEstado, motor.catalogo());
      } else {
        fase = Fase::Error;
      }
    }
    if (fase != Fase::Lista) {
      ImGui::TextColored(kAzul, "UTEC STREAM");
      ImGui::Separator();
      if (fase == Fase::Cargando) {
        const double s = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - inicioCarga)
                             .count();
        ImGui::Text("Cargando catalogo y construyendo arboles... %.1f s", s);
        ImGui::TextDisabled("La primera vez tarda unos segundos; despues se usa "
                            "el cache binario.");
      } else {
        ImGui::TextColored(kRojo, "ERROR: no se encontro el CSV '%s'.",
                           rutaCsv.c_str());
        ImGui::TextWrapped(
            "Descargalo desde el enlace del README y guardalo como "
            "data/wiki_movie_plots_deduped.csv, o pasa la ruta como primer "
            "argumento:  streaming_gui ruta/al/archivo.csv");
      }
      ImGui::End();
      dibujarFrame(ventana);
      continue;
    }

    // --- Cabecera + PATRON MEMENTO (Deshacer) --------------------------------
    ImGui::TextColored(kAzul, "UTEC STREAM");
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu peliculas | %zu palabras en el Trie | %u hilos",
                        motor.catalogo().tamano(),
                        motor.indice().vocabulario().numTerminos(),
                        pf::resolverHilos(0));
    ImGui::SameLine(ImGui::GetWindowWidth() - 230);
    ImGui::BeginDisabled(!historial.hayHistorial());
    if (ImGui::Button("Deshacer (Memento)")) {
      gestorEstado.restaurarEstado(historial.getEstadoAnterior());
      guardarEstado();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu)", historial.cantidad());
    ImGui::Separator();

    if (recomendacionesSucias) {
      recomendaciones = motor.recomendador().paraUsuario(
          estadoUsuario.meGusta(), estadoUsuario.verMasTarde(), 20);
      recomendacionesSucias = false;
    }

    if (ImGui::BeginTabBar("pestanas")) {
      // -----------------------------------------------------------------------
      // INICIO: Ver mas tarde + similares a los Me gusta (requisito)
      // -----------------------------------------------------------------------
      if (ImGui::BeginTabItem("Inicio")) {
        dosPaneles("inicio", [&]() {
          ImGui::TextColored(kAzul, "VER MAS TARDE (%zu)",
                             estadoUsuario.verMasTarde().size());
          ImGui::Separator();
          if (estadoUsuario.verMasTarde().empty()) {
            ImGui::TextDisabled("Vacio. Abre una pelicula y pulsa 'Ver mas tarde'.");
          }
          for (int doc : estadoUsuario.verMasTarde()) {
            const auto &p = motor.catalogo()[doc];
            std::string e = p.titulo + " (" + std::to_string(p.anio) + ")##vmt" +
                            std::to_string(doc);
            if (ImGui::Selectable(e.c_str(), docSeleccionado == doc)) seleccionar(doc);
          }

          ImGui::Spacing();
          ImGui::Spacing();
          ImGui::TextColored(kRosa, "RECOMENDADAS PARA TI");
          ImGui::Separator();
          if (estadoUsuario.meGusta().empty()) {
            ImGui::TextDisabled("Dale Me gusta a alguna pelicula y aqui apareceran "
                                "titulos parecidos.");
          } else {
            listaPaginada("recs", recomendaciones, paginaRecs, false);
          }
        });
        ImGui::EndTabItem();
      }

      // -----------------------------------------------------------------------
      // BUSCAR: Builder + autocompletado (Trie) + rango (AVL) + 5 en 5
      // -----------------------------------------------------------------------
      if (ImGui::BeginTabItem("Buscar")) {
        ImGui::SetNextItemWidth(420);
        if (ImGui::InputTextWithHint("##texto", "Palabra, frase o sub-palabra...",
                                     bufTexto, sizeof(bufTexto),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
          ejecutarBusqueda();
        }
        if (ImGui::IsItemEdited()) {
          // Autocompletado: prefijo de la ultima palabra en el TriePrefijos
          const std::string norm = pf::recortar(pf::normalizar(bufTexto));
          const size_t sp = norm.find_last_of(' ');
          const std::string prefijo =
              sp == std::string::npos ? norm : norm.substr(sp + 1);
          sugerencias.clear();
          if (prefijo.size() >= 2) {
            // Se piden hasta 300 palabras con ese prefijo al TriePrefijos y
            // se muestran las 6 que aparecen en mas peliculas.
            const auto &vocab = motor.indice().vocabulario();
            auto ids = vocab.conPrefijo(prefijo, 300);
            const size_t k = std::min<size_t>(6, ids.size());
            std::partial_sort(ids.begin(), ids.begin() + k, ids.end(),
                              [&](int a, int b) {
                                return motor.indice().frecuenciaDocumental(a) >
                                       motor.indice().frecuenciaDocumental(b);
                              });
            for (size_t i = 0; i < k; ++i) {
              sugerencias.push_back(vocab.termino(ids[i]));
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Buscar")) ejecutarBusqueda();
        ImGui::SameLine();
        if (ImGui::Button("Limpiar")) {
          bufTexto[0] = bufDirector[0] = bufReparto[0] = bufGenero[0] =
              bufOrigen[0] = '\0';
          filtroAnio = 0;
          usarRangoAvl = false;
          resultados.clear();
          sugerencias.clear();
          mensajeBusqueda = "Escribe una palabra, frase o sub-palabra.";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s | %.2f ms", mensajeBusqueda.c_str(), msBusqueda);

        if (!sugerencias.empty()) {
          ImGui::TextDisabled("Autocompletar (TriePrefijos):");
          for (const auto &s : sugerencias) {
            ImGui::SameLine();
            if (ImGui::SmallButton(s.c_str())) {
              // Reemplaza la ultima palabra escrita por la sugerencia.
              std::string t = bufTexto;
              const size_t sp = t.find_last_of(' ');
              t = (sp == std::string::npos ? std::string() : t.substr(0, sp + 1)) + s;
              std::strncpy(bufTexto, t.c_str(), sizeof(bufTexto) - 1);
              bufTexto[sizeof(bufTexto) - 1] = '\0';
              ejecutarBusqueda();
              break;
            }
          }
        }

        // Filtros por etiqueta (se combinan con el PATRON BUILDER)
        ImGui::SetNextItemWidth(170);
        ImGui::InputTextWithHint("##dir", "Director", bufDirector, sizeof(bufDirector));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170);
        ImGui::InputTextWithHint("##rep", "Reparto", bufReparto, sizeof(bufReparto));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        ImGui::InputTextWithHint("##gen", "Genero", bufGenero, sizeof(bufGenero));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        ImGui::InputTextWithHint("##ori", "Origen", bufOrigen, sizeof(bufOrigen));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("Anio exacto", &filtroAnio, 0);
        if (filtroAnio < 0) filtroAnio = 0;

        // Rango de anios con el ARBOL AVL
        ImGui::Checkbox("Filtrar por rango de anios (Arbol AVL)", &usarRangoAvl);
        ImGui::SameLine();
        ImGui::BeginDisabled(!usarRangoAvl);
        ImGui::SetNextItemWidth(260);
        ImGui::SliderInt2("##rango", rangoAvl, 1900, 2017);
        ImGui::EndDisabled();
        ImGui::Separator();

        dosPaneles("buscar", [&]() {
          if (resultados.empty()) {
            ImGui::TextDisabled("%s", mensajeBusqueda.c_str());
            ImGui::TextDisabled("Ejemplos: ship | ghost ship | bar | "
                                "director:hitchcock genero:horror");
          } else {
            listaPaginada("res", resultados, paginaBusqueda, true);
          }
        });
        ImGui::EndTabItem();
      }

      // -----------------------------------------------------------------------
      // MIS LISTAS
      // -----------------------------------------------------------------------
      if (ImGui::BeginTabItem("Mis listas")) {
        dosPaneles("listas", [&]() {
          ImGui::TextColored(kRosa, "ME GUSTA (%zu)", estadoUsuario.meGusta().size());
          ImGui::Separator();
          for (int doc : estadoUsuario.meGusta()) {
            const auto &p = motor.catalogo()[doc];
            std::string e = p.titulo + " (" + std::to_string(p.anio) + ")##mg" +
                            std::to_string(doc);
            if (ImGui::Selectable(e.c_str(), docSeleccionado == doc)) seleccionar(doc);
          }
          ImGui::Spacing();
          ImGui::TextColored(kAzul, "VER MAS TARDE (%zu)",
                             estadoUsuario.verMasTarde().size());
          ImGui::Separator();
          for (int doc : estadoUsuario.verMasTarde()) {
            const auto &p = motor.catalogo()[doc];
            std::string e = p.titulo + " (" + std::to_string(p.anio) + ")##lv" +
                            std::to_string(doc);
            if (ImGui::Selectable(e.c_str(), docSeleccionado == doc)) seleccionar(doc);
          }
          ImGui::Spacing();
          ImGui::TextColored(kVerde, "VISTAS VIA PROXY (%zu)",
                             estadoUsuario.vistas().size());
          ImGui::Separator();
          for (int doc : estadoUsuario.vistas()) {
            const auto &p = motor.catalogo()[doc];
            std::string e = p.titulo + " (" + std::to_string(p.anio) + ")##vi" +
                            std::to_string(doc);
            if (ImGui::Selectable(e.c_str(), docSeleccionado == doc)) seleccionar(doc);
          }
        });
        ImGui::EndTabItem();
      }

      // -----------------------------------------------------------------------
      // RENDIMIENTO: 1 hilo vs N hilos (en segundo plano)
      // -----------------------------------------------------------------------
      if (ImGui::BeginTabItem("Rendimiento paralelo")) {
        ImGui::TextWrapped(
            "Compara 1 hilo contra %u hilos en: (1) la construccion del indice "
            "(tokenizacion e insercion de strings en los Tries), (2) una "
            "consulta con re-ranking por frase y (3) un lote de 16 consultas. "
            "Tambien verifica que los resultados sean identicos.",
            pf::resolverHilos(0));
        ImGui::Spacing();

        if (listo(benchEnCurso)) {
          ultimoBench = benchEnCurso.get();
          hayBench = true;
        }
        const bool corriendo = benchEnCurso.valid();
        ImGui::BeginDisabled(corriendo);
        if (ImGui::Button("Ejecutar benchmark")) {
          benchEnCurso = std::async(std::launch::async, [&motor]() {
            return pf::MotorParalelo::ejecutarBenchmark(motor.catalogo(),
                                                        motor.indice());
          });
        }
        ImGui::EndDisabled();
        if (corriendo) {
          ImGui::SameLine();
          ImGui::TextColored(kAmarillo, "Ejecutando... (unos segundos)");
        }

        if (hayBench) {
          const auto &b = ultimoBench;
          auto fila = [](const char *nombre, double sec, double par, bool igual,
                         unsigned hilos) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(nombre);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f ms", sec);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f ms (%u hilos)", par, hilos);
            ImGui::TableNextColumn();
            ImGui::Text("%.2fx", par > 0 ? sec / par : 0.0);
            ImGui::TableNextColumn();
            ImGui::TextColored(igual ? kVerde : kRojo, igual ? "identicos" : "DISTINTOS");
          };
          ImGui::Spacing();
          if (ImGui::BeginTable("bench", 5,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Operacion");
            ImGui::TableSetupColumn("Secuencial");
            ImGui::TableSetupColumn("Paralelo");
            ImGui::TableSetupColumn("Speedup");
            ImGui::TableSetupColumn("Resultados");
            ImGui::TableHeadersRow();
            fila("Construir indice (insercion)", b.msInsercionSecuencial,
                 b.msInsercionParalela, b.insercionIdentica, b.numHilos);
            fila("Una consulta (re-ranking frase)", b.msConsultaSecuencial,
                 b.msConsultaParalela, b.consultaIdentica, b.numHilos);
            fila("Lote de 16 consultas", b.msLoteSecuencial, b.msLoteParalelo,
                 b.loteIdentico, b.numHilos);
            ImGui::EndTable();
          }
          ImGui::TextDisabled("Terminos insertados en el TriePrefijos: %zu",
                              b.terminosInsertados);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(kAmarillo, "ARBOLES EN MEMORIA");
        ImGui::Text("Trie de prefijos : %zu nodos | %zu terminos",
                    motor.indice().vocabulario().numNodos(),
                    motor.indice().vocabulario().numTerminos());
        ImGui::Text("Trie de sufijos  : %zu nodos",
                    motor.indice().sufijos().numNodos());
        ImGui::Text("Arbol AVL anios  : %zu nodos | altura %d",
                    motor.arbolAnios().totalNodos(),
                    motor.arbolAnios().alturaTotal());
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::End();
    dibujarFrame(ventana);
  }

  // Si el benchmark sigue corriendo, se espera a que termine antes de salir.
  if (benchEnCurso.valid()) benchEnCurso.wait();
  if (carga.valid()) carga.wait();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(ventana);
  glfwTerminate();
  return 0;
}
