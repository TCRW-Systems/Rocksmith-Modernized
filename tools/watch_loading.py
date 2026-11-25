"""Read-only menu-transition timing for the pinned local Rocksmith build.

No injection, debugger, process writes, network interception or UI input.
Pointers follow the pinned RSMods GameState/Offsets implementation.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rocksmith_diag import WindowsProcess

EXPECTED_GAME = 'bb056959c0c6371d4ecf78f84c5d27e2fae93a9d0258830c2a36f33e6a2778eb'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--seconds', type=float, default=240)
    parser.add_argument('--game', type=Path, required=True)
    args = parser.parse_args()
    game=args.game.resolve()
    if not 0 < args.seconds <= 600:
        raise SystemExit('Duration must be in (0, 600] seconds')
    if hashlib.sha256((game / 'Rocksmith2014.exe').read_bytes()).hexdigest() != EXPECTED_GAME:
        raise SystemExit('Unsupported executable; no process accessed')
    args.output.mkdir(parents=True, exist_ok=True)
    api = WindowsProcess()
    found = None
    deadline = time.monotonic() + 120
    while found is None and time.monotonic() < deadline:
        found = api.find(game)
        if found is None:
            time.sleep(.1)
    if found is None:
        raise SystemExit('Game not found')
    handle, pid = found
    k = api.k
    class Module(c.Structure):
        _fields_ = [('dwSize', w.DWORD), ('th32ModuleID', w.DWORD), ('th32ProcessID', w.DWORD),
                    ('GlblcntUsage', w.DWORD), ('ProccntUsage', w.DWORD), ('modBaseAddr', c.c_void_p),
                    ('modBaseSize', w.DWORD), ('hModule', w.HMODULE), ('szModule', w.WCHAR * 256),
                    ('szExePath', w.WCHAR * 260)]
    k.Module32FirstW.argtypes = [w.HANDLE, c.POINTER(Module)]
    k.Module32FirstW.restype = w.BOOL
    k.Module32NextW.argtypes = [w.HANDLE, c.POINTER(Module)]
    k.Module32NextW.restype = w.BOOL
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    k.ReadProcessMemory.restype = w.BOOL
    # Process creation can be observed before the loader exposes its module list.
    base = None
    module_deadline = time.monotonic() + 5
    while base is None and time.monotonic() < module_deadline:
        snapshot = k.CreateToolhelp32Snapshot(0x18, pid)
        try:
            entry = Module()
            entry.dwSize = c.sizeof(entry)
            ok = k.Module32FirstW(snapshot, c.byref(entry))
            while ok:
                if Path(entry.szExePath).resolve() == (game / 'Rocksmith2014.exe').resolve():
                    base = entry.modBaseAddr
                    break
                ok = k.Module32NextW(snapshot, c.byref(entry))
        finally:
            k.CloseHandle(snapshot)
        if base is None:
            time.sleep(.05)
    if base is None:
        k.CloseHandle(handle)
        raise RuntimeError('Exact game module not found')

    def read(address, size):
        if not address or not 0x10000 <= address <= 0xffffffff - size:
            raise ValueError('Invalid x86 read range')
        data = c.create_string_buffer(size)
        got = c.c_size_t()
        if not k.ReadProcessMemory(handle, address, data, size, c.byref(got)) or got.value != size:
            raise ValueError('Pointer unavailable')
        return data.raw

    def pointer(address):
        return struct.unpack('<I', read(address, 4))[0]

    def menu():
        try:
            level2 = pointer(pointer(base + 0xF6062C) + 0x28) + 0x8c
            # Only persist syntactically plausible menu identifiers, never arbitrary memory.
            for address in (pointer(level2), level2):
                try:
                    raw = read(address, 64).split(b'\0', 1)[0]
                    if re.fullmatch(rb'[A-Za-z_][A-Za-z0-9_ ]{1,62}', raw):
                        return raw.decode('ascii')
                except ValueError:
                    pass
        except ValueError:
            pass
        return None

    started = time.monotonic()
    previous = object()
    last_sample = 0
    try:
        with (args.output / 'menu-transitions.jsonl').open('w', encoding='utf-8', buffering=1) as transitions, \
             (args.output / 'process-samples.jsonl').open('w', encoding='utf-8', buffering=1) as samples:
            while time.monotonic() - started < args.seconds:
                now = datetime.now(timezone.utc).isoformat()
                current = menu()
                if current != previous:
                    transitions.write(json.dumps(dict(utc=now, menu=current, pid=pid)) + '\n')
                    previous = current
                if time.monotonic() - last_sample >= .5:
                    sample = api.sample(handle)
                    sample['utc'] = now
                    samples.write(json.dumps(sample) + '\n')
                    if sample.get('exited'):
                        break
                    last_sample = time.monotonic()
                time.sleep(.05)
    finally:
        k.CloseHandle(handle)


if __name__ == '__main__':
    main()
