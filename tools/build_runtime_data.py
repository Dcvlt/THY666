"""Validate a lossless chord archive and project it into local browser data.

The output is a musical/layout projection, not an archival replacement or a
redistribution license. Original archive bytes remain the canonical source.
"""

import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import sys

from chord_dump import ROOT, export_dump
from local_env import env_path


DEFAULT_ARCHIVE = env_path("THY666_CHORD_ARCHIVE", ROOT / ".tools/inputs/chords.lossless.json")
DEFAULT_OUTPUT = ROOT / "web/generated/chords.runtime.json"
RECOVERED_SHA256 = "93111f5ab9e87921b3a00c8d594572f15b77a087f705cfd996a66fd67cafad9a"
PAD_FIELDS = ("name", "midi_slots", "used_slot_count", "note_status", "notes", "color_id", "color_token")
LAYOUT = {
    "hardware_pad_count": 96,
    "stored_pad_count": 80,
    "chord_pad_count": 48,
    "melody_stored_pad_count": 32,
    "melody_hardware_pad_count": 48,
    "rows_per_column": 4,
    "columns_per_side": 12,
    "order": "serialized column-major",
    "orientation": "physical orientation and wiring unverified",
    "pad_reference_fields": ["source_offset", "pad_record_index"],
    "unassigned_reference": None,
    "pitch_convention": "scientific pitch notation: MIDI 60 = C4, MIDI 0 = C-1",
    "color_policy": "opaque original color IDs and tokens; no RGB palette is assigned",
}


def _json_bytes(value):
    return json.dumps(value, ensure_ascii=True, separators=(",", ":")).encode("ascii")


def safe_output(path, archive_path=DEFAULT_ARCHIVE):
    """Keep generated files away from all original and archival source folders."""
    destination = Path(path).resolve()
    archive_path = Path(archive_path).resolve()
    protected = ((ROOT / "Resources").resolve(), (ROOT / ".local-archive").resolve(),
                 DEFAULT_ARCHIVE.parent.resolve(), archive_path.parent)
    if any(destination.is_relative_to(folder) for folder in protected):
        raise ValueError("output must not overwrite original inputs or write inside an archive directory")
    return destination


def project_archive(archive, *, archive_sha256, archive_size_bytes):
    """Validate every archived byte/derived field, then deduplicate pad values."""
    reconstructed = export_dump(archive)
    if not isinstance(archive_sha256, str) or len(archive_sha256) != 64 or any(
            char not in "0123456789abcdef" for char in archive_sha256):
        raise ValueError("archive SHA-256 must be 64 lowercase hexadecimal characters")
    if type(archive_size_bytes) is not int or archive_size_bytes < 1:
        raise ValueError("archive size must be a positive integer")
    for scale in archive["scales"]:
        if len(scale["pads"]) != 80:
            raise ValueError("runtime schema requires exactly 80 stored records per scale")
        for pad in scale["pads"]:
            if pad["note_status"] not in ("active", "null"):
                raise ValueError(f"ambiguous note slots at scale {scale['index']} pad {pad['index']}: {pad['note_status']}")

    categories = [{"index": c["index"], "name": c["name"], "source_offset": c["offset"],
                   "target_token": c["target_token"], "count_token": c["count_token"],
                   "group_indices": list(c["child_indices"])} for c in archive["categories"]]
    groups = [{"index": g["index"], "name": g["name"], "source_offset": g["offset"],
               "category_index": g["parent_index"], "target_token": g["target_token"],
               "count_token": g["count_token"], "scale_indices": list(g["child_indices"]),
               "root_order": [archive["scales"][i]["name"].rsplit(" ", 1)[-1] for i in g["child_indices"]]}
              for g in archive["groups"]]
    records = []
    record_indices = {}
    scales = []
    status_counts = Counter()
    for scale in archive["scales"]:
        refs = []
        for pad in scale["pads"]:
            record = {key: copy.deepcopy(pad[key]) for key in PAD_FIELDS}
            encoded = _json_bytes(record)
            if encoded not in record_indices:
                record_indices[encoded] = len(records)
                records.append(record)
            refs.append([pad["offset"], record_indices[encoded]])
            status_counts[pad["note_status"]] += 1
        refs.extend([None] * 16)
        scales.append({"index": scale["index"], "name": scale["name"], "source_offset": scale["offset"],
                       "group_index": scale["parent_index"],
                       "category_index": archive["groups"][scale["parent_index"]]["parent_index"],
                       "variant_index": scale["variant_index"], "variant_token": scale["variant_token"],
                       "root_label": scale["name"].rsplit(" ", 1)[-1],
                       "pad_block_offset": scale["target_offset"], "pad_block_token": scale["target_token"],
                       "auxiliary_token": scale["auxiliary_token"], "pads": refs})
    return {
        "format": "thy333.runtime-chords", "version": 1,
        "provenance": {
            "source": copy.deepcopy(archive["source"]),
            "archive": {"format": archive["format"], "version": archive["version"],
                        "sha256": archive_sha256, "size_bytes": archive_size_bytes},
            "validation": "full structured archive reconstructed; source hashes and all derived metadata match",
            "reconstructed_sha256": hashlib.sha256(reconstructed).hexdigest(),
            "data_policy": "local projection; original notes, names, voicings, layouts and opaque color IDs preserved",
            "distribution_policy": "rights unchanged; generated runtime data is local and ignored by version control",
        },
        "layout": copy.deepcopy(LAYOUT),
        "counts": {"categories": len(categories), "groups": len(groups), "scales": len(scales),
                   "stored_pads": len(scales) * 80, "hardware_pad_cells": len(scales) * 96,
                   "unassigned_pads": len(scales) * 16, "unique_pad_records": len(records),
                   "active_pads": status_counts["active"], "null_pads": status_counts["null"]},
        "categories": categories, "groups": groups, "scales": scales, "pad_records": records,
    }


