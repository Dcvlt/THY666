import copy
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from chord_dump import (
    LOCAL_INPUTS, ROOT, _safe_output, compare_legacy, export_dump,
    import_dump, read_hexdump, validation_report,
)


def directory(text, suffix=b"\0\xaa\xbb"):
    encoded = text.encode("ascii") + suffix
    return encoded + b"\0" * (256 - len(encoded))


def pad(notes=(), color=0, name="NULL", suffix=b"\0\xfe"):
    slots = list(notes) + [0] * (7 - len(notes))
    header = ("".join(f"{n:02d}" for n in slots) + f"{color:02d}").encode("ascii")
    body = name.encode("ascii")
    if len(body) < 16:
        body += suffix[:16 - len(body)]
    return header + body + b"\0" * (16 - len(body))


def fixture():
    # Two scales; arbitrary bytes after NULs, between tables, and in the tail.
    data = bytearray(b"\xff" * (0x1900 + 19))
    data[:256] = directory("Category;0100;01")
    data[0x100:0x200] = directory("Group;0200;02")
    data[0x200:0x300] = directory("Group Ab;000500;E6aB00;00")
    data[0x300:0x400] = directory("Group A;000f00;E6aB00;01")
    data[0x400:0x410] = bytes(range(16))
    for block in (0x500, 0xF00):
        for index in range(80):
            data[block + index * 32:block + (index + 1) * 32] = pad()
        data[block:block + 32] = pad([57, 61, 64], 8, "Dbm6#5/A")
        data[block + 32:block + 64] = pad([57, 61, 76], 3, "Dbm6#5/A")
        data[block + 96:block + 128] = pad([99], 7, "X" * 16)
        for row, midi in enumerate((69, 57, 45, 33)):
            start = block + (48 + row) * 32
            data[start:start + 32] = pad([midi], 1, "A")
    data[0x1900:] = b"opaque\0trailer\xff\x01\x02\x03\x04\x05"
    return bytes(data)


def replace_directory(data, offset, text):
    return data[:offset] + directory(text) + data[offset + 256:]


