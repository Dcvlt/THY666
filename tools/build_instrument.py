"""Build the integrated STM32 instrument alpha offline; never package or flash."""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
from pathlib import Path
import subprocess
import sys
from build_diagnostic import ROOT, compiler_environment, sha, source_prefix_flags, anonymize_elf_file
from build_instrument_data import generate
from inspect_diagnostic_elf import inspect_elf
from native_compile import native_target_flags
from setup_diagnostic_deps import setup, USB_SOURCES, TINYUSB_ROOT, DEPS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--keep-debug', action='store_true',
                        help='Keep private toolchain DWARF paths for local debugging; default ELF anonymizes them')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('Use a fresh output directory inside build/')
    output.mkdir(parents=True)
    compiler, toolchain, env = compiler_environment()
    subprocess.run([sys.executable, str(ROOT / 'tools/build_studio_schema.py'), '--check'], cwd=ROOT, check=True)
    dependencies = setup()
    data = generate(output)
    board, app = ROOT / 'firmware/diagnostic', ROOT / 'firmware/instrument'
    sources = [app / n for n in ('app.c', 'board.c', 'pads.c', 'usb.c',
        'usb_descriptors.c', 'protocol.c', 'transport.c', 'led.c', 'control_lights.c', 'din.c', 'touch_config.c', 'settings.c')]
    sources += [board / n for n in ('startup.S', 'runtime.c', 'controls.c', 'led.c', 'center.c')]
    sources += [ROOT / 'core' / n for n in ('thy_core.c', 'thy_chord_layout.c', 'thy_voicing.c', 'thy_harmony.c', 'thy_harmony_workspace.c', 'thy_library.c', 'thy_library_ui.c', 'thy_library_engine.c', 'thy_mpe.c', 'thy_channel_expression.c', 'thy_expression_profile.c', 'thy_expression_engine.c', 'thy_midi.c', 'thy_clock.c', 'thy_transport.c', 'thy_arp.c', 'thy_rhythm.c', 'thy_calibration.c', 'thy_calibration_engine.c', 'thy_sequencer.c', 'thy_sequencer_ui.c', 'thy_sequence_engine.c', 'thy_song_transfer.c', 'thy_instrument.c', 'thy_lighting.c', 'thy_ui.c',
        'thy_state.c', 'thy_menu_data.c', 'thy_engine.c', 'thy_studio_ui.c', 'thy_display.c', 'thy_settings.c', 'thy_settings_transfer.c', 'thy_companion.c')]
    sources += [output / 'instrument_data.c']
    sources += [TINYUSB_ROOT / n for n in USB_SOURCES]
    inputs = set([*sources, *board.rglob('*.h'), *app.rglob('*.h'),
        *ROOT.joinpath('core').glob('*.h'), board / 'board.c', board / 'pads.c', app / 'linker.ld',
        Path(__file__), ROOT / 'tools/build_instrument_data.py',
        ROOT / 'tools/build_diagnostic.py', ROOT / 'tools/native_compile.py', ROOT / 'tools/local_env.py', ROOT / 'tools/anonymize_elf.py'])
    source_hashes = {('generated/instrument_data.c' if p == output / 'instrument_data.c'
        else str(p.relative_to(ROOT)).replace('\\', '/')): sha(p) for p in sorted(inputs)}
    identity = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest()[:16]
    info = output / 'diag_build_info.h'
    info.write_text(f'#define THY_DIAG_BUILD_ID "{identity}"\n', encoding='utf-8')
    flags = ['-target', 'thumb-freestanding-eabi', '-mcpu=cortex_m4', '-mfloat-abi=soft',
        '-ffreestanding', '-fno-builtin', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
        '-fdata-sections', '-ffunction-sections', '-flto', '-Oz', '-g', '-Wall', '-Wextra', '-Werror',
        '-std=c11', '-DSTM32F405xx', '-DTHY_DIAG_WITH_USB=1']
    flags.extend(source_prefix_flags())
    includes = [app, board, ROOT / 'core', output, TINYUSB_ROOT / 'src',
        DEPS / 'cmsis-core', DEPS / 'cmsis-device-f4', board / 'freestanding']
    include_flags = [flag for p in includes for flag in ('-I', str(p))]
    elf = output / 'THY666-instrument-alpha.elf'
    print('Building integrated instrument alpha ' + identity, flush=True)
    command = [str(compiler), 'cc', *flags, *include_flags, '-nostdlib', '-include', str(info),
        '-Wl,-T,' + str(app / 'linker.ld'), '-Wl,--gc-sections', '-Wl,--build-id=none',
        '-Wl,--entry=Reset_Handler', '-Wl,-z,stack-size=8192', *map(str, sources), '-o', str(elf)]
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    debug_paths = None if args.keep_debug else anonymize_elf_file(elf)
    inspection, image = inspect_elf(elf.read_bytes())
    binary = output / 'THY666-instrument-alpha.bin'
    binary.write_bytes(image)
    inspection_path = output / 'THY666-instrument-alpha.inspection.json'
    inspection_path.write_text(json.dumps(inspection, indent=2) + '\n', encoding='utf-8')
    manifest = {'format': 'thy666.integrated-instrument-alpha', 'version': 1,
        'build_id': identity, 'source_sha256': source_hashes, 'data': data,
        'toolchain': toolchain, 'dependencies': dependencies, 'flags': flags,
        'private_debug_paths_retained': args.keep_debug,
        'debug_path_anonymization': debug_paths,
        'memory_bytes': inspection['memory_bytes'], 'reserved_stack_bytes': 8192,
        'tests': [], 'flash_ready': False, 'device_validated': False,
        'scope': 'Offline instrument with live Harmony workspace, shared constrained voicing and note-role controls, full-surface contextual Harmony exploration, global cluster inversion and settings v18. Legacy settings, expression, physical calibration, library and songs migrate through the shared codec. Size-optimized LTO and bounded cooperative work; no device access.',
        'limits': ['This source revision has not been flashed or device-validated',
            'strike velocity uses recent ADC rise with millisecond resolution and no added attack window; physical soft/hard presets require target-specific qualification',
            'sensitivity profiles are user-specific configuration',
            'new framebuffer and note-color output are software-tested; physical component RGB naming and new timing remain unmeasured',
            'V17 preserves legacy byte offsets and appends Harmony direction/map/contrast preferences; new-version device restoration needs qualification; updater may erase settings',
            'calibration Saved/Calibrated review is playable after all48 selected-cluster pads; unapplied candidates are RAM-only until Apply',
            'stored melody retains 16 unassigned cells; generated layouts cover 48',
            'MPE first milestone is live pads only: Arps/sequencer and per-note expression recording are excluded while enabled; maximum15member channels and no voice stealing',
            'MPE pressure/CC74 cadence, optional split feel, shared-channel policies, receiver interoperability and target optimization need focused device qualification']}
    if args.test:
        # The MCU's tiny stdio shim must never shadow the host C library.
        native_includes = [flag for p in includes[:-1] for flag in ('-I', str(p))]
        native = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *native_includes]
        native += native_target_flags()
        # The actual-app harness replaces only the host-visible endpoint
        # declaration; the instrument app/core/transport source stays real.
        host_include = output / 'native-include'
        host_include.mkdir()
        host_usb = host_include / 'tusb.h'
        host_usb.write_text('#include <stdbool.h>\n#include <stdint.h>\n'
            'bool tud_midi_packet_write(const uint8_t packet[4]);\n', encoding='utf-8')
        jobs = [
            ('test_assigned_voices', [ROOT / 'tests/core/test_assigned_voices.c']),
            ('test_mpe', [ROOT / 'tests/core/test_mpe.c']),
            ('test_channel_expression', [ROOT / 'tests/core/test_channel_expression.c']),
            ('test_generated_expression_gate', [ROOT / 'tests/core/test_generated_expression_gate.c', ROOT / 'core/thy_sequencer.c']),
            ('test_expression_profile', [ROOT / 'tests/core/test_expression_profile.c']),
            ('test_expression_engine', [ROOT / 'tests/core/test_expression_engine.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c', ROOT / 'core/thy_settings.c']),
            ('test_chords', [ROOT / 'tests/core/test_chords.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c']),
            ('test_menu_reset', [ROOT / 'tests/core/test_menu_reset.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c', ROOT / 'core/thy_settings.c', ROOT / 'core/thy_studio_ui.c', ROOT / 'core/thy_ui.c']),
            ('test_chord_layout', [ROOT / 'tests/core/test_chord_layout.c']),
            ('test_voicing', [ROOT / 'tests/core/test_voicing.c']),
            ('test_harmony', [ROOT / 'tests/core/test_harmony.c']),
            ('test_library', [ROOT / 'tests/core/test_library.c']),
            ('test_library_ui', [ROOT / 'tests/core/test_library_ui.c']),
            ('test_library_engine', [ROOT / 'tests/core/test_library_engine.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c']),
            ('test_calibration', [ROOT / 'tests/core/test_calibration.c', ROOT / 'core/thy_calibration.c']),
            ('test_calibration_engine', [ROOT / 'tests/core/test_calibration_engine.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c', ROOT / 'core/thy_settings.c']),
            ('test_display', [ROOT / 'tests/core/test_display.c', ROOT / 'core/thy_display.c']),
            ('test_sequencer', [ROOT / 'tests/core/test_sequencer.c', ROOT / 'core/thy_sequencer.c']),
            ('test_sequencer_ui', [ROOT / 'tests/core/test_sequencer_ui.c', ROOT / 'core/thy_sequencer_ui.c', ROOT / 'core/thy_sequencer.c']),
            ('test_sequencer_engine', [ROOT / 'tests/core/test_sequencer_engine.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c']),
            ('test_song_transfer', [ROOT / 'tests/core/test_song_transfer.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c']),
            ('test_settings_transfer', [ROOT / 'tests/core/test_settings_transfer.c', ROOT / 'core/thy_settings_transfer.c', ROOT / 'core/thy_settings.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c']),
            ('test_companion', [ROOT / 'tests/core/test_companion.c', ROOT / 'core/thy_companion.c', ROOT / 'core/thy_settings.c', ROOT / 'core/thy_studio_ui.c', ROOT / 'core/thy_ui.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_menu_data.c']),
            ('test_lower_controls', [ROOT / 'tests/core/test_lower_controls.c', ROOT / 'core/thy_instrument.c']),
            ('test_touch_config', [ROOT / 'tests/core/test_touch_config.c', app / 'touch_config.c']),
            ('test_settings', [ROOT / 'tests/core/test_settings.c', ROOT / 'core/thy_settings.c',
                ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_state.c', ROOT / 'core/thy_engine.c', ROOT / 'core/thy_menu_data.c']),
            ('test_instrument', [ROOT / 'tests/core/test_instrument.c', ROOT / 'core/thy_instrument.c']),
            ('test_instrument_transport', [ROOT / 'tests/core/test_instrument_transport.c', app / 'transport.c']),
            ('test_instrument_catalog', [ROOT / 'tests/core/test_instrument_catalog.c',
                ROOT / 'core/thy_instrument.c', output / 'instrument_data.c']),
            ('test_instrument_din', [ROOT / 'tests/core/test_instrument_din.c']),
            ('test_instrument_control_lights', [ROOT / 'tests/core/test_instrument_control_lights.c',
                ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_engine.c',
                ROOT / 'core/thy_state.c', ROOT / 'core/thy_menu_data.c']),
            ('test_instrument_led', [ROOT / 'tests/core/test_instrument_led.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_lighting.c']),
            ('test_lighting', [ROOT / 'tests/core/test_lighting.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_lighting.c']),
            ('test_ui', [ROOT / 'tests/core/test_ui.c', ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_ui.c']),
            ('test_instrument_protocol', [ROOT / 'tests/core/test_instrument_protocol.c',
                ROOT / 'core/thy_instrument.c', app / 'protocol.c', app / 'transport.c', app / 'touch_config.c']),
            ('test_instrument_app', [ROOT / 'tests/core/test_instrument_app.c',
                ROOT / 'core/thy_instrument.c', ROOT / 'core/thy_state.c',
                ROOT / 'core/thy_engine.c', ROOT / 'core/thy_menu_data.c', ROOT / 'core/thy_settings.c', ROOT / 'core/thy_settings_transfer.c', ROOT / 'core/thy_companion.c', ROOT / 'core/thy_studio_ui.c', ROOT / 'core/thy_ui.c', app / 'transport.c']),
        ]
        for name, tested in jobs:
            target = output / (name + '.exe')
            extra = ['-I', str(host_include)] if name == 'test_instrument_app' else []
            rhythm_sources = [ROOT / 'core' / name for name in ('thy_harmony_workspace.c','thy_expression_engine.c','thy_library_engine.c','thy_rhythm.c','thy_calibration.c','thy_calibration_engine.c','thy_sequencer.c','thy_sequencer_ui.c','thy_sequence_engine.c','thy_song_transfer.c')] if any(p.name == 'thy_engine.c' for p in tested) else []
            if rhythm_sources and not any(p.name == 'thy_lighting.c' for p in tested): rhythm_sources.append(ROOT / 'core/thy_lighting.c')
            command = [str(compiler), 'cc', *extra, *native, str(ROOT / 'core/thy_core.c'),
                str(ROOT / 'core/thy_chord_layout.c'), str(ROOT / 'core/thy_voicing.c'), str(ROOT / 'core/thy_harmony.c'), str(ROOT / 'core/thy_library.c'), str(ROOT / 'core/thy_library_ui.c'), str(ROOT / 'core/thy_midi.c'), str(ROOT / 'core/thy_mpe.c'), str(ROOT / 'core/thy_channel_expression.c'), str(ROOT / 'core/thy_expression_profile.c'), str(ROOT / 'core/thy_clock.c'),
                str(ROOT / 'core/thy_transport.c'), str(ROOT / 'core/thy_arp.c'),
                *map(str, rhythm_sources), *map(str, tested), '-o', str(target)]
            subprocess.run(command, cwd=ROOT, env=env, check=True)
            result = subprocess.run([str(target)], cwd=ROOT, env=env, check=True,
                                    text=True, capture_output=True)
            print(result.stdout, end='', flush=True)
            recorded_sources = [*tested, host_usb] if name == 'test_instrument_app' else tested
            manifest['tests'].append({'name': name, 'passed': True, 'output': result.stdout,
                'binary_sha256': sha(target), 'source_sha256': {str(p.relative_to(ROOT)): sha(p) for p in recorded_sources}})
    manifest['artifacts'] = {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)}
        for p in (elf, binary, inspection_path)}
    for p in inputs:
        key = 'generated/instrument_data.c' if p == output / 'instrument_data.c' else str(p.relative_to(ROOT)).replace('\\', '/')
        if sha(p) != source_hashes[key]:
            raise ValueError('Build input changed during compilation: ' + key)
    manifest['sources_unchanged_during_build'] = True
    snapshot = output / 'source-snapshot'
    snapshot_paths = set(inputs)
    snapshot_paths.update(TINYUSB_ROOT.joinpath('src').rglob('*.h'))
    snapshot_paths.update(DEPS.joinpath('cmsis-core').glob('*'))
    snapshot_paths.update(DEPS.joinpath('cmsis-device-f4').glob('*'))
    snapshot_paths.update((ROOT / 'tests/core').glob('test_instrument*.c'))
    snapshot_paths.update((ROOT / 'tests/core/fixtures').glob('*.h'))
    snapshot_paths.update(ROOT / 'tests/core' / name for name in ('test_song_transfer.c', 'test_settings_transfer.c', 'test_companion.c', 'test_sequencer_engine.c', 'test_sequencer.c', 'test_sequencer_ui.c', 'test_display.c', 'test_lighting.c', 'test_ui.c', 'test_touch_config.c', 'test_studio_ui.c', 'test_alpha_polish.c', 'test_settings.c', 'test_views.c', 'test_engine.c'))
    snapshot_paths.update(ROOT / 'tests/core' / name for name in ('test_calibration.c','test_calibration_engine.c','test_mpe.c','test_assigned_voices.c','test_expression_engine.c','test_channel_expression.c','test_generated_expression_gate.c','test_expression_profile.c','test_chords.c'))
    snapshot_paths.update(ROOT / 'tests/core' / name for name in ('test_chord_layout.c','test_voicing.c','test_harmony.c','test_library.c','test_library_ui.c','test_library_engine.c'))
    snapshot_paths.update(ROOT / 'tests/core' / name for name in ('test_menu_reset.c','test_harmony_workspace.c','test_cluster_swap.c'))
    snapshot_paths.update((ROOT / 'tests').glob('test_instrument*.py'))
    snapshot_paths.update((ROOT / 'tests').glob('test_instrument*.ps1'))
    snapshot_paths.update((ROOT / 'tools').glob('*instrument*.ps1'))
    if args.test: snapshot_paths.add(host_usb)
    snapshot_paths.update(ROOT / 'tools' / p for p in ('build_diagnostic.py',
        'inspect_diagnostic_elf.py', 'setup_diagnostic_deps.py', 'build_runtime_data.py', 'chord_dump.py'))
    frozen = {}
    for p in sorted(snapshot_paths):
        if not p.is_file(): continue
        key = 'generated/instrument_data.c' if p == output / 'instrument_data.c' else str(p.relative_to(ROOT)).replace('\\', '/')
        dest = snapshot / key
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, dest)
        if sha(p) != sha(dest): raise ValueError('Snapshot mismatch: ' + key)
        frozen[key] = sha(dest)
    (snapshot / 'manifest.json').write_text(json.dumps(frozen, indent=2) + '\n', encoding='utf-8')
    manifest['source_snapshot'] = {'files': len(frozen),
        'manifest_sha256': sha(snapshot / 'manifest.json')}
    (output / 'THY666-instrument-alpha.manifest.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'build': identity, 'image_bytes': len(image),
        'memory': inspection['memory_bytes'], 'device_validated': False}), flush=True)


if __name__ == '__main__':
    main()
