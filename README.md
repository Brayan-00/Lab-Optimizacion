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


## Ejercicio A 


### Estructura y Funciones Principales del Código
*Sección destinada a identificar y documentar los bloques funcionales del archivo `point_cloud_collimation.cpp`.*

- **Generación del perfil objetivo (H / Riel):** This function creates a synthetic point cloud representing a railroad track, using a seeded pseudo-random generator so the same input seed produces the same cloud every time. It begins by creating a random-number engine and several distributions: one for the vertical rail positions, one for the horizontal tie positions, one for tie spacing, and several Gaussian distributions for small random offsets and widths. The result is stored in a vector of Point objects, where each Point has an x and y coordinate. It also reserves space for n points up front so the vector does not need to reallocate repeatedly during the loop.

The loop fills the cloud one point at a time. It computes a selector based on the loop index: selector = i % 10 / 10.0 gives a value in the range [0, 1), evenly cycling through ten buckets. If selector is below 0.35, it generates a point on the left rail: x is around 3600 plus a small Gaussian offset, and y is a random value between 1200 and 8800 with additional noise and then clamped to the canvas height. If selector is between 0.35 and 0.70, it does the same for the right rail at x ≈ 6400. Otherwise, it creates a tie point: a random x between 3200 and 6800, and a y based on a repeated cycle of tie positions starting near 1600 and stepping by 680 units. This produces a repeated “tie” pattern across the track, with noise and clamping to keep the points inside the visible canvas bounds.

The final step calls std::shuffle on the vector using the same RNG, which randomizes the order of the generated points. That matters because the cloud is built in a structured pattern, but the shuffle prevents the output from looking artificially ordered. The function is effectively a procedural generator: it builds a noisy, track-like H-shaped rail arrangement, with left and right rails and cross-ties, and returns that point set ready to be rendered or processed later.

- **Deformación aleatoria del perfil fuente:** Descripción del proceso de aplicación de transformaciones (rígidas y/o no rígidas) al perfil fuente (*source*).

This function intentionally perturbs a point cloud to simulate small non-rigid deformations in a track or rail surface. It first exits early when amplitude is zero or negative, because there is no deformation to apply. Then it creates a local random generator and a set of “bumps,” each described by a center point, a direction vector, and a sigma value. The bump direction is generated randomly in the plane, normalized to unit length, and the center is chosen uniformly inside the canvas bounds. This gives the deformation a few spatially localized drift fields that are stronger near the bump centers and weaker farther away.

The deformation itself is a combination of a smooth global wave and several localized Gaussian influence regions. For each point p, the function computes wave_x and wave_y using sine functions of x and y, scaled by amplitude. This creates a soft, continuous distortion across the cloud. It then adds random noise sampled from a normal distribution with standard deviation amplitude * 0.10. That makes each point move slightly differently, so the deformation is not perfectly uniform. The random noise is added to each axis separately, which creates a noisy yet smooth warping field.

After the global wave and local noise are computed, the function applies the bump effects. For every bump, it measures the distance from the point to the bump center, computes an influence weight using a Gaussian function, and adds a displacement proportional to amplitude * influence * direction. The Gaussian term is important because it makes each bump affect nearby points strongly and faraway points barely at all. The sigma value controls the spread of the bump: larger sigma means a wider area of influence. This is a classic “blob” deformation pattern, where each bump behaves like a soft localized force.

Finally, the point is clamped back into the valid canvas range using std::clamp, so the deformation cannot push points outside the image bounds. The function also accumulates the squared displacement magnitude dx * dx + dy * dy across all points and returns the root-mean-square displacement at the end. This gives a scalar summary of how much the cloud moved overall. It is a practical way to measure the intensity of the deformation after applying the random perturbation.


- **Construcción de la estructura `GridIndex`:** Explicación del funcionamiento de la estructura de datos espacial para optimizar búsquedas.


This class builds a spatial hash, often called a uniform grid, to make nearest-neighbor queries faster than scanning every point in the cloud. The constructor receives a reference to a vector of Point objects and a cell_size, then stores both as member variables. It iterates through every point, computes which grid cell it belongs to with cell_of, and inserts the point’s index into a map keyed by that cell. The map is named cells_, and its structure is “cell key -> list of point indices.” This is efficient because points that are close together in space are grouped into the same or neighboring cells, so a search can ignore most of the cloud instead of checking every point.

The grid cell index is created by cell_of(p), which divides the x and y coordinates by cell_size and applies floor. For example, if cell_size is 100, then all points with x in [0, 100) go to cell 0, those in [100, 200) go to cell 1, and so on. The key function packs the x and y cell coordinates into a single 64-bit integer so they can be used as a single hash key in the unordered_map. This is a common trick in spatial indexing: instead of storing nested maps or pairs, it flattens the pair into one value. The map lookup becomes fast, and the code can find all points in a given area by looking up just one cell ID.

