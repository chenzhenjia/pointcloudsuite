# Architecture overview

PointCloudSuite is organized as a multi-application CMake workspace.

```text
apps -> shared libraries in src -> Qt and platform services
tests -> shared libraries and selected compatibility sources
tools -> shared libraries and diagnostic entry points
```

## Current shared modules

- `pcv_core`: common point-cloud value types.
- `pcv_infrastructure`: operating-system paths for cache, logs, and exports.
- `pcv_io`: shared ASCII/binary PLY reading, cancellation, progress, and validated cache files.
- `pcv_filtering`: shared proportional and voxel sampling with explicit real-point and centroid policies.
- `pcv_output`: validated plane PNG/JSON/binary little-endian PLY output contract.

The existing processor source files remain application boundaries while
algorithms are extracted incrementally. New reusable code belongs in `src/`
with its project headers in `include/pcv/`, not directly in an application
directory.

## Dependency rules

- Shared algorithm code must not depend on Qt Widgets.
- Applications may depend on shared modules; shared modules must not depend on applications.
- Runtime-generated files must not be written into the source tree.
- Tests are registered only from the root `tests/` directory.

`pointcloudview` and `pointcloudstitch` may depend on all five shared modules. The
shared modules do not depend on either application; `pcv_output` additionally uses
`Qt::Gui` for `QImage` and matrix types. The current Debug CTest targets are
`ply_reader_tests`, `cloud_cache_tests`, `downsample_tests`,
`statistical_filter_tests`, `plane_output_tests`, and
`handeye_transform_tests`.
