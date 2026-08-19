# Filtering module

`pcv_filtering` owns reusable point-count reduction algorithms.

## Voxel policies

- `FirstInputPoint` preserves an actual measured input point and reports its source index.
- `Centroid` creates the arithmetic mean used by registration and diagnostic sampling.

The policy must always be selected explicitly at product call sites. Inspection
and final measurement workflows use real input points; ICP pyramid construction
may use centroids.

## Statistical filtering

`statistical_filter` owns adaptive cell-size estimation, the spatial hash,
expanding neighbor shells, K-neighbor mean distances, and the global
mean-plus-standard-deviation threshold. It returns the actual cell size,
threshold, and measured point count for diagnostics.

The viewer's existing `removeNoise` function remains as a compatibility and
workflow adapter; the filtering algorithm itself no longer depends on the app.
External consumers use `pcv::filtering` with `std::vector` point clouds, while
the Qt application adapters remain temporary `pcv::detail::filtering` clients.
