# Point Cloud Collimation

# Ejercicio A 

### Estructura y Funciones Principales del Código

- **Generación del perfil objetivo (H / Riel):**
  Esta función genera de una nube de puntos que semeja una vía de tren, de forma de H. Utiliza una semilla pseudoaleatoria fija y distribuye los puntos alternando de forma cíclica entre el riel izquierdo, el riel derecho y la estructura de los durmientes. A cada coordenada se le aplica un ruido y un límite para simular la dispersión real de los datos.

- **Deformación aleatoria del perfil fuente:**
  Esta función aplica una deformación no rígida a la nube de puntos. Se agregan deformaciones o **bumps** de una forma aleatoria. Cada **bump** es un elemento dentro de un vector que los almacena, cada uno de ellos presenta variaciones que agrega dispersión al conjunto.

- **Construcción de la estructura `GridIndex`:**
  Esta clase implementa una grilla para acelerar la búsqueda de puntos vecinos respecto a una búsqueda exhaustiva. Divide el espacio 2D en celdas cuadradas según un tamaño definido (*cell_size*), indexa las coordenadas en un mapa mediante una clave de 64 bits y agrupa los puntos en sus celdas correspondientes. Durante las consultas, la estructura busca hacia afuera desde la celda de origen, restringiendo las comprobaciones de distancia euclidiana únicamente a los puntos de las celdas cercanas.

- **Búsqueda de vecinos más cercanos:**
  Esta función calcula la distancia euclidiana al vecino más cercano para cada punto de una nube utilizando la grilla `GridIndex`. Recorre uno a uno los puntos, consulta la estructura utilizando comparaciones de distancias al cuadrado y convierte el resultado final con una raíz cuadrada. En caso de que un punto no encuentre ningún vecino en el índice, la función le asigna un valor por defecto (*missing_distance*), entregando como resultado un vector de distancias por punto ideal para evaluar la superposición o error de alineación entre dos nubes.

- **Estimación de la transformación rígida:**
  Esta función calcula la rotación y traslación en 2D para alinear una nube fuente con una objetivo a partir de un conjunto de correspondencias directas. Para lograrlo, calcula primero los centroides de ambos conjuntos y los resta para centrar los datos en el origen, eliminado el efecto del desplazamiento global. Luego, acumula los productos escalares de las coordenadas centradas para obtener el ángulo de rotación exacto mediante `atan2`, y finalmente calcula el vector de traslación necesario para posicionar la nube transformada sobre el centroide objetivo.

- **Comparación de perfiles (Centroides y Distancias):**
  Esta función evalúa la calidad de la alineación entre dos nubes de puntos mediante métricas geométricas bidireccionales y simétricas. Construye grillas espaciales para ambas nubes, calcula la distancia de cada punto fuente hacia su vecino objetivo más cercano y viceversa, y determina la distancia entre sus centroides globales. Además, fusiona todas las distancias obtenidas para extraer la mediana, el percentil 95, el valor máximo, el error cuadrático medio simétrico (RMSE) y un porcentaje de cobertura de puntos que se encuentran dentro de un umbral de tolerancia.

- **Renderizado de cuadros (Visor):**
  Esta función convierte una secuencia de fotogramas de la trayectoria en una animación de video en tiempo real utilizando el pipeline de GStreamer. Inicializa un flujo de video raw RGB a 960x720 píxeles y 5 FPS, dibuja mediante una función auxiliar los perfiles objetivo, fuente y las iteraciones intermedias, y envía cada búfer de imagen a la canalización multimedia. Maneja la sincronización de tiempos frame a frame y libera de manera limpia los recursos del sistema al concluir la reproducción de la trayectoria.

---

### Instrucciones de Ejecución y Exportación

El programa se ejecuta corriendo los siguientes comandos:

```bash
make clean 
make 
./point_cloud_collimation --export
```

Esto genera múltiples archivos csv y también los frames individuales de extensión ppm.

### Resultados obtenidos

A partir de los fotogramas generados, es posible visualizar la evolución de ambas nubes de puntos y el proceso de iteración mediante el cual la nube origen se transforma y alinea con la nube objetivo:

<p align="center">
  <img src="point-cloud-collimation/reconstruction/animation.gif" alt="Animación del proceso de colimación" width="600"/>
  <br>
  <em>Figura 1: Animación de la alineación y colimación de la nube de puntos.</em>
</p>

---

#### Análisis de Métricas y Gráficos de Convergencia

A partir de los datos exportados en los archivos CSV, se generaron las siguientes gráficas que caracterizan el comportamiento del algoritmo:

| Métrica de Error y Distancia | Análisis de Convergencia y Cobertura |
| :---: | :---: |
| ![Distancia de Centroides](point-cloud-collimation/reconstruction/plots/centroid_distance.png) | ![Convergencia](point-cloud-collimation/reconstruction/plots/convergence.png) |
| **Distancia de centroides** a lo largo de las iteraciones | **Criterio y velocidad de convergencia** |
| ![Puntuación de Cobertura](point-cloud-collimation/reconstruction/plots/coverage_score.png) | ![Distribución de Distancias](point-cloud-collimation/reconstruction/plots/distance_distribution.png) |
| **Puntuación de cobertura** del perfil | **Distribución de distancias** entre puntos |

