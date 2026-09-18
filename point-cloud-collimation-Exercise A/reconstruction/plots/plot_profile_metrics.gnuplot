# ============================================================
# Profile Metrics Visualization
# Point Cloud Collimation - Profiling Laboratory
# ============================================================

set datafile separator ","

# General style
set terminal pngcairo enhanced font "Arial,11" size 1000,700
set grid
set key outside
set xlabel "Iteration"
set border linewidth 1

# ------------------------------------------------------------
# 1. Centroid distance
# ------------------------------------------------------------

set output "centroid_distance.png"

set title "Centroid Distance During Iterations"
set ylabel "Distance"

plot "../reconstruction/profile_metrics.csv" using 1:10 \
    with linespoints linewidth 2 pointtype 7 \
    title "Centroid distance"


# ------------------------------------------------------------
# 2. RMSE metrics
# ------------------------------------------------------------

set output "rmse_metrics.png"

set title "Profile Matching Errors"
set ylabel "RMSE"

plot "../reconstruction/profile_metrics.csv" using 1:11 \
    with linespoints linewidth 2 pointtype 7 \
    title "Source to target", \
     "" using 1:12 \
    with linespoints linewidth 2 pointtype 5 \
    title "Target to source", \
     "" using 1:13 \
    with linespoints linewidth 2 pointtype 9 \
    title "Symmetric Chamfer RMSE", \
     "" using 1:18 \
    with linespoints linewidth 2 pointtype 11 \
    title "Match RMSE"


# ------------------------------------------------------------
# 3. Transformation parameters
# ------------------------------------------------------------

set output "transformation.png"

set title "Estimated Transformation"
set ylabel "Value"

plot "../reconstruction/profile_metrics.csv" using 1:3 \
    with linespoints linewidth 2 pointtype 7 \
    title "Rotation (deg)", \
     "" using 1:4 \
    with linespoints linewidth 2 pointtype 5 \
    title "Translation X", \
     "" using 1:5 \
    with linespoints linewidth 2 pointtype 9 \
    title "Translation Y"


# ------------------------------------------------------------
# 4. Coverage and profile score
# ------------------------------------------------------------

set output "coverage_score.png"

set title "Profile Coverage and Score"
set ylabel "Value"

plot "../reconstruction/profile_metrics.csv" using 1:17 \
    with linespoints linewidth 2 pointtype 7 \
    title "Coverage", \
     "" using 1:19 \
    with linespoints linewidth 2 pointtype 5 \
    title "Profile score"


# ------------------------------------------------------------
# 5. Distance distribution
# ------------------------------------------------------------

set output "distance_distribution.png"

set title "Distance Distribution"
set ylabel "Distance"

plot "../reconstruction/profile_metrics.csv" using 1:14 \
    with linespoints linewidth 2 pointtype 7 \
    title "Median distance", \
     "" using 1:15 \
    with linespoints linewidth 2 pointtype 5 \
    title "P95 distance", \
     "" using 1:16 \
    with linespoints linewidth 2 pointtype 9 \
    title "Maximum distance"


# ------------------------------------------------------------
# 6. Number of matches
# ------------------------------------------------------------

set output "matches.png"

set title "Number of Matches"
set ylabel "Matches"

plot "../reconstruction/profile_metrics.csv" using 1:2 \
    with linespoints linewidth 2 pointtype 7 \
    title "Matches"


# ------------------------------------------------------------
# 7. Score variation and transformation step
# ------------------------------------------------------------

set output "convergence.png"

set title "Convergence Indicators"
set ylabel "Value"

plot "../reconstruction/profile_metrics.csv" using 1:20 \
    with linespoints linewidth 2 pointtype 7 \
    title "Score variation", \
     "" using 1:21 \
    with linespoints linewidth 2 pointtype 5 \
    title "Transformation step"


unset output