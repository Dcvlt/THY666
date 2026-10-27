#!/usr/bin/env python3
"""Offline .333 inspection and strict, normalized HID transcript verification.

This module has no USB backend and cannot create or install firmware containers.
The transcript checker verifies a reconstructed host exchange, not device writes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

HEADER_FIELDS = (
    (6, 6, "firmware_enabled"), (12, 6, "scales_enabled"),
    (18, 6, "filesystem_prepare_enabled"), (24, 6, "firmware_start"),
    (30, 6, "firmware_end"), (36, 8, "metadata_word_0"),
    (44, 8, "metadata_word_1"), (52, 8, "metadata_word_2"),
    (60, 6, "required_hardware_version"), (66, 6, "firmware_encrypted"),
    (72, 6, "scales_encrypted"),
)

def crc32_normal(data, initial=0):
    """Updater CRC: polynomial 0x04C11DB7, no reflection or final XOR."""
    crc = initial & 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    return crc

def parse_update_container(data):
    """Parse the fixed-width THY333 installation container format.

    This uses bounded fields, unlike the original C++ parser's temporary buffer.
    End is the boundary between application and optional scale bytes, rather than
    an unconditional declaration of the whole file's payload length.
    """
    if len(data) < 78 or data[:6] != b"THY333":
        raise ValueError("Missing complete THY333 header")
    fields = []
    values = {}
    for offset, length, name in HEADER_FIELDS:
        raw = data[offset:offset + length]
        if not re.fullmatch(b"[0-9a-fA-F]+", raw):
            raise ValueError(f"Non-hexadecimal header field {name}")
        value = int(raw, 16)
        values[name] = value
        fields.append({"offset": offset, "length": length, "name": name,
                       "text": raw.decode("ascii"), "value": value})
    start, end = values["firmware_start"], values["firmware_end"]
    if not 0 <= start <= end <= len(data) - 78:
        raise ValueError("Application bounds extend outside payload or are reversed")
    app = data[78 + start:78 + end]
    return {"fields": fields, "values": values, "application_file_range": [78 + start, 78 + end],
            "application_bytes": len(app), "scale_file_range": [78 + end, len(data)],
            "scale_bytes": len(data) - 78 - end, "application_crc32_normal_zero_init": hex(crc32_normal(app)),
            "application_hid_reports": (len(app) + 63) // 64,
            "last_report_valid_data_bytes": ((len(app) - 1) % 64 + 1) if app else 0,
            "unencrypted_updater_overread_bytes": (-len(app)) % 64 if app and not values["firmware_encrypted"] else 0}


SIGNATURE = b"sSJFfddSAD$srAffy84JappEKL3$SALgK@3J4hwd5920xg1!8hs7J6Kg"
HEADER_BYTES = 78
UNRESOLVED = [
    "device-side metadata words 1 and 2 and CRC enforcement",
    "internal erase/write ranges and external staging ownership",
    "bootloader-to-application handoff and completion semantics",
    "update entry and restoration when the application cannot boot",
]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def plausible_initial_stack(stack):
    # Both regular SRAM and CCM can hold a Cortex-M4 stack; CCM is not DMA RAM.
    return stack % 8 == 0 and (0x20000000 < stack <= 0x20020000 or 0x10000000 < stack <= 0x10010000)


def command_payload(token):
    """Pure byte encoder for offline fixtures; never sends a command."""
    token = token.encode("ascii") if isinstance(token, str) else token
    if len(token) != 8:
        raise ValueError("Command token must contain exactly eight bytes")
    return token + SIGNATURE


def integer_payload(value):
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise ValueError("Integer payload must be an unsigned 32-bit integer")
    return struct.pack("<I", value) + bytes(60)


def inspect_container(data):
    parsed = parse_update_container(data)
    values = parsed["values"]
    start, end = parsed["application_file_range"]
    app = data[start:end]
    actual_crc = crc32_normal(app)
    issues = []
    if values["metadata_word_0"] != actual_crc:
        issues.append("Header metadata word 0 differs from the calculated application CRC")
    if values["filesystem_prepare_enabled"]:
        issues.append("Filesystem preparation occurs before the hardware-version check")
    if parsed["unencrypted_updater_overread_bytes"]:
        issues.append("Original unencrypted updater overreads the final report; deterministic padding is not established")
    if values["scales_enabled"]:
        issues.append("Scale subheader/flash contract remains unverified")
    elif parsed["scale_bytes"]:
        issues.append("Container has trailing scale-section bytes while scale update is disabled")
    vector = None
    if not values["firmware_encrypted"] and len(app) >= 8:
        stack, reset = struct.unpack_from("<II", app)
        vector = {"initial_msp": hex(stack), "reset_handler": hex(reset),
                  "msp_in_candidate_sram_or_ccm": plausible_initial_stack(stack),
                  "reset_thumb_bit": bool(reset & 1),
                  "reset_in_candidate_application": 0x08040000 <= (reset & ~1) < min(0x08040000 + len(app), 0x08100000),
                  "reset_in_candidate_application_slot": 0x08040000 <= (reset & ~1) < 0x08100000}
    return {"format": "thy333.container-inspection", "version": 1,
            "offline_only": True, "flash_ready": False, "file_bytes": len(data),
            "file_sha256": sha256(data), **parsed,
            "application_sha256": sha256(app), "header_word_0_matches_crc": values["metadata_word_0"] == actual_crc,
            "plaintext_vector": vector, "issues": issues,
            "unresolved_installation_requirements": list(UNRESOLVED)}


def draft_recipe(app):
    """Describe unanswered packaging fields without emitting a .333 header."""
    if len(app) < 8 or len(app) > 0xC0000:
        raise ValueError("Candidate application must be 8 bytes..768 KiB")
    stack, reset = struct.unpack_from("<II", app)
    if not plausible_initial_stack(stack):
        raise ValueError("Candidate initial stack is outside SRAM or misaligned")
    if not reset & 1 or not 0x08040000 <= (reset & ~1) < 0x08040000 + len(app):
        raise ValueError("Candidate reset vector does not point inside this image at 0x08040000")
    return {"format": "thy333.draft-container-recipe", "version": 1,
            "offline_only": True, "flash_ready": False, "container_emitted": False,
            "application_bytes": len(app), "application_sha256": sha256(app),
            "application_base_candidate": "0x08040000",
            "header_recipe": {"magic": "THY333", "firmware_enabled": 1,
                              "scales_enabled": 0, "filesystem_prepare_enabled": 0,
                              "firmware_start": 0, "firmware_end": len(app),
                              "metadata_word_0_candidate_payload_crc": hex(crc32_normal(app)),
                              "metadata_word_1": None, "metadata_word_2": None,
                              "required_hardware_version_candidate": 4,
                              "firmware_encrypted": 0, "scales_encrypted": 0},
            "original_metadata_reused": False,
            "original_unencrypted_updater_overread_bytes": (-len(app)) % 64,
            "padding_rule_verified": False,
            "gates": {"physical_target_confirmed": False, "complete_backup_verified": False,
                      "independent_recovery_verified": False, "device_update_handler_verified": False,
                      "metadata_word_1_verified": False, "metadata_word_2_verified": False,
                      "erase_boundaries_verified": False, "handoff_verified": False},
            "unresolved_installation_requirements": list(UNRESOLVED),
            "notes": ["This describes an application-only plaintext candidate; disabling filesystem/scales preparation needs device-handler validation",
                      "Copying legacy metadata words does not verify their meaning; an offline candidate must label explicit hypotheses",
                      "Report alignment is not permission to append bytes or alter metadata without checking device CRC/length semantics"]}


class TranscriptReader:
    """Reject errors, short reports, unexpected direction/order and extra traffic."""

    def __init__(self, transcript):
        if not isinstance(transcript, dict) or transcript.get("format") != "thy333.hid-transcript" or transcript.get("version") != 1:
            raise ValueError("Expected thy333.hid-transcript version 1")
        self.records = transcript.get("reports")
        if not isinstance(self.records, list):
            raise ValueError("Transcript reports must be an array")
        self.index = 0

    def report(self, direction, state):
        if self.index >= len(self.records):
            raise ValueError(f"Truncated transcript at {state}")
        record = self.records[self.index]
        self.index += 1
        if not isinstance(record, dict) or record.get("direction") != direction:
            raise ValueError(f"Unexpected report direction at {state}")
        raw = record.get("data_hex")
        if not isinstance(raw, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2})+", raw):
            raise ValueError(f"Invalid data_hex at {state}")
        data = bytes.fromhex(raw)
        expected_length = 65 if direction == "tx" else 64
        if len(data) != expected_length:
            raise ValueError(f"Expected {expected_length} bytes at {state}; got {len(data)}")
        if direction == "tx":
            if data[0] != 0:
                raise ValueError(f"Nonzero HID report ID at {state}")
            data = data[1:]
        return data

    def expect_command(self, direction, token):
        if self.report(direction, token) != command_payload(token):
            raise ValueError(f"Unexpected token or signature at {token}")

    def expect_metadata_marker(self, token):
        # receiveMetadata compares eight bytes directly, unlike waitForMessage.
        # The remaining report tail has no established signature constraint.
        if token not in ("FWMETABG", "FWMETAER", "FWMETADN"):
            raise ValueError("Unknown metadata marker")
        payload = self.report("rx", token)
        if payload[:8] != token.encode("ascii"):
            raise ValueError(f"Unexpected metadata marker at {token}")
        return {"token": token, "opaque_tail_hex": payload[8:].hex(),
                "tail_matches_known_signature": payload[8:] == SIGNATURE,
                "signature_required_by_recovered_host": False}

    def integer(self, direction, state, expected=None):
        data = self.report(direction, state)
        value = struct.unpack_from("<I", data)[0]
        # Host integer reports are initialized; replies have no established tail.
        if direction == "tx" and data[4:] != bytes(60):
            raise ValueError(f"Nonzero host integer padding at {state}")
        if expected is not None and value != expected:
            raise ValueError(f"Unexpected integer at {state}: {value:#x} != {expected:#x}")
        return value


def validate_transcript(container, transcript):
    """Validate the strict host-intended application-only fixture, offline.

    Reads are already normalized after HIDAPI strips a zero report ID. Strict
    fixture padding and CRC comparisons intentionally improve host weaknesses;
    historic traces with undefined final bytes require explicit investigation.
    No transcript can establish hardware recovery or device flash addresses.
    Real wire captures can include previously pending reads, extra signed
    completions and reboot before all five activation deliveries. Those need
    separate evidence-aware analysis; see docs/installation.md.
    """
    parsed = parse_update_container(container)
    values = parsed["values"]
    if not values["firmware_enabled"] or values["scales_enabled"] or parsed["scale_bytes"]:
        raise ValueError("Only complete application-only exchanges are supported")
    if not parsed["application_bytes"]:
        raise ValueError("An empty application cannot be validated")
    reader = TranscriptReader(transcript)
    if values["filesystem_prepare_enabled"]:
        reader.expect_command("tx", "XCFIFCFF")
        reader.expect_command("rx", "FILSTATU")
        filesystem_status = reader.integer("rx", "filesystem status")
        reader.expect_command("rx", "SFIFDONE")
    else:
        filesystem_status = None
    reader.expect_command("tx", "XCFILNFS")
    reader.expect_command("tx", "DUMPMETA")
    metadata_markers = [reader.expect_metadata_marker("FWMETABG")]
    hardware_version = reader.integer("rx", "hardware version", values["required_hardware_version"])
    metadata = [reader.integer("rx", f"device metadata {i}") for i in range(14)]
    metadata_markers.append(reader.expect_metadata_marker("FWMETADN"))
    reader.expect_command("tx", "DONECMDS")
    reader.expect_command("tx", "XCSMLRTF" if values["firmware_encrypted"] else "XCSMDIUS")
    reader.expect_command("rx", "FWSETRD1")
    parameters = [values["metadata_word_0"], values["metadata_word_1"], parsed["application_bytes"],
                  values["metadata_word_2"], 1, 0, 1]
    for i, value in enumerate(parameters):
        reader.integer("tx", f"application parameter {i}", value)
    reader.expect_command("rx", "FWSETRD2")
    start, end = parsed["application_file_range"]
    app = container[start:end]
    for offset in range(0, len(app), 64):
        expected = app[offset:offset + 64].ljust(64, b"\0")
        if reader.report("tx", f"application block {offset // 64}") != expected:
            raise ValueError(f"Firmware bytes or deterministic final padding differ at offset {offset}")
    reader.expect_command("tx", "DONE0000")
    reader.expect_command("rx", "TRACRCVL")
    transferred_crc = reader.integer("rx", "transferred CRC")
    reader.expect_command("rx", "EXTCRCVL")
    external_crc = reader.integer("rx", "external CRC")
    reader.expect_command("rx", "FWDLYRDY")
    decoded_crc = None
    if values["firmware_encrypted"]:
        reader.expect_command("rx", "DCRCRCVL")
        decoded_crc = reader.integer("rx", "decoded CRC")
        reader.expect_command("rx", "FWDCPRDY")
    for _ in range(5):
        reader.expect_command("tx", "ZCSMCCOZ")
    if reader.index != len(reader.records):
        raise ValueError("Unexpected traffic after completion")
    expected_crc = crc32_normal(app)
    crc_consistent = transferred_crc == external_crc == expected_crc
    decoded_matches_word_1 = decoded_crc == values["metadata_word_1"] if values["firmware_encrypted"] else None
    return {"format": "thy333.transcript-validation", "version": 1,
            "offline_only": True, "flash_ready": False, "sequence_consistent": True,
            "verification_passed": crc_consistent and values["metadata_word_0"] == expected_crc and
                                   decoded_matches_word_1 is not False,
            "reports_verified": reader.index, "application_bytes_verified": len(app),
            "deterministic_final_padding_verified": True,
            "hardware_version": hardware_version, "device_metadata": metadata,
            "metadata_marker_signature_established": False,
            "metadata_markers": metadata_markers,
            "filesystem_status_opaque": filesystem_status,
            "crc_replies_match_calculated_payload": crc_consistent,
            "calculated_crc": hex(expected_crc), "transferred_crc": hex(transferred_crc),
            "external_crc": hex(external_crc), "decoded_crc_opaque": hex(decoded_crc) if decoded_crc is not None else None,
            "decoded_crc_matches_header_word_1": decoded_matches_word_1,
            "decoded_crc_binding_scope": "Observed stock relationship; does not establish device enforcement for other images",
            "device_flash_contents_verified": False, "independent_recovery_verified": False,
            "unresolved_installation_requirements": list(UNRESOLVED)}


def write_report(path, report, inputs):
    path = Path(path).resolve()
    if path.suffix.lower() != ".json":
        raise ValueError("Reports must use a .json output filename")
    if "resources" in {part.lower() for part in path.parts}:
        raise ValueError("Reports cannot be written inside Resources")
    if path in {Path(value).resolve() for value in inputs}:
        raise ValueError("Report would overwrite an input artifact")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect = sub.add_parser("inspect", help="Inspect an existing .333 without modifying it")
    inspect.add_argument("container", type=Path)
    inspect.add_argument("--output", type=Path)
    recipe = sub.add_parser("draft-recipe", help="Inspect application bytes and record unanswered header fields; emits JSON only")
    recipe.add_argument("container", type=Path, help="Raw application .bin linked at candidate 0x08040000")
    recipe.add_argument("--output", type=Path)
    transcript = sub.add_parser("verify-transcript", help="Check the strict normalized host fixture; real wire observations can differ")
    transcript.add_argument("container", type=Path)
    transcript.add_argument("transcript", type=Path)
    transcript.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        raw = args.container.read_bytes()
        if args.command == "inspect":
            report = inspect_container(raw)
        elif args.command == "draft-recipe":
            report = draft_recipe(raw)
        else:
            report = validate_transcript(raw, json.loads(args.transcript.read_text(encoding="utf-8")))
        report["application_path" if args.command == "draft-recipe" else "container_path"] = str(args.container.resolve())
        inputs = [args.container]
        if args.command == "verify-transcript":
            inputs.append(args.transcript)
            report["transcript_path"] = str(args.transcript.resolve())
            report["transcript_sha256"] = sha256(args.transcript.read_bytes())
        if args.output:
            write_report(args.output, report, inputs)
        else:
            print(json.dumps(report, indent=2))
        if args.command == "verify-transcript" and not report["verification_passed"]:
            print("Transcript order matched, but payload/header/device CRCs disagree", file=sys.stderr)
            return 2
        return 0
    except (OSError, ValueError) as exc:
        print(f"Offline validation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
