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

## External data required for exact motion compensation

Exact compensation requires one of the following datasets for every PLY:

- a timestamp per point or scan line plus sampled robot flange poses;
- an encoder/trigger index per point plus a matching robot trajectory;
- documented proof that PLY vertex order or a local coordinate is the scanner
  acquisition order and that robot motion is uniform.

Until one of these is available, Start/End interpolation must be treated as an
approximation and verified with the ICP-disabled comparison result.
