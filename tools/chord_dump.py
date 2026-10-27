"""Lossless, dependency-free reader for the recovered THY333 musical-data dump.

This is an archival importer, not a firmware writer. Numeric/derived fields are
checked against the retained text tokens when exporting. Unknown bytes survive.
"""

import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import re
from local_env import env_path


ROOT = Path(__file__).resolve().parents[1]
LOCAL_INPUTS = ROOT / ".tools/inputs"
DIRECTORY_SIZE = 256
PAD_SIZE = 32
CHORD_PADS = 48
MELODY_PADS = 32
BLOCK_SIZE = (CHORD_PADS + MELODY_PADS) * PAD_SIZE
PITCH_CLASSES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
CONVENTIONS = {
    "notes": "seven two-digit decimal ASCII slots; trailing 00 slots are padding",
    "octaves": "scientific pitch notation: MIDI 60 = C4, MIDI 0 = C-1",
    "layout": "serialized column-major: four records per column; physical orientation and wiring unverified",
    "chord_grid": {"stored_pads": 48, "columns": 12, "rows_per_column": 4},
    "melody_grid": {"stored_pads": 32, "columns": 8, "rows_per_column": 4,
                    "hardware_pads_without_records": 16},
    "colors": "numeric palette/base-color or degree IDs retained; RGB palette unavailable",
    "auxiliary": "opaque hexadecimal field retained; not resolved as an in-dump pointer",
}


def _integer(value, label, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError(f"{label} must be an integer >= {minimum}")
    return value


def _token_number(token, base, label):
    pattern = r"[0-9]+" if base == 10 else r"[0-9a-fA-F]+"
    if not isinstance(token, str) or re.fullmatch(pattern, token) is None:
        raise ValueError(f"invalid {label}: {token!r}")
    return int(token, base)


def _ascii(text, label):
    if not isinstance(text, str) or any(not 32 <= ord(c) <= 126 for c in text):
        raise ValueError(f"{label} must contain printable ASCII only")
    return text.encode("ascii")


def _hex_bytes(text, label):
    if not isinstance(text, str) or re.fullmatch(r"(?:[0-9a-fA-F]{2})*", text) is None:
        raise ValueError(f"{label} must contain complete hexadecimal byte pairs")
    return bytes.fromhex(text)


def _slice(data, offset, size, label):
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"{label} at 0x{offset:X} extends beyond the dump")
    return data[offset:offset + size]


def _text_and_suffix(raw, label):
    end = raw.find(b"\0")
    if end < 0:
        end = len(raw)
    text = raw[:end].decode("ascii")
    _ascii(text, label)
    return text, raw[end:].hex()


def _directory(data, offset, kind, index):
    text, suffix = _text_and_suffix(_slice(data, offset, DIRECTORY_SIZE, kind), kind)
    fields = text.split(";")
    if len(fields) != (4 if kind == "scale" else 3) or not fields[0]:
        raise ValueError(f"invalid {kind} directory record at 0x{offset:X}")
    record = {"index": index, "offset": offset, "name": fields[0],
              "target_token": fields[1], "target_offset": _token_number(fields[1], 16, "target"),
              "suffix_hex": suffix}
    if kind == "scale":
        _token_number(fields[2], 16, "auxiliary field")
        record.update(auxiliary_token=fields[2], variant_token=fields[3],
                      variant_index=_token_number(fields[3], 10, "variant index"))
    else:
        record.update(count_token=fields[2], count=_token_number(fields[2], 10, "child count"))
        if not record["count"]:
            raise ValueError(f"empty {kind} directory at 0x{offset:X}")
    return record


def _ownership(parents, children, child_start, label):
    owners = {}
    for parent in parents:
        delta = parent["target_offset"] - child_start
        if delta < 0 or delta % DIRECTORY_SIZE:
            raise ValueError(f"misaligned/out-of-table {label} pointer at 0x{parent['offset']:X}")
        first = delta // DIRECTORY_SIZE
        if first + parent["count"] > len(children):
            raise ValueError(f"{label} child range extends beyond its directory")
        indices = list(range(first, first + parent["count"]))
        parent["child_indices"] = indices
        for index in indices:
            if index in owners:
                raise ValueError(f"{label} child ranges overlap at index {index}")
            owners[index] = parent["index"]
            children[index]["parent_index"] = parent["index"]
    if len(owners) != len(children):
        raise ValueError(f"{label} child ranges do not cover their directory")


