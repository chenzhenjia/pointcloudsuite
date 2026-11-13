"""Read-only initial-modeling result validator. Requires jsonschema and Pillow.

Example JSON is shape-only; --runtime-root additionally verifies actual assets.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker


def load(path):
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s)))


def validate(document, runtime_root=None):
    schema = load(Path(__file__).resolve().parents[1] / "docs/contracts/point_cloud_result_v1.schema.json")
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)
    raw = document["inputs"]["raw_clouds"]
    assert len({v["path_index"] for v in raw}) == len(raw), "duplicate path_index"
    if document["status"] == "failed":
        return
    mask = document["outputs"]["template_geometry"]["plane_mask"]
    for px, size, scale in zip(mask["pixel_size_px"], mask["physical_size_mm"], mask["mm_per_pixel"]):
        assert math.isclose(px * scale, size, rel_tol=1e-6), "mask scale mismatch"
    if runtime_root is None:
        return
    root = runtime_root.resolve(strict=True)

    def asset(ref):
        path = (root / ref).resolve(strict=True)
        assert path.is_relative_to(root) and path.is_file(), f"asset escapes root or is not a file: {ref}"
        return path

    def ply(ref):
        # Formal adapter assets have exactly six float32 fields, Binary LE.
        with asset(ref).open("rb") as file:
            header = []
            while True:
                line = file.readline()
                assert line and len(header) < 100, "invalid PLY header"
                header.append(line.decode("ascii").strip())
                if line.strip() == b"end_header":
                    break
            assert "format binary_little_endian 1.0" in header, "expected Binary LE PLY"
            assert [s for s in header if s.startswith("property ")] == [f"property float {s}" for s in ("x", "y", "z", "nx", "ny", "nz")], "unexpected PLY properties"
            count = int(next(s.split()[-1] for s in header if s.startswith("element vertex ")))
            assert count > 0, "empty point cloud"
            for _ in range(count):
                record = file.read(24)
                assert len(record) == 24 and all(map(math.isfinite, struct.unpack("<6f", record))), "invalid PLY payload"
            assert not file.read(1), "PLY count/payload mismatch"
        return count

    inputs = document["inputs"]
    asset(inputs["calibration_ref"])
    for value in raw:
        ply(value["ref"])
    base_count = ply(document["outputs"]["base_cloud_ref"])
    candidate_count = ply(document["outputs"]["template_geometry"]["point_cloud_ref"])
    assert 3 <= candidate_count <= base_count, "candidate/base point count mismatch"
    from PIL import Image
    with Image.open(asset(mask["ref"])) as image:
        assert image.mode == "L" and list(image.size) == mask["pixel_size_px"], "mask mode/dimension mismatch"
        colors = set(image.tobytes())
        assert 128 in colors and colors <= {0, 128, 255}, "mask grayscale mismatch"
    if "scan_path_output_ref" in inputs:
        scan = load(asset(inputs["scan_path_output_ref"]))
        assert scan["schema"] == "sr2026.scan-path-output" and scan["schema_version"] == 1
        for key in ("job_id", "run_id", "step_id", "business_code"):
            assert scan[key] == document[key], f"scan identity mismatch: {key}"
        assert scan["execution_status"] == "completed" and scan["coordinate_frame"] == "base"
        assert scan["points_per_path"] == 2 and len(scan["points"]) == 2 * scan["path_count"]
        assert max(v["path_index"] for v in raw) <= scan["path_count"]
        plan = load(asset(scan["source_plan_ref"]))
        assert plan["schema"] == "sr2026.process-plan" and plan["schema_version"] == 1
        for key in ("job_id", "run_id", "step_id", "workpiece_id"):
            assert plan[key] == document[key], f"plan identity mismatch: {key}"
        assert plan["process"]["business_code"] == document["business_code"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", type=Path)
    parser.add_argument("--runtime-root", type=Path)
    args = parser.parse_args()
    try:
        validate(load(args.result), args.runtime_root)
    except Exception as error:
        print(f"INVALID: {error}", file=sys.stderr)
        return 2
    print("VALID" + (" (assets checked)" if args.runtime_root else " (shape only)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
