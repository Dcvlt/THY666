"""Public menu generation must not depend on ignored firmware/data assets."""
import contextlib
import copy
import io
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_studio_schema as schema


class PublicSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = ROOT / "build/test-studio-schema"
        cls.scratch.mkdir(parents=True, exist_ok=True)

    def test_catalog_contains_display_metadata_only(self):
        scales = schema.load_scale_catalog()
        self.assertEqual([item["index"] for item in scales], list(range(70)))
        self.assertTrue(all(set(item) == {"index", "name"} for item in scales))

    def test_catalog_rejects_invalid_or_musical_records(self):
        original = schema.load_scale_catalog()
        variants = [original[:-1], {"groups": original}]
        for field, value in (("index", True), ("index", 69), ("name", " "),
                             ("notes", [60, 64, 67])):
            changed = copy.deepcopy(original)
            changed[0][field] = value
            variants.append(changed)
        with tempfile.TemporaryDirectory(dir=self.scratch) as folder:
            path = Path(folder) / "catalog.json"
            for index, value in enumerate(variants):
                with self.subTest(variant=index):
                    path.write_text(json.dumps(value), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        schema.load_scale_catalog(path)

    def copy_public_inputs(self, target_root):
        for name in ("web/studio-menu.js", "core/scale_catalog.json", "core/menu_compatibility.json"):
            target = target_root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        (target_root / "package.json").write_text('{"type":"module"}\n', encoding="utf-8")

    def test_protocol_identity_requires_exact_projection_and_valid_metadata(self):
        contract = json.loads((ROOT / "core/menu_compatibility.json").read_text(encoding="utf-8"))
        self.assertEqual(schema.protocol_identity(contract["projection_sha256"]), contract["protocol_identity"])
        changed = "0" * 64
        self.assertNotEqual(changed, contract["projection_sha256"])
        self.assertEqual(schema.protocol_identity(changed), changed)
        with tempfile.TemporaryDirectory(dir=self.scratch) as folder:
            path = Path(folder) / "compatibility.json"
            for key, value in (("format", "unknown"), ("version", 2),
                               ("projection_sha256", "0"), ("protocol_identity", None)):
                broken = {**contract, key: value}
                path.write_text(json.dumps(broken), encoding="utf-8")
                with self.subTest(field=key), self.assertRaises(ValueError):
                    schema.protocol_identity(contract["projection_sha256"], path)

    @unittest.skipUnless(shutil.which("node"), "Node.js is required for schema generation")
    def test_editorial_changes_preserve_wire_identity_but_semantic_changes_do_not(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as folder:
            public_root = Path(folder)
            self.copy_public_inputs(public_root)
            menu_path = public_root / "web/studio-menu.js"
            original = menu_path.read_text(encoding="utf-8")
            with patch.object(schema, "ROOT", public_root):
                before = schema.projection()
                menu_path.write_text(original.replace(
                    "Delay between chord-tone attacks, in milliseconds.",
                    "A documentation-only explanation of the same delay."), encoding="utf-8")
                editorial = schema.projection()
                menu_path.write_text(original.replace(
                    "number('chords.strumDelay', 'Strum delay · ms', 24,",
                    "number('chords.strumDelay', 'Strum delay · ms', 25,"), encoding="utf-8")
                semantic = schema.projection()
            header_path = public_root / "core/thy_menu_data.h"
            tables_path = public_root / "core/thy_menu_data.c"
            def identity(generated, field):
                return re.search(r'#define THY_MENU_' + field + r' "([a-f0-9]+)"',
                                 generated[header_path]).group(1)
            self.assertEqual(before[tables_path], editorial[tables_path])
            self.assertEqual(identity(before, "SCHEMA_SHA256"), identity(editorial, "SCHEMA_SHA256"))
            self.assertNotEqual(identity(before, "SOURCE_SHA256"), identity(editorial, "SOURCE_SHA256"))
            self.assertNotEqual(before[tables_path], semantic[tables_path])
            self.assertNotEqual(identity(before, "SCHEMA_SHA256"), identity(semantic, "SCHEMA_SHA256"))

    @unittest.skipUnless(shutil.which("node"), "Node.js is required for schema generation")
    def test_fresh_source_tree_reproduces_existing_schema_bytes(self):
        outputs = ("core/thy_menu_data.c", "core/thy_menu_data.h",
                   "web/generated/menu-schema.js")
        expected = {name: (ROOT / name).read_bytes() for name in outputs[:2]}
        self.assertIn(b"max_length, advanced;", expected["core/thy_menu_data.h"])
        self.assertIn(b"THY_ROW_ENGINES_PLAY = 0", expected["core/thy_menu_data.h"])
        self.assertNotIn(b"THY_ROW_ENGINES_SCALES", expected["core/thy_menu_data.h"])
        digests = {name: re.search(rb'#define THY_MENU_' + name.encode() + rb' "([a-f0-9]+)"',
                   expected["core/thy_menu_data.h"]).group(1).decode("ascii")
                   for name in ("SCHEMA_SHA256", "SOURCE_SHA256", "PROJECTION_SHA256")}
        expected[outputs[2]] = ('// Generated by tools/build_studio_schema.py.\n'
            '// Wire compatibility identity, followed by source and table provenance.\n' +
            ''.join('export const MENU_' + name + ' = "' + value + '";\n'
                    for name, value in digests.items()) +
            "export const SCALE_CATALOG = " + json.dumps(schema.load_scale_catalog()) + ";\n").encode("utf-8")
        with tempfile.TemporaryDirectory(dir=self.scratch) as folder:
            public_root = Path(folder)
            self.copy_public_inputs(public_root)
            self.assertFalse((public_root / "web/generated").exists())
            self.assertFalse((public_root / "analysis").exists())
            self.assertFalse((public_root / "Resources").exists())
            with patch.object(schema, "ROOT", public_root), \
                    patch.object(sys, "argv", ["build_studio_schema.py"]), \
                    contextlib.redirect_stdout(io.StringIO()):
                schema.main()
            for name, content in expected.items():
                self.assertEqual((public_root / name).read_bytes(), content, name)
            self.assertFalse((public_root / "web/generated/chords.runtime.json").exists())


if __name__ == "__main__":
    unittest.main()
