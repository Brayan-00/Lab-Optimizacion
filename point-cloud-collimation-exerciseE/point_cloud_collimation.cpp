#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#define POINTS_PER_CLOUD 100000
#define VARIATION 0.001

static constexpr double kCanvasWidth = 10000.0;
static constexpr double kCanvasHeight = 10000.0;
static constexpr double kCanvasCenterX = kCanvasWidth * 0.5;
static constexpr double kCanvasCenterY = kCanvasHeight * 0.5;

// Punto 2D en el lienzo (unidades del canvas, no pixeles).
struct Point {
  double x;
  double y;
};

// Transformación rígida 2D: rotación theta (rad) + traslación (tx, ty).
struct Transform2D {
  double theta;
  double tx;
  double ty;
};

// Par de puntos (fuente, objetivo) emparejados por vecino más cercano, con su distancia al cuadrado.
struct Match {
  Point source;
  Point target;
  double distance2;
};

// Resultado de comparar dos perfiles: centroides, RMSE en ambas direcciones y percentiles.
struct ProfileMetrics {
  Point target_centroid;
  Point source_centroid;
  double centroid_distance;
  double source_to_target_rmse;
  double target_to_source_rmse;
  double symmetric_chamfer_rmse;
  double median_distance;
  double p95_distance;
  double max_distance;
  double coverage;
};

// Snapshot de una iteración del ICP: transformación acumulada, métricas y criterios de convergencia.
struct IterationMetrics {
  int iteration;
  std::size_t matches;
  Transform2D transform;
  ProfileMetrics profile;
  double match_rmse;
  double profile_score;
  double score_variation;
  double transform_step;
};

// Resultado final del ICP: transformación recuperada, nube alineada e historial completo.
struct IcpResult {
  Transform2D source_to_target;
  std::vector<Point> aligned;
  std::vector<std::vector<Point>> snapshots;
  std::vector<Transform2D> transforms;
  std::vector<IterationMetrics> metrics_history;
  int iterations;
  double score;
};

static constexpr double kPi = 3.14159265358979323846;

// Envuelve un ángulo en radianes al rango (-pi, pi].
static double normalize_angle(double theta) {
  while (theta > kPi) {
    theta -= 2.0 * kPi;
  }
  while (theta < -kPi) {
    theta += 2.0 * kPi;
  }
  return theta;
}

// Aplica una rotación + traslación a un solo punto.
static Point apply_transform(const Point &p, const Transform2D &t) {
  const double c = std::cos(t.theta);
  const double s = std::sin(t.theta);
  return {c * p.x - s * p.y + t.tx, s * p.x + c * p.y + t.ty};
}

// Construye una transformación que rota alrededor del centro del lienzo y luego traslada (tx, ty).
static Transform2D transform_about_canvas_center(double theta, double tx,
                                                 double ty) {
  const double c = std::cos(theta);
  const double s = std::sin(theta);
  return {theta, kCanvasCenterX + tx - (c * kCanvasCenterX - s * kCanvasCenterY),
          kCanvasCenterY + ty - (s * kCanvasCenterX + c * kCanvasCenterY)};
}

// Calcula la transformación inversa de una rotación + traslación dada.
static Transform2D inverse_transform(const Transform2D &t) {
  const double c = std::cos(-t.theta);
  const double s = std::sin(-t.theta);
  return {-t.theta, -(c * t.tx - s * t.ty), -(s * t.tx + c * t.ty)};
}

// Aplica la misma transformación a cada punto de una nube completa.
static std::vector<Point> apply_transform(const std::vector<Point> &cloud,
                                          const Transform2D &t) {
  std::vector<Point> out;
  out.reserve(cloud.size());
  for (const Point &p : cloud) {
    out.push_back(apply_transform(p, t));
  }
  return out;
}