def _pad(data, offset, index):
    raw = _slice(data, offset, PAD_SIZE, "pad")
    if re.fullmatch(rb"[0-9]{16}", raw[:16]) is None:
        raise ValueError(f"non-decimal pad header at 0x{offset:X}")
    slots = [int(raw[i:i + 2]) for i in range(0, 14, 2)]
    name, suffix = _text_and_suffix(raw[16:], f"pad name at 0x{offset:X}")
    count = next((i + 1 for i in range(6, -1, -1) if slots[i]), 0)
    status = "active"
    if not count:
        status = "null" if name == "NULL" else "ambiguous_all_zero"
    elif 0 in slots[:count]:
        status = "ambiguous_zero_in_prefix"
    notes = [{"slot": i, "midi": n, "pitch_class": PITCH_CLASSES[n % 12], "octave": n // 12 - 1}
             for i, n in enumerate(slots[:count])]
    side_index = index if index < CHORD_PADS else index - CHORD_PADS
    return {"index": index, "offset": offset,
            "side": "chord" if index < CHORD_PADS else "melody", "side_index": side_index,
            "column": side_index // 4, "row_within_column": side_index % 4,
            "midi_slots": slots, "used_slot_count": count, "note_status": status, "notes": notes,
            "color_token": raw[14:16].decode("ascii"), "color_id": int(raw[14:16]),
            "name": name, "suffix_hex": suffix}


def _opaque(offset, raw):
    region = {"offset": offset, "length": len(raw)}
    if raw and raw.count(raw[:1]) == len(raw):
        region["fill_byte"] = raw[0]
    else:
        region["data_hex"] = raw.hex()
    return region


def _hashes(data):
    blob = b"blob " + str(len(data)).encode("ascii") + b"\0" + data
    return {"size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "git_blob_sha1": hashlib.sha1(blob).hexdigest()}


def import_dump(data):
    """Return a deterministic archive including original tokens and every byte."""
    try:
        first_category = _directory(data, 0, "category", 0)
        group_start = first_category["target_offset"]
        if group_start < DIRECTORY_SIZE or group_start % DIRECTORY_SIZE or group_start >= len(data):
            raise ValueError("invalid category/group boundary")
        categories = [_directory(data, off, "category", i)
                      for i, off in enumerate(range(0, group_start, DIRECTORY_SIZE))]
        first_group = _directory(data, group_start, "group", 0)
        scale_start = first_group["target_offset"]
        if scale_start <= group_start or scale_start % DIRECTORY_SIZE or scale_start >= len(data):
            raise ValueError("invalid group/scale boundary")
        groups = [_directory(data, off, "group", i)
                  for i, off in enumerate(range(group_start, scale_start, DIRECTORY_SIZE))]
        _ownership(categories, groups, group_start, "category")
        scale_count = sum(group["count"] for group in groups)
        scale_end = scale_start + scale_count * DIRECTORY_SIZE
        if scale_end > len(data):
            raise ValueError("declared scale directory exceeds the dump")
        scales = [_directory(data, scale_start + i * DIRECTORY_SIZE, "scale", i)
                  for i in range(scale_count)]
        _ownership(groups, scales, scale_start, "group")
        occupied = [(0, scale_end)]
        for scale in scales:
            start = scale["target_offset"]
            if start < scale_end or start % PAD_SIZE:
                raise ValueError(f"misaligned/directory-overlapping pad block at 0x{start:X}")
            _slice(data, start, BLOCK_SIZE, "pad block")
            occupied.append((start, start + BLOCK_SIZE))
        opaque = []
        cursor = 0
        for start, end in sorted(occupied):
            if start < cursor:
                raise ValueError("pad blocks overlap or share storage; unsupported by archive v1")
            if start > cursor:
                opaque.append(_opaque(cursor, data[cursor:start]))
            cursor = end
        if cursor < len(data):
            opaque.append(_opaque(cursor, data[cursor:]))
        for scale in scales:
            scale["pads"] = [_pad(data, scale["target_offset"] + i * PAD_SIZE, i)
                             for i in range(CHORD_PADS + MELODY_PADS)]
        return {"format": "thy333.chord-dump", "version": 1, "source": _hashes(data),
                "conventions": copy.deepcopy(CONVENTIONS), "categories": categories, "groups": groups,
                "scales": scales, "opaque_regions": opaque}
    except (UnicodeError, TypeError) as error:
        raise ValueError(f"invalid dump: {error}") from error


def _encode_directory(record, kind):
    name = record["name"]
    if ";" in name:
        raise ValueError("directory name cannot contain a semicolon")
    fields = [name, record["target_token"]]
    if kind == "scale":
        fields.extend((record["auxiliary_token"], record["variant_token"]))
    else:
        fields.append(record["count_token"])
    raw = _ascii(";".join(fields), "directory") + _hex_bytes(record["suffix_hex"], "directory suffix")
    if len(raw) != DIRECTORY_SIZE:
        raise ValueError("reconstructed directory record is not 256 bytes")
    return raw


def _encode_pad(pad):
    slots = pad["midi_slots"]
    if len(slots) != 7 or any(type(n) is not int or not 0 <= n <= 99 for n in slots):
        raise ValueError("a pad requires seven decimal note slots in 0..99")
    color = pad["color_token"]
    if not isinstance(color, str) or re.fullmatch(r"[0-9]{2}", color) is None:
        raise ValueError("pad color token must contain two decimal digits")
    raw = ("".join(f"{n:02d}" for n in slots) + color).encode("ascii")
    raw += _ascii(pad["name"], "pad name") + _hex_bytes(pad["suffix_hex"], "pad suffix")
    if len(raw) != PAD_SIZE:
        raise ValueError("reconstructed pad record is not 32 bytes")
    return raw


def export_dump(archive):
    """Rebuild records, check complete coverage, source hashes and derived metadata.

    No source binary is embedded in the archive. This reconstructs the decimal
    note headers, original directory text, names, padding and opaque regions.
    """
    try:
        if archive["format"] != "thy333.chord-dump" or archive["version"] != 1:
            raise ValueError("unsupported archive format/version")
        size = _integer(archive["source"]["size_bytes"], "source size", 1)
        fragments = []
        for key, kind in (("categories", "category"), ("groups", "group"), ("scales", "scale")):
            for record in archive[key]:
                fragments.append((record["offset"], _encode_directory(record, kind)))
        for scale in archive["scales"]:
            for pad in scale["pads"]:
                fragments.append((pad["offset"], _encode_pad(pad)))
        for region in archive["opaque_regions"]:
            length = _integer(region["length"], "opaque length", 1)
            if "fill_byte" in region:
                fill = _integer(region["fill_byte"], "fill byte")
                if fill > 255 or "data_hex" in region or length > size:
                    raise ValueError("invalid opaque fill region")
                raw = bytes([fill]) * length
            else:
                raw = _hex_bytes(region["data_hex"], "opaque data")
            if len(raw) != length:
                raise ValueError("opaque region length mismatch")
            fragments.append((region["offset"], raw))
        for offset, raw in fragments:
            _integer(offset, "fragment offset")
            if not raw or offset + len(raw) > size:
                raise ValueError("fragment is empty or exceeds source size")
        cursor = 0
        output = bytearray()
        for offset, raw in sorted(fragments, key=lambda fragment: fragment[0]):
            if offset != cursor:
                raise ValueError(f"archive has a gap/overlap at 0x{cursor:X}")
            output.extend(raw)
            cursor += len(raw)
        if cursor != size:
            raise ValueError("archive does not cover the complete dump")
        data = bytes(output)
        if _hashes(data) != archive["source"]:
            raise ValueError("reconstructed bytes do not match the source size/hashes")
        canonical = import_dump(data)
        # JSON comparison checks bool versus int as well as derived values.
        if json.dumps(canonical, sort_keys=True) != json.dumps(archive, sort_keys=True):
            raise ValueError("archive metadata differs from the reconstructed records")
        return data
    except (KeyError, TypeError, UnicodeError) as error:
        raise ValueError(f"invalid archive: {error}") from error


def legacy_name(name):
    """Historical normalization, used ONLY to compare the lossy JSON export."""
    for flat, sharp in (("Db", "C#"), ("Eb", "D#"), ("Gb", "F#"), ("Ab", "G#"), ("Bb", "A#")):
        name = name.replace(flat, sharp)
    return name


def compare_legacy(archive, legacy):
    chords = []
    seen = set()
    for scale in archive["scales"]:
        for pad in scale["pads"][:CHORD_PADS]:
            if not pad["used_slot_count"]:
                continue
            midi = tuple(note["midi"] for note in pad["notes"])
            key = (legacy_name(pad["name"]), midi)
            if key not in seen:
                seen.add(key)
                chords.append({"name": key[0], "notes": [PITCH_CLASSES[n % 12] for n in midi]})
    expected_groups = []
    for group in archive["groups"]:
        variants = []
        for index in group["child_indices"]:
            scale = archive["scales"][index]
            degrees = {}
            for pad in scale["pads"][CHORD_PADS:]:
                if pad["used_slot_count"]:
                    degrees[pad["color_id"]] = pad["notes"][0]["midi"] % 12
            variants.append({"root": scale["name"].rsplit(" ", 1)[-1],
                             "notes": [PITCH_CLASSES[degrees[i]] for i in range(1, 13) if i in degrees],
                             "chords": [legacy_name(pad["name"]) for pad in scale["pads"][:CHORD_PADS]
                                        if pad["used_slot_count"]]})
        expected_groups.append({"name": group["name"], "group": variants})
    actual_chords = [{"name": c.get("name"), "notes": c.get("notes")} for c in legacy.get("Chords", [])]
    actual_groups = [{"name": g.get("name"), "group": [
        {"root": v.get("root"), "notes": v.get("notes"), "chords": v.get("chords")}
        for v in g.get("group", [])]} for g in legacy.get("Scales", [])]
    mismatches = []
    if len(chords) != len(actual_chords):
        mismatches.append({"section": "Chords", "expected_count": len(chords), "actual_count": len(actual_chords)})
    for index, (expected, actual) in enumerate(zip(chords, actual_chords)):
        if expected != actual:
            mismatches.append({"section": "Chords", "index": index, "expected": expected, "actual": actual})
    if len(expected_groups) != len(actual_groups):
        mismatches.append({"section": "Scales", "expected_count": len(expected_groups), "actual_count": len(actual_groups)})
    for index, (expected, actual) in enumerate(zip(expected_groups, actual_groups)):
        if expected != actual:
            mismatches.append({"section": "Scales", "group_index": index})
    return {"all_compared_fields_match": not mismatches, "chord_records_compared": len(chords),
            "scale_groups_compared": len(expected_groups), "scale_variants_compared": len(archive["scales"]),
            "legacy_categories_count": len(legacy.get("Categories", [])),
            "legacy_chord_warning_count": sum("_warning" in c for c in legacy.get("Chords", [])),
            "ignored_fields": ["_warning"], "mismatch_count": len(mismatches), "mismatches": mismatches[:50]}


def read_hexdump(path):
    """Read the recovered full hexdump -C listing with strict address checks."""
    output = bytearray()
    final_address = None
    with Path(path).open(encoding="ascii") as stream:
        for number, line in enumerate(stream, 1):
            line = line.rstrip("\r\n")
            if re.fullmatch(r"[0-9a-fA-F]{8}", line):
                if final_address is not None:
                    raise ValueError("multiple hexdump end markers")
                final_address = int(line, 16)
                continue
            match = re.fullmatch(r"([0-9a-fA-F]{8})  (.*?)\s+\|.*\|", line)
            if not match or final_address is not None:
                raise ValueError(f"invalid hexdump line {number}")
            tokens = match[2].split()
            if len(tokens) != 16 or any(re.fullmatch(r"[0-9a-fA-F]{2}", t) is None for t in tokens):
                raise ValueError(f"invalid hexdump byte row {number}")
            if int(match[1], 16) != len(output):
                raise ValueError(f"hexdump address gap/overlap at line {number}")
            output.extend(bytes.fromhex("".join(tokens)))
    if final_address != len(output):
        raise ValueError("hexdump missing/mismatched end marker")
    return bytes(output)


def validation_report(archive):
    pads = [pad for scale in archive["scales"] for pad in scale["pads"]]
    anomalies = Counter()
    samples = []

    def finding(kind, scale, pad=None, **extra):
        anomalies[kind] += 1
        # Keep examples of rare findings visible even after thousands of common ones.
        if anomalies[kind] <= 5:
            item = {"kind": kind, "scale_index": scale["index"], "scale_name": scale["name"], **extra}
            if pad is not None:
                item.update(pad_index=pad["index"], pad_name=pad["name"], offset=pad["offset"])
            samples.append(item)

    for scale in archive["scales"]:
        melody = scale["pads"][CHORD_PADS:]
        scale_pcs = {note["midi"] % 12 for pad in melody for note in pad["notes"]}
        for pad in scale["pads"]:
            if pad["note_status"].startswith("ambiguous"):
                finding(pad["note_status"], scale, pad)
            if pad["side"] == "melody" and pad["used_slot_count"] > 1:
                finding("melody_pad_has_multiple_notes", scale, pad)
            if pad["used_slot_count"] and pad["color_id"] == 0:
                finding("active_pad_color_zero", scale, pad)
            if pad["side"] == "chord":
                off_scale = sorted({note["midi"] % 12 for note in pad["notes"]} - scale_pcs)
                if off_scale:
                    finding("chord_contains_pitch_outside_melody_set", scale, pad,
                            pitch_classes=[PITCH_CLASSES[n] for n in off_scale])
        for column in range(8):
            column_pads = melody[column * 4:column * 4 + 4]
            if all(pad["note_status"] == "null" for pad in column_pads):
                continue
            if any(pad["used_slot_count"] != 1 for pad in column_pads):
                finding("melody_column_not_four_single_notes", scale, column=column)
                continue
            midi = [pad["notes"][0]["midi"] for pad in column_pads]
            if any(a - b != 12 for a, b in zip(midi, midi[1:])):
                finding("melody_column_octave_spacing", scale, column=column, midi=midi)
            if len({pad["color_id"] for pad in column_pads}) != 1:
                finding("melody_column_color_changes", scale, column=column)
    active_midi = [note["midi"] for pad in pads for note in pad["notes"]]
    voicings = {}
    for pad in pads:
        if pad["side"] == "chord" and pad["used_slot_count"]:
            voicings.setdefault(pad["name"], set()).add(tuple(note["midi"] for note in pad["notes"]))
    multiple = {name: sorted(notes) for name, notes in sorted(voicings.items()) if len(notes) > 1}
    return {"structural_validation": "passed", "source": archive["source"],
            "counts": {"categories": len(archive["categories"]), "groups": len(archive["groups"]),
                       "scales": len(archive["scales"]), "pad_records": len(pads),
                       "chord_pads": sum(p["side"] == "chord" for p in pads),
                       "melody_pads": sum(p["side"] == "melody" for p in pads),
                       "null_pads": sum(p["note_status"] == "null" for p in pads),
                       "original_chord_names": len(voicings),
                       "original_chord_name_and_voicing_pairs": sum(map(len, voicings.values())),
                       "opaque_regions": len(archive["opaque_regions"])},
            "midi_range": [min(active_midi), max(active_midi)] if active_midi else None,
            "color_id_counts": {side: dict(sorted(Counter(p["color_id"] for p in pads if p["side"] == side).items()))
                                for side in ("chord", "melody")},
            "table_ranges": {"categories": [0, archive["groups"][0]["offset"]],
                             "groups": [archive["groups"][0]["offset"], archive["scales"][0]["offset"]],
                             "scales": [archive["scales"][0]["offset"], archive["scales"][-1]["offset"] + DIRECTORY_SIZE],
                             "pad_blocks": [[s["target_offset"], s["target_offset"] + BLOCK_SIZE] for s in archive["scales"]]},
            "opaque_regions": [{k: v for k, v in r.items() if k != "data_hex"} for r in archive["opaque_regions"]],
            "auxiliary_tokens": sorted({s["auxiliary_token"] for s in archive["scales"]}),
            "names_with_multiple_voicings": {"count": len(multiple), "examples": dict(list(multiple.items())[:20])},
            "musical_observations": {"counts": dict(sorted(anomalies.items())), "examples": samples,
                                     "policy": "observations only; original data is never corrected"},
            "limits": ["No RGB palette has been identified; numeric IDs alone cannot recreate exact LED colors",
                       "serialized positions are retained; physical row orientation and wiring need hardware confirmation",
                       "16 right-side hardware pads have no records here; no assignments are invented",
                       "auxiliary E6 fields and opaque tail are preserved without assigning a meaning",
                       "historical JSON has lost octaves, names, pad identity, colors and categories"]}


def _safe_output(path, inputs=()):
    path = Path(path).resolve()
    protected = ((ROOT / "Resources").resolve(), (ROOT / ".local-archive").resolve())
    if any(path.is_relative_to(folder) for folder in protected) or path in {Path(p).resolve() for p in inputs}:
        raise ValueError("output must not overwrite original inputs or write inside an archive directory")
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    reader = commands.add_parser("import", help="validate and create a lossless JSON archive")
    reader.add_argument("dump", type=Path, nargs="?", default=env_path("THY666_CHORD_DUMP"),
                        help="local raw musical-data dump; defaults to THY666_CHORD_DUMP")
    reader.add_argument("--output-dir", type=Path,
                        help="output folder; overrides the THY666_CHORD_ARCHIVE destination")
    reader.add_argument("--legacy", type=Path, help="compare the historical scales.json export")
    reader.add_argument("--hexdump", type=Path, help="verify the full SCALES.hex listing against the binary")
    writer = commands.add_parser("export", help="reconstruct and verify an archived dump")
    writer.add_argument("archive", type=Path)
    writer.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "export":
            output = _safe_output(args.output, (args.archive,))
            archive = json.loads(args.archive.read_text(encoding="utf-8"))
            data = export_dump(archive)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
            print(f"Reconstructed {len(data):,} bytes; source hashes and all metadata match: {output}")
            return 0
        if args.dump is None:
            raise ValueError("Supply a musical-data dump path or set THY666_CHORD_DUMP")
        inputs = [p for p in (args.dump, args.legacy, args.hexdump) if p is not None]
        archive_output = (args.output_dir / "chords.lossless.json" if args.output_dir is not None
                          else env_path("THY666_CHORD_ARCHIVE", LOCAL_INPUTS / "chords.lossless.json"))
        paths = [_safe_output(path, inputs) for path in
                 (archive_output, archive_output.parent / "validation.json", archive_output.parent / "roundtrip.bin")]
        data = args.dump.read_bytes()
        archive = import_dump(data)
        roundtrip = export_dump(archive)
        if roundtrip != data:
            raise ValueError("byte-for-byte reconstruction failed")
        report = validation_report(archive)
        report["lossless_byte_roundtrip"] = True
        success = True
        if args.legacy:
            report["legacy_comparison"] = compare_legacy(archive, json.loads(args.legacy.read_text(encoding="utf-8")))
            success &= report["legacy_comparison"]["all_compared_fields_match"]
        if args.hexdump:
            listing = read_hexdump(args.hexdump)
            match = listing == data
            report["hexdump_comparison"] = {"byte_for_byte_match": match, "source": _hashes(listing)}
            success &= match
        if args.dump.read_bytes() != data:
            raise ValueError("source dump changed during import")
        paths[0].parent.mkdir(parents=True, exist_ok=True)
        paths[0].write_text(json.dumps(archive, separators=(",", ":")) + "\n", encoding="utf-8")
        paths[1].write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        paths[2].write_bytes(roundtrip)
        print(f"Imported {report['counts']['scales']} scales, {report['counts']['pad_records']:,} pad records; byte roundtrip passed.")
        print(f"SHA-256: {archive['source']['sha256']}")
        print(f"Validation report: {paths[1]}")
        if not success:
            print("Historical export/hexdump comparison differs; inspect validation.json.")
        return 0 if success else 1
    except (ValueError, OSError) as error:
        parser.exit(2, f"Error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
