
# Ejercicio D

## Regiones instrumentadas con std::chrono

Se crearon las siguientes regiones:

- `target_generation`: genera el perfil objetivo (riel en H).
- `source_transform_deform_noise`: transforma, deforma y agrega ruido al perfil fuente.
- `grid_index_construction`: construye el `GridIndex`.
- `nearest_neighbor_search`: búsqueda de vecinos más cercanos en cada iteración del ICP.
- `estimate_transform`: estima la transformación rígida.
- `profile_metrics`: calcula las métricas de comparación entre perfiles.
- `export_csv_ppm`: exporta los CSV y los cuadros PPM.
- `viewer_render_send`: renderiza y envía los cuadros al visor de GStreamer.

## Cómo correr la instrumentación

Se agregó un parámetro `--profile-repeats N` al `main` para poder correr el programa varias veces automáticamente y sacar el promedio de cada región.

Solo lo referente al Ejercicio D (sin exportar ni visor):

```bash
./point_cloud_collimation --profile-repeats 5
```

Corrida completa (instrumentación + exportación + visor):

```bash
./point_cloud_collimation --profile-repeats 5 --export --viewer
```

Este es el log que imprimió el programa y que se usó para armar la tabla de abajo:

```text
=== Resumen de instrumentacion (promedios) === con N=5
region                           samples        avg_ms        total_ms
estimate_transform                   225        0.5154        115.9657
export_csv_ppm                        30      127.9592       3838.7769
grid_index_construction              235        4.9888       1172.3616
nearest_neighbor_search              225      225.3077      50694.2247
profile_metrics                      230      451.7698     103907.0452
source_transform_deform_noise          5       23.8520        119.2599
target_generation                      5        7.9395         39.6975
viewer_render_send                    29       20.1771        585.1350

Instrumentation CSV written to "instrumentation.csv"
```

`samples` es el número de veces que el programa entró a esa región (por ejemplo, `nearest_neighbor_search` entra 225 veces porque son ~45 iteraciones del ICP × 5 repeticiones).

## Comparación por región: Ejercicio D vs. Ejercicio B

La siguiente tabla sirve para saber qué región de la instrumentación (Ejercicio D) corresponde a qué función del código (Ejercicio B), y compara cuánto duraba cada una en un ejercicio y en el otro.

**Aclaración:** las diferencias que se ven no son solo por variación entre corridas sino también que el Ejercicio B se corrió en la computadora de Fiorela y el Ejercicio D en la de Fabián. Por eso conviene fijarse en los **porcentajes** más que en los segundos absolutos: así se puede analizar si una región ocupa la misma proporción del tiempo total del programa en ambos casos, sin que el hardware distinto distorsione la comparación.

| Región (Ejercicio D) | D: % del tiempo | D: seg. por corrida | Función(es) en B | B: seg. (sin export / con export) | B: % |
|---|---:|---:|---|---:|---:|
| profile_metrics | 64.75% | 20.78 s | `compare_profiles` — coincide: construye grids + busca vecinos en ambas direcciones + calcula centroides/RMSE | 42.15 s / 40.46 s | 66.16% / 59.79% |
| nearest_neighbor_search | 31.59% | 10.14 s | `collimate_icp` menos `compare_profiles` (el resto del bucle: la búsqueda directa de vecinos + `estimate_rigid_transform` + bookkeeping) | 21.50 s / 20.17 s | 33.75% / 29.81% |
| export_csv_ppm | 2.39% | 3.84 s (1 corrida con `--export`) | `export_reconstruction` (incluye `write_cloud_csv` + escritura de PPM) | 6.78 s (solo con `--export`) | 10.02% |
| grid_index_construction | 0.73% | 0.234 s | `GridIndex::GridIndex` (constructor) | 0.42 s / ~0.44 s* | 0.66% / 0.42%* |
| viewer_render_send | 0.36% | 0.585 s (1 corrida con `--export`) | `render_motion_frame` | 0.50 s (solo con `--export`) | 0.74% |
| source_transform_deform_noise | 0.07% | 0.024 s | `add_random_deformation` + `apply_transform` | no aparece en pprof (muy chica); perf ve `apply_transform` ≈0.05s* | ~0.06%* |
| estimate_transform | 0.07% | 0.023 s | `estimate_rigid_transform` | no aparece (por debajo del umbral que pprof reporta) | — |
| target_generation | 0.02% | 0.008 s | `generate_h_rail_cloud` | no aparece (demasiado rápida para el muestreo) | — |

\* pprof no la capturó en la corrida con `--export` (cae bajo su umbral de "nodos insignificantes"), así que se usó el % de `perf report` multiplicado por el tiempo total de esa corrida (`perf stat`) como estimación.

## ¿La región con mayor tiempo coincide con el hotspot de perf, Google Performance Tools y Valgrind?

En base a la tabla anterior se puede ver cómo la región con mayor tiempo es `profile_metrics`, que traducida a función sería `compare_profiles`, pues ambas hacen lo mismo: construyen los grids, buscan vecinos en ambas direcciones y calculan centroides/RMSE.

