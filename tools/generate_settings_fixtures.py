#!/usr/bin/env python3
"""Regenerate golden NVS key/value fixtures using historical settings writers.
Requires the release tags in the local clone and g++; never uses current writer
structs to represent historical storage. Synthetic credentials only.
"""
import pathlib, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
TAGS = ['Beta-0.1', 'Beta-0.2', 'Beta-0.3', 'Beta-0.3.1', 'Beta-0.3.2',
        'Beta-0.4', 'Beta-0.4.1', 'Beta-0.4.2', 'Beta-0.4.3']
def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)
for tag in TAGS:
    sha = git('rev-parse', tag+'^{commit}').decode().strip()
    with tempfile.TemporaryDirectory() as folder:
        folder = pathlib.Path(folder)
        for path in git('ls-tree', '-r', '--name-only', sha, 'src').decode().splitlines():
            dest = folder/path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(git('show', sha+':'+path))
        exe = folder/'writer'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra',
                        '-I', str(folder/'src'), '-I', str(ROOT/'tests/host'),
                        str(folder/'src/app_settings.cpp'),
                        str(ROOT/'tests/host/settings_fixture_writer.cpp'),
                        '-o', str(exe)], check=True)
        for mode in ['can', 'wifi']:
            body = subprocess.check_output([str(exe), mode]).decode()
            out = ROOT/'tests/host/fixtures/settings'/f'{tag}-{mode}.nvs'
            out.write_text(f'# Historical settings writer: {tag} {sha}\n'+body)
            print(out.relative_to(ROOT))
