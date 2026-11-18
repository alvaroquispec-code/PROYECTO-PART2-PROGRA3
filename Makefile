# Makefile de respaldo (si no hay CMake instalado).
# Compila la consola y las pruebas. La interfaz grafica necesita CMake
# porque descarga ImGui y GLFW automaticamente.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude -pthread
# -MMD -MP: recompila un .cpp cuando cambia cualquier .hpp que incluya
DEPFLAGS  = -MMD -MP
LDFLAGS  ?= -pthread
MOTOR     = src/normalizador.cpp src/csv.cpp src/catalogo.cpp src/trie.cpp \
            src/indice.cpp src/buscador.cpp src/recomendador.cpp \
            src/estado_usuario.cpp src/arbol_avl.cpp src/patrones.cpp \
            src/paralelo.cpp
OBJ_MOTOR = $(MOTOR:.cpp=.o)

all: streaming

streaming: $(OBJ_MOTOR) src/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

pruebas: $(OBJ_MOTOR) tests/pruebas.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_MOTOR) src/main.o tests/pruebas.o streaming pruebas src/*.d tests/*.d

-include $(OBJ_MOTOR:.o=.d) src/main.d tests/pruebas.d

.PHONY: all clean
