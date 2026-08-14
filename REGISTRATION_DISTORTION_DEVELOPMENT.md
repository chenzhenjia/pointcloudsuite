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

## Phase 3 approved implementation (robot-constrained multi-scale ICP)

The production path remains robot-world based. Multi-scale processing changes
only how the bounded ICP correction is estimated: each moving cloud is reduced
with the existing real-point voxel representative method at coarse, medium,
and fine levels, while the same correction is applied to the full-resolution
world cloud. The next level starts from the previous level's correction.

Default levels are `2.0`, `0.8`, and `0.25` mm. A level is skipped when its
voxel size is not smaller than the current cloud extent or when there are too
few points. No virtual centroid points are introduced and source indices remain
those of the measured PLY points.

Each accepted level records weighted XY RMSE and Z RMSE in addition to total
RMSE, Fitness, overlap, duplicate correspondence ratio, and robot correction
limits. These diagnostics distinguish lateral misalignment from depth noise.
Final acceptance still requires the robot correction bounds and the phase-2
overlap/quality thresholds.

## External data required for exact motion compensation

Exact compensation requires one of the following datasets for every PLY:

- a timestamp per point or scan line plus sampled robot flange poses;
- an encoder/trigger index per point plus a matching robot trajectory;
- documented proof that PLY vertex order or a local coordinate is the scanner
  acquisition order and that robot motion is uniform.

Until one of these is available, Start/End interpolation must be treated as an
approximation and verified with the ICP-disabled comparison result.

For the supplied `Point_Cloud_A` files, local Y decreases from approximately
0 to -300 along the organized scan rows.  Axis progress therefore follows the
first-to-last valid sample direction; if the axis decreases, progress is
reversed so the first sample maps to Start and the last to End.  The merge
cache version is bumped whenever this mapping changes.

## Phase 4 approved implementation (transform validation)

Before loading or using a merge cache, every Start, End, and enabled hand-eye
matrix is checked for finite values, a homogeneous bottom row of `[0 0 0 1]`,
orthonormal rotation columns, and a positive determinant near one. Invalid
matrices are rejected with the PLY index and matrix type instead of allowing
NaN, mirror, or degenerate transforms to enter world coordinates.

## Point_Cloud_A validation record (2026-08-14)

The supplied `Point_Cloud_A` directory contains `Point_Cloud_A01.ply` through
`Point_Cloud_A04.ply`, each with about 6.3 million measured vertices. Their
local bounds are approximately X=-94..96, Y=0..-300, and Z=-82..18. The PLY
vertices are organized by scan rows and local Y decreases from the first row
to the last row.

The robot mapping is:

```text
A01: Start (500, 150, 700, 0, 0, 180) -> End (500, -150, 700, 0, 0, 180)
A02: Start (600, 150, 700, 0, 0, 180) -> End (600, -150, 700, 0, 0, 180)
A03: Start (700, 150, 700, 0, 0, 180) -> End (700, -150, 700, 0, 0, 180)
A04: Start (800, 150, 700, 0, 0, 180) -> End (800, -150, 700, 0, 0, 180)
```

For these files `Local Y` is the recommended scan-progress source. The
implementation compares the first and last valid Y values and reverses the
normalized progress when the axis decreases, so the first scanned row maps to
Start and the last row maps to End. `VertexOrder` is not recommended: points
within a row are not robot-motion samples and applying their order produces
shear or other non-rigid distortion.

Validation order is: load all four files and check bounds; select Local Y and
run robot/world conversion with ICP disabled; inspect per-file world bounds
and Start/Mid/End diagnostics; only then enable bounded ICP. The merge-cache
format version is incremented when progress direction or transform assumptions
change, invalidating caches produced with the previous direction.

### Direction diagnosis from the real Point_Cloud_A run

The default-view run exposed a direction error in the earlier rule. With the supplied hand-eye rotation and robot poses, the combined transform maps camera local Y and robot base Y in opposite directions. Applying the additional LocalY reversal adds the 300 mm camera span to the 300 mm robot motion, producing a world Y span of about 602 mm and four disconnected bands.

An A/B test that exchanges each Start and End pose reduces the world Y span to about 3 mm. This confirms that pose pairing, not ICP or image resolution, is the primary fault. Future direction selection must evaluate transformed endpoint residuals for both forward and reversed pairings and choose the smaller residual. Local-axis monotonicity alone is insufficient because the hand-eye and flange rotations change the world-axis direction.

## Phase 1 implementation: explicit scan direction and cache isolation

`WorldCloudInput` now carries `ScanDirection::{Auto,Forward,Reverse}`. The production UI uses `Auto`; it compares transformed endpoint residuals for both mappings and records the selected direction in diagnostics. A caller can force either mapping for controlled A/B tests. The merge-cache version is incremented and the direction is serialized, so results from a different direction cannot be reused.

## Phase 2 plan: robust direction score and overlap validation

The next direction decision uses trimmed endpoint regions rather than one first
and one last vertex. For each organized scan, the first and last 1% (bounded
by a maximum sample count) of valid points are averaged. The local endpoint
vector is rotated by the mid-scan hand-eye/flange orientation. Two candidates
are scored: camera displacement plus robot Start-to-End translation, and camera
displacement minus that translation. The smaller norm is selected, and both
scores are written to diagnostics. This tests the physical cancellation
expected for a moving Eye-in-Hand scanner and is independent of local-axis
numeric order.

After conversion, phase 2 also reports adjacent-cloud XY bounding-box overlap
and rejects direction conclusions when neither candidate produces a plausible
overlap. Duplicate measured points are retained; overlap is a diagnostic, not
an instruction to delete points.

## Phase 3 plan: adjacent-cloud overlap gate

After each cloud is transformed and voxel-reduced, compute its XY bounding box
and compare it with the accumulated reference. Report intersection area divided
by the moving-cloud area and by the union area. The diagnostic records the
moving cloud id, overlap ratio, intersection bounds, and whether the overlap is
below the production warning threshold. This is a geometric gate before ICP:
low overlap or disjoint boxes indicate a robot pose, scan direction, or file
ordering problem and must not be hidden by ICP.

The gate is diagnostic in this phase and does not delete overlapping measured
points. A later UI phase may expose a configurable warning threshold.

## Phase 4 implementation: ICP preflight overlap gate

Before ICP correspondence search, the robot-world XY bounding-box coverage of
the moving cloud is compared with the accumulated reference. If coverage is
below `IcpOptions::minimumWorldOverlapForIcp` (default 0.10), ICP is skipped
and the diagnostic asks the operator to check Start/End poses, scan direction,
hand-eye calibration, and file order. Measured points remain unchanged; this
is a safety gate rather than a filter. Set `rejectIcpWhenLowWorldOverlap` to
false only for controlled diagnostics. The existing ICP Fitness, RMSE,
correspondence-overlap, and correction-limit checks remain active whenever
ICP is allowed to run.
