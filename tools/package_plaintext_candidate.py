#!/usr/bin/env python3
"""Build an OFFLINE plaintext .333 candidate bundle; never contact a device.

The candidate uses .333.candidate, requires explicit opaque metadata hypotheses
and preparation selection, and is never marked ready for installation. Output
is a new directory outside Resources. Interrupted bundles are kept for review;
manifest.json is written last and identifies a completed offline bundle only.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from thy333_container import HEADER_FIELDS, crc32_normal, parse_update_container
from thy333_container import UNRESOLVED, plausible_initial_stack

APP_BASE = 0x08040000
APP_BYTES_MAX = 0xC0000
VECTOR_BYTES = 98 * 4
BLOCK_BYTES = 64
CANDIDATE_NAME = "THY666-diagnostic.333.candidate"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def uint(value, bits, label):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(f"{label} must be an unsigned {bits}-bit integer")
    return value


def validate_application(app):
    if not isinstance(app, bytes) or not VECTOR_BYTES <= len(app) <= APP_BYTES_MAX:
        raise ValueError("Application must contain the 392-byte F405 vector table and fit the 768 KiB slot")
    vectors = struct.unpack_from("<98I", app)
    if not plausible_initial_stack(vectors[0]):
        raise ValueError("Initial stack must be 8-byte aligned in candidate SRAM/CCM")
    for index, pointer in enumerate(vectors[1:], 1):
        if pointer == 0 and index != 1:
            continue
        address = pointer & ~1
        if not pointer & 1 or not APP_BASE + VECTOR_BYTES <= address or address + 2 > APP_BASE + len(app):
            raise ValueError(f"Vector {index} must be Thumb code in the original application after its vector table")
    return {"initial_msp": f"0x{vectors[0]:08X}", "reset_handler": f"0x{vectors[1]:08X}",
            "vector_words_checked": 98, "original_image_bounds_checked": True,
            "hardware_abi_verified": False}


def encode_header(values):
    if set(values) != {name for _, _, name in HEADER_FIELDS}:
        raise ValueError("Header must contain exactly the recovered fields")
    header = bytearray(b"THY333")
    for offset, width, name in HEADER_FIELDS:
        uint(values[name], width * 4, name)
        if len(header) != offset:
            raise ValueError("Recovered header fields are not contiguous")
        header.extend(f"{values[name]:0{width}X}".encode("ascii"))
    if len(header) != 78:
        raise ValueError("Unexpected header length")
    return bytes(header)


def build_candidate(app, *, metadata_word_1, metadata_word_2, metadata_note,
                    filesystem_prepare, hardware_version, padding="reject",
                    word_0_policy="payload-crc", metadata_word_0=None,
                    scales_encrypted=0):
    """Return bytes and an audit, with no file/device operations or template copy."""
    vectors = validate_application(app)  # Must precede padding and its longer bound.
    for number, value in ((1, metadata_word_1), (2, metadata_word_2)):
        uint(value, 32, f"metadata word {number}")
    uint(hardware_version, 24, "hardware version")
    if hardware_version != 4:
        raise ValueError("This candidate supports only the observed hardware-version value 4")
    if type(filesystem_prepare) is not int or filesystem_prepare not in (0, 1):
        raise ValueError("Filesystem preparation must be explicitly 0 or 1")
    if type(scales_encrypted) is not int or scales_encrypted not in (0, 1):
        raise ValueError("Inactive scale encryption selector must be 0 or 1")
    if not isinstance(metadata_note, str) or not metadata_note.strip():
        raise ValueError("Opaque metadata requires a nonempty hypothesis/provenance note")
    if padding not in ("reject", "ff-to-report"):
        raise ValueError("Padding must be reject or ff-to-report")
    count = (-len(app)) % BLOCK_BYTES
    if count and padding == "reject":
        raise ValueError("Unaligned application: explicitly select ff-to-report padding for an offline hypothesis")
    padded = app + b"\xff" * count
    if len(padded) > APP_BYTES_MAX:
        raise ValueError("Report alignment exceeds the application slot")
    payload_crc = crc32_normal(padded)
    if word_0_policy == "payload-crc":
        if metadata_word_0 is not None:
            raise ValueError("payload-crc policy cannot also supply metadata word 0")
        word_0 = payload_crc
    elif word_0_policy == "explicit-hypothesis":
        word_0 = uint(metadata_word_0, 32, "explicit metadata word 0")
    else:
        raise ValueError("Word 0 policy must be payload-crc or explicit-hypothesis")
    values = dict(firmware_enabled=1, scales_enabled=0,
                  filesystem_prepare_enabled=filesystem_prepare, firmware_start=0,
                  firmware_end=len(padded), metadata_word_0=word_0,
                  metadata_word_1=metadata_word_1, metadata_word_2=metadata_word_2,
                  required_hardware_version=hardware_version, firmware_encrypted=0,
                  scales_encrypted=scales_encrypted)
    header = encode_header(values)
    container = header + padded
    parsed = parse_update_container(container)
    if parsed["values"] != values or parsed["application_file_range"] != [78, len(container)]:
        raise ValueError("Candidate does not round-trip through the independent parser")
    if parsed["scale_bytes"] or parsed["unencrypted_updater_overread_bytes"]:
        raise ValueError("Candidate has unexplained trailing data or a partial plaintext report")
    parameters = [word_0, metadata_word_1, len(padded), metadata_word_2, 1, 0, 1]
    manifest = {
        "format": "thy333.plaintext-packaging-candidate", "version": 1,
        "offline_only": True, "flash_ready": False, "device_execution_ready": False,
        "device_compatibility_verified": False, "container_emitted": True,
        "candidate_suffix": ".333.candidate", "application_base_candidate": f"0x{APP_BASE:08X}",
        "original_application": {"bytes": len(app), "sha256": sha256(app),
                                 "crc32_normal_zero_init": f"0x{crc32_normal(app):08X}"},
        "packaged_application": {"bytes": len(padded), "sha256": sha256(padded),
                                 "crc32_normal_zero_init": f"0x{payload_crc:08X}"},
        "padding": {"policy": padding, "bytes_added": count, "byte_hex": "FF" if count else None,
                    "application_offset": len(app), "inside_declared_application": True,
                    "length_and_crc_recalculated": True, "device_padding_rule_verified": False},
        "container": {"bytes": len(container), "sha256": sha256(container),
                      "header_hex": header.hex(), "fields": parsed["fields"],
                      "byte_regions_end_exclusive": {"header": [0, 78], "skipped_prefix": [78, 78],
                                                     "application": [78, len(container)],
                                                     "scale_tail": [len(container), len(container)]}},
        "vectors": vectors,
        "host_transfer": {"plaintext_token": "XCSMDIUS", "seven_parameters_u32": parameters,
                          "data_report_bytes": 64, "data_reports": len(padded) // 64,
                          "partial_final_report": False, "host_final_overread_bytes": 0,
                          "filesystem_prepare_enabled": bool(filesystem_prepare),
                          "scale_update_enabled": False,
                          "inactive_scale_encryption_selector": scales_encrypted,
                          "observed_plaintext_capture": False},
        "hypotheses": {"metadata_words_1_and_2": {"word_1": f"0x{metadata_word_1:08X}",
                                                 "word_2": f"0x{metadata_word_2:08X}",
                                                 "note": metadata_note.strip(), "device_semantics_verified": False},
                       "word_0": {"policy": word_0_policy, "value": f"0x{word_0:08X}",
                                  "matches_payload_crc": word_0 == payload_crc,
                                  "device_enforcement_verified": False},
                       "hardware_version": {"value": hardware_version, "physical_board_verified": False},
                       "filesystem_preparation_compatibility_verified": False},
        "unresolved_installation_requirements": list(UNRESOLVED),
        "limits": ["Offline structure and self-consistency are not proof the device accepts this header",
                   "Computed word 0 and explicitly supplied legacy word 0 are competing offline hypotheses, not established plaintext requirements",
                   "Appending FF changes the declared application length and its CRC; acceptance remains untested",
                   "Disabling preparation does not prove external flash remains unchanged during installation",
                   "Enabled preparation can alter external data; no live preparation or update was performed",
                   "No readback, application-independent recovery or bootloader erase boundary is established",
                   "No bootloader, scale payload, entitlement patch or original application code is added"]
    }
    return container, padded, manifest


def write_bundle(output, application_path, *, reference_path=None, **options):
    application_path = Path(application_path).resolve()
    paths = [application_path]
    if reference_path is not None:
        paths.append(Path(reference_path).resolve())
    output = Path(output).resolve()
    if "resources" in {part.lower() for part in output.parts}:
        raise ValueError("Candidate output cannot be inside Resources")
    if any(output == path or output in path.parents for path in paths):
        raise ValueError("Bundle directory overlaps an input artifact")
    if output.exists():
        raise FileExistsError("Bundle output must be a new directory")
    originals = {path: path.read_bytes() for path in paths}
    container, padded, manifest = build_candidate(originals[application_path], **options)
    manifest["sources"] = [{"path": str(path), "bytes": len(raw), "sha256": sha256(raw)}
                           for path, raw in originals.items()]
    manifest["generator_sources"] = [{"path": str(path.resolve()), "sha256": sha256(path.read_bytes())}
                                     for path in (Path(__file__), Path(__file__).with_name("thy333_container.py"))]
    if reference_path is not None:
        reference = parse_update_container(originals[paths[-1]])
        if reference["values"]["firmware_encrypted"] or not reference["application_bytes"]:
            raise ValueError("Comparison reference must contain a nonempty plaintext application")
        actual = parse_update_container(container)
        manifest["reference_comparison"] = {
            "semantics_verified_by_comparison": False,
            "reference_byte_regions_end_exclusive": {
                "header": [0, 78], "skipped_prefix": [78, reference["application_file_range"][0]],
                "application": reference["application_file_range"], "scale_tail": reference["scale_file_range"]},
            "reference_skipped_prefix_bytes": reference["values"]["firmware_start"],
            "reference_scale_bytes": reference["scale_bytes"],
            "reference_prefix_and_tail_copied": False,
            "fields": [{"name": name, "reference": reference["values"][name],
                        "candidate": actual["values"][name],
                        "equal": reference["values"][name] == actual["values"][name]}
                       for _, _, name in HEADER_FIELDS],
            "reference_overread_bytes": reference["unencrypted_updater_overread_bytes"],
            "reference_word_0_matches_crc": reference["values"]["metadata_word_0"] ==
                                             int(reference["application_crc32_normal_zero_init"], 16)}
    # No sources are written. The last manifest is the completion record; keep
    # partial directories after I/O failure instead of deleting evidence.
    output.mkdir(parents=True, exist_ok=False)
    artifacts = {CANDIDATE_NAME: container, "application.aligned.bin": padded}
    manifest["artifacts"] = []
    for name, raw in artifacts.items():
        with (output / name).open("xb") as handle:
            handle.write(raw)
        if (output / name).read_bytes() != raw:
            raise OSError(f"Output verification failed: {name}")
        manifest["artifacts"].append({"name": name, "bytes": len(raw), "sha256": sha256(raw)})
    if any(path.read_bytes() != raw for path, raw in originals.items()):
        raise OSError("An input changed during packaging; bundle is incomplete")
    manifest["source_bytes_unchanged_after_packaging"] = True
    manifest["bundle_complete"] = True
    with (output / "manifest.json").open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("application", type=Path, help="Independent raw F405 application linked at 0x08040000")
    parser.add_argument("--output", required=True, type=Path, help="New offline bundle directory outside Resources")
    parser.add_argument("--metadata-word-1", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--metadata-word-2", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--word-0-policy", required=True, choices=("payload-crc", "explicit-hypothesis"))
    parser.add_argument("--metadata-word-0", type=lambda v: int(v, 0), help="Required only for explicit-hypothesis policy")
    parser.add_argument("--metadata-note", required=True, help="Explicit hypothesis/provenance; never verified by reuse")
    parser.add_argument("--filesystem-prepare", required=True, type=int, choices=(0, 1))
    parser.add_argument("--scales-encrypted", type=int, choices=(0, 1), default=0,
                        help="Inactive selector: scale updates remain disabled; either selector value is retained while scale updates are disabled")
    parser.add_argument("--hardware-version", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--padding", choices=("reject", "ff-to-report"), default="reject")
    parser.add_argument("--reference-container", type=Path, help="Optional plaintext comparison only; never copies fields")
    args = parser.parse_args(argv)
    try:
        options = vars(args).copy()
        application = options.pop("application")
        output = options.pop("output")
        reference = options.pop("reference_container")
        report = write_bundle(output, application, reference_path=reference, **options)
        print(f"Offline candidate: {len(report['artifacts'])} artifacts + manifest; flash_ready=False; no device access")
        return 0
    except (OSError, ValueError) as exc:
        print(f"Offline packaging failed: {exc}. Any partial output is retained, not installation-qualified.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