| Correspondencia y RMSE | Transformación Aplicada |
| :---: | :---: |
| ![Correspondencias (Matches)](point-cloud-collimation/reconstruction/plots/matches.png) | ![Paso de Transformación](point-cloud-collimation/reconstruction/plots/transformation.png) |
| **Emparejamiento de puntos (*matches*)** entre perfiles | **Evolución del paso de transformación** |

<p align="center">
  <img src="point-cloud-collimation/reconstruction/plots/rmse_metrics.png" alt="Métricas RMSE" width="700"/>
  <br>
  <em>Figura 2: Evolución de las métricas de error cuadrático medio (RMSE).</em>
</p>


Finalmente, los centroides representan la posición promedio de todos los puntos de un perfil. En las primeras iteraciones, la distancia entre el centroide del perfil fuente (*source*) y el del objetivo (*target*) suele ser grande debido a la desalineación inicial; a medida que el algoritmo avanza, esta distancia disminuye progresivamente hasta estabilizarse; sin embargo, conforme se realizan las ultimas iteraciones se puede observar que esta distancia incremente. Mediante análisis visual de la animación presentada anteriormente, se puede observar que cuando se alinean las nubes de puntos no quedan superpuestas perfectamente, sino que presentan una desalineación. De aquí se puede comprender que por este desfase la distancia entre centroides final no resulta ser la ideal.

Las métricas de error evalúan la alineación mediante distintas perspectivas: `match_rmse` mide el error cuadrático medio considerando únicamente las parejas de puntos asociados entre ambos perfiles, `symmetric_chamfer_rmse` calcula el error de la fuente al objetivo y viceversa y `profile_score` es una métrica general que combina la precisión de las distancias con la tasa de cobertura de puntos alineados bajo un umbral de tolerancia.

Una deformación no rígida introduce distorsiones que alteran la forma interna del perfil. Dado que la transformación recuperada por el algoritmo está restringida a solo rotaciones y traslaciones, este no puede modelar los cambios locales, lo que impide que la transformación estimada coincida exactamente con la transformación ideal utilizada antes de deformar el objeto. Es decir que esta deformación hace que las dos nubes de puntos sean distintas, pero aún así el programa busca hacer que se alineen con bajo error.


# Ejercicio B

## Profiling con perf

### perf stat

Primero se utilizó `perf stat` para obtener estadísticas globales de la ejecución.

En la computadora utilizada, `perf_event_paranoid` tenía el valor 4. Esta configuración restringía el acceso a los contadores de hardware utilizados por `perf`, por lo que fue necesario ejecutar las mediciones con `sudo`.

Sin exportación:

```bash
sudo perf stat -e cycles,instructions,branches,branch-misses,cache-references,cache-misses ./point_cloud_collimation
```

Con exportación:

```bash
sudo perf stat -e cycles,instructions,branches,branch-misses,cache-references,cache-misses ./point_cloud_collimation --export
```

Los principales resultados fueron:

| Métrica | Sin `--export` | Con `--export` |
|---|---:|---:|
| Tiempo | 90.799 s | 105.118 s |
| Ciclos | 245,189,235,945 | 281,455,201,968 |
| Instrucciones | 206,752,172,429 | 239,079,272,994 |
| Branches | 27,446,425,765 | 33,774,596,421 |
| Branch misses | 688,580,519 | 749,063,863 |
| Cache references | 33,041,980,015 | 33,157,376,901 |
| Cache misses | 3,819,486,864 | 4,403,591,431 |

La ejecución con `--export` tardó aproximadamente 14.32 segundos más.

Los valores principales de estas pruebas se dejaron resumidos en:

```text
results-EjercicioB/perf_stat_results.txt
```

### Localización de hotspots con perf record

Después de obtener las estadísticas globales se utilizó `perf record` para tomar muestras durante la ejecución y `perf report` para observar en qué funciones se concentraban.

Sin exportación:

```bash
sudo perf record -g ./point_cloud_collimation
```

Este comando genera el archivo:

```text
perf.data
```

Para obtener un reporte más sencillo de revisar se utilizó:

```bash
sudo perf report -i perf.data --stdio --no-children --call-graph=none > perf_summary_no_export.txt
```

Para la ejecución con exportación se utilizó un nombre diferente para no sobrescribir el perfil anterior:

```bash
sudo perf record -g -o perf_export.data ./point_cloud_collimation --export
```

El resumen correspondiente se obtuvo con:

```bash
sudo perf report -i perf_export.data --stdio --no-children --call-graph=none > perf_summary_export.txt
```

El principal hotspot identificado fue `GridIndex::nearest()`.

Sin `--export`, esta función representó aproximadamente un 97.14 % del `self overhead`.

