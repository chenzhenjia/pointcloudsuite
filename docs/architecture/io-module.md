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

## First-stage loading optimization

- `pointcloudview` now uses the shared `pcv::detail::io::readPly` reader for
  asynchronous loading instead of maintaining a separate application parser.
- The reader allocates the declared vertex array once and writes by index,
  avoiding repeated `QVector::push_back` growth.
- Finite XYZ bounds are accumulated during parsing and returned with the load
  result, so the UI does not scan millions of points a second time just to set
  the Z color range.
- Existing ASCII and binary formats, point order, optional normals and error
  handling remain unchanged. Memory mapping and custom numeric parsing are
  intentionally deferred to the next optimization stage.

## ASCII parser optimization

- ASCII vertex values now use a locale-independent in-place float parser for
  signed decimal and scientific notation values, avoiding `strtof` setup for
  every field.
- Header property positions compute `lastRequiredIndex`; values after the last
  required coordinate/normal property are not converted and are only skipped
  as trailing text.
- The reader still rejects malformed numbers and non-finite/overflowing float
  values, preserves vertex order, and keeps binary parsing unchanged.

## Memory-mapped ASCII payload

- After parsing the header, ASCII vertex payloads are read through `QFile::map`
  and scanned in place with pointer arithmetic; this avoids per-line system
  calls and temporary byte-array allocations for large files.
- Newline boundaries are located directly in the mapped buffer, while the
  existing locale-independent float parser and point order are preserved.
- If mapping is unavailable, the reader falls back to buffered line reads, so
  network files and platforms without mapping support remain compatible.

## Stage-one loading baseline

- `PlyReadResult` records `headerElapsedMs`, `boundaryScanElapsedMs`, `parseElapsedMs`, and
  `totalElapsedMs` for successful reads. These are diagnostic timings only and
  do not change parser or UI behavior.
- The application logs reader timings and the cache-publication boundary;
  VBO upload timing remains a separate rendering concern. This baseline is
  required before enabling chunked parallel parsing.

## Stage-two ASCII chunk boundaries

- The mapped ASCII payload is divided into four preparatory chunks. Byte split
  candidates are advanced to the next complete newline, so no vertex line is
  shared by two chunks.
- A lightweight second scan counts lines in each chunk and verifies that the
  sum equals the Header vertex count. The current parser still consumes chunks
  serially; parallel numeric parsing is deferred until the next stage.