// [Ejercicio A: deformación aleatoria del perfil fuente]
// Deforma la nube de forma no rígida: suma ondas suaves globales, "protuberancias"
// localizadas (gaussianas 2D con dirección aleatoria) y ruido, para que la
// transformación recuperada por el ICP nunca calce perfectamente con la ideal.
static double add_random_deformation(std::vector<Point> &cloud, double amplitude,
                                     unsigned int seed) {
  if (amplitude <= 0.0) {
    return 0.0;
  }

  struct DeformationBump {
    Point center;
    Point direction;
    double sigma;
  };

  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> center_x(1500.0, 8500.0);
  std::uniform_real_distribution<double> center_y(1200.0, 8800.0);
  std::uniform_real_distribution<double> direction(-1.0, 1.0);
  std::uniform_real_distribution<double> sigma(700.0, 1800.0);
  std::normal_distribution<double> local_noise(0.0, amplitude * 0.10);

  std::vector<DeformationBump> bumps;
  for (int i = 0; i < 8; ++i) {
    Point d{direction(rng), direction(rng)};
    const double norm = std::hypot(d.x, d.y);
    if (norm > 1.0e-12) {
      d.x /= norm;
      d.y /= norm;
    }
    bumps.push_back({{center_x(rng), center_y(rng)}, d, sigma(rng)});
  }

  double displacement2_sum = 0.0;
  for (Point &p : cloud) {
    const double wave_x =
        amplitude * 0.35 * std::sin((2.0 * kPi * p.y / kCanvasHeight) * 2.7 +
                                    0.4);
    const double wave_y =
        amplitude * 0.25 * std::sin((2.0 * kPi * p.x / kCanvasWidth) * 3.1 -
                                    0.9);
    double dx = wave_x + local_noise(rng);
    double dy = wave_y + local_noise(rng);

    for (const DeformationBump &bump : bumps) {
      const double ex = p.x - bump.center.x;
      const double ey = p.y - bump.center.y;
      const double influence =
          std::exp(-(ex * ex + ey * ey) / (2.0 * bump.sigma * bump.sigma));
      dx += amplitude * influence * bump.direction.x;
      dy += amplitude * influence * bump.direction.y;
    }

    p.x = std::clamp(p.x + dx, 0.0, kCanvasWidth);
    p.y = std::clamp(p.y + dy, 0.0, kCanvasHeight);
    displacement2_sum += dx * dx + dy * dy;
  }

  return std::sqrt(displacement2_sum / static_cast<double>(cloud.size()));
}

// Combina dos transformaciones rígidas: aplica "delta" después de "current" (delta ∘ current).
static Transform2D compose(const Transform2D &delta,
                           const Transform2D &current) {
  const double c = std::cos(delta.theta);
  const double s = std::sin(delta.theta);
  return {normalize_angle(delta.theta + current.theta),
          c * current.tx - s * current.ty + delta.tx,
          s * current.tx + c * current.ty + delta.ty};
}

// [Ejercicio A: generación del perfil H o riel]
// Genera una nube de puntos sintética con forma de riel en H: dos rieles paralelos
// (35% + 35% de los puntos) y durmientes transversales (30% restante), con ruido
// gaussiano en cada eje y orden aleatorio (shuffle) para no dejar rastro del patrón.
static std::vector<Point> generate_h_rail_cloud(std::size_t n,
                                                unsigned int seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> rail_y(1200.0, 8800.0);
  std::uniform_real_distribution<double> tie_x(3200.0, 6800.0);
  std::uniform_int_distribution<int> tie_index(0, 10);
  std::normal_distribution<double> noise(0.0, 12.0);
  std::normal_distribution<double> rail_width(0.0, 24.0);
  std::normal_distribution<double> tie_width(0.0, 18.0);

  std::vector<Point> cloud;
  cloud.reserve(n);

  for (std::size_t i = 0; i < n; ++i) {
    const double selector = static_cast<double>(i % 10) / 10.0;

    if (selector < 0.35) {
      cloud.push_back({3600.0 + rail_width(rng),
                       std::clamp(rail_y(rng) + noise(rng), 0.0,
                                  kCanvasHeight)});
    } else if (selector < 0.70) {
      cloud.push_back({6400.0 + rail_width(rng),
                       std::clamp(rail_y(rng) + noise(rng), 0.0,
                                  kCanvasHeight)});
    } else {
      const double y = 1600.0 + static_cast<double>(tie_index(rng)) * 680.0;
      cloud.push_back(
          {std::clamp(tie_x(rng) + noise(rng), 0.0, kCanvasWidth),
           std::clamp(y + tie_width(rng), 0.0, kCanvasHeight)});
    }
  }

  std::shuffle(cloud.begin(), cloud.end(), rng);
  return cloud;
}

// [Ejercicio A: construcción de la estructura GridIndex + búsqueda de vecinos más cercanos]
// Índice espacial tipo "hash grid": divide el plano en celdas cuadradas de
// cell_size y guarda en un unordered_map, por celda, los índices de los puntos
// que caen ahí. Permite buscar el vecino más cercano sin comparar contra todos
// los puntos de la nube.
class GridIndex {
 public:
  // Construcción del índice: asigna cada punto a su celda (bucket) según su posición.
  GridIndex(const std::vector<Point> &points, double cell_size)
      : points_(points), cell_size_(cell_size) {
    for (std::size_t i = 0; i < points_.size(); ++i) {
      const auto cell = cell_of(points_[i]);
      cells_[key(cell.first, cell.second)].push_back(static_cast<int>(i));
    }
  }

