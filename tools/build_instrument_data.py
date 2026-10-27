"""Embed the validated lossless musical projection in local MCU application data."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
from build_runtime_data import DEFAULT_ARCHIVE, project_archive

ROOT = Path(__file__).resolve().parents[1]
ROOTS = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5,
         "Gb": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}
FLASH_RECORD_BYTES = 9
PREVIOUS_RECORD_BYTES = 16
HARDWARE_PAD_CELLS = 96
STORED_PAD_CELLS = 80
LAYOUT_BLOCK_CELLS = 8
EMPTY_RECORD = 65535


def flash_pad_bytes(record: dict) -> bytes:
    """Keep every original slot, explicit validity and color in nine bytes.

    Slot values are MIDI integers even when their validity bit is clear. Never
    infer validity from zero, omit inactive slots, transpose or deduplicate.
    The full archive validator remains responsible for musical provenance.
    """
    notes = record.get('midi_slots')
    color = record.get('color_id')
    if (not isinstance(notes, list) or len(notes) != 7 or
            any(type(note) is not int or not 0 <= note <= 127 for note in notes)):
        raise ValueError('Invalid original MIDI slots')
    if type(color) is not int or not 0 <= color <= 255:
        raise ValueError('Original color does not fit uint8_t')
    mask = 0
    for note in record.get('notes', ()):
        slot = note.get('slot')
        if type(slot) is not int or not 0 <= slot < 7 or mask & (1 << slot):
            raise ValueError('Invalid or duplicate original note slot')
        if note.get('midi') != notes[slot]:
            raise ValueError('Original note disagrees with its serialized slot')
        mask |= 1 << slot
    return bytes([*notes, mask, color])


def pack_catalog(records: list[dict], scales: list[dict]) -> dict:
    """Intern exact wire records and eight-reference layout blocks.

    Projection record identities can differ while their MCU serialization is
    identical. Intern only AFTER retaining all seven slot values, validity and
    color. Source records/provenance remain unchanged. All runtime decode uses
    the existing caller-owned 96-pad array, with no decompression workspace.

    The recovered archive always leaves its final sixteen cells unassigned.
    Check that contract rather than silently dropping future archive content.
    Empty records inside the stored eighty cells retain the explicit sentinel.
    """
    packed_records, record_remap, unique_records = [], [], {}
    for record in records:
        value = flash_pad_bytes(record)
        if value not in unique_records:
            if len(packed_records) >= EMPTY_RECORD:
                raise ValueError('Packed record dictionary exceeds uint16_t')
            unique_records[value] = len(packed_records)
            packed_records.append(value)
        record_remap.append(unique_records[value])
    blocks, layouts, unique_blocks = [], [], {}
    for scale in scales:
        pads = scale.get('pads')
        if not isinstance(pads, (list, tuple)) or len(pads) != HARDWARE_PAD_CELLS:
            raise ValueError('Invalid layout pad count')
        if any(ref is not None for ref in pads[STORED_PAD_CELLS:]):
            raise ValueError('Implicit layout tail is not entirely unassigned')
        refs = []
        for ref in pads[:STORED_PAD_CELLS]:
            if ref is None:
                refs.append(EMPTY_RECORD)
            elif (not isinstance(ref, (list, tuple)) or len(ref) != 2 or
                  type(ref[1]) is not int or not 0 <= ref[1] < len(records)):
                raise ValueError('Invalid layout dictionary reference')
            else:
                refs.append(record_remap[ref[1]])
        row = []
        for first in range(0, STORED_PAD_CELLS, LAYOUT_BLOCK_CELLS):
            value = tuple(refs[first:first + LAYOUT_BLOCK_CELLS])
            if value not in unique_blocks:
                if len(blocks) >= EMPTY_RECORD:
                    raise ValueError('Layout block dictionary exceeds uint16_t')
                unique_blocks[value] = len(blocks)
                blocks.append(value)
            row.append(unique_blocks[value])
        layouts.append(tuple(row))
    return {'records': packed_records, 'record_remap': record_remap,
            'blocks': blocks, 'layouts': layouts}


def catalog_root_offsets(scales: list[dict], groups: list[dict]) -> list[int]:
    """Prove and factor the archive's repeated root-index permutation.

    Root ordering is not chromatic identity. It must be the SAME permutation
    within each consecutive twelve-scale group before arithmetic is emitted.
    """
    if len(scales) != len(groups) * 12:
        raise ValueError('Unexpected root/group count')
    for index, scale in enumerate(scales):
        if scale['index'] != index or scale['group_index'] != index // 12:
            raise ValueError('Scale groups are not consecutive twelve-entry blocks')
    offsets = None
    for index, group in enumerate(groups):
        indices = group['scale_indices']
        if (group['index'] != index or len(indices) != 12 or
                set(indices) != set(range(index * 12, (index + 1) * 12))):
            raise ValueError('Group indices do not match the scale projection')
        roots = {ROOTS[scales[n]['root_label']]: n for n in indices}
        if set(roots) != set(range(12)):
            raise ValueError('Root lookup is incomplete')
        current = [roots[root] - index * 12 for root in range(12)]
        if offsets is not None and offsets != current:
            raise ValueError('Root index permutation differs between groups')
        offsets = current
    if offsets is None:
        raise ValueError('No root index permutation')
    return offsets


def generate(output: Path) -> dict:
    raw = DEFAULT_ARCHIVE.read_bytes()
    archive = json.loads(raw)
    projection = project_archive(archive, archive_sha256=hashlib.sha256(raw).hexdigest(),
                                 archive_size_bytes=len(raw))
    records, scales = projection["pad_records"], projection["scales"]
    if len(records) >= 65535 or len(scales) != 840:
        raise ValueError("Unexpected dictionary/layout bounds")
    packing = pack_catalog(records, scales)
    packed_records, blocks = packing['records'], packing['blocks']
    root_offsets = catalog_root_offsets(scales, projection['groups'])
    lines = ['/* Generated local data; provenance in instrument-data.json. */', '#include "data.h"']
    lines.extend([
        '/* Flash-only representation. Runtime notes remain signed int16_t. */',
        'typedef struct thy_flash_pad {',
        '    uint8_t notes[7], valid_mask, color;',
        '} thy_flash_pad;',
        '_Static_assert(sizeof(thy_flash_pad) == 9u, "Flash pad record must be nine bytes");',
        'static const thy_flash_pad records[] = {',
    ])
    for record in packed_records:
        lines.append(' {{' + ','.join(map(str, record[:7])) + '},' + str(record[7]) + ',' + str(record[8]) + '},')
    lines.append('};\nstatic const uint16_t pad_blocks[][8] = {')
    lines.extend(' {' + ','.join(map(str, row)) + '},' for row in blocks)
    lines.append('};\nstatic const uint16_t layout_blocks[840][10] = {')
    lines.extend(' {' + ','.join(map(str, row)) + '},' for row in packing['layouts'])
    lines.append('};\nstatic const char *const names[] = {')
    lines.extend(' ' + json.dumps(s['name'].upper(), ensure_ascii=True) + ',' for s in scales)
    lines.append('};\nstatic const uint8_t root_offsets[12] = {')
    lines.append(','.join(map(str, root_offsets)) + '};')
    lines.append('static const uint8_t degree_counts[70] = {')
    degree_rows = []
    for group in projection['groups']:
        c_scale = next(scales[i] for i in group['scale_indices'] if scales[i]['root_label'] == 'C')
        degrees = sorted({n['midi'] % 12 for ref in c_scale['pads'][48:] if ref is not None
                          for n in records[ref[1]]['notes']})
        if not degrees or degrees[0] != 0 or len(degrees) > 12:
            raise ValueError('Invalid canonical C scale degrees')
        degree_rows.append(degrees)
    lines.append(','.join(str(len(row)) for row in degree_rows) + '};')
    lines.append('static const int16_t scale_degrees[70][12] = {')
    lines.extend(' {' + ','.join(map(str, row)) + '},' for row in degree_rows)
    lines.append('};')
    lines.append(r'''
int thy_instrument_data_find(unsigned group, unsigned root) {
    return group < 70 && root < 12 ? (int)(group * 12u + root_offsets[root]) : -1;
}
const char *thy_instrument_data_name(unsigned scale) {
    return scale < 840 ? names[scale] : "INVALID SCALE";
}
int thy_instrument_data_load(unsigned scale, thy_instrument_pad pads[96],
    int16_t *anchor, int16_t degrees[12], uint8_t *count) {
    if (scale >= 840 || !pads || !anchor || !degrees || !count) return 0;
    for (unsigned p = 0; p < 80; ++p) {
        uint16_t ref = pad_blocks[layout_blocks[scale][p / 8u]][p % 8u];
        if (ref != 65535) {
            const thy_flash_pad *record = &records[ref];
            for (unsigned slot = 0; slot < 7; ++slot)
                pads[p].notes[slot] = (int16_t)record->notes[slot];
            pads[p].valid_mask = record->valid_mask;
            pads[p].color = record->color;
        } else pads[p] = (thy_instrument_pad){0};
    }
    /* Generator verified these sixteen cells are unassigned in every scale. */
    for (unsigned p = 80; p < 96; ++p) pads[p] = (thy_instrument_pad){0};
    unsigned group = scale / 12u;
    *count = degree_counts[group];
    for (unsigned d = 0; d < 12; ++d) degrees[d] = scale_degrees[group][d];
    *anchor = 60;
    for (unsigned p = 48; p < 96; ++p) if (pads[p].valid_mask & 1u) {
        *anchor = pads[p].notes[0]; break;
    }
    return 1;
}
''')
    destination = output / 'instrument_data.c'
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    record_bytes = len(packed_records) * FLASH_RECORD_BYTES
    block_bytes = len(blocks) * LAYOUT_BLOCK_CELLS * 2
    layout_bytes = len(scales) * (STORED_PAD_CELLS // LAYOUT_BLOCK_CELLS) * 2
    report = {'format': 'thy666.local-instrument-musical-data', 'version': 3,
              'archive_sha256': hashlib.sha256(raw).hexdigest(),
              'projection_counts': projection['counts'],
              'archive_pitch_policy': 'All original seven slots and validity retained; generated melody uses canonical C family degrees',
              'color_policy': 'Original numeric IDs retained; no physical RGB mapping asserted',
              'record_storage': {
                  'encoding': 'midi7-mask8-color8-v1',
                  'records': len(packed_records),
                  'projection_records': len(records),
                  'record_bytes': FLASH_RECORD_BYTES,
                  'packed_table_bytes': record_bytes,
                  'previous_record_bytes': PREVIOUS_RECORD_BYTES,
                  'previous_table_bytes': len(records) * PREVIOUS_RECORD_BYTES,
                  'table_bytes_saved': len(records) * PREVIOUS_RECORD_BYTES - record_bytes,
                  'serialized_dedup_saved_bytes': (len(records) - len(packed_records)) * FLASH_RECORD_BYTES,
                  'packed_records_sha256': hashlib.sha256(b''.join(packed_records)).hexdigest(),
                  'runtime_policy': 'Widen all seven uint8_t slots explicitly into the unchanged int16_t runtime pad on catalogue load',
                  'scope': 'Exact record-table storage difference; final linked image saving also includes decoder/code layout changes',
              },
              'layout_storage': {
                  'encoding': 'u16-block8-stored80-v1',
                  'layouts': len(scales), 'hardware_cells': HARDWARE_PAD_CELLS,
                  'stored_cells': STORED_PAD_CELLS, 'implicit_empty_from': STORED_PAD_CELLS,
                  'block_cells': LAYOUT_BLOCK_CELLS, 'block_count': len(blocks),
                  'block_table_bytes': block_bytes, 'layout_index_bytes': layout_bytes,
                  'table_bytes': block_bytes + layout_bytes,
                  'previous_table_bytes': len(scales) * HARDWARE_PAD_CELLS * 2,
                  'table_bytes_saved': len(scales) * HARDWARE_PAD_CELLS * 2 - block_bytes - layout_bytes,
                  'runtime_policy': 'One block lookup per stored cell; decode into existing caller-owned 96-pad layout; no new RAM workspace',
              },
              'index_storage': {
                  'encoding': 'consecutive12-root-permutation-v1',
                  'root_offsets': root_offsets, 'table_bytes': 12,
                  'previous_table_bytes': len(scales) * 3,
                  'table_bytes_saved': len(scales) * 3 - 12,
                  'proof': 'Every group is consecutive twelve-entry block with the same complete root permutation',
              },
              'generated_source_sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
              'generated_source_bytes': destination.stat().st_size,
              'distribution_policy': projection['provenance']['distribution_policy']}
    (output / 'instrument-data.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report
