"""Budget comparisons must bind artifacts and expose real capacity failures."""
import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from track_firmware_budget import (  # noqa: E402
    APP_START, BASELINE, CCM_BYTES, CCM_START, FLASH_BYTES, RAM_END, RAM_START,
    SETTINGS_END, SETTINGS_START, SRAM_BYTES, STACK_BYTES, budget_report, main, sha,
)


class BuildFixture:
    def __init__(self, folder, name, image_bytes=16, sram=24, ccm=8):
        self.folder = folder / name
        self.folder.mkdir()
        self.path = self.folder / 'THY666-instrument-alpha.manifest.json'
        self.image = bytes(range(16)) * (image_bytes // 16) + bytes(range(image_bytes % 16))
        self.elf = b'small synthetic ELF identity ' + name.encode()
        self.inspection = {
            'format': 'thy666.stm32-diagnostic-inspection', 'version': 1,
            'execution_mode': 'flash', 'image_start': APP_START,
            'image_end': APP_START + len(self.image), 'image_bytes': len(self.image),
            'image_sha256': sha(self.image), 'elf_sha256': sha(self.elf),
            'initial_stack': RAM_END,
            'memory_bytes': {'application_flash': image_bytes, 'sram': sram, 'ccm': ccm},
            'segments': [
                {'region': 'application_flash', 'virtual_address': APP_START,
                 'load_address': APP_START, 'file_bytes': image_bytes, 'memory_bytes': image_bytes},
                {'region': 'sram', 'virtual_address': RAM_START + 4,
                 'load_address': RAM_START + 4, 'file_bytes': 0, 'memory_bytes': sram},
                {'region': 'ccm', 'virtual_address': CCM_START,
                 'load_address': CCM_START, 'file_bytes': 0, 'memory_bytes': ccm},
            ],
            'symbols': {key: {'address': value} for key, value in {
                '__ram_end__': RAM_START + 4 + sram, '__stack_limit__': RAM_END - STACK_BYTES,
                '__stack_top__': RAM_END, '__settings_start__': SETTINGS_START,
                '__settings_end__': SETTINGS_END}.items()},
        }
        sources = {'core/fixture.c': sha(name.encode())}
        self.manifest = {'format': 'thy666.integrated-instrument-alpha', 'version': 1,
                         'source_sha256': sources,
                         'build_id': sha(json.dumps(sources, sort_keys=True).encode())[:16],
                         'sources_unchanged_during_build': True, 'reserved_stack_bytes': STACK_BYTES,
                         'memory_bytes': copy.deepcopy(self.inspection['memory_bytes'])}
        self.save()

    def save(self):
        blobs = {'THY666-instrument-alpha.bin': self.image,
                 'THY666-instrument-alpha.elf': self.elf,
                 'THY666-instrument-alpha.inspection.json':
                     (json.dumps(self.inspection, indent=2) + '\n').encode()}
        for name, raw in blobs.items():
            (self.folder / name).write_bytes(raw)
        self.manifest['artifacts'] = {name: {'bytes': len(raw), 'sha256': sha(raw)}
                                      for name, raw in blobs.items()}
        self.path.write_text(json.dumps(self.manifest, indent=2) + '\n', encoding='utf-8')

    def stack(self, folder, direct=1000, callbacks=1100, irq=1200, nesting=1300):
        record = {'format': 'thy666.actual-linked-stack-review', 'version': 1,
                  'build_id': self.manifest['build_id'], 'elf_sha256': sha(self.elf),
                  'image_sha256': sha(self.image), 'manifest_sha256': sha(self.path.read_bytes()),
                  'source_sha256': self.manifest['source_sha256'],
                  'reserved_stack_bytes': STACK_BYTES,
                  'covered_direct_paths': [{'direct_chain_upper_bytes': direct}],
                  'limits': ['Synthetic selected paths only.']}
        path = folder / 'linked.json'
        path.write_text(json.dumps(record), encoding='utf-8')
        supplement = {'format': 'thy666.linked-stack-callback-supplement', 'version': 2,
                      'build_id': record['build_id'], 'elf_sha256': record['elf_sha256'],
                      'source_sha256': record['source_sha256'], 'base_report_sha256': sha(path.read_bytes()),
                      'reserved_stack_bytes': STACK_BYTES, 'largest_reviewed_composition_bytes': callbacks,
                      'largest_reviewed_plus_single_usb_irq_bytes': irq,
                      'largest_reviewed_plus_three_reviewed_ordinary_isrs_bytes': nesting,
                      'limits': ['Synthetic IRQ model, no measured watermark.']}
        supplement_path = folder / 'supplement.json'
        supplement_path.write_text(json.dumps(supplement), encoding='utf-8')
        return path, supplement_path


class FirmwareBudgetTests(unittest.TestCase):
    def setUp(self):
        local = ROOT / 'analysis/local'
        local.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='budget-tool-tests-', dir=local)
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.baseline = BuildFixture(self.folder, 'baseline')
        self.current = BuildFixture(self.folder, 'current', image_bytes=20, sram=40, ccm=12)

    def report(self, **kwargs):
        return budget_report(self.current.path, self.baseline.path, **kwargs)

    def test_comparison_reports_allocations_address_span_percentages_and_unmeasured_limits(self):
        report = self.report()
        self.assertTrue(report['within_capacity'])
        ram = report['current']['resources']['static_sram']
        self.assertEqual((ram['used_bytes'], ram['address_span_bytes']), (40, 44))
        self.assertEqual(ram['remaining_bytes'], SRAM_BYTES - 40)
        self.assertEqual(ram['remaining_contiguous_bytes'], SRAM_BYTES - 44)
        self.assertAlmostEqual(ram['used_percent'], 40 * 100 / SRAM_BYTES, places=5)
        self.assertEqual(report['comparison']['raw_image']['delta_used_bytes'], 4)
        self.assertEqual(report['comparison']['static_sram']['delta_used_bytes'], 16)
        self.assertEqual(report['comparison']['ccm']['delta_remaining_bytes'], -4)
        self.assertEqual(report['timing_measurement'], 'unmeasured')
        self.assertEqual(report['runtime_performance'], 'unknown')
        self.assertFalse(report['linked_stack']['provided'])
        self.assertFalse(report['device_access'])
        self.assertFalse(report['flash_ready'])

    def test_changed_binary_or_elf_and_wrong_inspection_identity_are_rejected(self):
        for name in ('THY666-instrument-alpha.bin', 'THY666-instrument-alpha.elf'):
            with self.subTest(name=name):
                self.current.save()
                path = self.current.folder / name
                raw = path.read_bytes()
                path.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
                with self.assertRaisesRegex(ValueError, 'Artifact size/SHA256 mismatch'):
                    self.report()
        self.current.inspection['elf_sha256'] = '0' * 64
        self.current.save()
        with self.assertRaisesRegex(ValueError, 'Inspection ELF/image identity mismatch'):
            self.report()

    def test_non_integer_negative_and_unbounded_memory_counts_are_rejected(self):
        original = copy.deepcopy(self.current.manifest)
        for bad in (True, -1, 1.5, '40', 1 << 40):
            with self.subTest(value=bad):
                self.current.manifest = copy.deepcopy(original)
                self.current.manifest['memory_bytes']['sram'] = bad
                self.current.save()
                with self.assertRaises(ValueError):
                    self.report()
        self.current.manifest = original
        self.current.inspection['memory_bytes']['sram'] = False
        self.current.save()
        with self.assertRaises(ValueError):
            self.report()

    def test_inconsistent_totals_extents_symbols_and_overlaps_are_rejected(self):
        original = copy.deepcopy(self.current.inspection)
        changes = [lambda i: i['memory_bytes'].update(sram=41),
                   lambda i: i.update(image_end=i['image_end'] + 1),
                   lambda i: i['symbols']['__stack_limit__'].update(address=RAM_END - 4096),
                   lambda i: i['symbols']['__settings_start__'].update(address=SETTINGS_START + 4),
                   lambda i: i['segments'][1].update(memory_bytes=39),
                   lambda i: i['segments'].append(copy.deepcopy(i['segments'][1]))]
        for change in changes:
            with self.subTest(change=change):
                self.current.inspection = copy.deepcopy(original)
                change(self.current.inspection)
                self.current.save()
                with self.assertRaises(ValueError):
                    self.report()

    def test_build_id_and_artifact_count_types_are_checked(self):
        self.current.manifest['build_id'] = 'f' * 16
        self.current.save()
        with self.assertRaisesRegex(ValueError, 'build ID'):
            self.report()
        self.current.manifest['build_id'] = sha(json.dumps(
            self.current.manifest['source_sha256'], sort_keys=True).encode())[:16]
        self.current.save()
        self.current.manifest['artifacts']['THY666-instrument-alpha.bin']['bytes'] = True
        self.current.path.write_text(json.dumps(self.current.manifest), encoding='utf-8')
        with self.assertRaises(ValueError):
            self.report()

    def test_overlap_cannot_hide_behind_matching_allocation_totals(self):
        segment = copy.deepcopy(self.current.inspection['segments'][1])
        self.current.inspection['segments'].append(segment)
        self.current.inspection['memory_bytes']['sram'] *= 2
        self.current.manifest['memory_bytes']['sram'] *= 2
        self.current.save()
        with self.assertRaisesRegex(ValueError, 'Overlapping segment'):
            self.report()

    def test_each_physical_capacity_and_sram_alignment_gap_are_enforced(self):
        cases = [('raw', FLASH_BYTES + 1, 24, 8, 'raw_image'),
                 ('sram', 16, SRAM_BYTES + 1, 8, 'static_sram'),
                 ('alignment', 16, SRAM_BYTES, 8, 'static_sram'),
                 ('ccm', 16, 24, CCM_BYTES + 1, 'ccm')]
        for name, image, ram, ccm, region in cases:
            with self.subTest(region=name):
                fixture = BuildFixture(self.folder, name, image_bytes=image, sram=ram, ccm=ccm)
                report = budget_report(fixture.path, self.baseline.path)
                self.assertFalse(report['within_capacity'])
                self.assertFalse(report['current']['resources'][region]['within_capacity'])
                self.assertTrue(any(region in text for text in report['violations']))

    def test_matching_stack_and_supplement_include_only_reviewed_estimates(self):
        stack, supplement = self.current.stack(self.folder)
        report = self.report(stack_path=stack, supplement_path=supplement)
        evidence = report['linked_stack']
        self.assertTrue(evidence['within_capacity'])
        self.assertEqual(evidence['largest_reviewed_direct_bytes'], 1000)
        self.assertEqual(evidence['supplement']['largest_reviewed_plus_single_usb_irq_bytes'], 1200)
        self.assertEqual(evidence['runtime_high_water'], 'unmeasured')
        self.assertFalse(evidence['whole_program_stack_bound'])

    def test_stale_stack_and_supplement_and_supplement_without_base_are_rejected(self):
        stack, supplement = self.current.stack(self.folder)
        with self.assertRaises(ValueError):
            self.report(supplement_path=supplement)
        record = json.loads(stack.read_text())
        record['image_sha256'] = '0' * 64
        stack.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'image_sha256'):
            self.report(stack_path=stack)
        stack, supplement = self.current.stack(self.folder)
        record = json.loads(supplement.read_text())
        record['base_report_sha256'] = '0' * 64
        supplement.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'supplement identity'):
            self.report(stack_path=stack, supplement_path=supplement)

    def test_stack_overflow_is_a_capacity_failure(self):
        stack, supplement = self.current.stack(self.folder, direct=8200, callbacks=8300, irq=8400, nesting=8500)
        report = self.report(stack_path=stack, supplement_path=supplement)
        self.assertFalse(report['within_capacity'])
        self.assertIn('current.reviewed_stack exceeds reservation', report['violations'])

    def test_invalid_stack_counts_and_nonmonotonic_interrupt_estimates_are_rejected(self):
        stack, supplement = self.current.stack(self.folder, direct=True)
        with self.assertRaises(ValueError):
            self.report(stack_path=stack)
        stack, supplement = self.current.stack(self.folder, callbacks=1100, irq=1000)
        with self.assertRaisesRegex(ValueError, 'not monotonic'):
            self.report(stack_path=stack, supplement_path=supplement)

    def test_cli_writes_capacity_failure_once_and_refuses_overwrite(self):
        fixture = BuildFixture(self.folder, 'too-large', sram=SRAM_BYTES + 1)
        output = self.folder / 'budget.json'
        args = ['--manifest', str(fixture.path), '--baseline', str(self.baseline.path), '--output', str(output)]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(args), 2)
            first = output.read_bytes()
            self.assertFalse(json.loads(first)['within_capacity'])
            self.assertEqual(main(args), 1)
            self.assertEqual(output.read_bytes(), first)

    def test_invalid_input_does_not_create_report(self):
        (self.current.folder / 'THY666-instrument-alpha.bin').write_bytes(b'changed')
        output = self.folder / 'invalid.json'
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(['--manifest', str(self.current.path), '--baseline', str(self.baseline.path),
                                   '--output', str(output)]), 1)
        self.assertFalse(output.exists())

    @unittest.skipUnless(BASELINE.exists(), 'Preserved local STM32 build is not distributed')
    def test_preserved_real_build_read_only_counts_and_identities(self):
        before = {p: sha(p.read_bytes()) for p in BASELINE.parent.iterdir()
                  if p.name in ('THY666-instrument-alpha.manifest.json',
                                'THY666-instrument-alpha.inspection.json',
                                'THY666-instrument-alpha.bin', 'THY666-instrument-alpha.elf')}
        report = budget_report(BASELINE, BASELINE)
        self.assertEqual(report['current']['build_id'], '3c0b6712fc8eea71')
        self.assertTrue(report['within_capacity'])
        resources = report['current']['resources']
        self.assertEqual(resources['raw_image']['used_bytes'], 471308)
        self.assertEqual(resources['application_flash']['used_bytes'], 471280)
        self.assertEqual(resources['static_sram']['used_bytes'], 122200)
        self.assertEqual(resources['static_sram']['remaining_contiguous_bytes'], 676)
        self.assertEqual(resources['ccm']['used_bytes'], 62864)
        self.assertTrue(all(item['delta_used_bytes'] == 0 for item in report['comparison'].values()))
        self.assertEqual(before, {p: sha(p.read_bytes()) for p in before})


if __name__ == '__main__':
    unittest.main()
