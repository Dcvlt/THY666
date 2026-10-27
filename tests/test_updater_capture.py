"""Offline transfer reconstruction regressions; no capture or device APIs."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from decode_updater_capture import decode_capture, read_capture


def usb(payload=b'', *, irp=1, complete=False, endpoint=1, device=24,
        status=0, transfer=1, stage=0):
    length = 28 if transfer == 2 else 27
    header = struct.pack('<HQIHBHHBBI', length, irp, status, 9, int(complete),
                         2, device, endpoint, transfer, len(payload))
    return header + (bytes([stage]) if transfer == 2 else b'') + payload


def pcap(packets, *, timestamp_step=1000):
    output = struct.pack('<IHHIIII', 0xA1B2C3D4, 2, 4, 0, 0, 65535, 249)
    for index, packet in enumerate(packets):
        output += struct.pack('<4I', 1700000000, index * timestamp_step, len(packet), len(packet)) + packet
    return output


def ng_block(kind, body, endian='<'):
    size = 12 + len(body)
    return struct.pack(endian + 'II', kind, size) + body + struct.pack(endian + 'I', size)


def ng_option(code, value, endian='<'):
    return struct.pack(endian + 'HH', code, len(value)) + value + bytes((-len(value)) % 4)


def pcapng(packets, *, dropcount=None, stats=None, endian='<', extra_interface=False):
    raw = ng_block(0x0A0D0D0A, struct.pack(endian + 'IHHq', 0x1A2B3C4D, 1, 0, -1), endian)
    raw += ng_block(1, struct.pack(endian + 'HHI', 249, 0, 65535), endian)
    if extra_interface:
        raw += ng_block(1, struct.pack(endian + 'HHI', 1, 0, 65535), endian)
    for index, packet in enumerate(packets):
        opts = b'' if dropcount is None else ng_option(4, struct.pack(endian + 'Q', dropcount), endian)
        raw += ng_block(6, struct.pack(endian + '5I', 0, 0, 1234 + index, len(packet), len(packet)) +
                        packet + bytes((-len(packet)) % 4) + opts, endian)
    if stats:
        interface, counters = stats
        opts = b''.join(ng_option(code, struct.pack(endian + 'Q', count), endian) for code, count in counters)
        raw += ng_block(5, struct.pack(endian + '3I', interface, 0, 4321) + opts, endian)
    return raw


def decode(packets, **kwargs):
    return decode_capture(pcap(packets), bus=2, device=24, out_endpoint=1,
                          in_endpoint=0x81, **kwargs)


class CaptureTests(unittest.TestCase):
    def test_completed_transfers_preserve_order_and_wire_bytes(self):
        tx, rx = bytes(range(64)), bytes(reversed(range(64)))
        result = decode([usb(tx), usb(irp=2, endpoint=0x81),
                         usb(rx, irp=2, endpoint=0x81, complete=True), usb(complete=True)])
        reports = result['transcript']['reports']
        self.assertEqual([r['direction'] for r in reports], ['tx', 'rx'])
        self.assertEqual(bytes.fromhex(reports[0]['data_hex']), b'\0' + tx)
        self.assertEqual(bytes.fromhex(reports[1]['data_hex']), rx)
        self.assertEqual(reports[0]['source_packet_indices'], [0, 3])
        self.assertEqual(reports[0]['wire_payload_hex'], tx.hex())
        self.assertFalse(result['transcript']['backup_acquired'])

    def test_repeated_completion_is_an_ambiguity_not_a_second_report(self):
        completion = usb(complete=True)
        result = decode([usb(bytes(64)), completion, completion])
        self.assertEqual(len(result['transcript']['reports']), 1)
        self.assertIn('ambiguous_repeated_completion', [i['code'] for i in result['issues']])
        self.assertFalse(result['transcript']['paired_selected_transfers_complete'])
        self.assertEqual(len(result['selected_packets']), 3)

    def test_equal_timestamps_do_not_prove_duplicate_record_identity(self):
        completion = usb(complete=True)
        result = decode_capture(pcap([usb(bytes(64)), completion, completion], timestamp_step=0),
                                bus=2, device=24, out_endpoint=1, in_endpoint=0x81)
        self.assertFalse(result['transcript']['paired_selected_transfers_complete'])
        self.assertNotEqual(result['selected_packets'][1]['file_offset'], result['selected_packets'][2]['file_offset'])

    def test_repeated_pending_submission_invalidates_the_exchange(self):
        submission = usb(bytes(64))
        result = decode([submission, submission, usb(complete=True)])
        self.assertEqual(result['transcript']['reports'], [])
        self.assertIn('ambiguous_repeated_submission', [i['code'] for i in result['issues']])
        self.assertEqual(len(result['selected_packets']), 3)

    def test_irp_reuse_after_completion_preserves_both_transfers(self):
        result = decode([usb(bytes(64)), usb(complete=True),
                         usb(bytes([7]) * 64), usb(complete=True)])
        self.assertEqual(len(result['transcript']['reports']), 2)
        self.assertEqual(result['issues'], [])

    def test_failed_or_missing_completion_never_becomes_success(self):
        failed = decode([usb(bytes(64)), usb(complete=True, status=0xC0000001)])
        missing = decode([usb(bytes(64))])
        for result in (failed, missing):
            self.assertEqual(result['transcript']['reports'], [])
            self.assertFalse(result['transcript']['paired_selected_transfers_complete'])

    def test_other_devices_and_endpoints_are_not_application_reports(self):
        result = decode([usb(bytes(64), device=25), usb(complete=True, device=25),
                         usb(bytes(64), irp=3, endpoint=2),
                         usb(irp=3, endpoint=2, complete=True)])
        self.assertEqual(result['transcript']['reports'], [])
        self.assertEqual(len(result['selected_packets']), 2)

    def test_short_wire_data_is_preserved_without_padding(self):
        result = decode([usb(b'abcd'), usb(complete=True)])
        self.assertEqual(result['selected_packets'][0]['payload_hex'], '61626364')
        self.assertEqual(result['transcript']['reports'], [])
        self.assertIn('unexpected_wire_report', [i['code'] for i in result['issues']])

    def test_report_id_requires_explicit_wire_contract(self):
        packets = [usb(b'\0' + bytes(64)), usb(complete=True)]
        self.assertEqual(decode(packets)['transcript']['reports'], [])
        self.assertEqual(len(decode(packets, wire_zero_id=True)['transcript']['reports']), 1)

    def test_control_set_report_needs_exact_interface_opt_in(self):
        setup = struct.pack('<BBHHH', 0x21, 9, 0x0200, 0, 64)
        packets = [usb(setup + bytes(64), transfer=2, endpoint=0),
                   usb(transfer=2, endpoint=0, stage=3, complete=True)]
        self.assertEqual(decode(packets)['transcript']['reports'], [])
        self.assertEqual(len(decode(packets, hid_interface=0)['transcript']['reports']), 1)
        self.assertEqual(decode(packets, hid_interface=1)['transcript']['reports'], [])

    def test_legacy_control_set_report_pairs_data_and_status(self):
        setup = struct.pack('<BBHHH', 0x21, 9, 0x0200, 0, 64)
        result = decode([usb(setup, transfer=2, endpoint=0),
                         usb(bytes(64), transfer=2, endpoint=0, stage=1),
                         usb(transfer=2, endpoint=0, stage=2, complete=True)], hid_interface=0)
        self.assertEqual(len(result['transcript']['reports']), 1)
        self.assertEqual(result['transcript']['reports'][0]['source_packet_indices'], [0, 1, 2])
        self.assertTrue(result['transcript']['paired_selected_transfers_complete'])

    def test_control_terminal_stage_and_info_are_required(self):
        setup = struct.pack('<BBHHH', 0x21, 9, 0x0200, 0, 64)
        for stage, complete in ((0, True), (2, False), (3, False), (4, True)):
            with self.subTest(stage=stage, complete=complete):
                result = decode([usb(setup + bytes(64), transfer=2, endpoint=0),
                                 usb(transfer=2, endpoint=0, stage=stage, complete=complete)], hid_interface=0)
                self.assertEqual(result['transcript']['reports'], [])
                self.assertIn('invalid_control_stage', [i['code'] for i in result['issues']])
                self.assertFalse(result['transcript']['paired_selected_transfers_complete'])

    def test_control_setup_requires_eight_bytes_and_endpoint_zero(self):
        setup = struct.pack('<BBHHH', 0x21, 9, 0x0200, 0, 64)
        for payload, endpoint, expected in ((b'\x21\x09', 0, 'invalid_control_setup'),
                                            (setup + bytes(64), 1, 'invalid_control_endpoint')):
            with self.subTest(expected=expected):
                result = decode([usb(payload, transfer=2, endpoint=endpoint),
                                 usb(transfer=2, endpoint=endpoint, stage=3, complete=True)], hid_interface=0)
                self.assertEqual(result['transcript']['reports'], [])
                self.assertIn(expected, [i['code'] for i in result['issues']])

    def test_legacy_out_control_data_cannot_be_a_completion(self):
        setup = struct.pack('<BBHHH', 0x21, 9, 0x0200, 0, 64)
        result = decode([usb(setup, transfer=2, endpoint=0),
                         usb(bytes(64), transfer=2, endpoint=0, stage=1, complete=True),
                         usb(transfer=2, endpoint=0, stage=2, complete=True)], hid_interface=0)
        self.assertEqual(result['transcript']['reports'], [])
        self.assertIn('invalid_control_data_direction', [i['code'] for i in result['issues']])

    def test_legacy_in_control_data_failure_is_not_hidden_by_status_stage(self):
        setup = struct.pack('<BBHHH', 0xA1, 1, 0x0100, 0, 64)
        result = decode([usb(bytes(64), irp=2), usb(irp=2, complete=True),
                         usb(setup, transfer=2, endpoint=0x80),
                         usb(bytes(64), transfer=2, endpoint=0x80, stage=1, complete=True, status=0xC0000001),
                         usb(transfer=2, endpoint=0x80, stage=2, complete=True)], hid_interface=0)
        self.assertEqual(len(result['transcript']['reports']), 1)
        self.assertIn('failed_control_data', [i['code'] for i in result['issues']])
        self.assertFalse(result['transcript']['paired_selected_transfers_complete'])

    def test_complete_pair_does_not_claim_full_capture_completeness(self):
        result = decode([usb(bytes(64)), usb(complete=True)])
        self.assertTrue(result['transcript']['paired_selected_transfers_complete'])
        self.assertTrue(result['transcript']['full_capture_completeness_unproven'])
        self.assertNotIn('capture_transport_complete', result['transcript'])

    def test_inventory_or_no_reports_cannot_claim_pairing_completeness(self):
        packets = [usb(bytes(64)), usb(complete=True)]
        for result in (decode_capture(pcap(packets)), decode_capture(pcap(packets), bus=2, device=24),
                       decode_capture(pcap(packets), bus=2, device=25, out_endpoint=1)):
            self.assertFalse(result['transcript']['paired_selected_transfers_complete'])
            self.assertTrue(result['transcript']['full_capture_completeness_unproven'])

    def test_structural_truncation_is_rejected(self):
        raw = pcap([usb(bytes(64))])
        with self.assertRaises(ValueError):
            read_capture(raw[:-1])
        with self.assertRaises(ValueError):
            read_capture(raw[:20])

    def test_pcapng_enhanced_packet_timestamp_and_payload(self):
        packet = usb(bytes(64))
        raw = pcapng([packet])
        record = read_capture(raw)['records'][0]
        self.assertEqual(record['timestamp_ns'], 1234000)
        self.assertEqual(record['packet'], packet)
        with self.assertRaises(ValueError):
            read_capture(raw[:-4] + bytes(4))

    def test_pcapng_epb_dropcount_is_preserved_and_disqualifies_complete_pairing(self):
        packets = [usb(bytes(64)), usb(complete=True)]
        for endian in ('<', '>'):
            with self.subTest(endian=endian):
                result = decode_capture(pcapng(packets, dropcount=5, endian=endian),
                                        bus=2, device=24, out_endpoint=1, in_endpoint=0x81)
                self.assertEqual(len(result['transcript']['reports']), 1)
                self.assertFalse(result['transcript']['paired_selected_transfers_complete'])
                self.assertEqual(result['capture_drop_counters'][0]['counter'], 'epb_dropcount')
                self.assertEqual(result['capture_drop_counters'][0]['count'], 5)
                self.assertEqual(result['capture_drop_counters'][0]['packet_index'], 0)
                self.assertEqual(result['capture_drop_counters'][0]['raw_value_hex'], struct.pack(endian + 'Q', 5).hex())
                self.assertIn('capture_packet_drops', [i['code'] for i in result['issues']])

    def test_pcapng_statistics_drop_counters_are_preserved_and_checked(self):
        packets = [usb(bytes(64)), usb(complete=True)]
        for endian in ('<', '>'):
            for code, name in ((5, 'isb_ifdrop'), (7, 'isb_osdrop')):
                with self.subTest(endian=endian, counter=name):
                    result = decode_capture(pcapng(packets, stats=(0, [(code, 3)]), endian=endian),
                                            bus=2, device=24, out_endpoint=1, in_endpoint=0x81)
                    self.assertFalse(result['transcript']['paired_selected_transfers_complete'])
                    self.assertEqual(result['capture_drop_counters'][0]['counter'], name)
                    self.assertEqual(result['capture_drop_counters'][0]['count'], 3)
                    self.assertEqual(result['capture_drop_counters'][0]['timestamp_ns'], 4321000)
                    self.assertIsNone(result['capture_drop_counters'][0]['packet_index'])

    def test_zero_drop_counters_do_not_prove_full_capture_completeness(self):
        result = decode_capture(pcapng([usb(bytes(64)), usb(complete=True)], dropcount=0, stats=(0, [(5, 0), (7, 0)])),
                                bus=2, device=24, out_endpoint=1, in_endpoint=0x81)
        self.assertTrue(result['transcript']['paired_selected_transfers_complete'])
        self.assertTrue(result['transcript']['full_capture_completeness_unproven'])
        self.assertEqual(len(result['capture_drop_counters']), 4)
        self.assertEqual(result['issues'], [])

    def test_drop_counters_for_an_unrelated_interface_do_not_disqualify_pairing(self):
        result = decode_capture(pcapng([usb(bytes(64)), usb(complete=True)], stats=(1, [(5, 2)]), extra_interface=True),
                                bus=2, device=24, out_endpoint=1, in_endpoint=0x81)
        self.assertTrue(result['transcript']['paired_selected_transfers_complete'])
        self.assertEqual(result['capture_drop_counters'][0]['count'], 2)
        self.assertEqual(result['issues'], [])

    def test_malformed_pcapng_drop_metadata_is_rejected(self):
        base = pcapng([usb(bytes(64))])
        for stats_body in (struct.pack('<3I', 0, 0, 1234) + ng_option(5, bytes(4)),
                           struct.pack('<3I', 0, 0, 1234) + ng_option(7, bytes(8)) * 2,
                           struct.pack('<3I', 5, 0, 1234)):
            with self.subTest(body=stats_body.hex()):
                with self.assertRaises(ValueError):
                    read_capture(base + ng_block(5, stats_body))

        packet = usb(bytes(64))
        ng_header = pcapng([])
        packet_body = struct.pack('<5I', 0, 0, 1234, len(packet), len(packet)) + packet + bytes((-len(packet)) % 4)
        for opts in (ng_option(4, bytes(4)), ng_option(4, bytes(8)) * 2):
            with self.subTest(options=opts.hex()):
                with self.assertRaises(ValueError):
                    read_capture(ng_header + ng_block(6, packet_body + opts))


if __name__ == '__main__':
    unittest.main()
