"""Offline package bounds, explicit assumptions, framing and artifact preservation."""

import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from thy333_container import crc32_normal, parse_update_container
from package_plaintext_candidate import APP_BASE, APP_BYTES_MAX, CANDIDATE_NAME, build_candidate, encode_header, main, write_bundle


def application(size=512):
    data = bytearray(size)
    struct.pack_into("<II", data, 0, 0x20020000, APP_BASE + 393)
    return bytes(data)


def options(**changes):
    return {"metadata_word_1": 0x429B51D6, "metadata_word_2": 0xAA017D8C,
            "metadata_note": "Explicit opaque metadata fixture; meaning for this payload unverified",
            "filesystem_prepare": 0, "hardware_version": 4, **changes}


class CandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "build").mkdir(parents=True, exist_ok=True)

    def test_exact_header_and_parameters_round_trip_with_all_bytes_accounted(self):
        source = application()
        raw, padded, report = build_candidate(source, **options())
        self.assertEqual(raw[:36], b"THY333000001000000000000000000000200")
        self.assertEqual(raw[44:78], b"429B51D6AA017D8C000004000000000000")
        self.assertEqual(raw[78:], source)
        parsed = parse_update_container(raw)
        self.assertEqual(parsed["application_file_range"], [78, len(raw)])
        self.assertEqual(parsed["application_crc32_normal_zero_init"], hex(crc32_normal(source)))
        self.assertEqual(parsed["scale_bytes"], 0)
        self.assertEqual(report["host_transfer"]["seven_parameters_u32"],
                         [crc32_normal(source), 0x429B51D6, 512, 0xAA017D8C, 1, 0, 1])
        self.assertEqual(report["host_transfer"]["data_reports"], 8)
        self.assertFalse(report["flash_ready"])
        self.assertFalse(report["device_execution_ready"])
        self.assertFalse(report["device_compatibility_verified"])
        self.assertEqual(report["container"]["byte_regions_end_exclusive"]["scale_tail"], [590, 590])

    def test_padding_is_explicit_inside_application_and_changes_crc_and_length(self):
        source = application(513)
        with self.assertRaisesRegex(ValueError, "explicitly select"):
            build_candidate(source, **options())
        raw, padded, report = build_candidate(source, padding="ff-to-report", **options())
        self.assertEqual(padded, source + b"\xff" * 63)
        self.assertEqual(parse_update_container(raw)["values"]["firmware_end"], 576)
        self.assertEqual(report["padding"]["bytes_added"], 63)
        self.assertEqual(report["packaged_application"]["crc32_normal_zero_init"], f"0x{crc32_normal(padded):08X}")
        self.assertEqual(report["original_application"]["bytes"], 513)
        self.assertEqual(report["host_transfer"]["host_final_overread_bytes"], 0)
        self.assertFalse(report["padding"]["device_padding_rule_verified"])

    def test_padding_cannot_rescue_reset_pointing_past_original_end(self):
        source = bytearray(application(513))
        struct.pack_into("<I", source, 4, APP_BASE + 515)
        with self.assertRaisesRegex(ValueError, "original application"):
            build_candidate(bytes(source), padding="ff-to-report", **options())
        # A vector to the last single byte also lacks a complete Thumb halfword.
        with self.assertRaisesRegex(ValueError, "original application"):
            build_candidate(application(393), padding="ff-to-report", **options())

    def test_all_nonzero_vectors_must_be_thumb_inside_original_code(self):
        for index, value in ((1, 0), (1, APP_BASE + 392), (2, APP_BASE + 1),
                             (20, 0x08003043), (97, APP_BASE + 513)):
            source = bytearray(application())
            struct.pack_into("<I", source, index * 4, value)
            with self.subTest(index=index, value=value), self.assertRaisesRegex(ValueError, "Vector"):
                build_candidate(bytes(source), **options())

    def test_stack_and_candidate_size_are_bounded(self):
        for stack in (0x20000000, 0x20020008, 0x2001FFFF, 0x10010008):
            source = bytearray(application())
            struct.pack_into("<I", source, 0, stack)
            with self.subTest(stack=stack), self.assertRaisesRegex(ValueError, "stack"):
                build_candidate(bytes(source), **options())
        for source in (b"", bytes(391), bytes(APP_BYTES_MAX + 1), bytearray(application())):
            with self.subTest(size=len(source)), self.assertRaises(ValueError):
                build_candidate(source, **options())
        # The last permitted raw length can be aligned without crossing the slot.
        raw, padded, report = build_candidate(application(APP_BYTES_MAX - 1), padding="ff-to-report", **options())
        self.assertEqual(len(padded), APP_BYTES_MAX)
        self.assertEqual(report["padding"]["bytes_added"], 1)

    def test_metadata_and_preparation_are_explicit_and_typed(self):
        for change in ({"metadata_word_1": True}, {"metadata_word_2": -1},
                       {"metadata_word_2": 2**32}, {"hardware_version": 3},
                       {"hardware_version": True}, {"filesystem_prepare": False},
                       {"filesystem_prepare": 2}, {"metadata_note": "  "},
                       {"scales_encrypted": True}, {"scales_encrypted": 2},
                       {"padding": "zero"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                build_candidate(application(), **options(**change))
        for value in (0, 0xFFFFFFFF):
            raw, _, report = build_candidate(application(), **options(metadata_word_1=value, metadata_word_2=value))
            self.assertEqual(parse_update_container(raw)["values"]["metadata_word_1"], value)
        enabled = build_candidate(application(), **options(filesystem_prepare=1))[2]
        self.assertTrue(enabled["host_transfer"]["filesystem_prepare_enabled"])
        self.assertFalse(enabled["hypotheses"]["filesystem_preparation_compatibility_verified"])

    def test_inactive_scale_selector_is_preserved_without_enabling_scale_updates(self):
        default, _, default_report = build_candidate(application(), **options())
        matching, _, report = build_candidate(application(), scales_encrypted=1, **options())
        self.assertEqual(default[78:], matching[78:])
        self.assertEqual(default[:72], matching[:72])
        self.assertEqual(default[72:78], b"000000")
        self.assertEqual(matching[72:78], b"000001")
        self.assertEqual(parse_update_container(matching)["values"]["scales_enabled"], 0)
        self.assertEqual(parse_update_container(matching)["scale_bytes"], 0)
        self.assertEqual(default_report["host_transfer"]["inactive_scale_encryption_selector"], 0)
        self.assertEqual(report["host_transfer"]["inactive_scale_encryption_selector"], 1)
        self.assertFalse(report["host_transfer"]["scale_update_enabled"])

    def test_word_zero_hypotheses_are_distinct_and_not_device_requirements(self):
        raw, _, report = build_candidate(application(), word_0_policy="explicit-hypothesis",
                                         metadata_word_0=0xEBF3A7F7, **options())
        self.assertEqual(parse_update_container(raw)["values"]["metadata_word_0"], 0xEBF3A7F7)
        self.assertFalse(report["hypotheses"]["word_0"]["matches_payload_crc"])
        self.assertEqual(report["host_transfer"]["seven_parameters_u32"][0], 0xEBF3A7F7)
        self.assertFalse(report["hypotheses"]["word_0"]["device_enforcement_verified"])
        for change in ({"word_0_policy": "unknown"}, {"metadata_word_0": 1},
                       {"word_0_policy": "explicit-hypothesis"},
                       {"word_0_policy": "explicit-hypothesis", "metadata_word_0": True}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                build_candidate(application(), **options(**change))

    def test_synthetic_host_fixture_distinguishes_structural_and_crc_hypotheses(self):
        from test_thy333_container import transcript_fixture
        from thy333_container import validate_transcript
        raw = build_candidate(application(), **options())[0]
        good = validate_transcript(raw, transcript_fixture(raw))
        self.assertTrue(good["verification_passed"])
        self.assertFalse(good["device_flash_contents_verified"])
        legacy = build_candidate(application(), word_0_policy="explicit-hypothesis",
                                 metadata_word_0=0xEBF3A7F7, **options())[0]
        result = validate_transcript(legacy, transcript_fixture(legacy))
        self.assertTrue(result["sequence_consistent"])
        self.assertTrue(result["crc_replies_match_calculated_payload"])
        self.assertFalse(result["verification_passed"])

    def test_header_encoder_rejects_missing_extra_overwide_and_boolean_fields(self):
        values = parse_update_container(build_candidate(application(), **options())[0])["values"]
        for field, value in (("metadata_word_0", 2**32), ("firmware_end", 2**24), ("firmware_enabled", True)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                encode_header({**values, field: value})
        with self.assertRaises(ValueError):
            encode_header({**values, "extra": 1})
        with self.assertRaises(ValueError):
            encode_header({key: value for key, value in values.items() if key != "firmware_end"})

    def test_new_bundle_preserves_inputs_and_is_deterministic(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            source = root / "source.bin"
            source.write_bytes(application(513))
            first = write_bundle(root / "first", source, padding="ff-to-report", **options())
            second = write_bundle(root / "second", source, padding="ff-to-report", **options())
            self.assertEqual((root / "first" / CANDIDATE_NAME).read_bytes(), (root / "second" / CANDIDATE_NAME).read_bytes())
            self.assertEqual(first, second)
            self.assertEqual(source.read_bytes(), application(513))
            self.assertTrue(first["source_bytes_unchanged_after_packaging"])
            self.assertTrue(json.loads((root / "first/manifest.json").read_text())["bundle_complete"])
            self.assertEqual(sorted(p.name for p in (root / "first").iterdir()),
                             sorted([CANDIDATE_NAME, "application.aligned.bin", "manifest.json"]))

    def test_existing_resources_input_collisions_and_resolved_aliases_are_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            source = root / "input.bin"
            source.write_bytes(application())
            existing = root / "existing"
            existing.mkdir()
            for target in (existing, source, root, root / "ReSoUrCeS" / "candidate"):
                with self.subTest(target=target), self.assertRaises((ValueError, FileExistsError)):
                    write_bundle(target, source, **options())
            resources = root / "Resources"
            resources.mkdir()
            alias = root / "alias"
            try:
                alias.symlink_to(resources, target_is_directory=True)
            except OSError:
                pass  # Windows may require a symlink privilege; lexical guard ran.
            else:
                with self.assertRaisesRegex(ValueError, "Resources"):
                    write_bundle(alias / "candidate", source, **options())
            self.assertEqual(source.read_bytes(), application())

    def test_reference_is_comparison_only_and_all_its_regions_are_reported(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            source = root / "input.bin"
            source.write_bytes(application())
            ref_raw = build_candidate(application(), **options(filesystem_prepare=1, metadata_word_1=99))[0]
            values = parse_update_container(ref_raw)["values"]
            values.update(firmware_start=3, firmware_end=515)
            reference = root / "reference.333"
            reference.write_bytes(encode_header(values) + b"PRE" + ref_raw[78:] + b"TAIL")
            original = reference.read_bytes()
            report = write_bundle(root / "candidate", source, reference_path=reference, **options())
            self.assertEqual(reference.read_bytes(), original)
            self.assertEqual(report["reference_comparison"]["reference_skipped_prefix_bytes"], 3)
            self.assertEqual(report["reference_comparison"]["reference_scale_bytes"], 4)
            self.assertFalse(report["reference_comparison"]["reference_prefix_and_tail_copied"])
            candidate = parse_update_container((root / "candidate" / CANDIDATE_NAME).read_bytes())
            self.assertEqual(candidate["values"]["metadata_word_1"], 0x429B51D6)
            self.assertEqual(candidate["values"]["filesystem_prepare_enabled"], 0)
            self.assertEqual(candidate["scale_bytes"], 0)
            self.assertEqual(candidate["values"]["firmware_start"], 0)

    def test_source_drift_keeps_partial_evidence_without_completed_manifest(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            source = (root / "source.bin").resolve()
            source.write_bytes(application())
            original_read = Path.read_bytes
            reads = 0

            def changed_read(path):
                nonlocal reads
                if path == source:
                    reads += 1
                    if reads == 2:
                        return b"changed source"
                return original_read(path)

            with mock.patch.object(Path, "read_bytes", changed_read), self.assertRaisesRegex(OSError, "input changed"):
                write_bundle(root / "candidate", source, **options())
            self.assertTrue((root / "candidate" / CANDIDATE_NAME).is_file())
            self.assertFalse((root / "candidate/manifest.json").exists())
            self.assertEqual(source.read_bytes(), application())

    def test_cli_requires_hypotheses_and_emits_only_offline_candidate(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            source = root / "input.bin"
            source.write_bytes(application())
            args = [str(source), "--output", str(root / "candidate")]
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                main(args)
            args += ["--metadata-word-1", "0", "--metadata-word-2", "0", "--metadata-note", "Placeholders, unverified",
                     "--word-0-policy", "payload-crc", "--hardware-version", "4", "--filesystem-prepare", "0"]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main(args), 0)
                self.assertEqual(main(args), 1)
            self.assertTrue((root / "candidate" / CANDIDATE_NAME).is_file())
            self.assertFalse(any((root / "candidate").glob("*.333")))


if __name__ == "__main__":
    unittest.main()