def materialize_pad(runtime, scale_index, pad_index):
    """Resolve a schema-v1 pad reference into a full, independent pad object."""
    if type(scale_index) is not int or not 0 <= scale_index < len(runtime["scales"]):
        raise ValueError("scale index outside runtime data")
    if type(pad_index) is not int or not 0 <= pad_index < 96:
        raise ValueError("pad index must be an integer in 0..95")
    reference = runtime["scales"][scale_index]["pads"][pad_index]
    side_index = pad_index if pad_index < 48 else pad_index - 48
    identity = {"index": pad_index, "side": "chord" if pad_index < 48 else "melody",
                "side_index": side_index, "column": side_index // 4,
                "row_within_column": side_index % 4}
    if reference is None:
        return {**identity, "assigned": False, "source_offset": None, "name": None,
                "midi_slots": [0] * 7, "used_slot_count": 0, "note_status": "unassigned",
                "notes": [], "color_id": None, "color_token": None}
    return {**copy.deepcopy(runtime["pad_records"][reference[1]]), **identity,
            "source_offset": reference[0], "assigned": True}


def build_file(archive_path=DEFAULT_ARCHIVE, output_path=DEFAULT_OUTPUT):
    archive_path = Path(archive_path).resolve()
    output_path = safe_output(output_path, archive_path)
    raw = archive_path.read_bytes()
    archive = json.loads(raw)
    # The default is the immutable recovered dataset, never a silent substitute.
    if archive_path == DEFAULT_ARCHIVE.resolve() and archive.get("source", {}).get("sha256") != RECOVERED_SHA256:
        raise ValueError("default archive must match the pinned recovered dump SHA-256")
    runtime = project_archive(archive, archive_sha256=hashlib.sha256(raw).hexdigest(), archive_size_bytes=len(raw))
    output = _json_bytes(runtime) + b"\n"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    return {"output": str(output_path), "size_bytes": len(output),
            "sha256": hashlib.sha256(output).hexdigest(), "counts": runtime["counts"],
            "source_sha256": runtime["provenance"]["source"]["sha256"],
            "archive_sha256": runtime["provenance"]["archive"]["sha256"]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(build_file(args.archive, args.output), indent=2))
    except (OSError, ValueError, KeyError, IndexError) as error:
        print(f"runtime data error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