  // Búsqueda de vecino más cercano: expande anillos concéntricos de celdas
  // alrededor de la celda de la consulta (radius = 0, 1, 2, ...) hasta encontrar
  // un candidato y confirmar que ningún anillo posterior podría dar uno más cercano.
  bool nearest(const Point &query, Point &nearest_point,
               double &nearest_distance2) const {
    const auto base = cell_of(query);
    nearest_distance2 = std::numeric_limits<double>::max();
    bool found = false;

    for (int radius = 0; radius <= 16; ++radius) {
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != radius) {
            continue;
          }

          const auto it = cells_.find(key(base.first + dx, base.second + dy));
          if (it == cells_.end()) {
            continue;
          }

          for (int index : it->second) {
            const Point &candidate = points_[static_cast<std::size_t>(index)];
            const double ex = query.x - candidate.x;
            const double ey = query.y - candidate.y;
            const double d2 = ex * ex + ey * ey;
            if (d2 < nearest_distance2) {
              nearest_distance2 = d2;
              nearest_point = candidate;
              found = true;
            }
          }
        }
      }

      if (found && nearest_distance2 < cell_size_ * cell_size_ * radius * radius) {
        break;
      }
    }

    return found;
  }

 private:
  // Convierte una posición continua en coordenadas discretas de celda.
  std::pair<int, int> cell_of(const Point &p) const {
    return {static_cast<int>(std::floor(p.x / cell_size_)),
            static_cast<int>(std::floor(p.y / cell_size_))};
  }

  // Empaqueta las coordenadas (x, y) de celda en una sola llave de 64 bits para el hash map.
  static std::int64_t key(int x, int y) {
    return (static_cast<std::int64_t>(x) << 32) ^
           static_cast<std::uint32_t>(y);
  }

  const std::vector<Point> &points_;
  double cell_size_;
  std::unordered_map<std::int64_t, std::vector<int>> cells_;
};

// [Ejercicio A: estimación de la transformación rígida]
// Dado un conjunto de pares (fuente, objetivo) ya emparejados, calcula en forma
// cerrada la rotación y traslación óptimas (mínimos cuadrados) que alinean los
// puntos fuente con los objetivo: centra ambos conjuntos en su centroide, obtiene
// el ángulo vía atan2(cross, dot) de las coordenadas centradas, y despeja la
// traslación para que los centroides coincidan tras rotar.
static Transform2D estimate_rigid_transform(const std::vector<Match> &matches) {
  if (matches.empty()) {
    throw std::runtime_error("No matches available for transform estimation");
  }

  Point cs{0.0, 0.0};
  Point ct{0.0, 0.0};
  for (const Match &m : matches) {
    cs.x += m.source.x;
    cs.y += m.source.y;
    ct.x += m.target.x;
    ct.y += m.target.y;
  }

  const double inv_n = 1.0 / static_cast<double>(matches.size());
  cs.x *= inv_n;
  cs.y *= inv_n;
  ct.x *= inv_n;
  ct.y *= inv_n;

  double dot = 0.0;
  double cross = 0.0;
  for (const Match &m : matches) {
    const double sx = m.source.x - cs.x;
    const double sy = m.source.y - cs.y;
    const double tx = m.target.x - ct.x;
    const double ty = m.target.y - ct.y;
    dot += sx * tx + sy * ty;
    cross += sx * ty - sy * tx;
  }

  const double theta = std::atan2(cross, dot);
  const double c = std::cos(theta);
  const double s = std::sin(theta);

  return {theta, ct.x - (c * cs.x - s * cs.y),
          ct.y - (s * cs.x + c * cs.y)};
}

// Centroide (promedio simple) de una nube de puntos.
static Point centroid_of(const std::vector<Point> &cloud) {
  Point c{0.0, 0.0};
  for (const Point &p : cloud) {
    c.x += p.x;
    c.y += p.y;
  }
  const double inv_n = 1.0 / static_cast<double>(cloud.size());
  return {c.x * inv_n, c.y * inv_n};
}

// Ángulo del eje principal de la nube (tipo PCA en 2D) a partir de su matriz de covarianza.
// Solo la usa initial_pca_alignment, que está deshabilitada en collimate_icp.
static double principal_axis_angle(const std::vector<Point> &cloud) {
  const Point c = centroid_of(cloud);
  double xx = 0.0;
  double xy = 0.0;
  double yy = 0.0;

  for (const Point &p : cloud) {
    const double dx = p.x - c.x;
    const double dy = p.y - c.y;
    xx += dx * dx;
    xy += dx * dy;
    yy += dy * dy;
  }

  return 0.5 * std::atan2(2.0 * xy, xx - yy);
}

// Error cuadrático medio de vecino más cercano entre una nube y un índice (solo usado por initial_pca_alignment).
static double nearest_neighbor_mse(const GridIndex &index,
                                   const std::vector<Point> &cloud) {
  double sum = 0.0;
  std::size_t count = 0;
  for (const Point &p : cloud) {
    Point nearest_point{0.0, 0.0};
    double d2 = 0.0;
    if (index.nearest(p, nearest_point, d2)) {
      sum += d2;
      ++count;
    }
  }

  if (count == 0) {
    return std::numeric_limits<double>::infinity();
  }

  return sum / static_cast<double>(count);
}

