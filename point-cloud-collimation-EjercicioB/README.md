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