The purpose of the stored indices is to avoid expensive repeated distance checks. Later, when nearest(query, ...) is called, the code determines the cell of the query point, then explores outward in expanding rings of neighboring cells. It starts at the same cell and checks increasingly larger neighborhoods until it finds a candidate close enough to stop. For each candidate point in those cells, it computes squared Euclidean distance and updates nearest_point and nearest_distance2 if it is smaller than the best so far. This pattern is a good compromise between simplicity and speed: it preserves exact nearest-neighbor behavior while restricting the search to a localized region of the grid instead of the whole dataset. The one important gotcha is that the grid only works well when cell_size is chosen sensibly: too small and you get many cells with few points; too large and each cell contains too many points, reducing the benefit.
- **Búsqueda de vecinos más cercanos:** Detalle del algoritmo utilizado para la asociación de puntos entre perfiles.

This function computes the nearest-neighbor distance for every point in a cloud, using a previously built spatial index. It takes a const reference to a GridIndex and a const reference to the cloud being queried, and it also receives a numeric value named missing_distance to use when a point has no nearest neighbor within the index. The function creates an output vector distances and reserves space for exactly cloud.size() elements to avoid repeated reallocations during the loop. This is a small but useful optimization when the point set is large.

The loop visits each point p in the cloud. For each point, it creates a local nearest_point initialized to (0, 0) and a local double d2 initialized to 0.0. Then it calls index.nearest(p, nearest_point, d2). That method returns true if it found at least one point in the grid, and it fills nearest_point with the closest point and d2 with the squared Euclidean distance between p and that nearest point. If the search succeeds, the function converts the squared distance to a real distance with std::sqrt(d2), and pushes that value into distances. If the search fails, it assumes the point is missing or outside the indexed region and pushes missing_distance instead. This lets the caller treat unmatched points consistently in downstream metrics such as coverage, RMSE, or percentiles.

The purpose of this helper is to produce a per-point distance profile for a cloud relative to another cloud or dataset. It is especially useful in comparing how close source points are to target points, or vice versa. Because it stores one scalar distance per point, the caller can analyze overall spatial agreement without repeatedly handling the original point pairs. The key idea is that squared distances are compared internally for speed, while the final value is converted to Euclidean distance only when needed for reporting or scoring.

This function computes the nearest-neighbor distance for each point in a point cloud using a spatial index. It takes a const GridIndex reference and a const vector<Point>& cloud, then builds a distances vector with one entry per point. It calls reserve(cloud.size()) so the output vector does not need to resize repeatedly while the loop runs. This is a good practice for performance when dealing with many points.

Inside the loop, each point p is queried against the grid index. It creates a temporary nearest_point initialized to (0, 0) and a temporary d2 initialized to 0.0. Then it calls index.nearest(p, nearest_point, d2). If the index finds any point, the function stores the squared distance in d2 and then converts it to Euclidean distance with std::sqrt(d2). If no nearest point is found, the function uses missing_distance instead. This gives a distance value for every point in the cloud regardless of whether it had a valid match in the indexed structure.

The result is a vector of distances that represents how far each point is from the closest point in the indexed dataset. This is useful for evaluating overlap, coverage, or alignment quality between two clouds. The function intentionally uses squared distances internally in the grid search because comparing squares is cheaper and avoids unnecessary square roots until the final result. The final output is thus both efficient and easy to analyze statistically later.



- **Estimación de la transformación rígida:** Explicación del método para calcular la matriz de rotación y traslación óptima (e.g., ICP / SVD).

This code estimates a 2D rigid transform from a set of matched point pairs. It assumes each Match contains a source point and a target point, and the goal is to find the rotation and translation that best align the source cloud to the target cloud. The first check is important: if there are no matches, the function cannot compute a meaningful transform, so it throws a runtime error instead of silently producing garbage.

The next part computes the centroid of all source points and the centroid of all target points. A centroid is just the average position of the points in each set. The code sums every source x and y with the corresponding target x and y, then divides by the number of matches. This gives two center points, cs and ct, which represent the average position of the source cloud and the target cloud. By subtracting these centroids in the next loop, the code removes the global translation so the remaining calculation focuses only on orientation.

The core of the rotation estimate is the dot and cross accumulation. For each match, it computes the centered coordinates:

sx = source.x - cs.x
sy = source.y - cs.y
tx = target.x - ct.x
ty = target.y - ct.y
Then it accumulates:

dot += sx * tx + sy * ty
cross += sx * ty - sy * tx
This is the 2D equivalent of a cross-correlation between the source and target vectors after centering. The dot term measures how aligned the directions are, while the cross term measures the signed area of rotation. The angle is then computed with atan2(cross, dot), which gives the rotation angle in radians. This is robust because atan2 preserves the correct quadrant and handles both positive and negative rotation directions.

