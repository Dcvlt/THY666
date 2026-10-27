import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_runtime_data import (  # noqa: E402
    DEFAULT_ARCHIVE, PAD_FIELDS, RECOVERED_SHA256,
    build_file, materialize_pad, project_archive, safe_output,
)
from chord_dump import import_dump  # noqa: E402


def directory(text):
    encoded = text.encode("ascii")
    return encoded + b"\0" * (256 - len(encoded))


def pad(notes=(), color="00", name="NULL"):
    slots = list(notes) + [0] * (7 - len(notes))
    encoded = ("".join(f"{note:02d}" for note in slots) + color + name).encode("ascii")
    return encoded + b"\0" * (32 - len(encoded))


def fixture():
    data = bytearray(b"\xff" * 0x1900)
    data[:0x100] = directory("Original Category;0100;01")
    data[0x100:0x200] = directory("Original Group;0200;02")
    data[0x200:0x300] = directory("Original Ab;000500;E6aB00;00")
    data[0x300:0x400] = directory("Original A;000f00;E6aB00;01")
    for block in (0x500, 0xF00):
        for index in range(80):
            data[block + index * 32:block + (index + 1) * 32] = pad()
        data[block:block + 32] = pad((57, 61, 64), "08", "Dbm6#5/A")
        data[block + 32:block + 64] = pad((57, 61, 76), "03", "Dbm6#5/A")
        for row, pitch in enumerate((69, 57, 45, 33)):
            start = block + (48 + row) * 32
            data[start:start + 32] = pad((pitch,), "01", "A")
    return bytes(data)


def project(archive):
    raw = json.dumps(archive).encode("utf-8")
    return project_archive(archive, archive_sha256=hashlib.sha256(raw).hexdigest(), archive_size_bytes=len(raw))