// [Ejercicio A: búsqueda de vecinos más cercanos]
// Para cada punto de "cloud", busca su vecino más cercano en "index" y devuelve
// la distancia (o missing_distance si no encontró ninguno). La usa compare_profiles
// para medir qué tan bien calzan dos perfiles en ambas direcciones.
static std::vector<double> nearest_neighbor_distances(
    const GridIndex &index, const std::vector<Point> &cloud,
    double missing_distance) {
  std::vector<double> distances;
  distances.reserve(cloud.size());

  for (const Point &p : cloud) {
    Point nearest_point{0.0, 0.0};
    double d2 = 0.0;
    if (index.nearest(p, nearest_point, d2)) {
      distances.push_back(std::sqrt(d2));
    } else {
      distances.push_back(missing_distance);
    }
  }

  return distances;
}

// Percentil q (0..1) de un vector de valores, usando nth_element (O(n) promedio, sin ordenar todo).
static double percentile(std::vector<double> values, double q) {
  if (values.empty()) {
    return std::numeric_limits<double>::infinity();
  }

  q = std::clamp(q, 0.0, 1.0);
  const std::size_t index = static_cast<std::size_t>(
      std::lround(q * static_cast<double>(values.size() - 1)));
  std::nth_element(values.begin(), values.begin() + index, values.end());
  return values[index];
}

// Raíz del error cuadrático medio (RMSE) de una lista de distancias.
static double rmse_from_distances(const std::vector<double> &distances) {
  if (distances.empty()) {
    return std::numeric_limits<double>::infinity();
  }

  double sum2 = 0.0;
  for (double d : distances) {
    sum2 += d * d;
  }
  return std::sqrt(sum2 / static_cast<double>(distances.size()));
}

// [Ejercicio A: comparación de perfiles mediante centroides y distancias]
// Compara dos perfiles completos (no solo el emparejamiento del ICP): construye
// un GridIndex para cada uno, busca vecino más cercano en ambas direcciones
// (fuente->objetivo y objetivo->fuente, "symmetric chamfer"), y calcula distancia
// de centroides, RMSE en cada dirección y combinado, percentiles, máximo y
// cobertura (fracción de puntos por debajo de coverage_threshold).
static ProfileMetrics compare_profiles(const std::vector<Point> &target,
                                       const std::vector<Point> &source,
                                       double coverage_threshold,
                                       double missing_distance) {
  GridIndex target_index(target, 90.0);
  GridIndex source_index(source, 90.0);

  std::vector<double> source_distances =
      nearest_neighbor_distances(target_index, source, missing_distance);
  std::vector<double> target_distances =
      nearest_neighbor_distances(source_index, target, missing_distance);

  std::vector<double> all_distances = source_distances;
  all_distances.insert(all_distances.end(), target_distances.begin(),
                       target_distances.end());

  const Point target_centroid = centroid_of(target);
  const Point source_centroid = centroid_of(source);
  const double centroid_dx = target_centroid.x - source_centroid.x;
  const double centroid_dy = target_centroid.y - source_centroid.y;
  const double centroid_distance = std::hypot(centroid_dx, centroid_dy);

  std::size_t covered = 0;
  for (double d : all_distances) {
    if (d <= coverage_threshold) {
      ++covered;
    }
  }

  return {target_centroid,
          source_centroid,
          centroid_distance,
          rmse_from_distances(source_distances),
          rmse_from_distances(target_distances),
          rmse_from_distances(all_distances),
          percentile(all_distances, 0.50),
          percentile(all_distances, 0.95),
          *std::max_element(all_distances.begin(), all_distances.end()),
          static_cast<double>(covered) / static_cast<double>(all_distances.size())};
}

// Combina el RMSE simétrico y la distancia de centroides en un único puntaje escalar (normalizado por la diagonal del canvas).
static double profile_score(const ProfileMetrics &metrics) {
  const double canvas_diag = std::hypot(kCanvasWidth, kCanvasHeight);
  return (metrics.symmetric_chamfer_rmse + 0.25 * metrics.centroid_distance) /
         canvas_diag;
}

// Imprime en stdout una línea legible con todas las métricas de ProfileMetrics.
static void print_profile_metrics(const std::string &label,
                                  const ProfileMetrics &metrics) {
  std::cout << label << ": centroid_target=(" << metrics.target_centroid.x
            << ", " << metrics.target_centroid.y << ")"
            << " centroid_source=(" << metrics.source_centroid.x << ", "
            << metrics.source_centroid.y << ")"
            << " centroid_distance=" << metrics.centroid_distance
            << " rmse_s2t=" << metrics.source_to_target_rmse
            << " rmse_t2s=" << metrics.target_to_source_rmse
            << " chamfer_rmse=" << metrics.symmetric_chamfer_rmse
            << " median=" << metrics.median_distance
            << " p95=" << metrics.p95_distance
            << " max=" << metrics.max_distance
            << " coverage=" << metrics.coverage * 100.0 << "%\n";
}

