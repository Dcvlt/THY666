"""Offline .333 parsing and deterministic protocol regression tests."""

import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from thy333_container import HEADER_FIELDS, crc32_normal
from thy333_container import SIGNATURE, TranscriptReader, command_payload, draft_recipe, inspect_container, integer_payload, validate_transcript, write_report


def container_fixture(app=None, *, encrypted=0, filesystem=0, scales=0, trailing=b"", bad_crc=False):
    if app is None:
        app = struct.pack("<II", 0x20020000, 0x08040009) + bytes(range(61))
    values = dict(firmware_enabled=1, scales_enabled=scales, filesystem_prepare_enabled=filesystem,
                  firmware_start=0, firmware_end=len(app), metadata_word_0=crc32_normal(app) ^ int(bad_crc),
                  metadata_word_1=0x429B51D6, metadata_word_2=0xAA017D8C,
                  required_hardware_version=4, firmware_encrypted=encrypted, scales_encrypted=0)
    header = b"THY333" + b"".join(f"{values[name]:0{width}X}".encode() for _, width, name in HEADER_FIELDS)
    return header + app + trailing


def transcript_fixture(container):
    from thy333_container import parse_update_container
    parsed = parse_update_container(container)
    values = parsed["values"]
    reports = []
    def report(direction, raw):
        reports.append({"direction": direction, "data_hex": (b"\0" + raw if direction == "tx" else raw).hex()})
    def cmd(direction, token):
        report(direction, command_payload(token))
    def number(direction, value):
        report(direction, integer_payload(value))
    if values["filesystem_prepare_enabled"]:
        cmd("tx", "XCFIFCFF"); cmd("rx", "FILSTATU"); number("rx", 0); cmd("rx", "SFIFDONE")
    cmd("tx", "XCFILNFS"); cmd("tx", "DUMPMETA"); cmd("rx", "FWMETABG")
    number("rx", 4)
    for i in range(14):
        number("rx", i)
    cmd("rx", "FWMETADN"); cmd("tx", "DONECMDS")
    cmd("tx", "XCSMLRTF" if values["firmware_encrypted"] else "XCSMDIUS")
    cmd("rx", "FWSETRD1")
    for n in (values["metadata_word_0"], values["metadata_word_1"], parsed["application_bytes"], values["metadata_word_2"], 1, 0, 1):
        number("tx", n)
    cmd("rx", "FWSETRD2")
    start, end = parsed["application_file_range"]
    app = container[start:end]
    for offset in range(0, len(app), 64):
        report("tx", app[offset:offset + 64].ljust(64, b"\0"))
    cmd("tx", "DONE0000"); cmd("rx", "TRACRCVL"); number("rx", crc32_normal(app))
    cmd("rx", "EXTCRCVL"); number("rx", crc32_normal(app)); cmd("rx", "FWDLYRDY")
    if values["firmware_encrypted"]:
        cmd("rx", "DCRCRCVL"); number("rx", values["metadata_word_1"]); cmd("rx", "FWDCPRDY")
    for _ in range(5):
        cmd("tx", "ZCSMCCOZ")
    return {"format": "thy333.hid-transcript", "version": 1, "reports": reports}