class RuntimeProjectionTests(unittest.TestCase):
    def setUp(self):
        self.archive = import_dump(fixture())
        self.runtime = project(self.archive)

    def test_schema_metadata_and_original_directory_tokens(self):
        self.assertEqual((self.runtime["format"], self.runtime["version"]), ("thy333.runtime-chords", 1))
        self.assertEqual(self.runtime["categories"][0]["name"], "Original Category")
        self.assertEqual(self.runtime["categories"][0]["count_token"], "01")
        self.assertEqual(self.runtime["groups"][0]["root_order"], ["Ab", "A"])
        self.assertEqual(self.runtime["groups"][0]["scale_indices"], [0, 1])
        self.assertEqual(self.runtime["scales"][0]["variant_token"], "00")
        self.assertEqual(self.runtime["scales"][0]["pad_block_token"], "000500")
        self.assertEqual(self.runtime["scales"][0]["auxiliary_token"], "E6aB00")
        self.assertEqual(self.runtime["provenance"]["source"], self.archive["source"])
        self.assertEqual(self.runtime["provenance"]["reconstructed_sha256"], self.archive["source"]["sha256"])
        self.assertEqual(set(self.runtime["provenance"]), {
            "source", "archive", "validation", "reconstructed_sha256",
            "data_policy", "distribution_policy",
        })

    def test_all_required_pad_values_survive_deduplication(self):
        first = materialize_pad(self.runtime, 0, 0)
        second = materialize_pad(self.runtime, 0, 1)
        same_definition = materialize_pad(self.runtime, 1, 0)
        self.assertEqual(first["name"], "Dbm6#5/A")
        self.assertEqual(first["name"], second["name"])
        self.assertNotEqual(first["midi_slots"], second["midi_slots"])
        self.assertEqual(first["midi_slots"], [57, 61, 64, 0, 0, 0, 0])
        self.assertEqual([(n["slot"], n["midi"], n["pitch_class"], n["octave"]) for n in first["notes"]],
                         [(0, 57, "A", 3), (1, 61, "C#", 4), (2, 64, "E", 4)])
        self.assertEqual((first["color_token"], first["color_id"]), ("08", 8))
        self.assertEqual((first["source_offset"], same_definition["source_offset"]), (0x500, 0xF00))
        self.assertEqual(self.runtime["scales"][0]["pads"][0][1], self.runtime["scales"][1]["pads"][0][1])
        first["notes"][0]["midi"] = 1
        self.assertEqual(materialize_pad(self.runtime, 0, 0)["notes"][0]["midi"], 57)

    def test_layout_uses_serialized_indices_and_explicit_unassigned_cells(self):
        for scale in self.runtime["scales"]:
            self.assertEqual(len(scale["pads"]), 96)
            self.assertTrue(all(item is None for item in scale["pads"][80:]))
        melody = [materialize_pad(self.runtime, 0, index) for index in range(48, 53)]
        self.assertEqual([(p["column"], p["row_within_column"]) for p in melody],
                         [(0, 0), (0, 1), (0, 2), (0, 3), (1, 0)])
        self.assertEqual([p["notes"][0]["midi"] for p in melody[:4]], [69, 57, 45, 33])
        self.assertEqual(materialize_pad(self.runtime, 0, 79)["side_index"], 31)
        absent = materialize_pad(self.runtime, 0, 95)
        self.assertEqual((absent["side"], absent["side_index"], absent["column"], absent["row_within_column"]),
                         ("melody", 47, 11, 3))
        self.assertEqual(absent["note_status"], "unassigned")
        self.assertFalse(absent["assigned"])
        self.assertIsNone(absent["source_offset"])
        self.assertIsNone(absent["color_id"])
        self.assertEqual(absent["notes"], [])
        stored_null = materialize_pad(self.runtime, 0, 2)
        self.assertTrue(stored_null["assigned"])
        self.assertEqual(stored_null["note_status"], "null")

    def test_archival_tamper_and_malformed_metadata_are_rejected(self):
        for field, value in (("midi_slots", [58, 61, 64, 0, 0, 0, 0]), ("color_id", 9),
                             ("used_slot_count", 7), ("column", 10), ("note_status", "null")):
            archive = copy.deepcopy(self.archive)
            archive["scales"][0]["pads"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                project(archive)
        archive = copy.deepcopy(self.archive)
        archive["source"]["sha256"] = "0" * 64
        with self.assertRaises(ValueError):
            project(archive)
        with self.assertRaises(ValueError):
            project({})

    def test_ambiguous_zero_slots_are_rejected_instead_of_guessing(self):
        for notes, name in (((0, 60), "C"), ((), "C-1")):
            raw = bytearray(fixture())
            raw[0x500:0x520] = pad(notes, "01", name)
            archive = import_dump(bytes(raw))
            self.assertTrue(archive["scales"][0]["pads"][0]["note_status"].startswith("ambiguous"))
            with self.subTest(notes=notes), self.assertRaisesRegex(ValueError, "ambiguous note slots"):
                project(archive)

    def test_projection_provenance_validation_and_index_bounds(self):
        for digest, size in (("x" * 64, 1), ("A" * 64, 1), ("0" * 63, 1), ("0" * 64, False)):
            with self.subTest(digest=digest, size=size), self.assertRaises(ValueError):
                project_archive(self.archive, archive_sha256=digest, archive_size_bytes=size)
        for scale_index, pad_index in ((-1, 0), (2, 0), (False, 0), (0, -1), (0, 96), (0, True)):
            with self.subTest(scale=scale_index, pad=pad_index), self.assertRaises(ValueError):
                materialize_pad(self.runtime, scale_index, pad_index)

    def test_output_guard_protects_resource_and_archive_directories(self):
        for destination in (ROOT / "Resources/changed.json", DEFAULT_ARCHIVE,
                            DEFAULT_ARCHIVE.parent / "new.json", DEFAULT_ARCHIVE.parent / "sub/new.json",
                            ROOT / ".local-archive/snapshot/new.json"):
            with self.subTest(destination=destination), self.assertRaises(ValueError):
                safe_output(destination)
        archive = ROOT / "build/alternate-archive/source.json"
        with self.assertRaises(ValueError):
            safe_output(archive.parent / "another.json", archive)
        self.assertEqual(safe_output(ROOT / "web/generated/test.json"), ROOT / "web/generated/test.json")

    def test_file_build_is_deterministic_and_retains_input_bytes(self):
        scratch = ROOT / "build/test-runtime-data"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            directory_path = Path(temporary)
            source = directory_path / "source/archive.json"
            output = directory_path / "output/runtime.json"
            source.parent.mkdir()
            raw = json.dumps(self.archive, indent=2).encode("utf-8")
            source.write_bytes(raw)
            first = build_file(source, output)
            first_bytes = output.read_bytes()
            second = build_file(source, output)
            self.assertEqual(first, second)
            self.assertEqual(first_bytes, output.read_bytes())
            self.assertEqual(source.read_bytes(), raw)
            generated = json.loads(first_bytes)
            self.assertEqual(generated["provenance"]["archive"]["sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(generated["provenance"]["archive"]["size_bytes"], len(raw))


@unittest.skipUnless(DEFAULT_ARCHIVE.exists(), "ignored recovered chord archive unavailable")
class RecoveredRuntimeProjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = DEFAULT_ARCHIVE.read_bytes()
        cls.archive = json.loads(cls.raw)
        cls.runtime = project_archive(cls.archive, archive_sha256=hashlib.sha256(cls.raw).hexdigest(),
                                      archive_size_bytes=len(cls.raw))

    def test_full_corpus_counts_and_original_root_order(self):
        self.assertEqual(self.runtime["counts"], {"categories": 10, "groups": 70, "scales": 840,
                                               "stored_pads": 67200, "hardware_pad_cells": 80640,
                                               "unassigned_pads": 13440, "unique_pad_records": 7818,
                                               "active_pads": 63024, "null_pads": 4176})
        self.assertEqual(self.runtime["provenance"]["source"]["sha256"], RECOVERED_SHA256)
        self.assertEqual(self.runtime["provenance"]["archive"]["sha256"], hashlib.sha256(self.raw).hexdigest())
        for group in self.runtime["groups"]:
            self.assertEqual(group["root_order"], ["A", "Ab", "B", "Bb", "C", "D", "Db", "E", "Eb", "F", "G", "Gb"])

    def test_every_original_pad_preserves_notes_voicing_layout_and_color(self):
        for scale in self.archive["scales"]:
            for original in scale["pads"]:
                resolved = materialize_pad(self.runtime, scale["index"], original["index"])
                self.assertEqual({key: resolved[key] for key in PAD_FIELDS},
                                 {key: original[key] for key in PAD_FIELDS})
                self.assertEqual(resolved["source_offset"], original["offset"])
                self.assertEqual((resolved["index"], resolved["side"], resolved["side_index"],
                                  resolved["column"], resolved["row_within_column"]),
                                 (original["index"], original["side"], original["side_index"],
                                  original["column"], original["row_within_column"]))
            self.assertEqual(self.runtime["scales"][scale["index"]]["pads"][80:], [None] * 16)

    def test_source_pitch_anomaly_is_retained_without_reinterpreting_its_label(self):
        major_d = materialize_pad(self.runtime, 257, 57)
        self.assertEqual(major_d["name"], "F#/Gb")
        self.assertEqual(major_d["source_offset"], 0x111120)
        self.assertEqual(major_d["notes"], [{"slot": 0, "midi": 55, "pitch_class": "G", "octave": 3}])

    def test_source_scale_intervals_are_retained(self):
        for name, expected in (("Hirajoshi", [0, 2, 3, 7, 8]),
                               ("Neapolitan Major", [0, 1, 3, 5, 7, 9, 11])):
            group = next(item for item in self.runtime["groups"] if item["name"] == name)
            scale = next(self.runtime["scales"][index] for index in group["scale_indices"]
                         if self.runtime["scales"][index]["root_label"] == "C")
            pitch_classes = {note["midi"] % 12 for index in range(48, 80)
                             for note in materialize_pad(self.runtime, scale["index"], index)["notes"]}
            self.assertEqual(sorted(pitch_classes), expected, name)


if __name__ == "__main__":
    unittest.main()
