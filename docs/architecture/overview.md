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

The existing processor source files remain application boundaries while
algorithms are extracted incrementally. New reusable code belongs in `src/`
with its project headers in `include/pcv/`, not directly in an application
directory.

## Dependency rules

- Shared algorithm code must not depend on Qt Widgets.
- Applications may depend on shared modules; shared modules must not depend on applications.
- Runtime-generated files must not be written into the source tree.
- Tests are registered only from the root `tests/` directory.
