# Point Cloud Collimation

## Ejercicio C

### Proceso de compilación y desensamblado

```bash

make clean
make CXXFLAGS="-std=c++17 -g -O0 -Wall -Wextra -pedantic -fno-omit-frame-pointer"
g++ -std=c++17 -O0 -g -S -masm=intel $(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0) point_cloud_collimation.cpp -o point_cloud_collimation.s
objdump -drwC -Mintel ./point_cloud_collimation > point_cloud_collimation.objdump

```

Durante el proceso de análisis del código desensamblado, las funciones estimate_rigid_transform y add_random_deformation no se encontraron explícitamente. Una razón presumible es que la bandera -O2 aplicó optimizaciones que resultaron en la sustitución, integración (inlining) o modificación de estas funciones. Por esta razón, la compilación elimina el uso de esta bandera y la cambia a -O0 para garantizar que no se realicen optimizaciones de este tipo.
GridIndex::nearest

Esta función comprendida entre `d79a` y `dab9` presenta las siguientes instrucciones o llamadas costosas:

    - call std::unordered_map::find

    - call std::vector::operator[]

    - subsd / mulsd / addsd

    - cvtsi2sd 

Existen 2 bucles en esta función; el principal realiza un acceso indirecto a la tabla hash.

Hay múltiples saltos condicionales, específicamente con las instrucciones jne, jbe y jle.

En general, esta función está limitada por memoria debido a los fallos de caché que ocurren por el acceso a la tabla hash y el acceso no continuo a los puntos.


### compare_profiles

Esta función comprendida entre `d620` y `d799` presenta las siguientes instrucciones o llamadas costosas:

    - call hypot@plt, la cual realiza el cálculo de la distancia euclidiana
    - divsd
    - call GridIndex::GridIndex / call GridIndex::nearest

Existe un bucle que evalúa puntos realizando un acceso indirecto a GridIndex.

Los bucles contienen saltos condicionales como jb y jnb.

En general, esta función está limitada por memoria debido a los accesos no continuos a través de GridIndex.

### estimate_rigid_transform

Esta función comprendida entre `d320` y `d61f` presenta las siguientes instrucciones o llamadas costosas:

    - call atan2@plt
    - call sqrt@plt
    - divsd

Existe un bucle que realiza un acceso continuo sobre un vector.

Los bucles contienen saltos condicionales como jne y jmp, utilizados para la funcionalidad básica del bucle.

En general, esta función está limitada por cómputo, ya que los accesos a memoria sobre el vector son eficientes, reduciendo los fallos de caché. El cuello de botella se debe a las funciones matemáticas utilizadas.

### add_random_deformation

Esta función comprendida entre `d0a0` y `d31f` presenta las siguientes instrucciones o llamadas costosas:

    - call exp@plt
    - call sin@plt / call cos@plt
    - mulsd / addsd

Existe un bucle que realiza un acceso continuo sobre un vector.

Los saltos condicionales únicamente se utilizan para avanzar a través de la lista de puntos.

En general, esta función está limitada por cómputo debido al cálculo de las expresiones matemáticas.


### render_motion_frame

Esta función comprendida entre `ce10` y `d09f` presenta las siguientes instrucciones o llamadas costosas:

    - call round@plt / cvttsd2si
    - Lectura y escritura en un búfer

Existe un bucle que realiza un acceso continuo sobre el búfer de píxeles.

Se realizan saltos condicionales para determinar si los puntos transformados se encuentran dentro de los límites.

En general, esta función está limitada por memoria debido a la intensa lectura y escritura sobre el búfer.


### Resultados de perf annotate

### Instrucciones que Concentran más Muestras

A partir de los datos desensamblados de `perf annotate` la función `GridIndex::nearest` demuestra el mayor consumo:

| Muestras | % del Total | Instrucciones Ensamblador |
| :---: | :---: | :--- |
| **319,318**<br>**316,266**<br>**316,102**<br>**315,787** | **18.43%**<br>**18.25%**<br>**18.24%**<br>**18.23%** | `movq -0x68(%rbp), %rax`<br>`addq $0x4, %rax`<br>`movq %rax, -0x68(%rbp)`<br>`nop` |
| **35,065**<br>**34,784**<br>**34,562**<br>**34,549** | **2.02%**<br>**2.01%**<br>**1.99%**<br>**1.99%**  | `movl -0x74(%rbp), %edx`<br>`movq %rdx, %rsi`<br>`movslq %edx, %rdx`<br>`movq %rax, %rdi` |
| **50,888**<br>**28,435**<br>**24,010** | **2.94%**<br>**1.64%**<br>**1.38%** |  `setne %al`<br>`testb %al, %al`<br>`cmpq %rax, %rdx` |
| **10,031** | **0.58%** | `callq std::unordered_map::end()` |


De estos datos se puede notar que la función `GridIndex::nearest` es la que tiene mayor cantidad de muestras. Demostrando que para mejorar el desempeño del programa se debe iniciar optimizando esta función. Es importante considerar que la compilación se realizó con la bandera `-O0`, lo que significa que el compilador no realizó optimizaciones, se tomó esta decisión para que se realizara un análisis representativo de las funciones bajo estudio.

Si se utiliza la bandera `-O2` el programa tendría un mejor desempeño al evitar usar el stack prefiriendo el uso de registros que son más rápidos de acceder.

---


### 2. Correspondencia con el Hotspot del Ejercicio B

El Ejercicio B señala que las funciones de mayor impacto en tiempo de ejecución son `compare_profiles` (o `profile_metrics`) y `nearest_neighbor_search`. Al analizar el código fuente, se confirma que existe una **cadena de llamadas** directa que explica este resultado:

1. **Punto de entrada:** `compare_profiles` invoca dos veces a `nearest_neighbor_distances` para comparar las nubes de puntos en ambas direcciones:

```cpp
   std::vector<double> source_distances = nearest_neighbor_distances(target_index, source, missing_distance);
   std::vector<double> target_distances = nearest_neighbor_distances(source_index, target, missing_distance);

```

2. **Causa raíz:** Dentro de `nearest_neighbor_distances`, un bucle `for` consulta repetidamente a `GridIndex::nearest` para cada punto de la nube:


``` cpp
for (const Point &p : cloud) {
    Point nearest_point{0.0, 0.0};
    double d2 = 0.0;
    if (index.nearest(p, nearest_point, d2)) {
      distances.push_back(std::sqrt(d2));
    } else {
      distances.push_back(missing_distance);
    }
  }
  ```

---

### 3. Propuesta de Optimización y Justificación

Reemplazar el GridIndex actual (basado en std::unordered_map) por una estructura que almacene los datos de manera continua en memoria, como un std::vector. Esto reduce los fallos de caché derivados del acceso disperso a la tabla hash.

Por otro lado, al haber realizado la compilación con la bandera `-O0`, las instrucciones con más muestras correspondieron al incremento de la variable de control del bucle directamente sobre la pila (stack):


``` assembly
movq -0x68(%rbp), %rax
addq $0x4, %rax
movq %rax, -0x68(%rbp)
nop
```

Este sobrecosto se eliminará al activar banderas de optimización como -O2, las cuales mantendrán los contadores en registros del procesador. Además, la contigüidad de un vector facilitará la auto-vectorización del cálculo de distancias por parte del compilador.