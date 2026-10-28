"""Deterministically project the canonical studio-menu.js contract into C data.

No device access or firmware build. Node evaluates only the local declarative
schema; C owns navigation, validation, editor transactions and configuration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
INSTANCE = {"global": 0, "arp": 1, "strummer": 2, "track": 3, "route": 4, "view": 5, "shortcut": 6, "pad": 7, "side": 8}
COUNTS = {"global": 1, "arp": 2, "strummer": 2, "track": 21, "route": 9, "view": 9, "shortcut": 12, "pad": 98, "side": 3}
PAD_FIELDS = {"sensitivity." + name for name in ("amount", "linkedRelease", "releaseGap", "attack", "release", "floor", "ceiling", "curve", "velocityMode", "velocityCurve", "softSpeed", "hardSpeed", "minVelocity", "maxVelocity", "reset", "inherit", "customCurve", "scope", "profileEnabled", "profileSplit", "pressureCurve", "aftertouchCurve")}
TYPES = {"toggle": 0, "select": 1, "number": 2, "text": 3, "action": 4, "info": 5}
STRUMMER_FIELDS = {"chords." + name for name in ("continuous", "strumLatch", "cutoff", "repeatDelay", "pattern", "destinations", "channel")}
TRACK_FIELDS = {"sequencer." + name for name in ("active", "interaction", "playMode", "quantize", "followScale", "root", "name", "position", "destinations", "channel", "scale", "source", "processor", "mute", "loopStart", "loopEnd", "length", "grid", "recordControllers", "sendPressure")}


def identifier(value):
    return re.sub(r"[^A-Za-z0-9]+", "_", value).upper()


def cstring(value):
    return "NULL" if value is None else json.dumps(str(value), ensure_ascii=False)


def oled_label(value):
    normalized = unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")
    return " ".join(normalized.split())


def family(row):
    if row["id"] == "sequencer.resetTrack":
        return "track"
    if row["id"] == "sensitivity.velocityMode":
        return "side"
    if row["id"] in PAD_FIELDS:
        return "pad"
    if row["id"] == "engines.assignment":
        return "shortcut"
    if row["id"].startswith("views."):
        return "view"
    if row["id"].startswith("arps.") and row["id"] not in ("arps.target", "arps.open", "arps.quickKeys"):
        return "arp"
    if row["id"] in STRUMMER_FIELDS:
        return "strummer"
    if row["id"] in TRACK_FIELDS:
        return "track"
    if row.get("binding") in ("midi.channel", "midi.destinations", "midi.velocity", "midi.pressure", "midi.pressureBend", "midi.bendRange", "midi.bendSetup"):
        return "route"
    return "global"


def load_scale_catalog(path=None):
    """Load display names/IDs without requiring the private musical corpus."""
    path = ROOT / "core/scale_catalog.json" if path is None else Path(path)
    scales = json.loads(path.read_text(encoding="utf-8"))
    if (not isinstance(scales, list) or len(scales) != 70 or
            any(not isinstance(item, dict) or set(item) != {"index", "name"} or
                type(item["index"]) is not int or not isinstance(item["name"], str) or
                not item["name"].strip() for item in scales)):
        raise ValueError("Scale catalog must contain 70 index/name display records.")
    scales = sorted(scales, key=lambda item: item["index"])
    if [item["index"] for item in scales] != list(range(70)):
        raise ValueError("Scale catalog changed; review dynamic group ID contracts.")
    return scales


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False,
        separators=(",", ":"), sort_keys=True).encode("utf-8")).hexdigest()


def protocol_projection(source, *, version, groups, rows, options, values, texts, scales):
    """Hash generated tables and storage geometry, excluding editorial notes."""
    return canonical_digest({"format": "thy666.menu-protocol", "version": version,
        "tables": source, "counts": {"groups": groups, "rows": rows,
            "options": options, "values": values, "texts": texts, "scales": scales},
        "instance_counts": COUNTS, "instance_kinds": INSTANCE, "row_types": TYPES})


def protocol_identity(projection_sha256, path=None):
    """Retain an installed wire identity only for its exact table projection.

    The original wire ID included editorial text. Its compatibility record is
    an explicit identity mapping, not a claim about the current source hash.
    Future table changes use their semantic digest as a new protocol identity.
    """
    path = ROOT / "core/menu_compatibility.json" if path is None else Path(path)
    record = json.loads(path.read_text(encoding="utf-8"))
    if (record.get("format") != "thy666.menu-compatibility" or record.get("version") != 1 or
            any(not isinstance(record.get(key), str) or
                re.fullmatch(r"[a-f0-9]{64}", record[key]) is None
                for key in ("projection_sha256", "protocol_identity"))):
        raise ValueError("Invalid menu compatibility identity record")
    return record["protocol_identity"] if record["projection_sha256"] == projection_sha256 else projection_sha256


def projection():
    source = (ROOT / "web/studio-menu.js").as_uri()
    javascript = "const {STUDIO_MENU}=await import(" + json.dumps(source) + ");process.stdout.write(JSON.stringify(STUDIO_MENU));"
    result = subprocess.run(["node", "--input-type=module", "-e", javascript], cwd=ROOT, capture_output=True, check=True, encoding="utf-8")
    menu = json.loads(result.stdout)
    scales = load_scale_catalog()
    source_digest = canonical_digest({"menu": menu, "scales": scales})
    rows, options, groups = [], [], []
    value_count = text_count = 0
    for gi, group in enumerate(menu["groups"]):
        advanced = [row.get("advanced") for row in group["rows"]]
        if (any(type(value) is not bool for value in advanced) or
                advanced != sorted(advanced) or (group['id'] not in ('engines','engineSettings') and (not any(advanced) or all(advanced)))):
            raise ValueError(f"{group['id']}: basic options must precede a contiguous advanced section")
        groups.append({**group, "first_row": len(rows)})
        for original in group["rows"]:
            item = {**original, "group_index": gi, "family": family(original), "option_first": len(options), "storage_offset": 65535, "text_offset": 65535}
            row_options = original.get("options", [])
            options.extend(row_options)
            dynamic = 2 if original.get("dynamic") == "scale-shortcuts" else int(original.get("dynamic") == "scales")
            item["dynamic"] = dynamic
            item["option_count"] = len(row_options)
            value = original["default"]
            if original["type"] == "select":
                value = value if dynamic else next(index for index, option in enumerate(row_options) if option["value"] == value)
            if original["type"] in ("action", "info", "text"):
                value = 0
            item["default_value"] = int(value)
            if original["status"] != "live" and original["type"] in ("toggle", "number", "select"):
                item["storage_offset"] = value_count
                value_count += COUNTS[item["family"]]
            if original["type"] == "text":
                item["text_offset"] = text_count
                text_count += COUNTS[item["family"]]
            rows.append(item)
    if len(rows) != 347 or len(groups) != 15:
        raise ValueError("Menu cardinality changed; review stable C row IDs and shared-contract tests.")
    if len({row["id"] for row in rows}) != len(rows):
        raise ValueError("Duplicate row IDs")
    header = ["/* SPDX-License-Identifier: GPL-3.0-or-later */", "/* Generated by tools/build_studio_schema.py; edit web/studio-menu.js. */", "#ifndef THY_MENU_DATA_H", "#define THY_MENU_DATA_H", "#include <stddef.h>", "#include <stdint.h>", '#define THY_MENU_SCHEMA_SHA256 "@PROTOCOL@"', '/* Source provenance and protocol compatibility are distinct identities. */', f'#define THY_MENU_SOURCE_SHA256 "{source_digest}"', '#define THY_MENU_PROJECTION_SHA256 "@PROJECTION@"', f'#define THY_MENU_VERSION {menu["version"]}u', f"#define THY_MENU_GROUP_COUNT {len(groups)}u", f"#define THY_MENU_ROW_COUNT {len(rows)}u", f"#define THY_MENU_OPTION_COUNT {len(options)}u", f"#define THY_MENU_VALUE_COUNT {value_count}u", f"#define THY_MENU_TEXT_COUNT {text_count}u", "#define THY_MENU_DEFAULT_SCALE_COUNT 70u", "enum thy_menu_type { THY_MENU_TOGGLE=0, THY_MENU_SELECT=1, THY_MENU_NUMBER=2, THY_MENU_TEXT=3, THY_MENU_ACTION=4, THY_MENU_INFO=5 };", "enum thy_menu_status { THY_MENU_DESIGN=0, THY_MENU_LIVE=1 };", "enum thy_menu_instance { THY_MENU_GLOBAL=0, THY_MENU_ARP=1, THY_MENU_STRUMMER=2, THY_MENU_TRACK=3, THY_MENU_ROUTE=4, THY_MENU_VIEW=5, THY_MENU_SHORTCUT=6, THY_MENU_PAD=7, THY_MENU_SIDE=8 };", "enum thy_menu_row_id {"]
    header.extend(f'    THY_ROW_{identifier(row["id"])} = {index},' for index, row in enumerate(rows))
    header.extend(["};", "typedef struct thy_menu_option { const char *key; const char *label; } thy_menu_option;", "typedef struct thy_menu_group { const char *id; const char *label; const char *short_label; uint16_t first_row; uint8_t row_count; } thy_menu_group;", "typedef struct thy_menu_row {", "    const char *id, *label, *short_label, *binding, *default_text;", "    int32_t default_value, min, max, step;", "    uint16_t option_first, option_count, storage_offset, text_offset;", "    uint8_t group, type, status, instance_kind, dynamic, max_length, advanced;", "} thy_menu_row;", "extern const thy_menu_group thy_menu_groups[THY_MENU_GROUP_COUNT];", "extern const thy_menu_row thy_menu_rows[THY_MENU_ROW_COUNT];", "extern const thy_menu_option thy_menu_options[THY_MENU_OPTION_COUNT];", "extern const char *const thy_menu_scale_names[THY_MENU_DEFAULT_SCALE_COUNT];", "#endif", ""])
    source_lines = ["/* SPDX-License-Identifier: GPL-3.0-or-later */", "/* Generated by tools/build_studio_schema.py; edit web/studio-menu.js. */", '#include "thy_menu_data.h"', "const thy_menu_group thy_menu_groups[THY_MENU_GROUP_COUNT] = {"]
    for group in groups:
        source_lines.append("    {" + ", ".join([cstring(group["id"]), cstring(group["label"]), cstring(oled_label(group.get("shortLabel", group["label"]))), str(group["first_row"]), str(len(group["rows"]))]) + "},")
    source_lines.extend(["};", "const thy_menu_option thy_menu_options[THY_MENU_OPTION_COUNT] = {"])
    source_lines.extend("    {" + cstring(option["value"]) + ", " + cstring(option["label"]) + "}," for option in options)
    source_lines.extend(["};", "const thy_menu_row thy_menu_rows[THY_MENU_ROW_COUNT] = {"])
    for row in rows:
        row_type = row["type"]
        minimum = row.get("min", 0)
        maximum = row.get("max", 1 if row_type == "toggle" else 70 if row["dynamic"] == 2 else 69 if row["dynamic"] else max(0, row["option_count"] - 1))
        strings = [cstring(row["id"]), cstring(row["label"]), cstring(oled_label(row.get("shortLabel", row["label"]))), cstring(row.get("binding")), cstring(row["default"] if row_type in ("text", "info") else None)]
        numbers = [row["default_value"], minimum, maximum, row.get("step", 1), row["option_first"], row["option_count"], row["storage_offset"], row["text_offset"], row["group_index"], TYPES[row_type], int(row["status"] == "live"), INSTANCE[row["family"]], row["dynamic"], row.get("maxLength", 48), int(row["advanced"])]
        source_lines.append("    {" + ", ".join(strings + [str(value) for value in numbers]) + "},")
    source_lines.extend(["};", "const char *const thy_menu_scale_names[THY_MENU_DEFAULT_SCALE_COUNT] = {"])
    source_lines.extend("    " + cstring(item["name"]) + "," for item in scales)
    source_lines.extend(["};", ""])
    table_source = "\n".join(source_lines)
    projection_digest = protocol_projection(table_source, version=menu["version"],
        groups=len(groups), rows=len(rows), options=len(options), values=value_count,
        texts=text_count, scales=len(scales))
    schema_hash = protocol_identity(projection_digest)
    header_source = "\n".join(header).replace("@PROTOCOL@", schema_hash).replace("@PROJECTION@", projection_digest)
    return {ROOT / "core/thy_menu_data.h": header_source, ROOT / "core/thy_menu_data.c": table_source,
            ROOT / "web/generated/menu-schema.js": '// Generated by tools/build_studio_schema.py.\n'
            '// Wire compatibility identity, followed by source and table provenance.\n'
            f'export const MENU_SCHEMA_SHA256 = "{schema_hash}";\n'
            f'export const MENU_SOURCE_SHA256 = "{source_digest}";\n'
            f'export const MENU_PROJECTION_SHA256 = "{projection_digest}";\n'
            f'export const SCALE_CATALOG = {json.dumps(scales)};\n'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if generated projection differs")
    args = parser.parse_args()
    generated = projection()
    for path, content in generated.items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                raise SystemExit(f"Stale C menu schema: {path.relative_to(ROOT)}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")
    header = generated[ROOT / "core/thy_menu_data.h"]
    groups = re.search(r"#define THY_MENU_GROUP_COUNT (\d+)u", header).group(1)
    rows = re.search(r"#define THY_MENU_ROW_COUNT (\d+)u", header).group(1)
    print(f"Shared menu schema: {groups} groups, {rows} rows; projection " + ("verified" if args.check else "written"))


if __name__ == "__main__":
    main()
