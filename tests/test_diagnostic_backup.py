"""Meaningful corruption/coverage regressions for offline backup validation."""

import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from verify_thy333_backup import APPLICATION_OFFSET, EXTERNAL_BYTES, INTERNAL_BYTES, validate_backup


class BackupTests(unittest.TestCase):
    def setUp(self):
        temporary_root = ROOT / "build" / "offline-test-temp"
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=temporary_root)
        self.root = Path(self.temp.name)
        internal = bytearray(b"\xff" * INTERNAL_BYTES)
        struct.pack_into("<II", internal, 0, 0x20020000, 0x08000101)
        struct.pack_into("<II", internal, APPLICATION_OFFSET, 0x20020000, 0x08040101)
        external = bytes(range(256)) * (EXTERNAL_BYTES // 256)
        self.acquisition = {"format": "thy333.backup-acquisition", "version": 1,
                            "independent_reads_attested": True,
                            "board": {"markings_physically_confirmed": True, "mcu_marking": "STM32F405VGT6",
                                      "flash_marking": "W25Q128JV", "pcb_marking": "THY333 ECHO SUMMIT"}, "reads": []}
        for n in (1, 2):
            read = {"id": f"read-{n}", "captured_at": f"2026-10-14T11:0{n}:00Z", "method": "swd-read-only",
                    "identity": {"uid_hex": "0123456789abcdef01234567", "device_id": "0x413", "internal_flash_kib": 1024,
                                 "external_jedec_id_hex": "ef4018", "rdp_raw": "0xaa", "rdp_level": 0}}
            for name, raw in (("internal", internal), ("external", external), ("option_bytes_raw", b"options"),
                              ("identity_log", f"Read {n} chip identity".encode()), ("session_log", f"Read {n} acquired full image".encode())):
                file = self.root / f"{name}-{n}.bin"
                file.write_bytes(raw)
                read[name] = file.name
            self.acquisition["reads"].append(read)
        self.manifest = self.root / "acquisition.json"
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        self.manifest.write_text(json.dumps(self.acquisition))

    def test_complete_matching_reads_with_hashes_and_vectors(self):
        before = (self.root / "internal-1.bin").read_bytes()
        report = validate_backup(self.manifest)
        self.assertTrue(report["backup_bytes_verified"])
        self.assertFalse(report["restoration_verified"])
        self.assertFalse(report["flash_ready"])
        self.assertEqual(len(report["regions"]["internal_sectors"]), 12)
        self.assertEqual(len(report["regions"]["external_64k_blocks"]), 256)
        self.assertEqual(report["regions"]["lower_internal_region"]["bytes"], 0x40000)
        self.assertEqual((self.root / "internal-1.bin").read_bytes(), before)

    def test_ccm_stack_is_valid_even_though_ccm_is_not_dma_memory(self):
        for n in (1, 2):
            path = self.root / f"internal-{n}.bin"
            raw = bytearray(path.read_bytes())
            struct.pack_into("<I", raw, 0, 0x10010000)
            path.write_bytes(raw)
        self.assertTrue(validate_backup(self.manifest)["backup_bytes_verified"])

    def test_jv_im_jm_variant_identity_is_preserved(self):
        for read in self.acquisition["reads"]:
            read["identity"]["external_jedec_id_hex"] = "ef7018"
        self.save()
        report = validate_backup(self.manifest)
        self.assertEqual(report["sessions"][0]["identity"]["external_jedec_id_hex"], "ef7018")

    def test_recovered_chord_region_is_not_complete_external_backup(self):
        (self.root / "external-1.bin").write_bytes(b"\x01" * 2621440)
        with self.assertRaisesRegex(ValueError, "all 16 MiB"):
            validate_backup(self.manifest)

    def test_application_only_dump_does_not_preserve_bootloader(self):
        (self.root / "internal-1.bin").write_bytes((self.root / "internal-1.bin").read_bytes()[APPLICATION_OFFSET:])
        with self.assertRaisesRegex(ValueError, "1 MiB"):
            validate_backup(self.manifest)

    def test_corrupt_internal_external_or_option_bytes_rejected(self):
        for name in ("internal", "external", "option_bytes_raw"):
            path = self.root / f"{name}-2.bin"
            original = path.read_bytes()
            damaged = bytearray(original)
            damaged[-1] ^= 1
            path.write_bytes(damaged)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "reads differ"):
                validate_backup(self.manifest)
            path.write_bytes(original)

    def test_mismatched_board_identity_rejected(self):
        self.acquisition["reads"][1]["identity"]["uid_hex"] = "1123456789abcdef01234567"
        self.save()
        with self.assertRaisesRegex(ValueError, "identity/protection evidence differs"):
            validate_backup(self.manifest)

    def test_protected_unknown_or_wrong_target_rejected_without_unlock(self):
        good = copy.deepcopy(self.acquisition)
        for key, value in (("rdp_level", 1), ("rdp_raw", "0xbb"), ("device_id", "0x999"), ("internal_flash_kib", 512),
                           ("external_jedec_id_hex", "ef4017")):
            self.acquisition = copy.deepcopy(good)
            self.acquisition["reads"][0]["identity"][key] = value
            self.save()
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_backup(self.manifest)

    def test_shared_files_same_session_or_no_independence_attestation_rejected(self):
        good = copy.deepcopy(self.acquisition)
        changes = (("id", "read-1"), ("captured_at", "2026-10-14T11:01:00Z"), ("internal", "internal-1.bin"))
        for key, value in changes:
            self.acquisition = copy.deepcopy(good)
            self.acquisition["reads"][1][key] = value
            self.save()
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_backup(self.manifest)
        self.acquisition = good
        self.acquisition["independent_reads_attested"] = False
        self.save()
        with self.assertRaises(ValueError):
            validate_backup(self.manifest)

    def test_invalid_vectors_and_blank_external_bus_reads_rejected(self):
        internal_path = self.root / "internal-1.bin"
        original = internal_path.read_bytes()
        for offset, stack, reset in ((0, 0xffffffff, 0x08000101), (0, 0x20020000, 0x08000100),
                                     (APPLICATION_OFFSET, 0x20020000, 0x08000101)):
            raw = bytearray(original)
            struct.pack_into("<II", raw, offset, stack, reset)
            internal_path.write_bytes(raw)
            with self.assertRaises(ValueError):
                validate_backup(self.manifest)
        internal_path.write_bytes(original)
        for fill in (0, 255):
            (self.root / "external-1.bin").write_bytes(bytes([fill]) * EXTERNAL_BYTES)
            with self.assertRaisesRegex(ValueError, "not a credible"):
                validate_backup(self.manifest)

    def test_assumed_markings_cannot_be_reported_as_confirmed_backup(self):
        self.acquisition["board"]["markings_physically_confirmed"] = False
        self.save()
        with self.assertRaisesRegex(ValueError, "physical"):
            validate_backup(self.manifest)


if __name__ == "__main__":
    unittest.main()
