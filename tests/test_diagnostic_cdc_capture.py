"""CDC wire evidence regressions with synthetic packets, no live port access."""

import hashlib
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from analyze_diagnostic_cdc_capture import analyze_cdc


def usb(payload=b"", *, irp=1, endpoint=2, transfer=3, complete=False, status=0):
    hlen = 28 if transfer == 2 else 27
    header = struct.pack("<HQIHBHHBBI", hlen, irp, status, 9, int(complete), 7, 11, endpoint, transfer, len(payload))
    stage = bytes([3 if complete else 0]) if transfer == 2 else b""
    return header + stage + payload


def pcap(packets):
    return struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 249) + b"".join(struct.pack("<4I", 1700000000, i * 1000, len(p), len(p)) + p for i, p in enumerate(packets))


def descriptor():
    return usb(bytes.fromhex("1201000200000040feca0940000101020301"), irp=0, endpoint=0x80, transfer=2, complete=True)


def dtr(value, irp):
    return [usb(struct.pack("<BBHHH", 0x21, 0x22, value, 0, 0), irp=irp, endpoint=0, transfer=2),
            usb(irp=irp, endpoint=0, transfer=2, complete=True)]


def tx(payload, irp, status=0):
    return [usb(payload, irp=irp), usb(irp=irp, complete=True, status=status)]


def rx(payload, irp):
    return [usb(irp=irp, endpoint=0x82), usb(payload, irp=irp, endpoint=0x82, complete=True)]


class CdcCaptureTests(unittest.TestCase):
    def test_split_lines_first_help_and_reopen(self):
        packets = [descriptor()] + dtr(0, 2) + tx(b"\xc0", 3) + dtr(1, 4)
        packets += rx(b"HELLO THY", 5) + rx(b"DIAG/1 read-only\r\n", 6) + tx(b"he", 7) + tx(b"lp\n", 8)
        packets += rx(b"OK THYDIAG/1 commands=help,status,i2c\r\n", 9) + tx(b"i2c\n", 10) + rx(b"I2C attempted=0000000f\r\nBANK phase=address-write\r\n", 11)
        packets += dtr(0, 12) + dtr(1, 13) + rx(b"HELLO THYDIAG/1 reopened\r\n", 14) + tx(b"help\n", 15) + rx(b"OK THYDIAG/1 commands=help,i2c\r\n", 16)
        report = analyze_cdc(pcap(packets), bus=7, device=11)
        self.assertEqual(report["asserted_session_count"], 2)
        self.assertEqual([r["first_help_ok_observed"] for r in report["first_help_results"]], [True, True])
        asserted = [r for r in report["sessions"] if r["dtr_asserted"]]
        self.assertEqual(len(asserted[0]["greeting_lines"]), 1)
        self.assertEqual(asserted[0]["commands"][0]["text"], "help")
        self.assertEqual(len(asserted[0]["commands"][0]["packet_indices"]), 2)
        self.assertEqual(len(asserted[0]["i2c_lines"]), 2)
        self.assertEqual(report["sessions"][0]["tx_partial_tail"]["payload_hex"], "c0")

    def test_failed_out_preserved_but_not_command(self):
        report = analyze_cdc(pcap([descriptor()] + dtr(1, 2) + tx(b"help\n", 3, status=0xc0000001)), bus=7, device=11)
        self.assertEqual(len(report["payload_bearing_bulk_records"]), 1)
        self.assertEqual(report["first_help_results"], [])

    def test_old_error_is_not_reclassified_as_success(self):
        packets = [descriptor()] + dtr(1, 2) + rx(b"HELLO THYDIAG/1\r\n", 3) + tx(b"help\n", 4) + rx(b"ERR line-too-long\r\n", 5)
        report = analyze_cdc(pcap(packets), bus=7, device=11)
        self.assertFalse(report["first_help_results"][0]["first_help_ok_observed"])
        self.assertEqual(report["sessions"][-1]["error_lines"][0]["text"], "ERR line-too-long")

    def test_descriptor_and_provenance_binding_required(self):
        with self.assertRaisesRegex(ValueError, "CAFE"):
            analyze_cdc(pcap(tx(b"help\n", 1)), bus=7, device=11)
        raw = pcap([descriptor()])
        with self.assertRaisesRegex(ValueError, "hash"):
            analyze_cdc(raw, bus=7, device=11, provenance={"output_sha256": "wrong", "selected_packet_count": 1})
        provenance = {"output_sha256": hashlib.sha256(raw).hexdigest(), "selected_packet_count": 1,
                      "source_packet_indices": [10], "source_file_offsets": [20], "source_sha256": "source"}
        self.assertEqual(analyze_cdc(raw, bus=7, device=11, provenance=provenance)["source_capture_sha256_from_provenance"], "source")


if __name__ == "__main__":
    unittest.main()