class LosslessImportTests(unittest.TestCase):
    def setUp(self):
        self.data = fixture()
        self.archive = import_dump(self.data)

    def test_reconstructs_structured_records_padding_gaps_and_tail(self):
        loaded = json.loads(json.dumps(self.archive))
        self.assertEqual(export_dump(loaded), self.data)
        self.assertEqual(self.archive["categories"][0]["count_token"], "01")
        self.assertEqual(self.archive["scales"][1]["target_token"], "000f00")
        self.assertEqual(self.archive["scales"][0]["auxiliary_token"], "E6aB00")
        self.assertEqual(len(self.archive["opaque_regions"]), 2)
        self.assertTrue(self.archive["scales"][0]["pads"][0]["suffix_hex"].startswith("00fe"))

    def test_retains_original_enharmonic_name_octaves_and_color_token(self):
        original = self.archive["scales"][0]["pads"][0]
        self.assertEqual(original["name"], "Dbm6#5/A")
        self.assertEqual(original["midi_slots"], [57, 61, 64, 0, 0, 0, 0])
        self.assertEqual([(n["midi"], n["pitch_class"], n["octave"]) for n in original["notes"]],
                         [(57, "A", 3), (61, "C#", 4), (64, "E", 4)])
        self.assertEqual((original["color_token"], original["color_id"]), ("08", 8))

    def test_same_name_different_voicing_keeps_separate_pad_records(self):
        pads = self.archive["scales"][0]["pads"]
        self.assertEqual(pads[0]["name"], pads[1]["name"])
        self.assertNotEqual(pads[0]["midi_slots"], pads[1]["midi_slots"])
        self.assertEqual(pads[2]["note_status"], "null")
        self.assertEqual(pads[3]["index"], 3)
        self.assertEqual(pads[3]["name"], "X" * 16)
        self.assertEqual(len(pads), 80)

    def test_preserves_serialized_column_and_descending_octave_order(self):
        melody = self.archive["scales"][0]["pads"][48:]
        self.assertEqual([p["notes"][0]["midi"] for p in melody[:4]], [69, 57, 45, 33])
        self.assertEqual([(p["column"], p["row_within_column"]) for p in melody[:5]],
                         [(0, 0), (0, 1), (0, 2), (0, 3), (1, 0)])
        self.assertEqual(melody[-1]["side_index"], 31)
        self.assertEqual(melody[-1]["index"], 79)

    def test_reads_pad_blocks_through_declared_pointers(self):
        data = replace_directory(self.data, 0x200, "Group Ab;000f00;E6aB00;00")
        data = replace_directory(data, 0x300, "Group A;000500;E6aB00;01")
        archive = import_dump(data)
        self.assertEqual(archive["scales"][0]["pads"][0]["offset"], 0xF00)
        self.assertEqual(archive["scales"][1]["pads"][0]["offset"], 0x500)
        self.assertEqual(export_dump(archive), data)

    def test_rejects_out_of_bounds_and_misaligned_pointers(self):
        for text in ("Group Ab;000501;E60000;0", "Group Ab;000300;E60000;0",
                     "Group Ab;ffffff;E60000;0"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                import_dump(replace_directory(self.data, 0x200, text))
        with self.assertRaises(ValueError):
            import_dump(replace_directory(self.data, 0, "Category;0101;1"))

    def test_rejects_truncated_records_and_bad_decimal_headers(self):
        for data in (self.data[:10], self.data[:0x1800], self.data[:0x500] + b"Z" + self.data[0x501:]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                import_dump(data)

    def test_rejects_child_ranges_outside_directory_and_shared_blocks(self):
        with self.assertRaises(ValueError):
            import_dump(replace_directory(self.data, 0, "Category;0100;2"))
        with self.assertRaises(ValueError):
            import_dump(replace_directory(self.data, 0x300, "Group A;000500;E60000;1"))

    def test_export_rejects_gaps_overlaps_and_altered_bytes(self):
        for change in ("gap", "overlap", "bytes", "hash"):
            archive = copy.deepcopy(self.archive)
            if change == "gap":
                archive["opaque_regions"].pop()
            elif change == "overlap":
                archive["opaque_regions"].append(copy.deepcopy(archive["opaque_regions"][0]))
            elif change == "bytes":
                archive["scales"][0]["pads"][0]["midi_slots"][0] = 58
            else:
                archive["source"]["sha256"] = "0" * 64
            with self.subTest(change=change), self.assertRaises(ValueError):
                export_dump(archive)

    def test_export_rejects_inconsistent_derived_metadata(self):
        for field, value in (("column", 1), ("color_id", 9), ("index", False), ("note_status", "null")):
            archive = copy.deepcopy(self.archive)
            archive["scales"][0]["pads"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                export_dump(archive)
        archive = copy.deepcopy(self.archive)
        archive["scales"][0]["pads"][0]["notes"][0]["octave"] = 4
        with self.assertRaises(ValueError):
            export_dump(archive)

    def test_ambiguous_zero_slots_are_retained_and_flagged(self):
        block = 0x500
        data = self.data[:block] + pad([0, 60], 1, "C") + pad([], 1, "C-1") + self.data[block + 64:]
        archive = import_dump(data)
        pads = archive["scales"][0]["pads"]
        self.assertEqual(pads[0]["note_status"], "ambiguous_zero_in_prefix")
        self.assertEqual(pads[0]["midi_slots"][:2], [0, 60])
        self.assertEqual(pads[1]["note_status"], "ambiguous_all_zero")
        self.assertEqual(pads[1]["notes"], [])
        counts = validation_report(archive)["musical_observations"]["counts"]
        self.assertEqual(counts["ambiguous_zero_in_prefix"], 1)
        self.assertEqual(counts["ambiguous_all_zero"], 1)
        self.assertEqual(export_dump(archive), data)

    def test_canonical_fill_regions_and_metadata_are_isolated(self):
        data = bytearray(self.data)
        data[0x400:0x500] = b"\xff" * 256
        archive = import_dump(bytes(data))
        self.assertEqual(archive["opaque_regions"][0]["fill_byte"], 255)
        self.assertEqual(export_dump(archive), bytes(data))
        archive["conventions"]["melody_grid"]["stored_pads"] = 999
        self.assertEqual(import_dump(self.data)["conventions"]["melody_grid"]["stored_pads"], 32)

    def test_output_guard_protects_resources_and_the_input_file(self):
        with self.assertRaises(ValueError):
            _safe_output(ROOT / "Resources/new.bin")
        with self.assertRaises(ValueError):
            _safe_output(ROOT / ".local-archive/snapshot/new.bin")
        path = ROOT / "build/input.json"
        with self.assertRaises(ValueError):
            _safe_output(path, [path])
        self.assertEqual(_safe_output(ROOT / "build/new.bin"), ROOT / "build/new.bin")


class HexdumpTests(unittest.TestCase):
    def test_full_listing_and_addresses(self):
        text = "00000000  " + " ".join(f"{n:02x}" for n in range(16)) + "  |................|\n00000010\n"
        with patch.object(Path, "open", return_value=io.StringIO(text)):
            self.assertEqual(read_hexdump("unused.hex"), bytes(range(16)))
        for broken in (text.replace("00000000", "00000001"), text.replace("00000010", "00000011"),
                       text.replace("0a", "zz"), text.splitlines()[0] + "\n"):
            with patch.object(Path, "open", return_value=io.StringIO(broken)), self.assertRaises(ValueError):
                read_hexdump("unused.hex")


@unittest.skipUnless((LOCAL_INPUTS / "THYMEMBK.bin").exists(), "local raw musical dataset not available")
class RecoveredDatasetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = (LOCAL_INPUTS / "THYMEMBK.bin").read_bytes()
        cls.archive = import_dump(cls.data)

    def test_full_dump_json_archive_reconstruction_and_anchor_notes(self):
        self.assertEqual(self.archive["source"]["sha256"],
                         "93111f5ab9e87921b3a00c8d594572f15b77a087f705cfd996a66fd67cafad9a")
        self.assertEqual(export_dump(json.loads(json.dumps(self.archive))), self.data)
        self.assertEqual((len(self.archive["categories"]), len(self.archive["groups"]), len(self.archive["scales"])),
                         (10, 70, 840))
        first = self.archive["scales"][0]
        self.assertEqual([p["midi"] for p in first["pads"][0]["notes"]], [57, 61, 64])
        self.assertEqual(first["pads"][6]["name"], "Dbm6#5/A")
        self.assertEqual([p["notes"][0]["midi"] for p in first["pads"][48:52]], [69, 57, 45, 33])
        self.assertEqual([s["name"].rsplit(" ", 1)[-1] for s in self.archive["scales"][:12]],
                         ["A", "Ab", "B", "Bb", "C", "D", "Db", "E", "Eb", "F", "G", "Gb"])
        self.assertEqual(self.archive["scales"][-1]["target_offset"], 0x27C600)

    def test_historical_export_agrees_and_comparison_detects_changed_notes(self):
        path = LOCAL_INPUTS / "scales.json"
        if not path.exists():
            self.skipTest("optional comparison export unavailable")
        legacy = json.loads(path.read_text(encoding="utf-8"))
        report = compare_legacy(self.archive, legacy)
        self.assertTrue(report["all_compared_fields_match"])
        self.assertEqual(report["chord_records_compared"], 880)
        self.assertEqual(report["scale_variants_compared"], 840)
        self.assertEqual(report["legacy_chord_warning_count"], 190)
        legacy["Chords"][0]["notes"][0] = "C"
        report = compare_legacy(self.archive, legacy)
        self.assertFalse(report["all_compared_fields_match"])
        self.assertEqual(report["mismatch_count"], 1)

    def test_hexdump_is_an_independent_exact_copy(self):
        path = LOCAL_INPUTS / "SCALES.hex"
        if not path.exists():
            self.skipTest("optional comparison hexdump unavailable")
        self.assertEqual(read_hexdump(path), self.data)

    def test_data_observations_retain_anomalies_instead_of_fixing_them(self):
        report = validation_report(self.archive)
        self.assertEqual(report["counts"]["pad_records"], 67200)
        self.assertEqual(report["counts"]["null_pads"], 4176)
        self.assertEqual(report["counts"]["original_chord_name_and_voicing_pairs"], 1061)
        self.assertEqual(report["color_id_counts"]["melody"][8], 192)
        self.assertEqual(report["midi_range"], [24, 82])
        findings = report["musical_observations"]
        self.assertEqual(findings["counts"]["melody_column_octave_spacing"], 1)
        self.assertEqual(sum(e["kind"] == "melody_column_octave_spacing" for e in findings["examples"]), 1)
        major_d = self.archive["scales"][257]
        self.assertEqual(major_d["name"], "Major D")
        self.assertEqual([p["notes"][0]["midi"] for p in major_d["pads"][56:60]], [66, 55, 42, 30])
        self.assertEqual(major_d["pads"][57]["name"], "F#/Gb")
        self.assertEqual(major_d["pads"][57]["offset"], 0x111120)


if __name__ == "__main__":
    unittest.main()
