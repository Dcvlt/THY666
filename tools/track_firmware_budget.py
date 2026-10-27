"""Compare source-bound STM32 memory budgets offline; never access a device.

The manifest's ELF/BIN/inspection identities are checked, then memory totals,
load extents and linker boundaries are cross-checked against the recorded
inspection. This is a budget tracker, not a fresh ELF disassembly or timing test.
Existing reports are never overwritten. Capacity failures produce a report and
exit 2; malformed or mismatched evidence exits 1 without writing a report.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / 'build/instrument-expression-20261025-03/THY666-instrument-alpha.manifest.json'
BASELINE_BUILD_ID = '3c0b6712fc8eea71'
APP_START, SETTINGS_START, SETTINGS_END = 0x08040000, 0x080C0000, 0x08100000
RAM_START, RAM_END, CCM_START = 0x20000000, 0x20020000, 0x10000000
STACK_BYTES, FLASH_BYTES, SRAM_BYTES, CCM_BYTES = 8192, 524288, 122880, 65536
ARTIFACTS = ('THY666-instrument-alpha.bin', 'THY666-instrument-alpha.elf',
             'THY666-instrument-alpha.inspection.json')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def count(value, label):
    require(type(value) is int and 0 <= value <= 0xFFFFFFFF,
            f'{label} must be a nonnegative 32-bit integer')
    return value


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def hash_value(value, label):
    require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value),
            f'Invalid SHA256 for {label}')
    return value


class Inputs:
    def __init__(self):
        self.seen = {}

    def read(self, path):
        path = Path(path).resolve()
        require(path.is_relative_to(ROOT), f'Input must remain in workspace: {path}')
        if path not in self.seen:
            self.seen[path] = path.read_bytes()
        return self.seen[path]

    def json(self, path):
        value = json.loads(self.read(path))
        require(isinstance(value, dict), f'Expected JSON object: {path}')
        return value

    def unchanged(self):
        require(all(path.read_bytes() == raw for path, raw in self.seen.items()),
                'Budget inputs changed while being read')


def identity_map(value):
    require(isinstance(value, dict) and value, 'Missing compiled-source identities')
    for name, digest in value.items():
        require(isinstance(name, str) and name and not PureWindowsPath(name).drive,
                'Invalid compiled-source path')
        normalized = name.replace('\\', '/')
        require(not PurePosixPath(normalized).is_absolute() and
                '..' not in PurePosixPath(normalized).parts and ':' not in normalized,
                'Compiled-source path leaves its root')
        hash_value(digest, name)
    return value


def resource(used, capacity, span=None):
    result = {'used_bytes': used, 'capacity_bytes': capacity,
              'remaining_bytes': capacity - used,
              'used_percent': round(used * 100 / capacity, 6),
              'remaining_percent': round((capacity - used) * 100 / capacity, 6)}
    if span is not None:
        result.update(address_span_bytes=span, remaining_contiguous_bytes=capacity - span,
                      address_span_percent=round(span * 100 / capacity, 6))
    result['within_capacity'] = used <= capacity and (span is None or span <= capacity)
    return result


def load_build(manifest_path, inputs):
    manifest_path = Path(manifest_path).resolve()
    manifest = inputs.json(manifest_path)
    require(manifest.get('format') == 'thy666.integrated-instrument-alpha' and
            type(manifest.get('version')) is int and manifest['version'] == 1,
            'Unsupported instrument manifest format/version')
    sources = identity_map(manifest.get('source_sha256'))
    build_id = sha(json.dumps(sources, sort_keys=True).encode())[:16]
    require(manifest.get('build_id') == build_id, 'Manifest build ID/source identity mismatch')
    require(manifest.get('sources_unchanged_during_build') is True, 'Manifest records source drift')
    artifacts = manifest.get('artifacts')
    require(isinstance(artifacts, dict), 'Missing artifact identities')
    raw_artifacts = {}
    for name in ARTIFACTS:
        record = artifacts.get(name)
        require(isinstance(record, dict), f'Missing artifact: {name}')
        expected_size = count(record.get('bytes'), f'{name} bytes')
        expected_hash = hash_value(record.get('sha256'), name)
        path = (manifest_path.parent / name).resolve()
        require(path.parent == manifest_path.parent, f'Artifact leaves its build directory: {name}')
        raw = inputs.read(path)
        require(len(raw) == expected_size and sha(raw) == expected_hash,
                f'Artifact size/SHA256 mismatch: {name}')
        raw_artifacts[name] = raw
    image, elf, inspection_raw = (raw_artifacts[name] for name in ARTIFACTS)
    inspection = json.loads(inspection_raw)
    require(isinstance(inspection, dict) and
            inspection.get('format') == 'thy666.stm32-diagnostic-inspection' and
            type(inspection.get('version')) is int and inspection['version'] == 1 and
            inspection.get('execution_mode') == 'flash', 'Unsupported flash inspection')
    require(sha(image) == inspection.get('image_sha256') and
            sha(elf) == inspection.get('elf_sha256'), 'Inspection ELF/image identity mismatch')
    require(len(image) > 0 and count(inspection.get('image_bytes'), 'image bytes') == len(image),
            'Inspection raw image size mismatch')
    start = count(inspection.get('image_start'), 'image start')
    end = count(inspection.get('image_end'), 'image end')
    require(start == APP_START and end - start == len(image), 'Raw image extent mismatch')
    totals = manifest.get('memory_bytes')
    require(isinstance(totals, dict) and set(totals) == {'application_flash', 'sram', 'ccm'},
            'Memory totals must declare flash, SRAM and CCM')
    for region, used in totals.items():
        count(used, region + ' bytes')
    observed_totals = inspection.get('memory_bytes')
    require(isinstance(observed_totals, dict) and set(observed_totals) == set(totals),
            'Invalid inspection memory totals')
    for region, used in observed_totals.items():
        count(used, 'inspection ' + region + ' bytes')
    require(observed_totals == totals, 'Manifest/inspection memory totals mismatch')
    segments = inspection.get('segments')
    require(isinstance(segments, list) and segments, 'Missing inspection load segments')
    sums = {region: 0 for region in totals}
    ends = {'application_flash': APP_START, 'sram': RAM_START, 'ccm': CCM_START}
    bases = {'application_flash': APP_START, 'sram': RAM_START, 'ccm': CCM_START}
    allocations = {region: [] for region in totals}
    loads = []
    for segment in segments:
        require(isinstance(segment, dict), 'Invalid load segment')
        region = segment.get('region')
        require(region in sums, 'Unexpected segment memory region')
        virtual = count(segment.get('virtual_address'), 'segment virtual address')
        load = count(segment.get('load_address'), 'segment load address')
        memory = count(segment.get('memory_bytes'), 'segment memory bytes')
        files = count(segment.get('file_bytes'), 'segment file bytes')
        require(virtual >= bases[region] and memory <= 0xFFFFFFFF - virtual and files <= memory,
                'Invalid segment address/size')
        sums[region] += memory
        if memory:
            allocations[region].append((virtual, virtual + memory))
            ends[region] = max(ends[region], virtual + memory)
        if files:
            require(APP_START <= load and files <= end - load, 'Initialized load leaves raw image')
            loads.append((load, load + files))
    require(sums == totals, 'Memory totals do not equal segment allocations')
    for intervals in [*allocations.values(), loads]:
        ordered = sorted(intervals)
        require(all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:])),
                'Overlapping segment allocations or initialized loads')
    require(loads and min(a for a, _ in loads) == start and max(b for _, b in loads) == end,
            'Initialized segment extents do not cover image boundaries')
    symbols = inspection.get('symbols')
    require(isinstance(symbols, dict), 'Missing linker boundary symbols')

    def symbol(name):
        record = symbols.get(name)
        require(isinstance(record, dict), f'Missing linker symbol {name}')
        return count(record.get('address'), name)

    require(count(manifest.get('reserved_stack_bytes'), 'reserved stack') == STACK_BYTES,
            'Expected 8192-byte stack reservation')
    top, limit, ram_end = symbol('__stack_top__'), symbol('__stack_limit__'), symbol('__ram_end__')
    require(top == RAM_END and limit == RAM_END - STACK_BYTES and
            count(inspection.get('initial_stack'), 'initial stack') == top,
            'Stack/vector boundary mismatch')
    require(ram_end >= ends['sram'], 'RAM end symbol precedes static allocations')
    require(symbol('__settings_start__') == SETTINGS_START and
            symbol('__settings_end__') == SETTINGS_END, 'Settings partition boundary mismatch')
    resources = {
        'raw_image': resource(len(image), FLASH_BYTES),
        'application_flash': resource(totals['application_flash'], FLASH_BYTES,
                                      ends['application_flash'] - APP_START),
        'static_sram': resource(totals['sram'], SRAM_BYTES, ram_end - RAM_START),
        'ccm': resource(totals['ccm'], CCM_BYTES, ends['ccm'] - CCM_START),
    }
    return {'build_id': build_id, 'manifest': manifest_path.relative_to(ROOT).as_posix(),
            'manifest_sha256': sha(inputs.read(manifest_path)), 'elf_sha256': sha(elf),
            'image_sha256': sha(image), 'source_sha256': sources, 'resources': resources,
            'reserved_stack_bytes': STACK_BYTES, 'total_sram_bytes': RAM_END - RAM_START,
            'settings_reserved_bytes': SETTINGS_END - SETTINGS_START,
            'within_capacity': all(r['within_capacity'] for r in resources.values())}


def stack_evidence(path, supplement_path, build, inputs):
    if path is None:
        require(supplement_path is None, 'Stack supplement requires a linked-stack report')
        return {'provided': False, 'runtime_high_water': 'unmeasured',
                'whole_program_stack_bound': False}
    record = inputs.json(path)
    require(record.get('format') == 'thy666.actual-linked-stack-review' and
            type(record.get('version')) is int and record['version'] == 1,
            'Wrong linked-stack report format/version')
    for key in ('build_id', 'elf_sha256', 'image_sha256', 'manifest_sha256'):
        require(record.get(key) == build[key], f'Linked-stack {key} does not match build')
    require(record.get('source_sha256') == build['source_sha256'], 'Linked-stack source identities differ')
    require(count(record.get('reserved_stack_bytes'), 'reviewed stack') == STACK_BYTES,
            'Linked-stack reservation differs')
    paths = record.get('covered_direct_paths')
    require(isinstance(paths, list) and paths and all(isinstance(p, dict) for p in paths),
            'Linked-stack report has no valid reviewed paths')
    largest = max(count(p.get('direct_chain_upper_bytes'), 'direct stack bytes')
                  for p in paths)
    result = {'provided': True, 'report': Path(path).resolve().relative_to(ROOT).as_posix(),
              'report_sha256': sha(inputs.read(path)), 'largest_reviewed_direct_bytes': largest,
              'within_capacity': largest <= STACK_BYTES, 'runtime_high_water': 'unmeasured',
              'whole_program_stack_bound': False,
              'limitations': record.get('limits', [])}
    if supplement_path is not None:
        supplement = inputs.json(supplement_path)
        require(supplement.get('format') == 'thy666.linked-stack-callback-supplement' and
                type(supplement.get('version')) is int and supplement['version'] in (1, 2),
                'Wrong callback/IRQ supplement format/version')
        require(supplement.get('build_id') == build['build_id'] and
                supplement.get('elf_sha256') == build['elf_sha256'] and
                supplement.get('source_sha256') == build['source_sha256'] and
                supplement.get('base_report_sha256') == result['report_sha256'],
                'Callback/IRQ supplement identity mismatch')
        require(count(supplement.get('reserved_stack_bytes'), 'supplement stack') == STACK_BYTES,
                'Callback/IRQ reservation differs')
        fields = ('largest_reviewed_composition_bytes', 'largest_reviewed_plus_single_usb_irq_bytes',
                  'largest_reviewed_plus_three_reviewed_ordinary_isrs_bytes')
        values = {field: count(supplement.get(field), field) for field in fields}
        require(values[fields[0]] <= values[fields[1]] <= values[fields[2]],
                'Callback/IRQ estimates are not monotonic')
        result['supplement'] = {'path': Path(supplement_path).resolve().relative_to(ROOT).as_posix(),
                                'sha256': sha(inputs.read(supplement_path)), **values,
                                'limitations': supplement.get('limits', [])}
        result['within_capacity'] &= max(values.values()) <= STACK_BYTES
    return result


def budget_report(manifest_path, baseline_path=BASELINE, stack_path=None, supplement_path=None):
    inputs = Inputs()
    current = load_build(manifest_path, inputs)
    baseline = load_build(baseline_path, inputs)
    if Path(baseline_path).resolve() == BASELINE:
        require(baseline['build_id'] == BASELINE_BUILD_ID, 'Preserved baseline build identity changed')
    evidence = stack_evidence(stack_path, supplement_path, current, inputs)
    comparison = {}
    for name, value in current['resources'].items():
        old = baseline['resources'][name]
        comparison[name] = {'delta_used_bytes': value['used_bytes'] - old['used_bytes'],
                            'delta_remaining_bytes': value['remaining_bytes'] - old['remaining_bytes'],
                            'delta_used_percentage_points': round(value['used_percent'] - old['used_percent'], 6)}
        if 'address_span_bytes' in value:
            comparison[name]['delta_address_span_bytes'] = value['address_span_bytes'] - old['address_span_bytes']
            comparison[name]['delta_contiguous_remaining_bytes'] = value['remaining_contiguous_bytes'] - old['remaining_contiguous_bytes']
    violations = [f'{label}.{name} exceeds capacity' for label, build in [('current', current), ('baseline', baseline)]
                  for name, value in build['resources'].items() if not value['within_capacity']]
    if evidence.get('within_capacity') is False:
        violations.append('current.reviewed_stack exceeds reservation')
    # Source identities establish build IDs; avoid duplicating them in the report.
    for build in (current, baseline):
        del build['source_sha256']
    inputs.unchanged()
    return {'format': 'thy666.firmware-budget', 'version': 1, 'current': current,
            'baseline': baseline, 'comparison': comparison, 'linked_stack': evidence,
            'within_capacity': not violations, 'violations': violations,
            'checks': {'artifact_identities_verified': True,
                       'inspection_allocation_totals_verified': True,
                       'stack_and_settings_boundaries_verified': True,
                       'inputs_unchanged_during_review': True},
            'device_access': False, 'flash_ready': False,
            'timing_measurement': 'unmeasured', 'runtime_performance': 'unknown',
            'limits': ['Budgets use the artifact-hash-bound saved inspection; this is not a fresh ELF parser or source audit.',
                       'Static SRAM allocated bytes exclude alignment holes; occupied span and contiguous gap are reported separately.',
                       'Stack evidence covers only its explicitly reviewed paths and assumptions; device high-water is unmeasured.',
                       'Memory fit does not establish latency, scan/MIDI responsiveness, hardware readiness or recovery.'],
            'input_identities': {p.relative_to(ROOT).as_posix(): {'bytes': len(raw), 'sha256': sha(raw)}
                                 for p, raw in inputs.seen.items()}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True, help='Phase instrument manifest')
    parser.add_argument('--baseline', type=Path, default=BASELINE,
                        help='Baseline manifest; defaults to preserved 3c0b6712 build')
    parser.add_argument('--stack-report', type=Path)
    parser.add_argument('--stack-supplement', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='Fresh JSON under analysis/local/')
    args = parser.parse_args(argv)
    try:
        output = args.output.resolve()
        require(output.is_relative_to(ROOT / 'analysis/local') and output.suffix.lower() == '.json',
                'Output must be a JSON file under analysis/local/')
        require(output.parent.is_dir() and not output.exists(), 'Output parent must exist and report must be fresh')
        report = budget_report(args.manifest, args.baseline, args.stack_report, args.stack_supplement)
        with output.open('x', encoding='utf-8', newline='\n') as handle:
            json.dump(report, handle, indent=2)
            handle.write('\n')
        print(json.dumps({'build_id': report['current']['build_id'],
                          'baseline': report['baseline']['build_id'],
                          'within_capacity': report['within_capacity'],
                          'violations': report['violations'],
                          'output': output.relative_to(ROOT).as_posix()}))
        return 0 if report['within_capacity'] else 2
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f'Firmware budget tracking failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
