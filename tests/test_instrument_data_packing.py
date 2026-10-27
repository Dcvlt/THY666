"""Verify flash-only packing against the original, lossless local catalogue."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_instrument_data import (  # noqa: E402
    EMPTY_RECORD, FLASH_RECORD_BYTES, PREVIOUS_RECORD_BYTES, ROOTS,
    catalog_root_offsets, flash_pad_bytes, generate, pack_catalog,
)
from build_runtime_data import DEFAULT_ARCHIVE, project_archive  # noqa: E402


def record(notes, used, color=255):
    return {'midi_slots': notes, 'notes': [{'slot': slot, 'midi': notes[slot]} for slot in used],
            'color_id': color}


def block(source, declaration):
    start = source.index(declaration) + len(declaration)
    return source[start:source.index('};', start)]


def numeric_rows(source, declaration):
    return [tuple(map(int, re.findall(r'\d+', line)))
            for line in block(source, declaration).splitlines() if line.strip()]


class FlashPadTests(unittest.TestCase):
    def test_zero_validity_and_inactive_nonzero_slots_are_preserved(self):
        original = record([0, 127, 1, 126, 64, 32, 99], [0, 6], 255)
        before = copy.deepcopy(original)
        self.assertEqual(flash_pad_bytes(original), bytes([0, 127, 1, 126, 64, 32, 99, 65, 255]))
        self.assertEqual(original, before)
        self.assertEqual(flash_pad_bytes(record([127] * 7, [], 0)), bytes([127] * 7 + [0, 0]))

    def test_unrepresentable_or_incoherent_records_fail_without_truncation(self):
        base = record([0, 12, 24, 48, 60, 96, 127], [0, 6], 12)
        for slot in range(7):
            for value in (-1, 128, True, 1.5, '60'):
                invalid = copy.deepcopy(base)
                invalid['midi_slots'][slot] = value
                with self.subTest(slot=slot, value=value), self.assertRaises(ValueError):
                    flash_pad_bytes(invalid)
        for color in (-1, 256, True, 1.5, '12'):
            invalid = {**base, 'color_id': color}
            with self.subTest(color=color), self.assertRaises(ValueError):
                flash_pad_bytes(invalid)
        for changes in ({'midi_slots': [1] * 6}, {'midi_slots': [1] * 8},
                        {'notes': [{'slot': -1, 'midi': 0}]},
                        {'notes': [{'slot': 7, 'midi': 0}]},
                        {'notes': [{'slot': 0, 'midi': 0}] * 2},
                        {'notes': [{'slot': 0, 'midi': 1}]}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                flash_pad_bytes({**base, **changes})


class CatalogDictionaryTests(unittest.TestCase):
    def test_interning_retains_inactive_slots_masks_colors_and_empty_identity(self):
        original = record([0, 127, 1, 126, 64, 32, 99], [0, 6], 255)
        duplicate = {**copy.deepcopy(original), 'archive_only_metadata': 'distinct projection identity'}
        masked = {**copy.deepcopy(original), 'notes': []}
        colored = {**copy.deepcopy(original), 'color_id': 254}
        inactive = copy.deepcopy(original)
        inactive['midi_slots'][1] = 126
        zero = record([0] * 7, [], 0)
        records = [original, duplicate, masked, colored, inactive, zero]
        pads = [[0, n] for n in range(len(records))] + [None] * (96 - len(records))
        scales = [{'pads': pads}, {'pads': copy.deepcopy(pads)}]
        before = copy.deepcopy((records, scales))
        result = pack_catalog(records, scales)
        self.assertEqual((records, scales), before)
        self.assertEqual(result, pack_catalog(records, scales), 'Stable first-occurrence interning is deterministic')
        self.assertEqual(len(result['records']), 5)
        self.assertEqual(result['record_remap'], [0, 0, 1, 2, 3, 4])
        self.assertEqual(result['layouts'][0], result['layouts'][1])
        references = [ref for block_id in result['layouts'][0] for ref in result['blocks'][block_id]]
        self.assertEqual(references[:6], [0, 0, 1, 2, 3, 4])
        self.assertEqual(references[6:], [EMPTY_RECORD] * 74)
        for original_ref, packed_ref in zip(pads[:6], references[:6]):
            self.assertEqual(result['records'][packed_ref], flash_pad_bytes(records[original_ref[1]]))
        self.assertEqual(result['records'][references[5]], bytes(9))
        self.assertNotEqual(references[5], EMPTY_RECORD, 'A real silent record is not an absent cell')

    def test_malformed_references_and_nonempty_implicit_tail_are_rejected(self):
        records = [record([0] * 7, [], 0)]
        for invalid in ([], [0], [0, 0, 0], 'bad', {}, [0, -1], [0, 1], [0, True], [0, 0.0]):
            pads = [None] * 96
            pads[0] = invalid
            with self.subTest(ref=invalid), self.assertRaises(ValueError):
                pack_catalog(records, [{'pads': pads}])
        for index in range(80, 96):
            pads = [None] * 96
            pads[index] = [0, 0]  # Even an all-zero record is semantically assigned.
            with self.subTest(tail=index), self.assertRaises(ValueError):
                pack_catalog(records, [{'pads': pads}])
        for pads in (None, [], [None] * 95, [None] * 97):
            with self.subTest(pads=pads), self.assertRaises(ValueError):
                pack_catalog(records, [{'pads': pads}])

    def test_arithmetic_root_lookup_requires_the_complete_same_permutation(self):
        roots = list(ROOTS)
        scales = [{'index': n, 'group_index': n // 12, 'root_label': roots[n % 12]}
                  for n in range(24)]
        groups = [{'index': n, 'scale_indices': list(range(n * 12, n * 12 + 12))} for n in range(2)]
        self.assertEqual(catalog_root_offsets(scales, groups), list(range(12)))
        changed = copy.deepcopy(scales)
        changed[12]['root_label'], changed[13]['root_label'] = changed[13]['root_label'], changed[12]['root_label']
        with self.assertRaises(ValueError):
            catalog_root_offsets(changed, groups)
        changed = copy.deepcopy(scales)
        changed[12]['group_index'] = 0
        with self.assertRaises(ValueError):
            catalog_root_offsets(changed, groups)
        changed_groups = copy.deepcopy(groups)
        changed_groups[1]['scale_indices'][0] = 0
        with self.assertRaises(ValueError):
            catalog_root_offsets(scales, changed_groups)


@unittest.skipUnless(DEFAULT_ARCHIVE.exists(), 'The lossless local catalogue is not distributed in the repository')
class FullCataloguePackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = DEFAULT_ARCHIVE.read_bytes()
        cls.projection = project_archive(json.loads(raw),
            archive_sha256=hashlib.sha256(raw).hexdigest(), archive_size_bytes=len(raw))
        temporary_root = (ROOT / '.tools/tmp').resolve()
        temporary_root.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix='catalogue-packing-', dir=temporary_root)
        cls.output = Path(cls.temporary.name).resolve()
        if not cls.output.is_relative_to(temporary_root):
            raise RuntimeError('Catalogue test temporary path escaped its workspace')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.report = generate(cls.output)
        cls.source = (cls.output / 'instrument_data.c').read_text(encoding='utf-8')
        cls.records = numeric_rows(cls.source, 'static const thy_flash_pad records[] = {')
        cls.blocks = numeric_rows(cls.source, 'static const uint16_t pad_blocks[][8] = {')
        cls.layout_blocks = numeric_rows(cls.source, 'static const uint16_t layout_blocks[840][10] = {')
        cls.layouts = [tuple(ref for block_id in row for ref in cls.blocks[block_id]) + (65535,) * 16
                       for row in cls.layout_blocks]

    def test_every_record_slot_mask_color_and_storage_receipt_match_the_archive(self):
        archived_records = self.projection['pad_records']
        expected = list(dict.fromkeys(flash_pad_bytes(record) for record in archived_records))
        self.assertEqual(len(self.records), 5234)
        self.assertEqual([bytes(value) for value in self.records], expected)
        for index, (actual, packed) in enumerate(zip(self.records, expected)):
            with self.subTest(record=index):
                self.assertEqual(bytes(actual), packed)
        receipt = self.report['record_storage']
        packed = b''.join(bytes(value) for value in self.records)
        self.assertEqual(receipt['record_bytes'], FLASH_RECORD_BYTES)
        self.assertEqual(receipt['packed_table_bytes'], len(packed))
        self.assertEqual(receipt['projection_records'], len(archived_records))
        self.assertEqual(receipt['records'], len(expected))
        self.assertEqual(receipt['previous_table_bytes'], len(archived_records) * PREVIOUS_RECORD_BYTES)
        self.assertEqual(receipt['table_bytes_saved'], len(archived_records) * PREVIOUS_RECORD_BYTES - len(packed))
        self.assertEqual(receipt['serialized_dedup_saved_bytes'], 23256)
        self.assertEqual(receipt['packed_records_sha256'], hashlib.sha256(packed).hexdigest())
        self.assertEqual(self.report['generated_source_sha256'],
                         hashlib.sha256((self.output / 'instrument_data.c').read_bytes()).hexdigest())
        self.assertEqual(json.loads((self.output / 'instrument-data.json').read_text(encoding='utf-8')), self.report)
        self.assertIn('_Static_assert(sizeof(thy_flash_pad) == 9u', self.source)
        self.assertNotIn('__attribute__((packed))', self.source)

    def test_every_layout_reference_and_expanded_cell_is_lossless(self):
        self.assertEqual(len(self.layouts), 840)
        active = silent = absent = 0
        for scale, references in zip(self.projection['scales'], self.layouts):
            self.assertEqual(len(references), 96)
            for pad, (original, actual) in enumerate(zip(scale['pads'], references)):
                if original is None:
                    self.assertEqual(actual, 65535, (scale['index'], pad))
                    absent += 1
                    silent += 1
                    continue
                archived = self.projection['pad_records'][original[1]]
                expanded = self.records[actual]
                self.assertEqual(expanded[:7], tuple(archived['midi_slots']))
                self.assertEqual(expanded[7:], (sum(1 << n['slot'] for n in archived['notes']), archived['color_id']))
                active += bool(expanded[7])
                silent += not expanded[7]
        self.assertEqual((active, silent, absent), (63024, 17616, 13440))

    def test_block_dictionary_storage_is_bounded_exact_and_requires_no_ram_table(self):
        self.assertEqual(len(self.blocks), 4358)
        self.assertEqual(len(set(self.blocks)), len(self.blocks))
        self.assertTrue(all(len(row) == 8 for row in self.blocks))
        self.assertEqual(len(self.layout_blocks), 840)
        self.assertTrue(all(len(row) == 10 for row in self.layout_blocks))
        for row in self.layout_blocks:
            self.assertTrue(all(0 <= ref < len(self.blocks) for ref in row))
        receipt = self.report['layout_storage']
        self.assertEqual((receipt['stored_cells'], receipt['implicit_empty_from'], receipt['block_cells']), (80, 80, 8))
        self.assertEqual(receipt['block_table_bytes'], len(self.blocks) * 16)
        self.assertEqual(receipt['layout_index_bytes'], 840 * 10 * 2)
        self.assertEqual(receipt['table_bytes'], 86528)
        self.assertEqual(receipt['table_bytes_saved'], 74752)
        self.assertNotIn('static const uint16_t layouts[840][96]', self.source)
        self.assertNotIn('static uint16_t ', self.source)
        self.assertEqual(receipt['table_bytes_saved'] + self.report['record_storage']['serialized_dedup_saved_bytes'], 98008)

    def test_names_roots_groups_degrees_and_melody_anchors_are_unchanged(self):
        scales = self.projection['scales']
        names = [json.loads(line.strip().removesuffix(',')) for line in
                 block(self.source, 'static const char *const names[] = {').splitlines() if line.strip()]
        self.assertEqual(names, [scale['name'].upper() for scale in scales])
        offsets = tuple(map(int, re.findall(r'\d+', block(self.source, 'static const uint8_t root_offsets[12] = {'))))
        degree_rows = numeric_rows(self.source, 'static const int16_t scale_degrees[70][12] = {')
        counts = tuple(map(int, re.findall(r'\d+', block(self.source, 'static const uint8_t degree_counts[70] = {'))))
        self.assertEqual(offsets, (4, 6, 5, 8, 7, 9, 11, 10, 1, 0, 3, 2))
        self.assertEqual(tuple(scale['index'] // 12 for scale in scales), tuple(scale['group_index'] for scale in scales))
        self.assertEqual(self.report['index_storage']['table_bytes_saved'], 2508)
        self.assertNotIn('scale_lookup[70][12]', self.source)
        self.assertNotIn('groups[840]', self.source)
        for group in self.projection['groups']:
            expected_roots = {ROOTS[scales[index]['root_label']]: index for index in group['scale_indices']}
            self.assertEqual(tuple(group['index'] * 12 + offset for offset in offsets),
                             tuple(expected_roots[root] for root in range(12)))
            scale = next(scales[index] for index in group['scale_indices'] if scales[index]['root_label'] == 'C')
            degrees = sorted({note['midi'] % 12 for ref in scale['pads'][48:] if ref is not None
                              for note in self.projection['pad_records'][ref[1]]['notes']})
            self.assertEqual(degree_rows[group['index']], tuple(degrees))
            self.assertEqual(counts[group['index']], len(degrees))
        for scale, references in zip(scales, self.layouts):
            original_anchor = next((self.projection['pad_records'][ref[1]]['midi_slots'][0]
                                    for ref in scale['pads'][48:] if ref is not None and
                                    any(note['slot'] == 0 for note in self.projection['pad_records'][ref[1]]['notes'])), 60)
            expanded_anchor = next((self.records[ref][0] for ref in references[48:]
                                    if ref != 65535 and self.records[ref][7] & 1), 60)
            self.assertEqual(expanded_anchor, original_anchor, scale['index'])

    @unittest.skipUnless((ROOT / '.tools/config/core-toolchain.json').exists(), 'Pinned native compiler not installed')
    def test_compiled_c_loader_matches_all_original_values_and_root_lookups(self):
        """Exercise actual generated C; expected checksums come from the archive."""
        toolchain = json.loads((ROOT / '.tools/config/core-toolchain.json').read_text(encoding='utf-8'))
        compiler = Path(toolchain['executable'])
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(), toolchain['executable_sha256'])
        references = []
        scales, records = self.projection['scales'], self.projection['pad_records']
        group_degrees = {}
        for group in self.projection['groups']:
            scale = next(scales[index] for index in group['scale_indices'] if scales[index]['root_label'] == 'C')
            group_degrees[group['index']] = sorted({note['midi'] % 12 for ref in scale['pads'][48:] if ref is not None
                                                    for note in records[ref[1]]['notes']})
        for scale in scales:
            raw = bytearray()
            anchor = None
            for pad, ref in enumerate(scale['pads']):
                archived = records[ref[1]] if ref is not None else None
                notes = archived['midi_slots'] if archived else [0] * 7
                mask = sum(1 << note['slot'] for note in archived['notes']) if archived else 0
                for note in notes:
                    raw.extend(note.to_bytes(2, 'little'))
                raw.extend((mask, archived['color_id'] if archived else 0))
                if pad >= 48 and mask & 1 and anchor is None:
                    anchor = notes[0]
            raw.extend((60 if anchor is None else anchor).to_bytes(2, 'little'))
            degrees = group_degrees[scale['group_index']]
            raw.append(len(degrees))
            for degree in degrees + [0] * (12 - len(degrees)):
                raw.extend(degree.to_bytes(2, 'little'))
            raw.extend(scale['name'].upper().encode('ascii') + b'\0')
            references.append(zlib.crc32(raw))
        oracle = ','.join(f'UINT32_C(0x{value:08x})' for value in references)
        root_cases = ','.join('{' + ','.join(map(str, (scale['group_index'], ROOTS[scale['root_label']], scale['index']))) + '}'
                              for scale in scales)
        harness = '#include <stdio.h>\n#include "data.h"\n' + f'static const uint32_t expected[840]={{{oracle}}};\n' + \
            f'static const uint16_t roots[840][3]={{{root_cases}}};\n' + r'''
static uint32_t byte_crc(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    return crc;
}
static uint32_t word_crc(uint32_t crc, int16_t value) {
    crc=byte_crc(crc,(uint8_t)value);return byte_crc(crc,(uint8_t)((uint16_t)value>>8));
}
int main(void) {
    thy_instrument_pad pads[96];int16_t anchor,degrees[12];uint8_t count;
    for(unsigned scale=0;scale<840;scale++) {
        if(!thy_instrument_data_load(scale,pads,&anchor,degrees,&count)) return 1;
        uint32_t crc=UINT32_MAX;
        for(unsigned pad=0;pad<96;pad++) {
            for(unsigned slot=0;slot<7;slot++) crc=word_crc(crc,pads[pad].notes[slot]);
            crc=byte_crc(crc,pads[pad].valid_mask);crc=byte_crc(crc,pads[pad].color);
        }
        crc=word_crc(crc,anchor);crc=byte_crc(crc,count);
        for(unsigned degree=0;degree<12;degree++) crc=word_crc(crc,degrees[degree]);
        const char *name=thy_instrument_data_name(scale);
        do {crc=byte_crc(crc,(uint8_t)*name);} while(*name++);
        if(~crc!=expected[scale]) {fprintf(stderr,"Catalogue mismatch at scale %u\n",scale);return 2;}
        if(thy_instrument_data_find(roots[scale][0],roots[scale][1])!=roots[scale][2]) return 3;
    }
    puts("840 compiled catalogue loads and root lookups match the original lossless projection");
    return 0;
}
'''
        source, executable = self.output / 'oracle.c', self.output / 'catalogue-oracle.exe'
        source.write_text(harness, encoding='utf-8')
        env = os.environ.copy()
        for key, directory in (('ZIG_LOCAL_CACHE_DIR', ROOT / '.tools/zig-cache/catalog-local'),
                               ('ZIG_GLOBAL_CACHE_DIR', ROOT / '.tools/zig-cache/catalog-global'),
                               ('TEMP', self.output), ('TMP', self.output)):
            directory.mkdir(parents=True, exist_ok=True)
            env[key] = str(directory)
        built = subprocess.run([str(compiler), 'cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-I', str(ROOT / 'core'), '-I', str(ROOT / 'firmware/instrument'),
            str(self.output / 'instrument_data.c'), str(source), '-o', str(executable)],
            cwd=ROOT, env=env, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr[-2000:])
        tested = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True)
        self.assertEqual(tested.returncode, 0, tested.stderr[-2000:])
        self.assertIn('840 compiled catalogue loads and root lookups match', tested.stdout)


if __name__ == '__main__':
    unittest.main()