class ContainerTests(unittest.TestCase):
    def test_crc_known_nonreflected_vectors(self):
        self.assertEqual(crc32_normal(b"123456789"), 0x89A1897F)
        self.assertEqual(crc32_normal(b"123456789", 0xFFFFFFFF), 0x0376E6E7)
        self.assertEqual(crc32_normal(b"", 0x12345678), 0x12345678)

    def test_draft_recipe_leaves_unverified_words_unset_and_emits_no_container(self):
        app = struct.pack("<II", 0x20020000, 0x08040009) + bytes(range(61))
        result = draft_recipe(app)
        self.assertFalse(result["container_emitted"])
        self.assertFalse(result["flash_ready"])
        self.assertIsNone(result["header_recipe"]["metadata_word_1"])
        self.assertIsNone(result["header_recipe"]["metadata_word_2"])
        self.assertEqual(result["header_recipe"]["filesystem_prepare_enabled"], 0)
        self.assertEqual(result["header_recipe"]["scales_enabled"], 0)
        self.assertTrue(all(value is False for value in result["gates"].values()))
        with self.assertRaises(ValueError):
            draft_recipe(struct.pack("<II", 0x20020000, 0x08050001))

    def test_crc_known_independent_vector(self):
        self.assertEqual(crc32_normal(b"123456789"), 0x89A1897F)
        self.assertEqual(len(command_payload("FWSETRD1")), 64)

    def test_inspection_keeps_bounded_fields_and_marks_unready(self):
        raw = container_fixture()
        result = inspect_container(raw)
        self.assertEqual(result["application_bytes"], 69)
        self.assertEqual(result["unencrypted_updater_overread_bytes"], 59)
        self.assertTrue(result["header_word_0_matches_crc"])
        self.assertTrue(result["plaintext_vector"]["reset_in_candidate_application"])
        self.assertFalse(result["flash_ready"])

    def test_malformed_header_and_application_ranges_rejected(self):
        good = container_fixture()
        for broken in (good[:77], b"BAD333" + good[6:], good[:6] + b"??????" + good[12:],
                       good[:30] + b"FFFFFF" + good[36:]):
            with self.subTest(broken=broken[:40]), self.assertRaises(ValueError):
                inspect_container(broken)

    def test_combined_bounds_and_mutation_warning(self):
        result = inspect_container(container_fixture(filesystem=1, scales=1, trailing=b"SCALE"))
        self.assertEqual(result["scale_bytes"], 5)
        self.assertTrue(any("before" in issue for issue in result["issues"]))
        self.assertTrue(any("Scale" in issue for issue in result["issues"]))

    def test_slot_membership_does_not_claim_reset_is_in_short_payload(self):
        raw = container_fixture(app=struct.pack("<II", 0x20020000, 0x08080001) + bytes(56))
        vector = inspect_container(raw)["plaintext_vector"]
        self.assertFalse(vector["reset_in_candidate_application"])
        self.assertTrue(vector["reset_in_candidate_application_slot"])

    def test_encoded_decoded_crc_disagreement_is_explicit(self):
        raw = container_fixture(encrypted=1)
        trace = transcript_fixture(raw)
        marker = command_payload("DCRCRCVL").hex()
        index = next(i for i, item in enumerate(trace["reports"]) if item["data_hex"] == marker) + 1
        good = validate_transcript(raw, trace)
        self.assertTrue(good["verification_passed"])
        self.assertTrue(good["decoded_crc_matches_header_word_1"])
        trace["reports"][index]["data_hex"] = integer_payload(0xDEADBEEF).hex()
        bad = validate_transcript(raw, trace)
        self.assertTrue(bad["sequence_consistent"])
        self.assertTrue(bad["crc_replies_match_calculated_payload"])
        self.assertFalse(bad["decoded_crc_matches_header_word_1"])
        self.assertFalse(bad["verification_passed"])

    def test_plain_and_encrypted_complete_sequences(self):
        for encrypted in (0, 1):
            for filesystem in (0, 1):
                raw = container_fixture(encrypted=encrypted, filesystem=filesystem)
                result = validate_transcript(raw, transcript_fixture(raw))
                self.assertTrue(result["sequence_consistent"])
                self.assertTrue(result["crc_replies_match_calculated_payload"])
                self.assertFalse(result["device_flash_contents_verified"])
                self.assertFalse(result["flash_ready"])

    def test_metadata_marker_tail_is_not_a_recovered_signature_check(self):
        raw = container_fixture()
        trace = transcript_fixture(raw)
        tail = bytes(range(56))
        for item in trace["reports"]:
            if item["direction"] == "rx" and bytes.fromhex(item["data_hex"])[:8] in (b"FWMETABG", b"FWMETADN"):
                item["data_hex"] = (bytes.fromhex(item["data_hex"])[:8] + tail).hex()
        report = validate_transcript(raw, trace)
        self.assertTrue(report["verification_passed"])
        self.assertFalse(report["metadata_marker_signature_established"])
        self.assertEqual([marker["opaque_tail_hex"] for marker in report["metadata_markers"]], [tail.hex()] * 2)
        self.assertTrue(all(not marker["tail_matches_known_signature"] for marker in report["metadata_markers"]))
        original = validate_transcript(raw, transcript_fixture(raw))
        self.assertTrue(all(marker["tail_matches_known_signature"] for marker in original["metadata_markers"]))
        trace["reports"][2]["data_hex"] = (b"FWMETXBG" + tail).hex()
        with self.assertRaisesRegex(ValueError, "metadata marker"):
            validate_transcript(raw, trace)

    def test_metadata_error_marker_also_preserves_opaque_tail(self):
        tail = b"\xff" * 56
        reader = TranscriptReader({"format": "thy333.hid-transcript", "version": 1,
                                   "reports": [{"direction": "rx", "data_hex": (b"FWMETAER" + tail).hex()}]})
        marker = reader.expect_metadata_marker("FWMETAER")
        self.assertEqual(marker["opaque_tail_hex"], tail.hex())
        self.assertFalse(marker["tail_matches_known_signature"])

    def test_bad_signature_direction_length_and_report_id_rejected(self):
        raw = container_fixture()
        original = transcript_fixture(raw)
        variants = []
        for change in (dict(direction="rx"), dict(data_hex="00"),
                       dict(data_hex="01" + original["reports"][0]["data_hex"][2:]),
                       dict(data_hex=original["reports"][0]["data_hex"][:-2] + "00")):
            variant = copy.deepcopy(original)
            variant["reports"][0].update(change)
            variants.append(variant)
        for variant in variants:
            with self.assertRaises(ValueError):
                validate_transcript(raw, variant)

    def test_hardware_mismatch_and_truncated_extra_sequences_rejected(self):
        raw = container_fixture()
        trace = transcript_fixture(raw)
        wrong_hardware = copy.deepcopy(trace)
        wrong_hardware["reports"][3]["data_hex"] = integer_payload(5).hex()
        extra = copy.deepcopy(trace)
        extra["reports"].append(extra["reports"][-1])
        missing = copy.deepcopy(trace)
        missing["reports"].pop()
        for broken in (wrong_hardware, extra, missing):
            with self.assertRaises(ValueError):
                validate_transcript(raw, broken)

    def test_application_count_corruption_and_tail_overread_rejected(self):
        raw = container_fixture()
        trace = transcript_fixture(raw)
        # Seven integer parameters start after FWSETRD1, before FWSETRD2.
        header_marker = command_payload("FWSETRD1").hex()
        first = next(i for i, item in enumerate(trace["reports"]) if item["data_hex"] == header_marker) + 1
        count = copy.deepcopy(trace)
        count["reports"][first + 2]["data_hex"] = (b"\0" + integer_payload(1000)).hex()
        last_block_index = next(i for i, item in enumerate(trace["reports"]) if item["data_hex"] == (b"\0" + command_payload("DONE0000")).hex()) - 1
        tail = copy.deepcopy(trace)
        tail["reports"][last_block_index]["data_hex"] = tail["reports"][last_block_index]["data_hex"][:-2] + "ff"
        for broken in (count, tail):
            with self.assertRaises(ValueError):
                validate_transcript(raw, broken)

    def test_crc_disagreement_is_explicit_and_never_flash_ready(self):
        raw = container_fixture(bad_crc=True)
        self.assertFalse(inspect_container(raw)["header_word_0_matches_crc"])
        trace = transcript_fixture(raw)
        marker = command_payload("EXTCRCVL").hex()
        index = next(i for i, item in enumerate(trace["reports"]) if item["data_hex"] == marker) + 1
        trace["reports"][index]["data_hex"] = integer_payload(0).hex()
        report = validate_transcript(raw, trace)
        self.assertFalse(report["crc_replies_match_calculated_payload"])
        self.assertFalse(report["flash_ready"])

    def test_report_output_never_overwrites_sources_or_existing_reports(self):
        temporary_root = ROOT / "build" / "offline-test-temp"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as tmp:
            root = Path(tmp)
            source = root / "source.json"
            source.write_text("original")
            for output in (source, root / "Resources" / "report.json", root / "draft.333"):
                with self.assertRaises(ValueError):
                    write_report(output, {}, [source])
            target = root / "result.json"
            write_report(target, {"offline_only": True}, [source])
            with self.assertRaises(FileExistsError):
                write_report(target, {}, [])
            self.assertEqual(source.read_text(), "original")
            self.assertEqual(json.loads(target.read_text()), {"offline_only": True})


if __name__ == "__main__":
    unittest.main()
