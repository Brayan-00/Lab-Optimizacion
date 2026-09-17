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

 Breve descripción del proyecto, su propósito en el procesamiento/alineación de perfiles (vía o riel tipo H) y el objetivo general de este laboratorio/práctica.

---

## 🛠️ 1. Estructura y Funciones Principales del Código
*Sección destinada a identificar y documentar los bloques funcionales del archivo `point_cloud_collimation.cpp`.*

- **Generación del perfil objetivo (H / Riel):** Explicación sintética de cómo se sintetiza o carga la nube de puntos del perfil de referencia (*target*).
- **Deformación aleatoria del perfil fuente:** Descripción del proceso de aplicación de transformaciones (rígidas y/o no rígidas) al perfil fuente (*source*).
- **Construcción de la estructura `GridIndex`:** Explicación del funcionamiento de la estructura de datos espacial para optimizar búsquedas.
- **Búsqueda de vecinos más cercanos:** Detalle del algoritmo utilizado para la asociación de puntos entre perfiles.
- **Estimación de la transformación rígida:** Explicación del método para calcular la matriz de rotación y traslación óptima (e.g., ICP / SVD).
- **Comparación de perfiles (Centroides y Distancias):** Métodos empleados para la cuantificación de discrepancias geométricas.
- **Renderizado de cuadros (Visor):** Breve síntesis del pipeline de generación visual de fotogramas (`.ppm`).

---

## 🚀 2. Instrucciones de Ejecución y Exportación
*Sección con los comandos necesarios para reproducir los experimentos y exportar datos.*

```bash
# Compilación (si aplica)
# ...

# Ejecución exportando resultados a la carpeta reconstruction/
./point_cloud_collimation --export