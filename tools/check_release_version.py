"""Check exact release-tag and package version agreement; never modifies files."""
import argparse
import json
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def check(tag: str | None = None) -> str:
    version = json.loads((ROOT / 'package.json').read_text())['version']
    if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', version):
        raise ValueError('Version must be X.X.X without a prefix or suffix')
    if tag is not None and tag != 'v' + version:
        raise ValueError(f'Tag {tag!r} does not match v{version}')
    for folder, package in [('desktop', 'thy666-desktop'), ('updater', 'thy666-updater')]:
        config = tomllib.loads((ROOT / folder / 'Cargo.toml').read_text())
        if config['package']['version'] != version:
            raise ValueError(f'{folder}/Cargo.toml version mismatch')
        lock = tomllib.loads((ROOT / folder / 'Cargo.lock').read_text())
        for row in lock['package']:
            if row['name'] in ('thy666-desktop', 'thy666-updater') and row['version'] != version:
                raise ValueError(f'{folder}/Cargo.lock version mismatch')
    if json.loads((ROOT / 'desktop/tauri.conf.json').read_text())['version'] != version:
        raise ValueError('Tauri version mismatch')
    return version


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag')
    print(check(parser.parse_args().tag))
