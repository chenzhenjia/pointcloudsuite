# Point-cloud I/O module

`pcv_io` is the single reusable boundary for loading PLY point clouds.

## Supported input

- ASCII PLY
- Binary little-endian PLY
- Binary big-endian PLY
- Arbitrary scalar property order
- Optional `nx`, `ny`, and `nz` normal properties

The reader rejects missing `x`, `y`, or `z` properties, unsupported scalar
types, vertex list properties, truncated payloads, and malformed headers.

## Cache contract

Cache files are stored below the application-local cache directory. A cache is
accepted only when its format version, source size, source modification time,
point count, and payload length are valid. Cache writes use `QSaveFile` so a
crash cannot leave a partially written cache as the current entry.

External consumers call the STL-based `pcv::io::readPly` and
`pcv::io::readPlyCached` API. Existing Qt applications temporarily use the
private `pcv::detail::io` implementation until their workflow adapters are
migrated to the public boundary.
