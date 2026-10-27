#!/usr/bin/env python3
"""Validate previously acquired THY333 backup files, without device access.

The tool accepts two separate read sessions with raw identity/protection evidence.
Matching bytes establish repeatability, not an electrically verified acquisition
or tested restoration. It never reads a connected controller or writes memory.
"""

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

from thy333_container import plausible_initial_stack, write_report


INTERNAL_BASE = 0x08000000
INTERNAL_BYTES = 1024 * 1024
APPLICATION_OFFSET = 0x40000
EXTERNAL_BYTES = 16 * 1024 * 1024
SECTOR_BYTES = [0x4000] * 4 + [0x10000] + [0x20000] * 7


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_record(path, data):
    return {"path": str(path), "bytes": len(data), "sha256": sha256(data)}


def vector_check(raw, offset, low_address, high_address, label):
    stack, reset = struct.unpack_from("<II", raw, offset)
    if not plausible_initial_stack(stack):
        raise ValueError(f"{label} initial stack is outside candidate SRAM or misaligned")
    if not reset & 1 or not low_address <= (reset & ~1) < high_address:
        raise ValueError(f"{label} reset vector is outside the expected Thumb region")
    return {"offset": hex(offset), "address": hex(INTERNAL_BASE + offset),
            "initial_msp": hex(stack), "reset_handler": hex(reset),
            "plausible": True, "firmware_authenticity_proven": False}


def _integer(value, label):
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError as exc:
            raise ValueError(f"Invalid {label}") from exc
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    return value


def _timestamp(value):
    if not isinstance(value, str):
        raise ValueError("Each read needs a captured_at timestamp with timezone")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError("Invalid read timestamp") from exc
    if parsed.tzinfo is None:
        raise ValueError("Read timestamps need a timezone")
    return parsed


