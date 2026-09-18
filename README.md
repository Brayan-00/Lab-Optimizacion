# Point Cloud Collimation

This example generates two 2D point clouds. The number of points is controlled
by `POINTS_PER_CLOUD` in the source file. The clouds represent an H-shaped rail
structure: two long rails and repeated cross ties. The second cloud is produced
by rotating and translating the first cloud, applying a reproducible random
non-rigid deformation, then adding small sensor noise.

All point coordinates live in a `10000 x 10000` canvas. The target profile is
generated inside `[0, 10000] x [0, 10000]`, and the displaced source profile is
kept inside the same window after deformation and sensor noise.

The program estimates the rotation and translation needed to collimate the
second cloud onto the first one. It uses:

- Centroid and distance-distribution metrics to compare both profiles.
- PCA-based initialization to avoid poor ICP local minima.
- Grid-based nearest-neighbor matching.
- A 2D rigid transform estimate at each ICP iteration.
- A convergence condition based on symmetric profile distance and transform
  step size, instead of only MSE variation.

## Build

```bash
make
```

The viewer requires GStreamer development packages:

```bash
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
```

## Run

```bash
./point_cloud_collimation
```

By default, the source cloud includes a random deformation with amplitude
`60` coordinate units. Change it with:

```bash
./point_cloud_collimation --deformation 0.10
```

Disable deformation to recover the almost exact rigid transform:

```bash
./point_cloud_collimation --no-deformation
```

Run with viewer:

```bash
./point_cloud_collimation --viewer
```

The viewer animates the same reconstructed sequence exported by `--export`.
Each frame projects the full `10000 x 10000` canvas into the video window and
shows the fixed target, the initial displaced source, previous source positions
as a motion trail, and the current aligned source position.

Export reconstructed profiles and movement frames:

```bash
./point_cloud_collimation --export
```

Use a custom output directory:

```bash
./point_cloud_collimation --output reconstruction_run
```

The export creates:

- `target_profile.csv`: fixed reference H/rail profile.
- `source_initial_profile.csv`: displaced and rotated source profile.
- `source_final_profile.csv`: final aligned source profile.
- `source_motion.csv`: accumulated rotation and translation for each frame.
- `profile_metrics.csv`: centroid distance, nearest-neighbor RMSE in both
  directions, symmetric Chamfer RMSE, median distance, p95 distance, max
  distance, coverage, profile score, score variation, and transform step.
- `source_frame_*.csv`: reconstructed source coordinates at each movement step.
- `frame_*.ppm`: rendered profile overlays for each movement step.

The viewer publishes generated RGB frames through GStreamer using:

```text
appsrc ! videoconvert ! autovideosink
```

Color convention:

- Blue: reference target cloud.
- Red: initial displaced and rotated source cloud.
- Pale green: previous source positions across the collimation iterations.
- Green: current aligned source cloud.

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