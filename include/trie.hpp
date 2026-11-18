// trie.hpp — Las dos estructuras de arbol del proyecto.
//
// Ambos arboles guardan UN CARACTER por nodo, tal como exige el enunciado.
// Los hijos se representan como lista enlazada de hermanos (primerHijo /
// hermano) en lugar de un arreglo de 36 punteros por nodo: con ~5 millones de
// nodos, el arreglo costaria >700 MB mientras que la lista enlazada cuesta
// 16 bytes por nodo. El alfabeto es [a-z0-9] (36 simbolos), asi que recorrer
// la lista de hermanos es O(36) en el peor caso y ~O(3) en la practica.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "binario.hpp"

namespace pf {

// ---------------------------------------------------------------------------
// NODOS ADAPTATIVOS
//
// La lista de hermanos ahorra memoria, pero para bajar de un nodo a su hijo
// 'c' hay que recorrer los hermanos: O(sigma) por letra, con sigma = 36. Medido
// sobre el vocabulario real, el 57 % de los nodos tiene un solo hijo y solo el
// ~3 % tiene 4 o mas, pero esos pocos (la raiz tiene 36) concentran el costo.
//
// Solucion: despues de construir el arbol, los nodos con kHijosParaDenso o mas
// hijos reciben ademas un arreglo de 36 indices (uno por simbolo [a-z0-9]) y
// saltan al hijo en O(1). El resto sigue con la lista, que tiene como maximo
// kHijosParaDenso - 1 elementos. Asi cada letra cuesta O(1) en el peor caso y
// buscar una palabra es O(|palabra|) garantizado.
//
// El indice del arreglo se guarda en los 3 bytes de relleno que el nodo ya
// tenia (antes se desperdiciaban), asi que el nodo sigue midiendo 16 bytes.
// ---------------------------------------------------------------------------
constexpr int kHijosParaDenso = 4;
constexpr int kSimbolos = 36; // [a-z0-9]
using TablaHijos = std::array<int32_t, kSimbolos>;

namespace detalle {
inline int simbolo(unsigned char c) {
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  return -1;
}
// denso[3] guarda (indice en la tabla + 1) en 24 bits; 0 = nodo no denso.
template <typename N> int32_t indiceDenso(const N &n) {
  return static_cast<int32_t>(n.denso[0]) |
         (static_cast<int32_t>(n.denso[1]) << 8) |
         (static_cast<int32_t>(n.denso[2]) << 16);
}
template <typename N> void ponerIndiceDenso(N &n, int32_t v) {
  n.denso[0] = static_cast<uint8_t>(v & 0xFF);
  n.denso[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  n.denso[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
}
// Hijo de 'padre' con el caracter c, o -1. O(1) si el padre es denso; si no,
// recorre a lo sumo kHijosParaDenso - 1 hermanos.
template <typename N>
int32_t hijo(const std::vector<N> &nodos, const std::vector<TablaHijos> &tablas,
             int32_t padre, unsigned char c) {
  const int32_t d = indiceDenso(nodos[padre]);
  if (d != 0) {
    const int s = simbolo(c);
    return s < 0 ? -1 : tablas[d - 1][s];
  }
  for (int32_t h = nodos[padre].primerHijo; h != -1; h = nodos[h].hermano) {
    if (nodos[h].c == c) return h;
  }
  return -1;
}
// Recalcula que nodos son densos y sus tablas. Determinista.
template <typename N>
void densificar(std::vector<N> &nodos, std::vector<TablaHijos> &tablas) {
  tablas.clear();
  for (N &n : nodos) ponerIndiceDenso(n, 0);
  for (N &n : nodos) {
    int hijos = 0;
    bool alfabetoValido = true;
    for (int32_t h = n.primerHijo; h != -1; h = nodos[h].hermano) {
      ++hijos;
      if (simbolo(nodos[h].c) < 0) alfabetoValido = false;
    }
    if (hijos < kHijosParaDenso || !alfabetoValido) continue;
    TablaHijos t;
    t.fill(-1);
    for (int32_t h = n.primerHijo; h != -1; h = nodos[h].hermano) {
      t[simbolo(nodos[h].c)] = h;
    }
    tablas.push_back(t);
    ponerIndiceDenso(n, static_cast<int32_t>(tablas.size()));
  }
}
// Al agregar un hijo a un nodo que ya es denso, se actualiza su tabla.
template <typename N>
void registrarHijo(std::vector<N> &nodos, std::vector<TablaHijos> &tablas,
                   int32_t padre, int32_t nuevo) {
  const int32_t d = indiceDenso(nodos[padre]);
  const int s = simbolo(nodos[nuevo].c);
  if (d != 0 && s >= 0) tablas[d - 1][s] = nuevo;
}
} // namespace detalle

// ---------------------------------------------------------------------------
// TriePrefijos: diccionario del vocabulario.
// Cada palabra distinta del corpus se inserta una sola vez y recibe un id.
// Resuelve (a) busqueda exacta de palabra y (b) busqueda por prefijo, ambas en
// O(|patron|) sin tablas hash.
// ---------------------------------------------------------------------------
class TriePrefijos {
public:
  TriePrefijos();

  // Inserta la palabra si no existe y devuelve su id de termino.
  int insertar(const std::string &palabra);

  // Devuelve el id del termino, o -1 si no esta en el vocabulario. O(|palabra|)
  int buscar(const std::string &palabra) const;

  // Ids de todos los terminos que empiezan con el prefijo dado.
  std::vector<int> conPrefijo(const std::string &prefijo, size_t maxIds) const;

  // Crea las tablas de los nodos densos (ver NODOS ADAPTATIVOS). Se llama al
  // terminar de construir y al cargar el cache. Insertar despues sigue siendo
  // correcto.
  void densificar() { detalle::densificar(nodos_, tablas_); }
  size_t numNodosDensos() const { return tablas_.size(); }

  // Reconstruye la cadena asociada a un id de termino.
  const std::string &termino(int id) const { return terminos_[id]; }

  size_t numTerminos() const { return terminos_.size(); }
  size_t numNodos() const { return nodos_.size(); }
  size_t bytesAproximados() const;

  void escribir(EscritorBinario &e) const;
  bool leer(LectorBinario &l);

private:
  struct Nodo {
    int32_t primerHijo = -1;
    int32_t hermano = -1;
    int32_t idTermino = -1; // >= 0 si aqui termina una palabra
    uint8_t c = 0;
    uint8_t denso[3] = {0, 0, 0}; // indice de su tabla de hijos + 1 (0 = no denso)
  };
  static_assert(sizeof(Nodo) == 16, "el nodo debe seguir midiendo 16 bytes");
  std::vector<Nodo> nodos_;
  std::vector<std::string> terminos_;
  std::vector<TablaHijos> tablas_; // solo para los nodos densos

  int32_t descender(const std::string &s) const; // -1 si el camino no existe
};

// ---------------------------------------------------------------------------
// TrieSufijos: trie generalizado de sufijos SOBRE EL VOCABULARIO.
//
// Para cada palabra distinta se insertan todos sus sufijos y se marca el nodo
// final con el id del termino. Entonces:
//
//     terminos que contienen el patron P
//        = terminos marcados en el subarbol al que se llega descendiendo P
//
// porque P aparece en T en la posicion i si y solo si el sufijo T[i..] empieza
// con P. La busqueda es O(|P| + tamano del subarbol).
//
// Se construye sobre el VOCABULARIO (143 535 palabras distintas, 1.07 M de
// caracteres) y no sobre el texto completo (13.3 M de tokens, 75 MB): esa es la
// decision que hace viable la busqueda por sub-cadena en memoria.
// ---------------------------------------------------------------------------
class TrieSufijos {
public:
  TrieSufijos();

  // Inserta los |t| sufijos de t, todos marcados con idTermino.
  void insertarTermino(const std::string &t, int idTermino);

  // Ids de los terminos del vocabulario que contienen 'patron' como sub-cadena.
  // 'maxIds' acota el recorrido para patrones muy cortos y frecuentes.
  std::vector<int> terminosQueContienen(const std::string &patron,
                                        size_t maxIds) const;

  size_t numNodos() const { return nodos_.size(); }
  size_t numNodosDensos() const { return tablas_.size(); }
  size_t bytesAproximados() const;

  // Ver NODOS ADAPTATIVOS arriba.
  void densificar() { detalle::densificar(nodos_, tablas_); }

  void escribir(EscritorBinario &e) const;
  bool leer(LectorBinario &l);

private:
  struct Nodo {
    int32_t primerHijo = -1;
    int32_t hermano = -1;
    int32_t cabezaLista = -1; // lista enlazada de ids de termino
    uint8_t c = 0;
    uint8_t denso[3] = {0, 0, 0}; // indice de su tabla de hijos + 1 (0 = no denso)
  };
  static_assert(sizeof(Nodo) == 16, "el nodo debe seguir midiendo 16 bytes");
  struct Entrada {
    int32_t idTermino;
    int32_t siguiente;
  };
  std::vector<Nodo> nodos_;
  std::vector<Entrada> entradas_;
  std::vector<TablaHijos> tablas_; // solo para los nodos densos
};

} // namespace pf
