# Carpeta de datos

El dataset **no se versiona**: pesa 81 MB y el caché que genera el programa
pesa otros 201 MB (GitHub rechaza archivos de más de 100 MB).

Descárgalo desde:

**https://drive.google.com/file/d/1UJkRuCF8UD92W_DT7S8dXCYzaR_9wqB_/view?usp=sharing**

y guárdalo aquí con este nombre exacto:

```
data/wiki_movie_plots_deduped.csv
```

Si se descarga como `.zip`, descomprímelo primero; si el archivo trae otro
nombre (por ejemplo `wiki_movie_plots_deduped (1).csv`), renómbralo.

Comprobación rápida (Linux/macOS):

```bash
wc -c data/wiki_movie_plots_deduped.csv   # 81193310 bytes
head -1 data/wiki_movie_plots_deduped.csv # Release Year,Title,Origin/Ethnicity,...
```

Los programas aceptan también una ruta distinta como primer argumento:

```bash
./build/streaming_gui /otra/ruta/peliculas.csv
./build/streaming     /otra/ruta/peliculas.csv
```

La primera ejecución escribe `wiki_movie_plots_deduped.csv.cache` junto al CSV;
también está en `.gitignore`.
