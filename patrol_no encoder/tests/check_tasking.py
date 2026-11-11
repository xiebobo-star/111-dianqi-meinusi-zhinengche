"""Compile application sources with TASKING and real TC264 headers; no flashing.

This checks compilation/assembly only, not a full firmware link.
Usage: python tests/check_tasking.py --compiler <cctc.exe> --output <outside-project-dir>
"""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--compiler', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = Path(a.output).resolve()
if out == root or root in out.parents:
    raise SystemExit('Use an output directory outside the ADS project.')
out.mkdir(parents=True, exist_ok=True)
config = ET.parse(root / '.cproject').find('.//configuration[@name="Debug"]')
option = config.find('.//option[@superClass="com.infineon.aurix.buildsystem.managed.c.compiler.tasking.include"]')
includes = []
for item in option.findall('listOptionValue'):
    value = item.get('value').strip('"')
    prefix = '${workspace_loc:/${ProjName}/'
    if not value.startswith(prefix) or not value.endswith('}'):
        raise ValueError('Unsupported include path: ' + value)
    includes.append('-I' + (root / value[len(prefix):-1]).as_posix())
compiler = Path(a.compiler).resolve()
log = out / 'tasking-check.log'
with log.open('w', encoding='utf-8') as f:
    for mode, defines in [('vision_drive', []), ('vision_preview', ['-DCAR_DRIVE_ENABLE=0']),
                          ('legacy_drive', ['-DCAR_VISION_ONLY=0'])]:
        folder = out / mode
        folder.mkdir(exist_ok=True)
        for source in sorted((root / 'user').glob('*.c')):
            assembly = folder / (source.stem + '.src')
            obj = folder / (source.stem + '.o')
            commands = [
                [str(compiler), '-cs', '-D__CPU__=tc26xb', '--iso=99', '--language=+volatile',
                 '--exceptions', '--anachronisms', '--fp-model=3', '-O0', '--tradeoff=4',
                 '--compact-max-size=200', '-g', '-Wc-w544', '-Wc-w557', '-Ctc26xb',
                 '-Y0', '-N0', '-Z0'] + defines + includes + ['-o', str(assembly), str(source)],
                [str(compiler.with_name('astc.exe')), '-Og', '-Os', '--error-limit=42',
                 '-o', str(obj), str(assembly)],
            ]
            for command in commands:
                f.write(json.dumps(command) + '\n')
                result = subprocess.run(command, cwd=folder, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
                output = result.stdout.decode('utf-8', errors='replace')
                f.write(output)
                f.flush()
                if result.returncode:
                    print(output, flush=True)
                    raise SystemExit(f'TASKING failed: {mode}/{source.name}; log: {log}')
            print(f'TASKING compiled/assembled {mode}/{source.name}', flush=True)
    f.write('PASS: 27 application translation units, compile and assembly only.\n')
print('PASS: 3 modes x 9 application sources; full firmware link NOT performed.', flush=True)
