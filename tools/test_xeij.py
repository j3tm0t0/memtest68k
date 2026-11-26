#!/usr/bin/env python3
"""Boot the actual target image in XEiJ; assert a full pass and low-RAM restore.
On Linux run with xvfb-run. No host desktop, OCR, or mock framebuffers required.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def symbols(path):
    """Read the big-endian ELF32 symbol table without a host cross-toolchain."""
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x02':
        raise ValueError('Expected big-endian ELF32')
    offset = struct.unpack_from('>I', data, 32)[0]
    size, count = struct.unpack_from('>HH', data, 46)
    sections = [struct.unpack_from('>10I', data, offset+i*size) for i in range(count)]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4]+strings_section[5]]
        for pos in range(section[4], section[4]+section[5], section[9]):
            name, value = struct.unpack_from('>II', data, pos)
            end = strings.index(b'\0', name)
            result[strings[name:end].decode()] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xeij', required=True, type=Path)
    parser.add_argument('--medium', required=True, choices=['fd', 'hds'])
    parser.add_argument('--cpu', required=True, choices=['68000', '68030'])
    parser.add_argument('--memory', type=int, choices=range(1, 13))
    parser.add_argument('--full-walking', action='store_true')
    parser.add_argument('--fault', action='store_true')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--java-home', type=Path)
    args = parser.parse_args()
    if not args.xeij.with_name('jSerialComm-2.11.4.jar').is_file():
        parser.error('Place the bundled jSerialComm-2.11.4.jar beside XEiJ.jar')
    if not (args.xeij.parent/'data'/'IPLROM.DAT').is_file():
        parser.error('Extract the bundled data/ directory beside XEiJ.jar')
    cpu = 3 if args.cpu == '68030' else 0
    ram = args.memory or (4 if cpu else 1)
    case = f'{args.medium}-{args.cpu}' + ('-fault' if args.fault else '')
    if args.memory: case += f'-{ram}M'
    if args.full_walking: case += '-full-walking'
    output = (args.output or ROOT/'build'/'xeij'/case).resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Each test has its own settings, disks, agent classes and logs.
    for name in ['result.json', 'screen.png']:
        (output/name).unlink(missing_ok=True)
    elf = ROOT/'build'/'boot'/('hds_standalone.elf' if args.medium == 'hds' else 'standalone.elf')
    syms = symbols(elf)
    image = ROOT/'build'/('memtest68-hd.hds' if args.medium == 'hds' else 'memtest68-fd.xdf')
    disk = output/image.name
    shutil.copyfile(image, disk)
    classes = output/'classes'
    classes.mkdir(exist_ok=True)
    def java_tool(name):
        home = args.java_home or (Path(os.environ['JAVA_HOME']) if 'JAVA_HOME' in os.environ else None)
        return str(home/'bin'/name) if home else name
    subprocess.run([java_tool('javac'), '-cp', str(args.xeij.resolve()), '-d', str(classes),
                    str(ROOT/'tools'/'XEiJHeadless.java')], check=True)
    manifest = output/'MANIFEST.MF'
    manifest.write_text('Premain-Class: XEiJHeadless\n')
    agent = output/'agent.jar'
    subprocess.run([java_tool('jar'), 'cfm', str(agent), str(manifest), '-C', str(classes), '.'], check=True)
    properties = output/'case.properties'
    properties.write_text(f'output={output.as_posix()}\nreport={syms["boot_report"]}\n'
                          f'verify={0xe40000 + syms["verify"] - syms["low_probe_image_start"]}\n'
                          f'cpu={cpu}\nram={ram*1024*1024}\nfault={str(args.fault).lower()}\n'
                          f'full_walking={str(args.full_walking).lower()}\n')
    drive = 'sc3' if args.medium == 'hds' else 'fd0'
    command = [java_tool('java'), '-Xmx512m', f'-javaagent:{agent}={properties}',
               f'-Djava.io.tmpdir={output}', '-jar', str(args.xeij.resolve()),
               f'-ini={output/"XEiJ.ini"}', '-saveonexit=off', '-config=default',
               f'-model={"X68030" if cpu else "XVI"}', f'-memory={ram}', '-clock=1000',
               f'-boot={drive}', f'-{drive}={disk}', '-flrinformed=on', '-usecanvas=off',
               '-sound=off', '-keyboard=none']
    print(f'XEiJ: {case}', flush=True)
    with (output/'emulator.log').open('w') as log:
        try:
            proc = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=660)
        except subprocess.TimeoutExpired:
            print(f'FAIL: emulator timeout; see {output}', file=sys.stderr)
            return 1
    result_path = output/'result.json'
    result = json.loads(result_path.read_text()) if result_path.exists() else {'success': False}
    for name, path in [('image', disk), ('elf', elf), ('xeij', args.xeij)]:
        result[name+'_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    result_path.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2), flush=True)
    if proc.returncode or not result['success']:
        print((output/'emulator.log').read_text(), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
