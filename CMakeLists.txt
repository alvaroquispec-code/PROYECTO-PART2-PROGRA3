cmake_minimum_required(VERSION 3.16)
project(UTEC_Stream CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# La interfaz grafica necesita OpenGL. Si no esta disponible se puede compilar
# solo la consola y las pruebas con:  cmake -B build -DCONSTRUIR_GUI=OFF
option(CONSTRUIR_GUI "Compilar la interfaz grafica (ImGui + GLFW + OpenGL)" ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Motor: todo el codigo del proyecto salvo los main()
# ---------------------------------------------------------------------------
add_library(motor STATIC
    src/normalizador.cpp
    src/csv.cpp
    src/catalogo.cpp
    src/trie.cpp
    src/indice.cpp
    src/buscador.cpp
    src/recomendador.cpp
    src/estado_usuario.cpp
    src/arbol_avl.cpp
    src/patrones.cpp
    src/paralelo.cpp
)
target_include_directories(motor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(motor PUBLIC Threads::Threads)

# 1. Interfaz de consola
add_executable(streaming src/main.cpp)
target_link_libraries(streaming PRIVATE motor)

# 2. Pruebas automatizadas
add_executable(pruebas tests/pruebas.cpp)
target_link_libraries(pruebas PRIVATE motor)

# 3. Interfaz grafica
if(CONSTRUIR_GUI)
  find_package(OpenGL REQUIRED)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG 3.3.8
      GIT_SHALLOW TRUE)  # descarga solo esa version, no todo el historial
  FetchContent_Declare(imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.90.4
      GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw imgui)

  add_library(imgui_lib STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui_lib PUBLIC
      ${imgui_SOURCE_DIR}
      ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

  add_executable(streaming_gui src/gui_main.cpp)
  target_link_libraries(streaming_gui PRIVATE motor imgui_lib)
endif()