Calculado en términos de porcentaje (pues el Ejercicio B se corrió en la computadora de Fiorela y el Ejercicio D en la de Fabián) se ve que coinciden en gran manera:

- Región `profile_metrics`: **64.75%**
- Función `compare_profiles`: **66.16%** (sin export) / **59.79%** (con export)

Esto es porque `profile_metrics` busca vecinos más cercanos dos veces (fuente→objetivo y objetivo→fuente), mientras que `nearest_neighbor_search` lo hace una sola vez por iteración del ICP. Como las dos regiones hacen básicamente el mismo trabajo de fondo, tiene sentido que `profile_metrics` tarde alrededor del doble: 451.77 ms vs. 225.31 ms de promedio, casi exactamente 2x.

## ¿Cuánto overhead introduce su instrumentación?

Para calcular el overhead que introduce la instrumentación se corrió el código original 5 veces y el código instrumentado otras 5 veces nuevas, para sacar un promedio igual en ambos casos y evitar variación por condiciones distintas (el primer set de resultados con el que se comparó se había calculado días antes). Esto se hizo con el siguiente comando:

```bash
cd ../point-cloud-collimation-original && \
make clean && make && \
perf stat -r 5 ./point_cloud_collimation > /dev/null 2> ../point-cloud-collimation-forExerciseD/results-EjercicioD/overhead/perf_stat_original.txt && \
cd ../point-cloud-collimation-forExerciseD && \
make clean && make && \
perf stat -r 5 ./point_cloud_collimation > /dev/null 2> results-EjercicioD/overhead/perf_stat_instrumentado.txt && \
echo "LISTO"
```

Se evidenció lo siguiente:

| | Promedio (5 corridas) | Desv. estándar |
|---|---:|---:|
| Original | 25.756 s | ± 0.220 s (±0.85%) |
| Instrumentado | 26.591 s | ± 0.361 s (±1.36%) |
| Diferencia | **+835 ms (+3.24%)** | |

Esto da como conclusión que la instrumentación sí introduce cierto overhead frente a no tenerlo; sin embargo, es un 3.24% del tiempo total, lo que lo hace no tan penalizado en cuanto a tiempo, y definitivamente vale la pena para poder tener un control más preciso de los tiempos de cada parte del software.

## ¿Qué partes del programa son más fáciles de entender con instrumentación manual que con muestreo?

La ventaja de la instrumentación manual es que uno decide qué medir y le pone una etiqueta con significado, mientras que el muestreo solo dice en qué función estaba el programa en ese instante, sin explicar el porqué. En este programa eso se nota en casi todas las regiones. La búsqueda de vecinos más cercanos (`nearest_neighbor_search`) y el cálculo de métricas de calidad (`profile_metrics`) usan por dentro la misma función (`GridIndex::nearest`), así que las herramientas de muestreo las mezclan en un solo número y no dejan saber cuánto le corresponde a cada una; con instrumentación manual, en cambio, queda claro que `profile_metrics` pesa el doble porque hace esa búsqueda en ambas direcciones. Algo parecido pasa con la exportación de CSV y PPM: en las herramientas de muestreo ese costo aparece repartido en varias funciones internas de la librería estándar (formateo de texto, escritura a disco) que nadie reconocería como "la exportación" a simple vista, mientras que en la instrumentación es una sola línea clara. Y las regiones más rápidas del programa, como generar el perfil objetivo o estimar la transformación, ni siquiera alcanzan a aparecer en los reportes de muestreo porque ocurren demasiado rápido para que el muestreo las capture, pero el cronómetro manual sí les pone un número exacto. Por eso la idea aplica en general a casi todas las regiones: el muestreo organiza la información según la función del binario, mientras que la instrumentación la organiza según el significado que esa parte del código tiene dentro del algoritmo.

## ¿Qué información no puede obtener con instrumentación manual?

La limitación de la instrumentación manual es que solo sabe cuánto tardó el bloque que uno decidió medir, sin explicar qué pasa dentro de ese bloque ni por qué es lento. En este programa eso también se nota en varias partes. La instrumentación dice que `nearest_neighbor_search` tarda cierto tiempo por iteración, pero no dice que buena parte de ese tiempo en realidad se va recorriendo la tabla hash interna de la cuadrícula (`std::_Hashtable::_M_find_before_node`) en vez de comparando coordenadas directamente — ese desglose solo lo mostraron las herramientas de muestreo del Ejercicio B. Tampoco dice si la lentitud es porque el procesador está calculando mucho o porque está esperando traer datos de memoria; eso solo lo mostró `perf stat` con los cache-misses y branch-misses medidos en el Ejercicio B, algo que `std::chrono` no puede ver. Y si hubiera una parte lenta del programa que no se le hubiera ocurrido medir a uno, la instrumentación jamás la mostraría, mientras que perf, pprof o Valgrind la habrían encontrado solas, sin necesitar adivinar dónde mirar primero. Por eso la instrumentación manual confirma cuánto tarda cada parte, pero para saber por qué tarda eso y a qué nivel —instrucciones, memoria, caché, o partes que ni se sospechaban— hacen falta las herramientas externas de los Ejercicios B y C.