// Alineación inicial alternativa vía PCA (compara el ángulo del eje principal de
// ambas nubes, probando 0 y 180 grados de ambigüedad). No se usa: está deshabilitada
// en collimate_icp (el ICP arranca siempre desde la transformación identidad).
[[maybe_unused]] static Transform2D
initial_pca_alignment(const std::vector<Point> &target,
                      const std::vector<Point> &source,
                      const GridIndex &index) {
  const Point ct = centroid_of(target);
  const Point cs = centroid_of(source);
  const double target_angle = principal_axis_angle(target);
  const double source_angle = principal_axis_angle(source);

  Transform2D best{0.0, 0.0, 0.0};
  double best_mse = std::numeric_limits<double>::infinity();

  for (double extra_angle : {0.0, kPi}) {
    Transform2D candidate{
        normalize_angle(target_angle - source_angle + extra_angle), 0.0, 0.0};
    const double c = std::cos(candidate.theta);
    const double s = std::sin(candidate.theta);
    candidate.tx = ct.x - (c * cs.x - s * cs.y);
    candidate.ty = ct.y - (s * cs.x + c * cs.y);

    const std::vector<Point> aligned = apply_transform(source, candidate);
    const double mse = nearest_neighbor_mse(index, aligned);
    if (mse < best_mse) {
      best_mse = mse;
      best = candidate;
    }
  }

  return best;
}

// Bucle principal del algoritmo ICP (Iterative Closest Point): en cada iteración
// busca vecino más cercano por punto [Ejercicio A: búsqueda de vecinos más cercanos],
// estima la transformación rígida que mejor los alinea [estimación de la
// transformación rígida], la acumula, y evalúa la calidad global con
// compare_profiles [comparación de perfiles]. Termina al converger (variación de
// score y del paso de transformación por debajo de VARIATION) o tras 80 iteraciones.
static IcpResult collimate_icp(const std::vector<Point> &target,
                               const std::vector<Point> &source,
                               bool save_snapshots) {
  const double match_threshold = 420.0;
  const double convergence_threshold = VARIATION;
  const double canvas_diag = std::hypot(kCanvasWidth, kCanvasHeight);
  const double missing_distance = canvas_diag;
  GridIndex index(target, 90.0);
  // Initial Transformation: = initial_pca_alignment(target, source, index);
  // Disabled
  Transform2D total{};
  std::vector<Point> current = apply_transform(source, total);
  std::vector<std::vector<Point>> snapshots;
  std::vector<Transform2D> transforms;
  std::vector<IterationMetrics> metrics_history;

  ProfileMetrics initial_metrics =
      compare_profiles(target, current, match_threshold, missing_distance);
  double previous_score = profile_score(initial_metrics);
  double score = previous_score;
  int iterations = 0;

  if (save_snapshots) {
    snapshots.push_back(source);
    snapshots.push_back(current);
    transforms.push_back({0.0, 0.0, 0.0});
    transforms.push_back(total);
  }

  std::cout << "initial_transform theta_deg="
            << normalize_angle(total.theta) * 180.0 / kPi
            << " t=(" << total.tx << ", " << total.ty << ")"
            << " mse=" << nearest_neighbor_mse(index, current) << "\n";
  print_profile_metrics("initial_profile_metrics", initial_metrics);
  metrics_history.push_back(
      {0, 0, total, initial_metrics, 0.0, score, 0.0, 0.0});

  for (int iter = 1; iter <= 80; ++iter) {
    std::vector<Match> matches;
    matches.reserve(current.size());
    double distance_sum = 0.0;

    for (const Point &p : current) {
      Point nearest_point{0.0, 0.0};
      double d2 = 0.0;
      if (index.nearest(p, nearest_point, d2) &&
          d2 < match_threshold * match_threshold) {
        matches.push_back({p, nearest_point, d2});
        distance_sum += d2;
      }
    }

    if (matches.size() < current.size() / 2) {
      throw std::runtime_error("Too few nearest-neighbor matches");
    }

    const Transform2D delta = estimate_rigid_transform(matches);
    total = compose(delta, total);
    current = apply_transform(current, delta);
    const double match_rmse =
        std::sqrt(distance_sum / static_cast<double>(matches.size()));
    const ProfileMetrics metrics =
        compare_profiles(target, current, match_threshold, missing_distance);
    score = profile_score(metrics);

    const double score_variation =
        std::abs(previous_score - score) / std::max(previous_score, 1.0e-12);
    const double transform_step =
        (std::hypot(delta.tx, delta.ty) +
         std::abs(delta.theta) * canvas_diag * 0.5) /
        canvas_diag;

    std::cout << "iter=" << std::setw(2) << iter
              << " matches=" << std::setw(5) << matches.size()
              << " match_rmse=" << std::fixed << std::setprecision(8)
              << match_rmse
              << " profile_score=" << score
              << " score_variation=" << score_variation * 100.0
              << "% transform_step=" << transform_step * 100.0
              << "% delta_theta_deg=" << delta.theta * 180.0 / kPi
              << " delta_t=(" << delta.tx << ", " << delta.ty << ")\n";
    print_profile_metrics("  profile_metrics", metrics);
    metrics_history.push_back({iter, matches.size(), total, metrics, match_rmse,
                               score, score_variation, transform_step});

    iterations = iter;
    if (iter >= 3 && score_variation < convergence_threshold &&
        transform_step < convergence_threshold) {
      break;
    }

    previous_score = score;
    if (save_snapshots && (iter % 2 == 0 || iter < 8)) {
      snapshots.push_back(current);
      transforms.push_back(total);
    }
  }

  if (save_snapshots) {
    snapshots.push_back(current);
    transforms.push_back(total);
  }

  return {total, current, snapshots, transforms, metrics_history, iterations,
          score};
}

