#!/usr/bin/env python3
"""Decode an existing USBPcap pcap/pcapng into evidence and a HID transcript.

Offline only: no capture backend, USB access, updater invocation or device write.
Format references: https://desowin.org/usbpcap/captureformat.html and
https://www.ietf.org/archive/id/draft-tuexen-opsawg-pcapng-05.html .
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from thy333_container import write_report

USBPCAP_LINKTYPE = 249


def bounded(raw, offset, length, label):
    if offset < 0 or length < 0 or offset + length > len(raw):
        raise ValueError(f"Truncated {label}")
    return raw[offset:offset + length]


def options(raw, endian):
    offset, result = 0, []
    while offset < len(raw):
        bounded(raw, offset, 4, "pcapng option")
        code, length = struct.unpack_from(endian + "HH", raw, offset)
        offset += 4
        if code == 0:
            if length:
                raise ValueError("Invalid pcapng end-of-options length")
            break
        value = bounded(raw, offset, length, "pcapng option value")
        result.append((code, value))
        offset += (length + 3) & ~3
        if offset > len(raw):
            raise ValueError("Truncated pcapng option padding")
    return result


def read_capture(raw):
    """Return packet records with exact bytes, offsets and integer timestamps."""
    magic = bounded(raw, 0, 4, "capture magic")
    classic = {b"\xd4\xc3\xb2\xa1": ("<", 1000000), b"\xa1\xb2\xc3\xd4": (">", 1000000),
               b"\x4d\x3c\xb2\xa1": ("<", 1000000000), b"\xa1\xb2\x3c\x4d": (">", 1000000000)}
    records, interfaces, drop_counters = [], [], []
    if magic in classic:
        endian, units = classic[magic]
        bounded(raw, 0, 24, "pcap header")
        major, minor, timezone, sigfigs, snaplen, network = struct.unpack_from(endian + "HHiIII", raw, 4)
        if (major, minor) != (2, 4) or not snaplen:
            raise ValueError("Unsupported pcap version/snapshot size")
        linktype = network & 0xFFFF
        interfaces.append({"section": 0, "interface": 0, "linktype": linktype, "snaplen": snaplen, "timestamp_units_per_second": units})
        offset = 24
        while offset < len(raw):
            bounded(raw, offset, 16, "pcap packet header")
            seconds, fraction, captured, original = struct.unpack_from(endian + "4I", raw, offset)
            if fraction >= units or captured > original or captured > snaplen:
                raise ValueError("Invalid pcap packet length/timestamp")
            packet = bounded(raw, offset + 16, captured, "pcap packet data")
            records.append({"packet_index": len(records), "file_offset": offset, "section": 0, "interface": 0,
                            "linktype": linktype, "timestamp_ns": seconds * 1000000000 + fraction * 1000000000 // units,
                            "captured_bytes": captured, "original_bytes": original, "packet": packet})
            offset += 16 + captured
        return {"container_format": "pcap", "interfaces": interfaces, "records": records, "drop_counters": drop_counters}
    if magic != b"\x0a\x0d\x0d\x0a":
        raise ValueError("Expected classic pcap or pcapng")
    offset, endian, section, section_interfaces = 0, None, -1, []
    while offset < len(raw):
        bounded(raw, offset, 12, "pcapng block")
        is_section = raw[offset:offset + 4] == b"\x0a\x0d\x0d\x0a"
        if is_section:
            order = raw[offset + 8:offset + 12]
            endian = "<" if order == b"\x4d\x3c\x2b\x1a" else ">" if order == b"\x1a\x2b\x3c\x4d" else None
            if endian is None:
                raise ValueError("Invalid pcapng byte-order magic")
        if endian is None:
            raise ValueError("pcapng block before section header")
        typ, length = struct.unpack_from(endian + "II", raw, offset)
        if length < 12 or length % 4:
            raise ValueError("Invalid pcapng block length")
        block = bounded(raw, offset, length, "pcapng block body")
        if struct.unpack_from(endian + "I", block, length - 4)[0] != length:
            raise ValueError("pcapng leading/trailing lengths disagree")
        body = block[8:-4]
        if is_section:
            bounded(body, 0, 16, "pcapng section header")
            major, minor = struct.unpack_from(endian + "HH", body, 4)
            if (major, minor) != (1, 0):
                raise ValueError("Unsupported pcapng section version")
            section += 1
            section_interfaces = []
        elif typ == 1:
            bounded(body, 0, 8, "pcapng interface description")
            linktype, reserved, snaplen = struct.unpack_from(endian + "HHI", body)
            units, time_offset = 1000000, 0
            for code, value in options(body[8:], endian):
                if code == 9:
                    if len(value) != 1:
                        raise ValueError("Invalid if_tsresol option")
                    exponent = value[0] & 0x7F
                    if exponent > 63:
                        raise ValueError("Unreasonable timestamp resolution")
                    units = (2 if value[0] & 0x80 else 10) ** exponent
                elif code == 14:
                    if len(value) != 8:
                        raise ValueError("Invalid if_tsoffset option")
                    time_offset = struct.unpack(endian + "q", value)[0]
            interface = {"section": section, "interface": len(section_interfaces), "linktype": linktype,
                         "snaplen": snaplen, "timestamp_units_per_second": units, "timestamp_offset_seconds": time_offset}
            section_interfaces.append(interface)
            interfaces.append(interface)
        elif typ == 6:
            bounded(body, 0, 20, "pcapng enhanced packet")
            interface_id, high, low, captured, original = struct.unpack_from(endian + "5I", body)
            if interface_id >= len(section_interfaces) or captured > original:
                raise ValueError("Invalid pcapng packet interface/length")
            interface = section_interfaces[interface_id]
            if interface["snaplen"] and captured > interface["snaplen"]:
                raise ValueError("pcapng packet exceeds snapshot length")
            packet = bounded(body, 20, captured, "pcapng captured packet")
            padded = (captured + 3) & ~3
            bounded(body, 20, padded, "pcapng packet padding")
            seen_dropcount = False
            for code, value in options(body[20 + padded:], endian):
                if code == 4:
                    if len(value) != 8 or seen_dropcount:
                        raise ValueError("Invalid or repeated epb_dropcount option")
                    seen_dropcount = True
                    drop_counters.append({"section": section, "interface": interface_id,
                                          "linktype": interface["linktype"], "file_offset": offset,
                                          "packet_index": len(records), "counter": "epb_dropcount",
                                          "count": struct.unpack(endian + "Q", value)[0], "raw_value_hex": value.hex()})
            ticks = (high << 32) | low
            records.append({"packet_index": len(records), "file_offset": offset, "section": section, "interface": interface_id,
                            "linktype": interface["linktype"], "timestamp_ns": interface["timestamp_offset_seconds"] * 1000000000 + ticks * 1000000000 // interface["timestamp_units_per_second"],
                            "captured_bytes": captured, "original_bytes": original, "packet": packet})
        elif typ == 5:
            bounded(body, 0, 12, "pcapng interface statistics")
            interface_id, high, low = struct.unpack_from(endian + "3I", body)
            if interface_id >= len(section_interfaces):
                raise ValueError("Invalid pcapng statistics interface")
            interface = section_interfaces[interface_id]
            ticks = (high << 32) | low
            timestamp = interface["timestamp_offset_seconds"] * 1000000000 + ticks * 1000000000 // interface["timestamp_units_per_second"]
            seen_counters = set()
            for code, value in options(body[12:], endian):
                if code in (5, 7):
                    if len(value) != 8 or code in seen_counters:
                        raise ValueError("Invalid or repeated pcapng statistics drop counter")
                    seen_counters.add(code)
                    drop_counters.append({"section": section, "interface": interface_id,
                                          "linktype": interface["linktype"], "file_offset": offset,
                                          "packet_index": None, "timestamp_ns": timestamp,
                                          "counter": "isb_ifdrop" if code == 5 else "isb_osdrop",
                                          "count": struct.unpack(endian + "Q", value)[0], "raw_value_hex": value.hex()})
        elif typ in (2, 3):
            raise ValueError("Obsolete/simple pcapng packet blocks are unsupported; export Enhanced Packet Blocks")
        offset += length
    return {"container_format": "pcapng", "interfaces": interfaces, "records": records, "drop_counters": drop_counters}


def decode_usb_packet(record):
    raw = record["packet"]
    bounded(raw, 0, 27, "USBPcap base header")
    header_len, irp, status, function, info, bus, device, endpoint, transfer, data_length = struct.unpack_from("<HQIHBHHBBI", raw)
    if header_len < 27 or header_len > len(raw) or info & ~1:
        raise ValueError("Invalid USBPcap header length/info bits")
    payload = raw[header_len:]
    truncated = record["captured_bytes"] < record["original_bytes"] or len(payload) != data_length
    if len(payload) > data_length:
        raise ValueError("USBPcap payload exceeds declared data length")
    result = {key: value for key, value in record.items() if key != "packet"}
    result.update(header_bytes=header_len, irp_id=hex(irp), status=hex(status), status_value=status,
                  urb_function=hex(function), completion=bool(info & 1), bus=bus, device=device,
                  endpoint=endpoint, transfer_type=transfer, declared_data_bytes=data_length,
                  payload_hex=payload.hex(), raw_packet_hex=raw.hex(), truncated=truncated)
    if transfer == 2:
        if header_len < 28:
            raise ValueError("Missing USBPcap control stage")
        result["control_stage"] = raw[27]
        if raw[27] == 0 and len(payload) >= 8:
            request_type, request, value, index, length = struct.unpack_from("<BBHHH", payload)
            result["setup"] = {"request_type": request_type, "request": request, "value": value,
                               "index": index, "length": length}
    return result


def decode_capture(raw, *, bus=None, device=None, out_endpoint=None, in_endpoint=None, hid_interface=None, wire_zero_id=False):
    capture = read_capture(raw)
    packets = [decode_usb_packet(record) for record in capture["records"] if record["linktype"] == USBPCAP_LINKTYPE]
    if not packets:
        raise ValueError("No USBPcap linktype 249 packets in this capture")
    if (bus is None) != (device is None):
        raise ValueError("Select both bus and device or neither for inventory")
    selected = [p for p in packets if bus is None or (p["bus"], p["device"]) == (bus, device)]
    inventory = []
    for identity in sorted({(p["bus"], p["device"], p["endpoint"], p["transfer_type"]) for p in packets}):
        matching = [p for p in packets if (p["bus"], p["device"], p["endpoint"], p["transfer_type"]) == identity]
        inventory.append({"bus": identity[0], "device": identity[1], "endpoint": identity[2], "transfer_type": identity[3],
                          "packets": len(matching), "payload_lengths": sorted({len(bytes.fromhex(p["payload_hex"])) for p in matching})})
    issues, reports, pending, completed = [], [], {}, {}
    def issue(code, packet, detail, severity="error"):
        issues.append({"code": code, "packet_index": packet["packet_index"], "detail": detail, "severity": severity})
    def emit(direction, payload, source_packet, pair_packets, transport):
        original = payload
        if wire_zero_id:
            if len(payload) != 65 or payload[0] != 0:
                issue("unexpected_wire_report", source_packet, "Explicit zero-ID wire mode requires 65 bytes beginning with zero")
                return
            payload = payload[1:]
        elif len(payload) != 64:
            issue("unexpected_wire_report", source_packet, f"Expected 64 wire bytes, found {len(payload)}; no padding or ID guessing performed")
            return
        normalized = b"\0" + payload if direction == "tx" else payload
        reports.append({"direction": direction, "data_hex": normalized.hex(), "wire_payload_hex": original.hex(),
                        "timestamp_ns": source_packet["timestamp_ns"], "source_packet_index": source_packet["packet_index"],
                        "source_packet_indices": [p["packet_index"] for p in pair_packets],
                        "transport": transport, "irp_id": source_packet["irp_id"], "completion_status": "0x0"})
    if bus is not None:
        selected_interfaces = {(p["section"], p["interface"]) for p in selected}
        for counter in capture["drop_counters"]:
            if (counter["section"], counter["interface"]) in selected_interfaces and counter["count"]:
                issues.append({"code": "capture_packet_drops", "packet_index": counter["packet_index"],
                               "file_offset": counter["file_offset"], "section": counter["section"],
                               "interface": counter["interface"], "counter": counter["counter"],
                               "count": counter["count"], "severity": "error",
                               "detail": "Capture interface reports lost packets; selected-device completeness cannot be established"})
        for packet in selected:
            control = packet["transfer_type"] == 2
            interrupt = packet["transfer_type"] == 1 and packet["endpoint"] in (out_endpoint, in_endpoint)
            if not control and not interrupt:
                continue
            if packet["truncated"]:
                issue("truncated_transfer", packet, "Original packet/data length exceeds captured bytes")
            key = (packet["section"], packet["interface"], packet["bus"], packet["device"], packet["irp_id"])
            stage = packet.get("control_stage")
            if control:
                invalid_stage = (stage not in (0, 1, 2, 3) or
                                 (stage == 0 and packet["completion"]) or
                                 (stage in (2, 3) and not packet["completion"]))
                if invalid_stage:
                    issue("invalid_control_stage", packet, "Control SETUP must be a submission; STATUS/COMPLETE must be a completion")
                    if key in pending:
                        pending[key]["invalid"] = True
                    continue
                if packet["endpoint"] & 0x7F:
                    issue("invalid_control_endpoint", packet, "Control transfer does not use endpoint zero")
                    if key in pending:
                        pending[key]["invalid"] = True
                    continue
                if stage == 0 and "setup" not in packet:
                    issue("invalid_control_setup", packet, "Control SETUP lacks its eight-byte request")
                    if key in pending:
                        pending[key]["invalid"] = True
                    continue
            if control and stage == 1:
                exchange = pending.get(key)
                if exchange and exchange["submit"].get("control_stage") == 0:
                    setup = exchange["submit"]["setup"]
                    if packet["completion"] != bool(setup["request_type"] & 0x80):
                        issue("invalid_control_data_direction", packet, "Legacy control DATA direction differs from SETUP")
                        exchange["invalid"] = True
                    if packet["completion"] and packet["status_value"] != 0:
                        issue("failed_control_data", packet, f"Legacy control DATA status {packet['status']}; data excluded from successful transcript")
                        exchange["invalid"] = True
                    if exchange["data_packets"]:
                        issue("ambiguous_control_data", packet, "Repeated legacy DATA stage cannot be distinguished from lost/reused IRP traffic")
                        exchange["invalid"] = True
                    exchange["data_packets"].append(packet)
                else:
                    issue("orphan_control_data", packet, "Legacy control DATA stage lacks its SETUP")
                continue
            if not packet["completion"]:
                if key in pending:
                    if pending[key]["submit"]["raw_packet_hex"] == packet["raw_packet_hex"]:
                        issue("ambiguous_repeated_submission", packet, "Repeated submission may be an IRP reuse with a lost completion; cannot pair safely")
                    else:
                        issue("overlapping_irp", packet, "IRP reused before completion; cannot pair safely")
                    pending[key]["invalid"] = True
                    continue
                pending[key] = {"submit": packet, "data_packets": [], "invalid": packet["truncated"]}
                continue
            exchange = pending.pop(key, None)
            if exchange is None:
                if completed.get(key) == packet["raw_packet_hex"]:
                    issue("ambiguous_repeated_completion", packet, "Repeated completion may be an IRP reuse with a lost submission; completeness is unknown")
                    continue
                # Successful IN completion is evidence of received data, but the
                # missing request remains an explicit capture completeness gap.
                issue("orphan_completion", packet, "Completion has no captured submission")
                continue
            completed[key] = packet["raw_packet_hex"]
            submit = exchange["submit"]
            pair_packets = [submit, *exchange["data_packets"], packet]
            if exchange["invalid"] or any(p["truncated"] for p in pair_packets):
                continue
            if packet["status_value"] != 0:
                issue("failed_completion", packet, f"USB completion status {packet['status']}; data excluded from successful transcript")
                continue
            if packet["transfer_type"] != submit["transfer_type"] or (not control and packet["endpoint"] != submit["endpoint"]):
                issue("mismatched_completion", packet, "URB endpoint/type changed across submission/completion")
                continue
            if interrupt:
                if packet["endpoint"] == out_endpoint:
                    emit("tx", bytes.fromhex(submit["payload_hex"]), submit, pair_packets, "interrupt")
                elif packet["endpoint"] == in_endpoint:
                    emit("rx", bytes.fromhex(packet["payload_hex"]), packet, pair_packets, "interrupt")
            elif hid_interface is not None:
                setup = submit.get("setup")
                if not setup or setup["index"] != hid_interface:
                    continue
                if setup["request_type"] == 0x21 and setup["request"] == 9 and setup["value"] == 0x0200:
                    parts = [bytes.fromhex(submit["payload_hex"])[8:]] + [bytes.fromhex(p["payload_hex"]) for p in exchange["data_packets"]]
                    payload = b"".join(parts)
                    if len(payload) != setup["length"]:
                        issue("control_length_mismatch", submit, "SET_REPORT payload does not match SETUP wLength")
                        continue
                    emit("tx", payload, submit, pair_packets, "control-set-report")
        for exchange in pending.values():
            issue("missing_completion", exchange["submit"], "No completion captured for selected URB")
    reports.sort(key=lambda report: report["source_packet_index"])
    if reports:
        first = reports[0]["timestamp_ns"]
        for report in reports:
            report["timestamp_ms"] = (report["timestamp_ns"] - first) / 1000000
    transcript = {"format": "thy333.hid-transcript", "version": 1, "reports": reports,
                  "capture_sha256": hashlib.sha256(raw).hexdigest(), "normalization": "explicit zero-ID wire" if wire_zero_id else "64-byte wire payload; synthetic HIDAPI TX report ID zero",
                  "paired_selected_transfers_complete": bus is not None and bool(reports) and not any(issue["severity"] == "error" for issue in issues),
                  "full_capture_completeness_unproven": True,
                  "flash_contents_verified": False, "backup_acquired": False}
    return {"format": "thy333.updater-capture-decode", "version": 1, "offline_only": True,
            "capture_sha256": hashlib.sha256(raw).hexdigest(), "capture_bytes": len(raw),
            "container_format": capture["container_format"], "interfaces": capture["interfaces"],
            "capture_drop_counters": capture["drop_counters"],
            "inventory": inventory, "selection": {"bus": bus, "device": device, "out_endpoint": out_endpoint,
                                                   "in_endpoint": in_endpoint, "hid_interface": hid_interface},
            "selected_packets": selected, "issues": issues, "transcript": transcript,
            "limits": ["Successful USB transfer completion is not device flash readback or erase-address proof",
                       "Control/descriptor traffic is preserved; only explicitly selected interrupt endpoints and HID SET_REPORT interface are normalized",
                       "Pairing of captured selected transfers does not prove that the capture contains every transfer; absent or zero drop counters do not establish full capture completeness",
                       "No missing bytes, final-block padding, report IDs or USB identities are inferred"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--bus", type=lambda v: int(v, 0))
    parser.add_argument("--device", type=lambda v: int(v, 0))
    parser.add_argument("--out-endpoint", type=lambda v: int(v, 0))
    parser.add_argument("--in-endpoint", type=lambda v: int(v, 0))
    parser.add_argument("--hid-interface", type=lambda v: int(v, 0), help="Opt in to decoding EP0 HID SET_REPORT for this exact interface")
    parser.add_argument("--wire-zero-id", action="store_true", help="Explicitly confirmed 65-byte wire reports include a leading zero report ID")
    parser.add_argument("--output", type=Path, required=True, help="New evidence .json file")
    parser.add_argument("--transcript", type=Path, help="New normalized transcript .json file")
    args = parser.parse_args()
    try:
        report = decode_capture(args.capture.read_bytes(), bus=args.bus, device=args.device, out_endpoint=args.out_endpoint,
                                in_endpoint=args.in_endpoint, hid_interface=args.hid_interface, wire_zero_id=args.wire_zero_id)
        report["capture_path"] = str(args.capture.resolve())
        if args.transcript and (args.bus is None or not report["transcript"]["reports"]):
            raise ValueError("Transcript output requires explicit device selection and matching completed HID transfers")
        inputs = [args.capture]
        if args.transcript:
            if args.transcript.resolve() == args.output.resolve():
                raise ValueError("Evidence and transcript output paths must differ")
            write_report(args.transcript, report["transcript"], inputs)
        write_report(args.output, report, inputs)
        print(f"Decoded {len(report['selected_packets'])} selected packets, {len(report['transcript']['reports'])} HID reports, {len(report['issues'])} issues")
        return 2 if any(issue["severity"] == "error" for issue in report["issues"]) else 0
    except (OSError, ValueError) as exc:
        print(f"Offline capture decode failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
