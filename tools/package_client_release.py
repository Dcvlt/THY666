"""Package a verified native Client build as unsigned DMG or Windows installer.

Run build_desktop.py --test --without-releases first. No hardware is accessed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile

from build_desktop import ROOT, TARGET, native_source_identities, verify_artifact
from check_release_version import check


def package(makensis: str | None = None) -> list[Path]:
    version = check()
    record = json.loads((TARGET / 'desktop-build.json').read_text())
    if not record.get('tests_run') or record['assets'].get('compile_check_only') is not False:
        raise ValueError('A full tested Client build is required')
    if record['native_source_sha256'] != native_source_identities():
        raise ValueError('Native source changed; rebuild before packaging')
    executable = ROOT / record['artifact']['output']
    if verify_artifact(executable, record['host']) != record['artifact']:
        raise ValueError('Executable differs from tested build')
    if any(row['file'] != 'README.txt' for row in record['assets']['resources']):
        raise ValueError('Public Client releases must omit local firmware resources')
    out = ROOT / 'build/releases'
    out.mkdir(parents=True, exist_ok=True)
    host = record['host']
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='client-package-') as temporary:
        stage = Path(temporary)
        if host.endswith('apple-darwin'):
            app = ROOT / record['development_app']
            if (app / 'Contents/MacOS/thy666-desktop').read_bytes() != executable.read_bytes():
                raise ValueError('App executable differs from verified executable')
            shutil.copytree(app, stage / 'THY666.app')
            (stage / 'Applications').symlink_to('/Applications')
            resources = stage / 'THY666.app/Contents/Resources'
            shutil.copyfile(ROOT / 'THIRD_PARTY_NOTICES.md', resources / 'PROJECT_NOTICES.md')
            shutil.copyfile(ROOT / 'docs/licenses/THY666-MIT-legacy.txt', resources / 'THY666-MIT-legacy.txt')
            font_text = b''
            for notice in sorted((ROOT / 'web/assets/fonts').glob('*/OFL.txt')):
                font_text += notice.parent.name.encode() + b'\n' + notice.read_bytes() + b'\n'
            (resources / 'FONT_LICENSES.txt').write_bytes(font_text)
            arch = 'arm64' if host.startswith('aarch64') else 'x64'
            output = out / f'THY666-Client-v{version}-macos-{arch}.dmg'
            if output.exists():
                raise ValueError('Refusing to overwrite an existing release asset')
            subprocess.run(['hdiutil', 'create', '-volname', 'THY666', '-srcfolder', str(stage),
                            '-format', 'UDZO', str(output)], check=True)
            subprocess.run(['hdiutil', 'verify', str(output)], check=True)
        elif host == 'x86_64-pc-windows-msvc':
            import zipfile
            portable = ROOT / record['package']['file']
            if hashlib.sha256(portable.read_bytes()).hexdigest() != record['package']['sha256']:
                raise ValueError('Portable package differs from tested build')
            with zipfile.ZipFile(portable) as archive:
                for name in archive.namelist():
                    if not (stage / name).resolve().is_relative_to(stage.resolve()):
                        raise ValueError('Invalid portable package path')
                archive.extractall(stage / 'payload')
            shutil.copyfile(ROOT / 'THIRD_PARTY_NOTICES.md', stage / 'payload/PROJECT_NOTICES.md')
            shutil.copyfile(ROOT / 'docs/licenses/THY666-MIT-legacy.txt', stage / 'payload/THY666-MIT-legacy.txt')
            output = out / f'THY666-Client-v{version}-windows-x64-setup.exe'
            if output.exists():
                raise ValueError('Refusing to overwrite an existing release asset')
            subprocess.run([makensis or 'makensis', f'/DVERSION={version}',
                            f'/DPAYLOAD={stage / "payload"}', f'/DOUTPUT={output}',
                            str(ROOT / 'desktop/installer.nsi')], check=True)
        else:
            raise ValueError('Release installer supports macOS and Windows x64')
    checksum = output.with_suffix(output.suffix + '.sha256')
    checksum.write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
    return [output, checksum]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--makensis')
    for path in package(parser.parse_args().makensis):
        print(path.relative_to(ROOT))