// Pinta un solo pixel RGB, con origen (0,0) abajo-izquierda como en coordenadas del canvas.
static void plot_point(std::vector<unsigned char> &rgb, int width, int height,
                       int x, int y, unsigned char r, unsigned char g,
                       unsigned char b) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }

  const std::size_t offset =
      static_cast<std::size_t>((height - 1 - y) * width + x) * 3;
  rgb[offset + 0] = r;
  rgb[offset + 1] = g;
  rgb[offset + 2] = b;
}

// [Ejercicio A: renderizado de los cuadros para el visor]
// Proyecta una nube de puntos completa (coordenadas del canvas) a pixeles del frame y los pinta de un color dado.
static void draw_cloud(std::vector<unsigned char> &rgb,
                       const std::vector<Point> &cloud, int width, int height,
                       unsigned char r, unsigned char g, unsigned char b) {
  for (const Point &p : cloud) {
    const int x = static_cast<int>(
        std::lround((p.x / kCanvasWidth) * static_cast<double>(width - 1)));
    const int y = static_cast<int>(
        std::lround((p.y / kCanvasHeight) * static_cast<double>(height - 1)));
    plot_point(rgb, width, height, x, y, r, g, b);
  }
}

// Dibuja un rectángulo sólido relleno del color dado (usado para la barra de progreso y la leyenda del visor).
static void draw_rect(std::vector<unsigned char> &rgb, int width, int height,
                      int x0, int y0, int x1, int y1, unsigned char r,
                      unsigned char g, unsigned char b) {
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      plot_point(rgb, width, height, x, y, r, g, b);
    }
  }
}

// Dibuja sobre el frame la barra de progreso y la leyenda de colores (fuente/objetivo/alineado).
static void draw_viewer_overlay(std::vector<unsigned char> &rgb, int width,
                                int height, std::size_t frame_index,
                                std::size_t frame_count) {
  const int x0 = 24;
  const int y0 = height - 46;
  const int bar_width = 240;
  const int bar_height = 12;
  const double progress =
      frame_count > 1
          ? static_cast<double>(frame_index) / static_cast<double>(frame_count - 1)
          : 1.0;
  const int filled = static_cast<int>(std::lround(progress * bar_width));

  draw_rect(rgb, width, height, x0, y0, x0 + bar_width, y0 + bar_height, 235,
            235, 235);
  draw_rect(rgb, width, height, x0, y0, x0 + filled, y0 + bar_height, 40, 170,
            80);

  draw_rect(rgb, width, height, 24, 24, 48, 38, 50, 90, 210);
  draw_rect(rgb, width, height, 24, 46, 48, 60, 230, 120, 90);
  draw_rect(rgb, width, height, 24, 68, 48, 82, 170, 205, 170);
  draw_rect(rgb, width, height, 24, 90, 48, 104, 40, 170, 80);
}

// Renderiza un cuadro completo del visor: objetivo en azul, fuente inicial en
// rojo, rastro de posiciones intermedias, y la posición actual (frame_index) en
// verde, más el overlay de progreso.
static std::vector<unsigned char> render_motion_frame(
    const std::vector<Point> &target, const std::vector<Point> &source,
    const std::vector<std::vector<Point>> &frames, std::size_t frame_index,
    int width, int height) {
  std::vector<unsigned char> rgb(static_cast<std::size_t>(width * height * 3),
                                 255);

  draw_cloud(rgb, source, width, height, 230, 120, 90);
  draw_cloud(rgb, target, width, height, 50, 90, 210);

  for (std::size_t i = 1; i < frame_index && i < frames.size(); ++i) {
    const unsigned char shade =
        static_cast<unsigned char>(205 - std::min<std::size_t>(i * 10, 55));
    draw_cloud(rgb, frames[i], width, height, 170, shade, 170);
  }

  draw_cloud(rgb, frames[frame_index], width, height, 40, 170, 80);
  draw_viewer_overlay(rgb, width, height, frame_index, frames.size());
  return rgb;
}

// Escribe una nube de puntos como CSV (label,index,x,y).
static void write_cloud_csv(const std::filesystem::path &path,
                            const std::vector<Point> &cloud,
                            const std::string &label) {
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Could not write " + path.string());
  }

  out << "label,index,x,y\n";
  out << std::fixed << std::setprecision(10);
  for (std::size_t i = 0; i < cloud.size(); ++i) {
    out << label << "," << i << "," << cloud[i].x << "," << cloud[i].y
        << "\n";
  }
}