def validate_backup(acquisition_path):
    acquisition_path = Path(acquisition_path).resolve()
    capture = json.loads(acquisition_path.read_text(encoding="utf-8"))
    if not isinstance(capture, dict) or capture.get("format") != "thy333.backup-acquisition" or capture.get("version") != 1:
        raise ValueError("Expected thy333.backup-acquisition version 1")
    if capture.get("independent_reads_attested") is not True:
        raise ValueError("Two independently performed reads must be attested; copied files are not a second read")
    board = capture.get("board")
    if not isinstance(board, dict) or board.get("markings_physically_confirmed") is not True:
        raise ValueError("Confirm physical MCU/flash/PCB markings before validating this target")
    mcu_marking, flash_marking = board.get("mcu_marking"), board.get("flash_marking")
    if not isinstance(mcu_marking, str) or not re.fullmatch(r"STM32F405VGT[67]", mcu_marking, re.I):
        raise ValueError("This validator supports the confirmed STM32F405VG target only")
    if not isinstance(flash_marking, str) or "25Q128JV" not in flash_marking.upper():
        raise ValueError("Confirm the W25Q128JV-family external flash marking")
    if not board.get("pcb_marking"):
        raise ValueError("PCB name/revision must be recorded")
    reads = capture.get("reads")
    if not isinstance(reads, list) or len(reads) != 2:
        raise ValueError("Exactly two independent read sessions are required")
    input_paths = {acquisition_path}
    used_paths = set()
    sessions = []
    identity_keys = None
    previous_raw = None
    seen_ids, seen_times = set(), set()
    region_records = None
    vectors = None
    for read in reads:
        if not isinstance(read, dict) or not isinstance(read.get("id"), str) or not read["id"].strip() or read["id"] in seen_ids:
            raise ValueError("Each read needs a distinct nonempty session ID")
        seen_ids.add(read["id"])
        instant = _timestamp(read.get("captured_at"))
        if instant in seen_times:
            raise ValueError("Read sessions must have distinct acquisition timestamps")
        seen_times.add(instant)
        if read.get("method") not in ("swd-read-only", "rom-dfu-read-only", "verified-application-read-only"):
            raise ValueError("Record an explicit read-only acquisition method")
        identity = read.get("identity")
        if not isinstance(identity, dict):
            raise ValueError("Each read needs its captured chip identity and protection state")
        uid = identity.get("uid_hex")
        if not isinstance(uid, str):
            raise ValueError("Each read needs a hexadecimal UID string")
        uid = uid.lower()
        if not re.fullmatch(r"[0-9a-f]{24}", uid) or uid in ("0" * 24, "f" * 24):
            raise ValueError("Each read needs the nonblank 96-bit STM32 unique ID")
        if _integer(identity.get("device_id"), "device ID") & 0xFFF != 0x413:
            raise ValueError("Captured device ID does not match STM32F405/407 family")
        if _integer(identity.get("internal_flash_kib"), "internal flash density") != 1024:
            raise ValueError("Captured internal flash density is not 1024 KiB")
        jedec = identity.get("external_jedec_id_hex")
        if not isinstance(jedec, str) or jedec.lower() not in ("ef4018", "ef7018"):
            raise ValueError("Captured external JEDEC ID does not match expected W25Q128JV capacity")
        if _integer(identity.get("rdp_raw"), "raw RDP value") != 0xAA or _integer(identity.get("rdp_level"), "RDP level") != 0:
            raise ValueError("Protected or unknown target: do not unlock or erase it to obtain a backup")
        current_keys = {key: identity[key] for key in ("uid_hex", "device_id", "internal_flash_kib", "external_jedec_id_hex", "rdp_raw", "rdp_level")}
        # Normalize representations when comparing independently logged identity.
        current_keys.update(uid_hex=uid, device_id=_integer(identity["device_id"], "device ID"),
                            internal_flash_kib=1024, external_jedec_id_hex=jedec.lower(), rdp_raw=0xAA, rdp_level=0)
        if identity_keys is not None and current_keys != identity_keys:
            raise ValueError("Chip identity/protection evidence differs between reads")
        identity_keys = current_keys
        blobs, records = {}, {}
        for name in ("internal", "external", "option_bytes_raw", "identity_log", "session_log"):
            supplied = read.get(name)
            if not isinstance(supplied, str) or not supplied:
                raise ValueError(f"Each read needs {name} file evidence")
            path = (acquisition_path.parent / supplied).resolve()
            if path in used_paths:
                raise ValueError("Each independent read must have its own files, including logs and option bytes")
            used_paths.add(path)
            input_paths.add(path)
            raw = path.read_bytes()
            if not raw:
                raise ValueError(f"Empty {name} evidence")
            blobs[name] = raw
            records[name] = file_record(path, raw)
        if len(blobs["internal"]) != INTERNAL_BYTES:
            raise ValueError("Internal dump must cover exactly 0x08000000..0x08100000 (1 MiB)")
        if len(blobs["external"]) != EXTERNAL_BYTES:
            raise ValueError("External dump must cover all 16 MiB, not only the recovered chord region")
        if blobs["external"] in (b"\0" * EXTERNAL_BYTES, b"\xff" * EXTERNAL_BYTES):
            raise ValueError("Blank/constant external dump is not a credible populated-device backup")
        vectors = [vector_check(blobs["internal"], 0, INTERNAL_BASE, INTERNAL_BASE + INTERNAL_BYTES, "Boot"),
                   vector_check(blobs["internal"], APPLICATION_OFFSET, INTERNAL_BASE + APPLICATION_OFFSET, INTERNAL_BASE + INTERNAL_BYTES, "Application")]
        if previous_raw is not None:
            for name in ("internal", "external", "option_bytes_raw"):
                if previous_raw[name] != blobs[name]:
                    raise ValueError(f"Independent {name} reads differ; preserve both and investigate")
        previous_raw = blobs
        sessions.append({"id": read["id"], "captured_at": read["captured_at"],
                         "method": read["method"], "identity": current_keys, "files": records})
    internal = previous_raw["internal"]
    external = previous_raw["external"]
    offset = 0
    internal_sectors = []
    for index, length in enumerate(SECTOR_BYTES):
        internal_sectors.append({"sector": index, "address": hex(INTERNAL_BASE + offset),
                                 "bytes": length, "sha256": sha256(internal[offset:offset + length])})
        offset += length
    assert offset == INTERNAL_BYTES
    external_blocks = [{"offset": hex(offset), "bytes": 65536, "sha256": sha256(external[offset:offset + 65536])}
                       for offset in range(0, EXTERNAL_BYTES, 65536)]
    region_records = {"internal_sectors": internal_sectors,
                      "lower_internal_region": {"address": hex(INTERNAL_BASE), "bytes": APPLICATION_OFFSET,
                                                "sha256": sha256(internal[:APPLICATION_OFFSET])},
                      "application_region": {"address": hex(INTERNAL_BASE + APPLICATION_OFFSET), "bytes": INTERNAL_BYTES - APPLICATION_OFFSET,
                                             "sha256": sha256(internal[APPLICATION_OFFSET:])},
                      "external_64k_blocks": external_blocks}
    return {"format": "thy333.backup-validation", "version": 1,
            "offline_only": True, "backup_bytes_verified": True,
            "independent_reads_attested": True, "independence_machine_proven": False,
            "restoration_verified": False, "flash_ready": False,
            "board": board, "sessions": sessions, "vector_checks": vectors,
            "regions": region_records, "source_acquisition": file_record(acquisition_path, acquisition_path.read_bytes()),
            "limits": ["Captured identity and acquisition independence are supplied evidence; the tool cannot prove electrical wiring or read coverage",
                       "Matching dumps and plausible vectors do not prove that a restoration will work",
                       "OTP, backup SRAM, external status/security registers and auxiliary-controller firmware are separate acquisition questions"],
            "input_paths": sorted(str(path) for path in input_paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("acquisition", type=Path, help="JSON manifest of two independently captured read sessions")
    parser.add_argument("--output", type=Path, help="New report .json outside Resources; never overwrites existing files")
    args = parser.parse_args()
    try:
        report = validate_backup(args.acquisition)
        if args.output:
            write_report(args.output, report, report["input_paths"])
        else:
            print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError) as exc:
        print(f"Offline backup validation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