Con `--export`, continuó siendo la función dominante, con aproximadamente un 88.67 %.

## Profiling con Valgrind / Callgrind

También se utilizó Callgrind para analizar el programa desde otra perspectiva, contando principalmente referencias de instrucciones.

Sin exportación:

```bash
valgrind --tool=callgrind --callgrind-out-file=callgrind_no_export.out ./point_cloud_collimation
```

El reporte se convirtió a un archivo de texto mediante:

```bash
callgrind_annotate callgrind_no_export.out > callgrind_report_no_export.txt
```

Con exportación:

```bash
valgrind --tool=callgrind --callgrind-out-file=callgrind_export.out ./point_cloud_collimation --export
```

El reporte correspondiente se obtuvo con:

```bash
callgrind_annotate callgrind_export.out > callgrind_report_export.txt
```

Los resultados principales fueron:

| Configuración | Ir total | `GridIndex::nearest()` |
|---|---:|---:|
| Sin `--export` | 205,733,271,542 | 81.01 % |
| Con `--export` | 237,412,442,596 | 70.20 % |

## Profiling con Google Performance Tools

Para esta parte fue necesario realizar algunos ajustes debido a las herramientas disponibles en la computadora.

El comando `google-pprof` no estaba disponible directamente en el sistema. Por esta razón se utilizó `libprofiler` de Google Performance Tools para generar los perfiles y la versión actual de `pprof` para analizarlos.

Para permitir el profiling se recompiló temporalmente el programa enlazando `libprofiler` y conservando el frame pointer:

```bash
make clean
make CXXFLAGS="-std=c++17 -O2 -g -Wall -Wextra -pedantic -fno-omit-frame-pointer" GST_LIBS="-Wl,--no-as-needed -lprofiler -Wl,--as-needed $(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)"
```

Se verificó que `libprofiler` estuviera enlazada con el ejecutable mediante:

```bash
ldd ./point_cloud_collimation | grep profiler
```

Sin exportación se generó el perfil con:

```bash
CPUPROFILE=point_cloud.prof ./point_cloud_collimation
```

y se analizó con:

```bash
~/go/bin/pprof -text ./point_cloud_collimation point_cloud.prof > pprof_report_no_export.txt
```

Para la ejecución con exportación:

```bash
CPUPROFILE=point_cloud_export.prof ./point_cloud_collimation --export
```

El reporte se generó con:

```bash
~/go/bin/pprof -text ./point_cloud_collimation point_cloud_export.prof > pprof_report_export.txt
```

Los resultados principales fueron:

| Configuración | `nearest()` flat | `nearest()` cumulative |
|---|---:|---:|
| Sin `--export` | 64.23 % | 98.24 % |
| Con `--export` | 56.97 % | 87.97 % |

## Archivos de resultados

Los reportes utilizados para documentar el ejercicio se encuentran en:

```text
results-EjercicioB/
├── callgrind_report_export.txt
├── callgrind_report_no_export.txt
├── perf_stat_results.txt
├── perf_summary_export.txt
├── perf_summary_no_export.txt
├── pprof_report_export.txt
└── pprof_report_no_export.txt
```

Los perfiles crudos, como `perf.data`, `perf_export.data` y los archivos `.prof`, se utilizaron durante el análisis, pero no se incluyeron como resultados finales del repositorio.

## Respuestas del Ejercicio B

### ¿Cuáles fueron los hotspots encontrados con cada herramienta?

Las tres herramientas mostraron que la mayor parte del costo del programa está relacionada con `GridIndex::nearest()`, que es la función encargada de buscar los vecinos más cercanos.

Con `perf report`, esta función presentó aproximadamente un 97.14 % de `self overhead` sin `--export` y un 88.67 % con `--export`.

Con Callgrind, `GridIndex::nearest()` representó aproximadamente un 81.01 % de las referencias de instrucciones sin exportación y un 70.20 % con exportación.

Por último, con `pprof` se obtuvo un 64.23 % `flat` y un 98.24 % `cumulative` sin `--export`. Con exportación los valores fueron 56.97 % `flat` y 87.97 % `cumulative`.

### ¿Coinciden perf, Google Performance Tools y Valgrind?

Sí. Los valores obtenidos no son iguales porque cada herramienta mide el programa de una forma diferente, pero las tres identificaron `GridIndex::nearest()` como la región dominante.

### ¿Cuál es el costo de utilizar `--export`?

Según `perf stat`, el tiempo pasó de aproximadamente 90.80 s sin exportación a 105.12 s con exportación.

Esto representa un aumento cercano al 15.77 %. Con `--export` al parecer se realiza trabajo adicional.

### ¿Qué herramienta fue más clara para decidir qué optimizar?

Puede decirse que, `perf report` fue la herramienta más clara para identificar qué parte del programa convendría estudiar primero para una optimización, porque mostró directamente que `GridIndex::nearest()` concentraba aproximadamente un 97.14 % del `self overhead` sin exportación.

Callgrind y `pprof` fueron útiles para confirmar este resultado utilizando otras formas de medición.