// Escribe el historial de transformaciones acumuladas (una fila por frame) como CSV.
static void write_transform_csv(const std::filesystem::path &path,
                                const std::vector<Transform2D> &transforms) {
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Could not write " + path.string());
  }

  out << "frame,theta_rad,theta_deg,tx,ty\n";
  out << std::fixed << std::setprecision(10);
  for (std::size_t i = 0; i < transforms.size(); ++i) {
    out << i << "," << transforms[i].theta << ","
        << normalize_angle(transforms[i].theta) * 180.0 / kPi << ","
        << transforms[i].tx
        << "," << transforms[i].ty << "\n";
  }
}

// Escribe el historial completo de métricas por iteración (transformación + ProfileMetrics + criterios de convergencia) como CSV.
static void write_metrics_csv(
    const std::filesystem::path &path,
    const std::vector<IterationMetrics> &metrics_history) {
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Could not write " + path.string());
  }

  out << "iteration,matches,theta_deg,tx,ty,target_centroid_x,"
      << "target_centroid_y,source_centroid_x,source_centroid_y,"
      << "centroid_distance,rmse_source_to_target,rmse_target_to_source,"
      << "symmetric_chamfer_rmse,median_distance,p95_distance,max_distance,"
      << "coverage,match_rmse,profile_score,score_variation,transform_step\n";
  out << std::fixed << std::setprecision(10);

  for (const IterationMetrics &row : metrics_history) {
    out << row.iteration << "," << row.matches << ","
        << normalize_angle(row.transform.theta) * 180.0 / kPi << ","
        << row.transform.tx << "," << row.transform.ty << ","
        << row.profile.target_centroid.x << ","
        << row.profile.target_centroid.y << ","
        << row.profile.source_centroid.x << ","
        << row.profile.source_centroid.y << ","
        << row.profile.centroid_distance << ","
        << row.profile.source_to_target_rmse << ","
        << row.profile.target_to_source_rmse << ","
        << row.profile.symmetric_chamfer_rmse << ","
        << row.profile.median_distance << "," << row.profile.p95_distance
        << "," << row.profile.max_distance << "," << row.profile.coverage
        << "," << row.match_rmse << "," << row.profile_score << ","
        << row.score_variation << "," << row.transform_step << "\n";
  }
}

// Escribe un frame RGB como imagen PPM binaria (formato P6).
static void write_ppm(const std::filesystem::path &path,
                      const std::vector<unsigned char> &rgb, int width,
                      int height) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("Could not write " + path.string());
  }

  out << "P6\n" << width << " " << height << "\n255\n";
  out.write(reinterpret_cast<const char *>(rgb.data()),
            static_cast<std::streamsize>(rgb.size()));
}

// [Ejercicio A: exportación de CSV y cuadros PPM]
// Escribe a disco todos los CSV de la reconstrucción (perfiles, movimiento,
// métricas) y, por cada snapshot del ICP, su CSV y su frame PPM renderizado.
static void export_reconstruction(
    const std::filesystem::path &output_dir, const std::vector<Point> &target,
    const std::vector<Point> &source, const IcpResult &result) {
  const int width = 960;
  const int height = 720;

  std::filesystem::create_directories(output_dir);
  write_cloud_csv(output_dir / "target_profile.csv", target, "target");
  write_cloud_csv(output_dir / "source_initial_profile.csv", source,
                  "source_initial");
  write_cloud_csv(output_dir / "source_final_profile.csv", result.aligned,
                  "source_final");
  write_transform_csv(output_dir / "source_motion.csv", result.transforms);
  write_metrics_csv(output_dir / "profile_metrics.csv",
                    result.metrics_history);

  for (std::size_t i = 0; i < result.snapshots.size(); ++i) {
    std::ostringstream name;
    name << "frame_" << std::setw(3) << std::setfill('0') << i << ".ppm";
    write_cloud_csv(output_dir / ("source_frame_" + name.str().substr(6, 3) +
                                  ".csv"),
                    result.snapshots[i], "source_frame");
    write_ppm(output_dir / name.str(),
              render_motion_frame(target, source, result.snapshots, i, width,
                                  height),
              width, height);
  }

  std::cout << "Exported reconstruction to " << output_dir << "\n";
  std::cout << "  target_profile.csv: fixed reference profile\n";
  std::cout << "  source_initial_profile.csv: displaced/rotated source\n";
  std::cout << "  source_frame_*.csv and frame_*.ppm: source motion by step\n";
  std::cout << "  source_motion.csv: accumulated rotation and translation\n";
  std::cout << "  profile_metrics.csv: centroid and profile-distance metrics\n";
}

