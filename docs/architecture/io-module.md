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

## Stage-three parallel parsing

- Mapped ASCII vertex payloads are split into verified chunks and parsed
  concurrently. Each worker writes only to its fixed, non-overlapping range in
  the preallocated point array and maintains private bounds/error state.
- The first boundary scan stops after the declared vertex line count, so later
  ASCII face or other element payloads are not mistaken for vertices.
- Results are published only after all workers join and their point counts and
  bounds are merged. Mapping failure retains the serial buffered fallback.
- A worker-specific parse or cancellation error is preserved through the final
  point-count validation instead of being replaced by a generic incomplete-data
  message.
- `ply_reader_tests` accepts an optional external PLY path and reports its
  format, point count, bounds and reader timings after the built-in regression
  fixtures pass. This provides a repeatable baseline for large production data.

## Stage-four adaptive ASCII parallelism

- The mapped ASCII reader selects one worker for small payloads, up to two for
  medium payloads and up to four for large payloads. The default is additionally
  capped by `std::thread::hardware_concurrency()` so low-core systems are not
  oversubscribed.
- `PlyReadOptions::asciiWorkerCount` can force a diagnostic worker count from
  one through eight. Production callers leave it at zero for adaptive behavior.
- Mapping fallback remains single-threaded. The selected count is returned in
  `PlyReadResult::asciiWorkerCount` and the test executable accepts it as an
  optional second argument for repeatable one/two/four-worker comparisons.

## Stage-five compact binary XYZ fast path

- Binary PLY files whose vertex layout is exactly contiguous `float x/y/z`
  use a mapped, index-partitioned reader. Each worker decodes its fixed point
  range directly into the preallocated `Point3D` array and keeps private bounds.
- Compact binary parallelism uses the same adaptive one/two/four-worker policy
  as ASCII. The selected binary worker count remains private to the reader;
  it is deliberately not exposed through the cross-module result structure.
  This keeps the public ABI stable for incremental Qt Creator builds.
- Files with normals, colors, intensity, reordered properties or other scalar
  layouts retain the generic scalar reader. Mapping failure also falls back to
  that path, preserving all existing binary little/big-endian compatibility.
- Per-point atomic progress accounting is skipped when the caller did not
  provide a progress callback. Cancellation polling and final point-count
  validation remain active independently.
- Both GUI processor translation units declare `ply_reader.h` as an explicit
  CMake `OBJECT_DEPENDS` input. Localized MSVC `/showIncludes` output can leave
  an empty dependency file under the Qt Creator Makefile generator; the
  explicit dependency prevents a new reader library from linking against a
  caller compiled with an older result layout.
