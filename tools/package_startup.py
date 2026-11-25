"""Create a local two-file prototype ZIP from explicitly selected runtime files."""
import argparse
import configparser
import hashlib
from pathlib import Path
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    dll = args.dll.read_bytes()
    if len(dll) < 64 or dll[:2] != b'MZ':
        raise SystemExit('Expected a PE DLL')
    offset = struct.unpack_from('<I', dll, 0x3c)[0]
    if offset + 24 > len(dll) or dll[offset:offset + 4] != b'PE\0\0':
        raise SystemExit('Invalid PE header')
    machine = struct.unpack_from('<H', dll, offset + 4)[0]
    characteristics = struct.unpack_from('<H', dll, offset + 22)[0]
    if machine != 0x14c or not characteristics & 0x2000:
        raise SystemExit('Expected an x86 DLL, not a game/test executable')
    config = args.config.read_bytes()
    ini = configparser.ConfigParser(interpolation=None)
    ini.read_string(config.decode('utf-8-sig'))
    keys = {'SkipGamepadEnumeration', 'SkipKnownKeyboardMouseXInputChecks',
            'SkipIntro', 'StartupHighQoS'}
    expected = {key.lower() for key in keys}
    if ini.defaults() or ini.sections() != ['Startup'] or set(ini['Startup']) != expected:
        raise SystemExit('Config must contain only the four portable Startup options')
    if any(value not in ('0', '1') for value in ini['Startup'].values()):
        raise SystemExit('Startup options must be 0 or 1')
    files = {'dinput8.dll': dll, 'RSStartup.ini': config}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    with zipfile.ZipFile(args.output) as archive:
        if archive.namelist() != list(files) or archive.testzip() is not None:
            raise RuntimeError('ZIP integrity check failed')
        for name, content in files.items():
            if archive.read(name) != content:
                raise RuntimeError('ZIP content verification failed')
            print(f'{name}: {len(content)} bytes, SHA-256 {hashlib.sha256(content).hexdigest()}')
    print(f'Local prototype package: {args.output.resolve()}')


if __name__ == '__main__':
    main()
