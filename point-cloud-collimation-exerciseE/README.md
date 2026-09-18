# Ejercicio E

Así se pueden correr ambos generando un txt con los resultados además del log:

```bash
cd /home/fabian/Documentos/Lab-Optimizacion/point-cloud-collimation-original
perf stat ./point_cloud_collimation > /dev/null 2> /home/fabian/Documentos/Lab-Optimizacion/point-cloud-collimation-exerciseE/logs/perf_stat_original.txt

cd /home/fabian/Documentos/Lab-Optimizacion/point-cloud-collimation-exerciseE
perf stat ./point_cloud_collimation_optimized > /dev/null 2> logs/perf_stat_optimizado.txt
```

## Hipótesis de lo que se cambiará

En el Ejercicio D se vio que casi todo el tiempo del programa se iba en dos partes que en el fondo hacen lo mismo: buscar el punto más cercano dentro de una cuadrícula (`GridIndex::nearest`). Una de ellas, `profile_metrics`, se llevaba sola casi dos tercios del tiempo total porque hace esa búsqueda para los 100,000 puntos de la nube, dos veces (fuente→objetivo y objetivo→fuente), en cada iteración del ICP.

La hipótesis es que si se reduce cuántas veces se llama a esa búsqueda, o qué tan cara es cada llamada, el tiempo total debería bajar en proporción, sin perder precisión en el resultado final (la posición estimada del riel). Se plantean dos cambios, cada uno con una razón técnica propia además de "es lo que más tarda":

1. **Menos búsquedas** en `profile_metrics`: esta función solo puntúa qué tan bien va la colimación en cada iteración, no calcula la transformación en sí, así que no necesita revisar la nube completa para dar un número confiable.
2. **Búsquedas más baratas** en el ICP: la búsqueda de vecino siempre exploraba hasta 16 anillos de celdas para garantizar el vecino global más cercano, pero el ICP descarta cualquier punto más lejano que `match_threshold`, así que explorar tantos anillos es trabajo de más.

## ¿Qué se optimizó y cómo cambió en el código?

### Optimización 1 — muestreo en `profile_metrics`

**Antes:**

```cpp
static ProfileMetrics compare_profiles(const std::vector<Point> &target,
                                       const std::vector<Point> &source,
                                       double coverage_threshold,
                                       double missing_distance) {
  ...
  std::vector<double> source_distances =
      nearest_neighbor_distances(target_index, source, missing_distance);
  std::vector<double> target_distances =
      nearest_neighbor_distances(source_index, target, missing_distance);
```

**Después:**

```cpp
static ProfileMetrics compare_profiles(const std::vector<Point> &target,
                                       const std::vector<Point> &source,
                                       double coverage_threshold,
                                       double missing_distance,
                                       std::size_t sample_stride) {
  ...
  const std::vector<Point> source_sample = sample_cloud(source, sample_stride);
  const std::vector<Point> target_sample = sample_cloud(target, sample_stride);

  std::vector<double> source_distances =
      nearest_neighbor_distances(target_index, source_sample, missing_distance);
  std::vector<double> target_distances =
      nearest_neighbor_distances(source_index, target_sample, missing_distance);
```

(+ nueva función `sample_cloud`, y las 2 llamadas a `compare_profiles` en `collimate_icp` ahora pasan `kProfileMetricsSampleStride = 5`.)

`sample_cloud` toma 1 de cada 5 puntos de la nube (la nube ya viene desordenada desde antes, así que agarrar cada quinto punto equivale a una muestra al azar). `compare_profiles` usa esa muestra, en vez de la nube completa, para calcular el RMSE, los percentiles y la cobertura. Los centroides se siguen calculando con todos los puntos porque son baratos (no necesitan buscar vecinos). El resultado es el mismo tipo de métrica, con 5 veces menos búsquedas.

### Optimización 2 — radio de búsqueda acotado en el ICP

**Antes:**

```cpp
bool nearest(const Point &query, Point &nearest_point,
             double &nearest_distance2) const {
  ...
  for (int radius = 0; radius <= 16; ++radius) {
```

```cpp
if (index.nearest(p, nearest_point, d2) &&
    d2 < match_threshold * match_threshold) {
```

**Después:**

```cpp
bool nearest(const Point &query, Point &nearest_point,
             double &nearest_distance2, int max_radius = 16) const {
  ...
  for (int radius = 0; radius <= max_radius; ++radius) {
```

```cpp
if (index.nearest(p, nearest_point, d2, kMatchSearchMaxRadius) &&
    d2 < match_threshold * match_threshold) {
```

(+ nueva constante `kMatchSearchMaxRadius = ceil(420/90)+1 = 6`.)

Se agregó un límite (`max_radius`) a cuántos anillos de celdas explora la búsqueda de vecino más cercano. En el ICP, como solo importan los puntos a menos de `match_threshold = 420` unidades y cada celda mide 90, con 6 anillos ya alcanza para cubrir esa distancia — cualquier punto más lejos se iba a descartar de todas formas. Antes se exploraban hasta 16 anillos siempre, sin importar el umbral. El resultado es que cada búsqueda de vecino en el ICP recorre menos celdas.

## Resultados

- `logs/perf_stat_original.txt` → 26.307 s
- `logs/perf_stat_optimizado.txt` → 11.321 s

Esto hace al código alrededor de 2.3x más rápido con solo los dos cambios planteados.
