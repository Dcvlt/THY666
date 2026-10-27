#!/usr/bin/env python3
"""Offline CDC session timeline from an already filtered USBPcap file.

Preserves requests, failed transfers and exact bytes. Reconstructed lines and
command/reply windows follow captured URB ordering, not a device state machine.
Never opens a serial port, USB device, recorder or updater.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import sys

from decode_updater_capture import USBPCAP_LINKTYPE, decode_usb_packet, read_capture
from thy333_container import write_report


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _lines(chunks):
    """Frame LF lines without decoding away malformed bytes or partial tails."""
    buffer, origins, lines = bytearray(), [], []
    for chunk in chunks:
        for byte in bytes.fromhex(chunk["payload_hex"]):
            buffer.append(byte)
            origins.append(chunk["packet_index"])
            if byte == 10:
                raw = bytes(buffer)
                text = raw[:-1].removesuffix(b"\r").decode("ascii", errors="backslashreplace")
                lines.append({"payload_hex": raw.hex(), "text": text,
                              "first_packet_index": origins[0], "last_packet_index": origins[-1],
                              "packet_indices": sorted(set(origins))})
                buffer.clear()
                origins.clear()
    return lines, {"payload_hex": bytes(buffer).hex(), "packet_indices": sorted(set(origins))}


def analyze_cdc(raw, *, bus, device, out_endpoint=2, in_endpoint=0x82, provenance=None):
    if type(bus) is not int or not 0 <= bus <= 65535 or type(device) is not int or not 1 <= device <= 127:
        raise ValueError("Explicit numeric USB bus and address (1..127) are required")
    if not 1 <= out_endpoint <= 15 or not 0x81 <= in_endpoint <= 0x8f:
        raise ValueError("Explicit bulk OUT/IN endpoint directions are invalid")
    capture = read_capture(raw)
    records = capture["records"]
    if provenance:
        if provenance.get("output_sha256") != sha(raw) or provenance.get("selected_packet_count") != len(records):
            raise ValueError("Capture/provenance hash or record-count mismatch")
        if len(provenance.get("source_packet_indices", [])) != len(records) or len(provenance.get("source_file_offsets", [])) != len(records):
            raise ValueError("Incomplete extraction provenance mapping")
    rows, descriptors = [], []
    for record in records:
        if record["linktype"] != USBPCAP_LINKTYPE:
            raise ValueError("Filtered capture must contain only USBPcap linktype 249")
        p = decode_usb_packet(record)
        if (p["bus"], p["device"]) != (bus, device):
            continue
        payload = bytes.fromhex(p["payload_hex"])
        row = {k: p[k] for k in ("packet_index", "file_offset", "timestamp_ns", "irp_id", "endpoint", "transfer_type", "completion", "status", "status_value", "payload_hex", "truncated", "declared_data_bytes")}
        row["payload_bytes"] = len(payload)
        row["utc"] = datetime.fromtimestamp(p["timestamp_ns"] / 1e9, timezone.utc).isoformat().replace("+00:00", "Z")
        if provenance:
            row["source_packet_index"] = provenance["source_packet_indices"][p["packet_index"]]
            row["source_file_offset"] = provenance["source_file_offsets"][p["packet_index"]]
        if "setup" in p:
            row["setup"] = p["setup"]
        if p.get("control_stage") is not None:
            row["control_stage"] = p["control_stage"]
        rows.append(row)
        if p["transfer_type"] == 2 and p["completion"] and p["status_value"] == 0 and not p["truncated"] and len(payload) == 18 and payload[:2] == b"\x12\x01":
            descriptors.append({"packet_index": p["packet_index"], "vid": struct.unpack_from("<H", payload, 8)[0],
                                "pid": struct.unpack_from("<H", payload, 10)[0], "payload_hex": payload.hex(),
                                "synthetic_irp_zero_metadata": p["irp_id"] == "0x0"})
    if {(r["vid"], r["pid"]) for r in descriptors} != {(0xcafe, 0x4009)}:
        raise ValueError("Selected address must have consistent captured CAFE:4009 descriptor evidence")
    pending, pair_conflicts, class_rows = {}, [], []
    for row in rows:
        if row["transfer_type"] not in (2, 3):
            continue
        # Control direction can differ across stages; the IRP and transfer type bind it.
        key = (row["transfer_type"], row["irp_id"], row["endpoint"] if row["transfer_type"] == 3 else None)
        if row["completion"]:
            submit = pending.pop(key, None)
            if submit:
                submit["completion_record"] = {k: row[k] for k in ("packet_index", "timestamp_ns", "status", "status_value", "payload_hex", "truncated")}
                row["submission_packet_index"] = submit["packet_index"]
                row["submission_truncated"] = submit["truncated"]
        else:
            if key in pending:
                pair_conflicts.append({"earlier_packet_index": pending[key]["packet_index"], "later_packet_index": row["packet_index"], "irp_id": row["irp_id"]})
            pending[key] = row
            setup = row.get("setup")
            if row["transfer_type"] == 2 and setup and setup["request_type"] & 0x60 == 0x20:
                request = setup["request"]
                row["name"] = {0x20: "SET_LINE_CODING", 0x21: "GET_LINE_CODING", 0x22: "SET_CONTROL_LINE_STATE"}.get(request, "OTHER_CDC_CLASS_REQUEST")
                if request == 0x22:
                    row.update(dtr=bool(setup["value"] & 1), rts=bool(setup["value"] & 2))
                if request == 0x20:
                    coding = bytes.fromhex(row["payload_hex"])[8:]
                    if len(coding) == 7:
                        baud, stop, parity, bits = struct.unpack("<IBBB", coding)
                        row["line_coding"] = {"baud": baud, "stop_bits_encoding": stop, "parity_encoding": parity, "data_bits": bits}
                class_rows.append(row)
    dtr_requests = [r for r in class_rows if r["name"] == "SET_CONTROL_LINE_STATE"]
    completed_dtr = {r["completion_record"]["packet_index"]: r for r in dtr_requests if r.get("completion_record", {}).get("status_value") == 0 and not r["truncated"] and not r["completion_record"]["truncated"]}
    sessions = [{"session_index": 0, "dtr_asserted": False, "start_kind": "capture-prefix", "start_packet_index": 0, "tx_chunks": [], "rx_chunks": []}]
    current = sessions[0]
    last_dtr = None
    bulk = []
    for row in rows:
        if row["packet_index"] in completed_dtr:
            dtr = completed_dtr[row["packet_index"]]
            previous_value = bool(last_dtr and last_dtr["dtr"])
            last_dtr = dtr
            if dtr["dtr"] != previous_value:
                current["end_packet_index"] = row["packet_index"]
                current = {"session_index": len(sessions), "dtr_asserted": dtr["dtr"],
                           "start_kind": "successful-captured-DTR-transition", "start_packet_index": row["packet_index"],
                           "dtr_request_packet_index": dtr["packet_index"], "tx_chunks": [], "rx_chunks": []}
                sessions.append(current)
        if row["transfer_type"] != 3 or row["endpoint"] not in (out_endpoint, in_endpoint) or not row["payload_bytes"]:
            continue
        row["last_successful_captured_dtr_request_packet_index"] = last_dtr["packet_index"] if last_dtr else None
        row["last_successful_captured_dtr_value"] = last_dtr["dtr"] if last_dtr else None
        row["session_index"] = current["session_index"]
        bulk.append(row)
        if row["truncated"]:
            continue
        if row["endpoint"] == out_endpoint and not row["completion"] and row.get("completion_record", {}).get("status_value") == 0 and not row["completion_record"]["truncated"]:
            current["tx_chunks"].append(row)
        elif row["endpoint"] == in_endpoint and row["completion"] and row["status_value"] == 0:
            current["rx_chunks"].append(row)
    for session in sessions:
        session["tx_lines"], session["tx_partial_tail"] = _lines(session["tx_chunks"])
        session["rx_lines"], session["rx_partial_tail"] = _lines(session["rx_chunks"])
        session["greeting_lines"] = [r for r in session["rx_lines"] if r["text"].startswith("HELLO THYDIAG/1 ")]
        session["commands"] = session["tx_lines"]
        session["response_windows"] = []
        for i, command in enumerate(session["commands"]):
            next_command = session["commands"][i + 1] if i + 1 < len(session["commands"]) else None
            responses = [r for r in session["rx_lines"] if r["first_packet_index"] > command["last_packet_index"] and (next_command is None or r["last_packet_index"] < next_command["first_packet_index"])]
            session["response_windows"].append({"command": command, "following_rx_lines_before_next_command": responses})
        help_windows = [r for r in session["response_windows"] if r["command"]["text"] == "help"]
        session["first_help_response_window"] = help_windows[0] if help_windows else None
        session["first_help_ok_observed"] = any(r["text"].startswith("OK THYDIAG/1 commands=") for r in help_windows[0]["following_rx_lines_before_next_command"]) if help_windows else None
        session["error_lines"] = [r for r in session["rx_lines"] if r["text"].startswith("ERR ")]
        session["i2c_lines"] = [r for r in session["rx_lines"] if r["text"].startswith(("I2C", "BANK")) or " phase=" in r["text"]]
    return {"format": "thy666.diagnostic-cdc-capture-analysis", "version": 1, "offline_only": True,
            "capture_sha256": sha(raw), "capture_bytes": len(raw), "selection": {"bus": bus, "device": device, "out_endpoint": out_endpoint, "in_endpoint": in_endpoint},
            "descriptor_evidence": descriptors, "class_control_requests": class_rows, "control_line_requests": dtr_requests,
            "payload_bearing_bulk_records": bulk, "irp_pair_conflicts": pair_conflicts,
            "sessions": sessions, "asserted_session_count": sum(r["dtr_asserted"] for r in sessions),
            "first_help_results": [{"session_index": r["session_index"], "dtr_asserted": r["dtr_asserted"], "first_help_ok_observed": r["first_help_ok_observed"]} for r in sessions if r["first_help_response_window"]],
            "source_capture_sha256_from_provenance": provenance.get("source_sha256") if provenance else None,
            "generator_sha256": sha(Path(__file__).read_bytes()),
            "device_flash_contents_verified": False,
            "limits": ["Input is already filtered; excluded traffic and physical identity are not established here.",
                       "Successful captured DTR requests provide protocol context, not direct measurements of firmware state.",
                       "TX commands use submissions with captured successful completions; RX bytes use successful completions, including explicitly identifiable unpaired records.",
                       "LF lines are reassembled within DTR intervals; malformed bytes, partial tails and failed URBs remain visible.",
                       "Command/reply windows follow URB ordering and may be ambiguous with pipelining, retransmission, missing captures or unrelated asynchronous output.",
                       "Pre-session C0 origin is not attributed to a host process. Capture drop freedom and discarded internal FIFO bytes are unproven.",
                       "An OK after a fresh greeting supports the tested session; absence of deliberately injected C0 does not recreate the prior disturbance."]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--bus", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--device", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--out-endpoint", type=lambda v: int(v, 0), default=2)
    parser.add_argument("--in-endpoint", type=lambda v: int(v, 0), default=0x82)
    parser.add_argument("--provenance", type=Path)
    parser.add_argument("--max-capture-bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if not 0 < args.capture.stat().st_size <= args.max_capture_bytes:
            raise ValueError("Input exceeds filtered-capture size bound; stream-extract selected addresses first")
        provenance = json.loads(args.provenance.read_text(encoding="utf-8")) if args.provenance else None
        report = analyze_cdc(args.capture.read_bytes(), bus=args.bus, device=args.device, out_endpoint=args.out_endpoint, in_endpoint=args.in_endpoint, provenance=provenance)
        report["capture_path"] = str(args.capture.resolve())
        if args.provenance:
            report["provenance_path"] = str(args.provenance.resolve())
            report["provenance_sha256"] = sha(args.provenance.read_bytes())
        write_report(args.output, report, [args.capture] + ([args.provenance] if args.provenance else []))
        print(json.dumps({"output": str(args.output.resolve()), "capture_sha256": report["capture_sha256"], "asserted_session_count": report["asserted_session_count"], "first_help_results": report["first_help_results"]}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Offline diagnostic CDC analysis failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
