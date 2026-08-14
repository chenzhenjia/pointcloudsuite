# Registration Distortion Development Record

## Scope

The registration pipeline is Qt 6 and C++ only.  It does not use CUDA, PCL,
or VTK.  All post-load processing operates on the current display cache.

## Current transform

For a point `p` in depth-camera coordinates, the application currently uses:

```text
p_world(i) = T_base_flange(t_i) * T_flange_depth * p_depth(i)
```

`T_flange_depth` is read from `DepthInRobotPose` in the Eye-In-Hand XML.  The
calibration pose uses fixed-XYZ radians.  Robot Start/End poses use ZYX degrees.
The flange transform is interpolated by linear translation and quaternion SLERP.

## Confirmed risk

The current application does not receive timestamps, scan-line identifiers, or
robot trajectory samples.  It estimates `t_i` from one of:

- PLY vertex order;
- local X;
- local Y;
- local Z.

This is only valid when the selected source represents the actual acquisition
sequence.  A wrong source, a non-uniform robot speed, or a path that differs
from Start-to-End interpolation produces per-point geometric distortion.  A
rigid ICP correction cannot repair this kind of distortion.

## Implementation order

1. Provide an Eye-In-Hand-only comparison path by disabling ICP.
2. Make merge cache keys include every transform and processing setting.
3. Validate the Euler-derived hand-eye matrix against `RTmatDepth2robot` from
   the same XML file.
4. Report per-cloud world bounds and ICP quality metrics.
5. Reject unsafe ICP updates when overlap or correspondence quality is too low.

## Phase 1 approved implementation (robot-constrained 2.5D ICP)

The robot/world transform remains mandatory for the production registration
path.  ICP is only a bounded correction applied after
`T_base_flange(t) * T_flange_depth`.

The first 2.5D phase uses the existing spatial-hash ICP and adds an
anisotropic metric: XY residuals keep unit weight while Z residuals use a
configurable `zWeight` (default `0.25`).  Large depth residuals are further
downweighted with `edgeWeight` (default `0.20`) so flying pixels and height
discontinuities cannot dominate the rigid update.  This is a conservative
depth-reliability proxy; true row/column gradient weighting is deferred until
PLY organization can be proven.

Production ICP correction limits default to 10 mm translation and 2 degrees
rotation.  If a correction exceeds either limit, the robot-derived world
coordinates are retained and the diagnostic reports rejection.  Pure-visual
mode remains diagnostic-only and must not be used for robot localization.

The phase-1 acceptance record reports correspondence count, Fitness, RMSE,
correction translation/rotation, and the rejection reason.  Multi-scale ICP,
point-to-plane ICP, organized-grid recovery, and true depth-gradient masks are
separate later phases and must be documented before implementation.

## Phase 2 approved implementation (overlap and duplicate-point diagnostics)

Scans may intentionally contain spatially overlapping points.  Overlap is not
removed as noise and is not merged by inventing centroid points.  After each
robot-based ICP correction, the sampled moving points are matched again against
the accumulated world reference using the same correspondence distance and
2.5D metric.  The result records:

- overlap ratio: valid correspondences / sampled moving points;
- unique reference ratio: unique matched reference points / valid correspondences;
- duplicate correspondence ratio: `1 - unique reference ratio`.

Duplicate matches are expected when two scans cover the same surface or have
different sampling densities.  They are reported separately from Fitness so a
high overlap is not mistaken for a failure.  ICP acceptance requires a minimum
overlap (default `0.10`) and still obeys the robot correction limits.  A very
low unique-reference ratio is reported as correspondence collapse and is not
silently treated as independent geometric evidence.

## External data required for exact motion compensation

Exact compensation requires one of the following datasets for every PLY:

- a timestamp per point or scan line plus sampled robot flange poses;
- an encoder/trigger index per point plus a matching robot trajectory;
- documented proof that PLY vertex order or a local coordinate is the scanner
  acquisition order and that robot motion is uniform.

Until one of these is available, Start/End interpolation must be treated as an
approximation and verified with the ICP-disabled comparison result.
