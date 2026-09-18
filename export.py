#!/usr/bin/env python3
"""Copy the current Windows build into a folder and ZIP."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name')
    parser.add_argument('--output', type=Path, default=ROOT / 'exports')
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    if args.name in ('', '.', '..') or any(c in args.name for c in '\\/:*?"<>|'):
        parser.error('use a folder name, not a path')

    output = args.output.resolve()
    folder = output / args.name
    archive = output / (args.name + '.zip')
    if folder.exists() or archive.exists():
        parser.error('that name already exists')
    for source in (ROOT / 'bin', ROOT / 'assets'):
        if output == source or source in output.parents:
            parser.error('output must be outside bin/ and assets/')
    if args.build:
        subprocess.run([sys.executable, str(ROOT / 'build.py'), 'release', 'windows'],
                       cwd=ROOT, check=True)
    for name in ('bin/blackhole.exe', 'bin/SDL2.dll', 'bin/ktx.dll', 'assets'):
        if not (ROOT / name).exists():
            parser.error(f'missing: {name}')
    if not list((ROOT / 'bin/shaders').rglob('*.spv')):
        parser.error('missing compiled shaders')

    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as temp:
        package = Path(temp) / args.name
        (package / 'bin').mkdir(parents=True)
        for source in [ROOT / 'bin/blackhole.exe', *(ROOT / 'bin').glob('*.dll')]:
            shutil.copy2(source, package / 'bin' / source.name)
        shutil.copytree(ROOT / 'bin/shaders', package / 'bin/shaders')
        shutil.copytree(ROOT / 'assets', package / 'assets')
        if (ROOT / 'imgui.ini').exists():
            shutil.copy2(ROOT / 'imgui.ini', package / 'imgui.ini')
        (package / 'run.cmd').write_text(
            '@echo off\nsetlocal\ncd /d "%~dp0"\n'
            'set "VKG_ASSET_ROOT=%~dp0"\n'
            'set "VKG_SHADER_ROOT=%~dp0bin\\shaders"\n'
            'bin\\blackhole.exe %*\n', encoding='ascii')
        packed = shutil.make_archive(str(Path(temp) / args.name), 'zip', temp, args.name)
        package.rename(folder)
        Path(packed).rename(archive)
    print(f'{folder}\n{archive}')


if __name__ == '__main__':
    main()