// [Ejercicio A: renderizado de los cuadros para el visor]
// Abre un pipeline de GStreamer (appsrc -> videoconvert -> autovideosink) y le
// empuja un frame RGB renderizado por cada snapshot del ICP, a 5 fps, para
// visualizar en vivo la colimación.
static void show_with_gstreamer(const std::vector<Point> &target,
                                const std::vector<Point> &source,
                                const std::vector<std::vector<Point>> &frames) {
  const int width = 960;
  const int height = 720;
  const int fps = 5;

  gst_init(nullptr, nullptr);
  GError *error = nullptr;
  GstElement *pipeline = gst_parse_launch(
      "appsrc name=src is-live=true format=time "
      "caps=video/x-raw,format=RGB,width=960,height=720,framerate=5/1 "
      "! videoconvert ! autovideosink sync=false",
      &error);

  if (pipeline == nullptr) {
    std::string message = error != nullptr ? error->message : "unknown error";
    if (error != nullptr) {
      g_error_free(error);
    }
    throw std::runtime_error("Could not create GStreamer pipeline: " + message);
  }

  GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
  if (appsrc == nullptr) {
    gst_object_unref(pipeline);
    throw std::runtime_error("Could not find appsrc element");
  }

  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  for (std::size_t i = 0; i < frames.size(); ++i) {
    std::vector<unsigned char> rgb =
        render_motion_frame(target, source, frames, i, width, height);
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, rgb.size(), nullptr);
    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    std::memcpy(map.data, rgb.data(), rgb.size());
    gst_buffer_unmap(buffer, &map);

    GST_BUFFER_PTS(buffer) =
        static_cast<GstClockTime>(i) * GST_SECOND / fps;
    GST_BUFFER_DURATION(buffer) = GST_SECOND / fps;

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    if (ret != GST_FLOW_OK) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(180));
  }

  gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
  std::this_thread::sleep_for(std::chrono::seconds(3));
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(appsrc);
  gst_object_unref(pipeline);
}

// Imprime en stdout una transformación (ángulo en grados + traslación).
static void print_transform(const std::string &label, const Transform2D &t) {
  std::cout << label << ": theta=" << std::fixed << std::setprecision(5)
            << normalize_angle(t.theta) * 180.0 / kPi << " deg, tx=" << t.tx
            << ", ty=" << t.ty << "\n";
}

// Punto de entrada: parsea flags de CLI, genera los dos perfiles H (objetivo y
// fuente deformada+ruidosa), corre el ICP para colimarlos, y opcionalmente
// exporta la reconstrucción y/o abre el visor de GStreamer.
int main(int argc, char **argv) {
  bool viewer = false;
  bool export_outputs = false;
  double deformation_amplitude = 60.0;
  std::filesystem::path output_dir = "reconstruction";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--viewer") {
      viewer = true;
    } else if (arg == "--export") {
      export_outputs = true;
    } else if (arg == "--deformation" && i + 1 < argc) {
      deformation_amplitude = std::stod(argv[++i]);
    } else if (arg == "--no-deformation") {
      deformation_amplitude = 0.0;
    } else if (arg == "--output" && i + 1 < argc) {
      output_dir = argv[++i];
      export_outputs = true;
    } else {
      std::cerr << "usage: " << argv[0]
                << " [--viewer] [--export] [--output directory]"
                << " [--deformation units] [--no-deformation]\n";
      return EXIT_FAILURE;
    }
  }

  try {
    const std::size_t points_per_cloud = POINTS_PER_CLOUD;
    const Transform2D target_to_source =
        transform_about_canvas_center(18.0 * kPi / 180.0, 620.0, -430.0);

    std::vector<Point> target = generate_h_rail_cloud(points_per_cloud, 7);
    std::vector<Point> source = apply_transform(target, target_to_source);
    const double deformation_rms =
        add_random_deformation(source, deformation_amplitude, 31);

    std::mt19937 rng(23);
    std::normal_distribution<double> sensor_noise(0.0, 10.0);
    for (Point &p : source) {
      p.x = std::clamp(p.x + sensor_noise(rng), 0.0, kCanvasWidth);
      p.y = std::clamp(p.y + sensor_noise(rng), 0.0, kCanvasHeight);
    }

    std::cout << "Generated two H-shaped rail point clouds with "
              << points_per_cloud << " points each.\n";
    std::cout << "Canvas: " << kCanvasWidth << " x " << kCanvasHeight
              << " coordinate units\n";
    std::cout << "Applied random non-rigid source deformation: amplitude="
              << deformation_amplitude << " units, rms=" << deformation_rms
              << " units\n";
    print_transform("Synthetic target->source transform", target_to_source);
    std::cout << "Collimating source cloud onto target cloud...\n";

    IcpResult result = collimate_icp(target, source, viewer || export_outputs);
    const Transform2D expected_source_to_target =
        inverse_transform(target_to_source);

    print_transform("Expected source->target transform",
                    expected_source_to_target);
    print_transform("Recovered source->target transform",
                    result.source_to_target);
    std::cout << "Finished after " << result.iterations
              << " iterations with profile_score=" << std::fixed
              << std::setprecision(8) << result.score << "\n";

    if (export_outputs) {
      export_reconstruction(output_dir, target, source, result);
    }

    if (viewer) {
      std::cout << "Opening GStreamer viewer: blue=target, red=initial source, "
                   "green=aligned source.\n";
      show_with_gstreamer(target, source, result.snapshots);
    }
  } catch (const std::exception &ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