Finally, the function builds the transform returned as {theta, tx, ty}. The returned translation is computed so that after rotating the source centroid by theta, it lands exactly on the target centroid. The formula uses the rotation matrix:

x' = c * x - s * y
y' = s * x + c * y
Then the translation is chosen as:

tx = ct.x - (c * cs.x - s * cs.y)
ty = ct.y - (s * cs.x + c * cs.y)
This means: rotate the source point around the origin using the estimated angle, then translate it so the rotated centroid matches the target centroid. In other words, the function computes the best rigid transform that aligns one point set to another while preserving distances and angles.

- **Comparación de perfiles (Centroides y Distancias):** Métodos empleados para la cuantificación de discrepancias geométricas.


This function compares two point clouds, target and source, by computing several geometric and distance-based metrics that summarize how similar they are. It first builds a spatial index for each cloud using a grid cell size of 90.0. The idea is to speed up nearest-neighbor queries: instead of checking every point in a cloud for every query point, the grid lets the function look only at nearby cells. Then it computes nearest-neighbor distances from each cloud to the other one using nearest_neighbor_distances. Specifically, source_distances measures how far each point in source is from the nearest point in target, and target_distances measures how far each point in target is from the nearest point in source. These are directional distance measures, which together give a sense of mismatch in both directions.

After that, the function concatenates both vectors into all_distances. This produces a combined set of distances that is used for overall statistics such as median distance, p95 distance, maximum distance, and overall coverage. It also computes the centroids of the two clouds using centroid_of and then calculates centroid_distance as the Euclidean distance between them. A centroid is just the average x and y position of all points in a cloud, so centroid_distance tells how far apart the overall centers are. This metric is useful because two clouds may have similar shapes but differ by a global offset; this value captures that translation.

The next step counts how many entries in all_distances are below or equal to coverage_threshold. This gives a coverage ratio, expressed as covered / all_distances.size(). In practice, it measures the fraction of all distances that are considered “good enough” or “covered.” The function then returns a ProfileMetrics struct containing all the metrics in a single object: the two centroids, centroid distance, RMSE in each direction, symmetric RMSE over all distances, median distance, 95th percentile distance, maximum distance, and coverage. This makes the function a compact summary of alignment quality between two point profiles. A key point is that it uses both directional and symmetric metrics because a profile can be close in one direction but not the other, and the combined metrics help diagnose which side is misaligned.



- **Renderizado de cuadros (Visor):** Breve síntesis del pipeline de generación visual de fotogramas (`.ppm`).



This function creates a short GStreamer animation showing the motion of a point cloud over time. It takes the target cloud, the source cloud, and a list of intermediate frames, then renders each frame into a 960x720 RGB image and pushes it into a live video pipeline. The first step sets the video dimensions and frame rate: width = 960, height = 720, and fps = 5. These values match the caps string used in the pipeline, which is the metadata that tells GStreamer what kind of video it is receiving.

The function initializes GStreamer with gst_init(nullptr, nullptr), then builds a pipeline string:
- appsrc is used to feed raw frames into the pipeline
- caps=video/x-raw,format=RGB,width=960,height=720,framerate=5/1 describes the incoming frame format
- videoconvert converts it to a format accepted by the sink
- autovideosink displays it on screen
- sync=false prevents the display from waiting unnecessarily, which is useful for previewing generated frames

If gst_parse_launch fails, it throws a runtime error with the GStreamer message so the caller knows why the pipeline could not be created. It then looks up the appsrc element by name inside the pipeline, and if that fails it cleans up the pipeline and throws another error. This is a standard defensive pattern in GStreamer: create the pipeline, verify the needed element exists, then start playback.

Once the pipeline is ready, the function loops over frames. For each frame index i, it calls render_motion_frame(target, source, frames, i, width, height). That helper renders the current composite view: source and target clouds are shown, previous frames are faintly drawn, and the current frame is highlighted. The returned RGB byte vector is then wrapped in a GstBuffer. The code maps the buffer for writing, copies the pixel data into it, and unmaps it. The timestamp and duration are set so GStreamer knows when each frame should be shown: GST_BUFFER_PTS(buffer) = i * GST_SECOND / fps, and GST_BUFFER_DURATION(buffer) = GST_SECOND / fps. The buffer is then pushed into the appsrc with gst_app_src_push_buffer, which feeds it into the pipeline. A short sleep is used between frames to pace the playback at roughly 5 FPS.

After the loop completes, the function sends an end-of-stream signal to the source, waits a little longer, then sets the pipeline to GST_STATE_NULL to stop it cleanly. Finally, it unrefs appsrc and the pipeline to release resources. In short, this function is a lightweight video preview pipeline: it renders a generated motion sequence into RGB images and streams them to the local display as an animation.


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

Mediante los frames se puede visualizar la evolución de las dos nubes de puntos y como una se manipula hasta que se encuentre lo mejor alineada posible.

![Animacion](point-cloud-collimation/reconstruction/animation.gif)