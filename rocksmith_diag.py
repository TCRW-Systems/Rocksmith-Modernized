"""Read-only Rocksmith diagnostics. Python 3.11+, no third-party dependencies."""
from __future__ import annotations

import argparse
import collections
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import uuid

DEFAULT_OUTPUT = Path(__file__).resolve().parent / "reports"
CONFIGS = ("Rocksmith.ini", "RS_ASIO.ini", "RSMods.ini")
LOGS = ("RS_ASIO-log.txt", "RSMods_debug.txt")
BINARIES = ("Rocksmith2014.exe", "xinput1_3.dll", "RS_ASIO.dll", "avrt.dll", "D3DX9_42.dll")


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


class DumpReader:
    """Bounds-check every reference; malformed dumps must not stop an inventory."""

    def __init__(self, data):
        self.data = data

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f"Truncated dump reference at {offset:#x}, size {size}")
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.read(offset, struct.calcsize(fmt)))

    def string(self, offset):
        length, = self.unpack("<I", offset)
        if length % 2 or length > 1024 * 1024:
            raise ValueError("Invalid minidump string length")
        return self.read(offset + 4, length).decode("utf-16-le", errors="replace")


def parse_minidump(data):
    r = DumpReader(data)
    if r.read(0, 4) != b"MDMP":
        raise ValueError("Not a Windows minidump")
    count, directory = r.unpack("<II", 8)
    if count > 4096:
        raise ValueError("Unreasonable stream count")
    streams = {}
    for i in range(count):
        kind, size, offset = r.unpack("<III", directory + i * 12)
        r.read(offset, size)
        streams[kind] = (size, offset)

    def stream(kind, minimum):
        size, offset = streams[kind]
        if size < minimum:
            raise ValueError(f"Stream {kind} is too short")
        return offset

    result = {"modules": []}
    arch = None
    if 7 in streams:  # MINIDUMP_SYSTEM_INFO
        offset = stream(7, 8)
        arch, = r.unpack("<H", offset)
        result["architecture"] = {0: "x86", 9: "x64", 12: "arm64"}.get(arch, str(arch))
        result["logical_processors_at_capture"] = r.read(offset + 6, 1)[0]
    if 4 in streams:  # MINIDUMP_MODULE_LIST: each module is 108 bytes
        offset = stream(4, 4)
        module_count, = r.unpack("<I", offset)
        if module_count > 65536 or 4 + module_count * 108 > streams[4][0]:
            raise ValueError("Invalid module list length")
        for i in range(module_count):
            entry = offset + 4 + i * 108
            base, size = r.unpack("<QI", entry)
            name_rva, = r.unpack("<I", entry + 20)
            name = r.string(name_rva).replace("\\", "/").rsplit("/", 1)[-1]
            result["modules"].append({"name": name, "base": base, "size": size})
    if 6 in streams:  # MINIDUMP_EXCEPTION_STREAM
        offset = stream(6, 168)
        code, flags, record, address, nparams = r.unpack("<IIQQI", offset + 8)
        if nparams > 15:
            raise ValueError("Invalid exception parameter count")
        params = r.unpack("<15Q", offset + 40)[:nparams]
        if arch == 0:  # Some x86 dumps sign-extend exception addresses.
            address &= 0xFFFFFFFF
        module = next((m for m in result["modules"]
                       if m["base"] <= address < m["base"] + m["size"]), None)
        exception = {"code": hex(code), "address": hex(address),
                     "parameters": [hex(p) for p in params],
                     "module": module["name"] if module else None,
                     "module_offset": hex(address - module["base"]) if module else None}
        if code == 0xC0000005 and len(params) >= 2:
            exception["access"] = {0: "read", 1: "write", 8: "execute"}.get(params[0], "unknown")
        result["exception"] = exception
    else:
        result["exception"] = None
    return result


def binary_info(path):
    data = path.read_bytes()
    result = {"size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    if data[:2] != b"MZ":
        raise ValueError("Missing DOS executable signature")
    pe, = struct.unpack_from("<I", data, 60)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine, = struct.unpack_from("<H", data, pe + 4)
    flags, = struct.unpack_from("<H", data, pe + 22)
    result.update(machine=hex(machine), large_address_aware=bool(flags & 0x20))
    return result


def ensure_output(game, parent, prefix):
    game, parent = game.resolve(), parent.resolve()
    if parent == game or game in parent.parents:
        raise ValueError("Output must be outside the game installation")
    name = datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = parent / (prefix + "-" + name)
    output.mkdir(parents=True)
    return output


def validate_game(game):
    game = game.resolve()
    if not (game / "Rocksmith2014.exe").is_file():
        raise ValueError(f"Rocksmith2014.exe not found in {game}")
    return game


def snapshot(game, output):
    """Only read installation files. Every write is under output."""
    report = {"schema_version": 1, "captured_utc": utc_now(), "game_directory": str(game),
              "warnings": [], "binaries": {}, "crashes": [], "library": {}}
    evidence = output / "evidence"
    evidence.mkdir()
    text_files = {}
    for name in CONFIGS + LOGS:
        source = game / name
        if not source.is_file():
            continue
        try:
            data = source.read_bytes()
            (evidence / name).write_bytes(data)
            text_files[name] = data.decode("utf-8-sig", errors="replace")
        except OSError as exc:
            report["warnings"].append(f"Could not read {name}: {exc}")
    for name in BINARIES:
        path = game / name
        if path.is_file():
            try:
                report["binaries"][name] = binary_info(path)
            except (OSError, ValueError, struct.error) as exc:
                report["warnings"].append(f"Could not inspect {name}: {exc}")
    records = []
    dlc = game / "dlc"
    walk_errors = []
    if not dlc.is_dir():
        report["warnings"].append("DLC directory not found")
    for directory, folders, files in os.walk(dlc, followlinks=False, onerror=walk_errors.append):
        for name in files:
            if not name.lower().endswith(".psarc"):
                continue
            path = Path(directory) / name
            try:
                stat = path.stat()
                records.append({"path": path.relative_to(game).as_posix(),
                                "size_bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns})
            except OSError as exc:
                report["warnings"].append(f"Could not stat {path}: {exc}")
    report["warnings"].extend(str(e) for e in walk_errors)
    records.sort(key=lambda item: item["path"].casefold())
    counts = collections.Counter(Path(item["path"]).name.casefold() for item in records)
    report["library"] = {
        "archive_count": len(records), "total_bytes": sum(item["size_bytes"] for item in records),
        "directory_count": len({str(Path(item["path"]).parent) for item in records}),
        "duplicate_filenames": {name: n for name, n in counts.items() if n > 1},
        "inventory_sha256": hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest(),
        "note": "Archives are not song counts. No ownership, content validity, or duplicate-song check performed.",
        "directory_links": "Directory symlinks are not followed; linked libraries need separate inspection.",
    }
    write_json(output / "library.json", records)
    for path in sorted(game.glob("*.mdmp")):
        entry = {"file": path.name}
        try:
            data = path.read_bytes()
            (evidence / path.name).write_bytes(data)
            entry.update(parse_minidump(data))
        except (OSError, ValueError, struct.error) as exc:
            entry["error"] = str(exc)
        report["crashes"].append(entry)
    versions = {}
    for name, pattern in [("RSMods", r"RSMODS Version:\s*([^\s]+)"),
                          ("RS_ASIO", r"Wrapper DLL loaded \(v([^\)]+)\)")]:
        text = text_files.get("RSMods_debug.txt" if name == "RSMods" else "RS_ASIO-log.txt", "")
        match = re.search(pattern, text)
        versions[name] = match.group(1) if match else None
    report["versions_from_last_logs"] = versions
    report["log_observations"] = {
        "asio_xrun_lines": len(re.findall(r"\bxrun\b", text_files.get("RS_ASIO-log.txt", ""))),
        "rsmods_error_lines": [line for line in text_files.get("RSMods_debug.txt", "").splitlines()
                               if "[ERROR]" in line],
        "note": "Logs may come from different sessions; errors are observations, not crash diagnoses.",
    }
    write_json(output / "snapshot.json", report)
    exception_groups = collections.Counter(
        (item.get("exception") or {}).get("module") or "unmapped / unavailable"
        for item in report["crashes"] if "error" not in item)
    lines = ["Rocksmith Modernized baseline", f"Captured: {report['captured_utc']}",
             f"Game: {game}", f"DLC archives: {len(records)} (not an exact song count)",
             f"DLC size: {report['library']['total_bytes'] / 2**30:.2f} GiB",
             f"Versions from last logs: {versions}", f"Existing dumps: {len(report['crashes'])}",
             f"Exception locations: {dict(exception_groups)}",
             f"ASIO xrun log lines: {report['log_observations']['asio_xrun_lines']}",
             "No game files changed. No game launched. No performance improvement measured.",
             "Exception locations do not establish root cause."]
    if report["warnings"]:
        lines += ["Warnings:"] + report["warnings"]
    (output / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report


class WindowsProcess:
    """Read process counters with Windows APIs; no injection or process writes."""

    def __init__(self):
        if os.name != "nt":
            raise RuntimeError("Session recording requires Windows")
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        self.p = ctypes.WinDLL("psapi", use_last_error=True)
        self.k.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        self.k.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        self.k.CloseHandle.argtypes = [wintypes.HANDLE]
        self.k.CloseHandle.restype = wintypes.BOOL
        self.k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.k.OpenProcess.restype = wintypes.HANDLE

        class Entry(ctypes.Structure):
            _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                        ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                        ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                        ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                        ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]

        class Memory(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
                "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]

        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in (
                "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

        self.Entry, self.Memory, self.IO = Entry, Memory, IO
        for name in ("Process32FirstW", "Process32NextW"):
            fn = getattr(self.k, name)
            fn.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
            fn.restype = wintypes.BOOL
        self.k.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                    wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.k.QueryFullProcessImageNameW.restype = wintypes.BOOL
        self.k.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.k.GetProcessTimes.restype = wintypes.BOOL
        self.k.GetProcessIoCounters.argtypes = [wintypes.HANDLE, ctypes.POINTER(IO)]
        self.k.GetProcessIoCounters.restype = wintypes.BOOL
        self.k.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.k.GetExitCodeProcess.restype = wintypes.BOOL
        self.p.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]
        self.p.GetProcessMemoryInfo.restype = wintypes.BOOL

    def find(self, game):
        handle = self.k.CreateToolhelp32Snapshot(2, 0)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            entry = self.Entry()
            entry.dwSize = ctypes.sizeof(entry)
            ok = self.k.Process32FirstW(handle, ctypes.byref(entry))
            while ok:
                if entry.szExeFile.lower() == "rocksmith2014.exe":
                    # Confirm full image path, avoiding unrelated copies of the game.
                    process = self.k.OpenProcess(0x1000 | 0x10, False, entry.th32ProcessID)
                    if process:
                        path = ctypes.create_unicode_buffer(32768)
                        size = wintypes.DWORD(len(path))
                        if self.k.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(size)):
                            if Path(path.value).resolve() == (game / "Rocksmith2014.exe").resolve():
                                return process, entry.th32ProcessID
                        self.k.CloseHandle(process)
                ok = self.k.Process32NextW(handle, ctypes.byref(entry))
        finally:
            self.k.CloseHandle(handle)
        return None

    @staticmethod
    def filetime(value):
        return (value.dwHighDateTime << 32) | value.dwLowDateTime

    def sample(self, handle):
        code = wintypes.DWORD()
        if not self.k.GetExitCodeProcess(handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        if code.value != 259:
            return {"exited": True, "exit_code": code.value}
        creation, exit_time, kernel, user = (wintypes.FILETIME() for _ in range(4))
        if not self.k.GetProcessTimes(handle, ctypes.byref(creation), ctypes.byref(exit_time),
                                      ctypes.byref(kernel), ctypes.byref(user)):
            raise ctypes.WinError(ctypes.get_last_error())
        result = {"cpu_seconds": (self.filetime(kernel) + self.filetime(user)) / 10**7,
                  "process_created_unix": self.filetime(creation) / 10**7 - 11644473600}
        memory = self.Memory()
        memory.cb = ctypes.sizeof(memory)
        if self.p.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
            result.update(working_set_bytes=memory.WorkingSetSize, private_bytes=memory.PrivateUsage,
                          page_faults=memory.PageFaultCount)
        else:
            result["memory_error"] = ctypes.get_last_error()
        io = self.IO()
        if self.k.GetProcessIoCounters(handle, ctypes.byref(io)):
            result.update(io_read_bytes=io.ReadTransferCount, io_write_bytes=io.WriteTransferCount)
        else:
            result["io_error"] = ctypes.get_last_error()
        return result


def append_event(session, label):
    if not (session / "session.json").is_file():
        raise ValueError("Not a recording directory (session.json missing)")
    with (session / "events.jsonl").open("a", encoding="utf-8") as f:
        f.write(json.dumps({"utc": utc_now(), "unix_seconds": time.time(), "label": label}) + "\n")


def record(args, game):
    api = WindowsProcess()
    output = ensure_output(game, args.output, "session")
    metadata = {"schema_version": 1, "started_utc": utc_now(), "game_directory": str(game),
                "interval_seconds": args.interval, "status": "waiting_for_game",
                "limitations": ["No frame timing or input latency measurement",
                                 "Process I/O includes cached I/O; it is not physical disk throughput",
                                 "CPU percent is relative to one logical processor; may exceed 100",
                                 "Exit code alone does not distinguish a crash from an ordinary exit"]}
    write_json(output / "session.json", metadata)
    before = output / "before"
    before.mkdir()
    snapshot(game, before)
    print(f"Session: {output}", flush=True)
    print("Waiting for Rocksmith. Start it normally in Steam. Ctrl+C stops recording only.", flush=True)
    print(f'To mark an event in another terminal: python rocksmith_diag.py mark "{output}" library_ready', flush=True)
    handle = None
    previous = None
    started = time.monotonic()
    deadline = started + args.wait_seconds
    try:
        while handle is None:
            found = api.find(game)
            if found:
                handle, pid = found
                metadata.update(status="recording", pid=pid, detected_utc=utc_now())
                write_json(output / "session.json", metadata)
                append_event(output, "process_detected")
                print(f"Recording PID {pid}", flush=True)
                break
            if time.monotonic() >= deadline:
                metadata["status"] = "no_process_found"
                return output
            time.sleep(min(args.interval, 1))
        record_start = time.monotonic()
        with (output / "samples.jsonl").open("a", encoding="utf-8", buffering=1) as f:
            while True:
                tick = time.monotonic()
                counters = api.sample(handle)
                if counters.get("exited"):
                    metadata.update(status="process_exited", exit_code=counters["exit_code"])
                    append_event(output, "process_exited")
                    break
                row = {"utc": utc_now(), "unix_seconds": time.time(), **counters}
                if previous:
                    elapsed = tick - previous[0]
                    row["cpu_percent_one_core"] = max(0, counters["cpu_seconds"] - previous[1]) / elapsed * 100
                previous = tick, counters["cpu_seconds"]
                f.write(json.dumps(row) + "\n")
                if args.duration and tick - record_start >= args.duration:
                    metadata["status"] = "duration_reached"
                    break
                time.sleep(max(0, args.interval - (time.monotonic() - tick)))
    except KeyboardInterrupt:
        metadata["status"] = "stopped_by_user"
    except OSError as exc:
        metadata.update(status="sampling_error", error=str(exc))
    finally:
        if handle is not None:
            api.k.CloseHandle(handle)
        metadata["ended_utc"] = utc_now()
        after = output / "after"
        after.mkdir()
        try:
            snapshot(game, after)
        except OSError as exc:
            metadata["after_snapshot_error"] = str(exc)
        write_json(output / "session.json", metadata)
    return output


def positive(value):
    number = float(value)
    if not 0 < number < float("inf"):
        raise argparse.ArgumentTypeError("Must be a finite positive number")
    return number


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, help="Rocksmith installation directory (required for snapshot/record)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("snapshot", help="Inventory configuration, logs and existing crashes without launching")
    watch = commands.add_parser("record", help="Wait for the game and record process counters")
    watch.add_argument("--interval", type=positive, default=1.0)
    watch.add_argument("--wait-seconds", type=positive, default=300.0)
    watch.add_argument("--duration", type=positive, help="Stop after this many seconds of recording")
    marker = commands.add_parser("mark", help="Append a timestamped manual event to a session")
    marker.add_argument("session", type=Path)
    marker.add_argument("label", help="For example library_ready, song_start, practice_seek, observed_stall")
    args = parser.parse_args(argv)
    try:
        if args.command == "mark":
            append_event(args.session.resolve(), args.label)
            print("Marker saved")
            return 0
        if args.game is None:
            raise ValueError('Pass --game <Rocksmith installation directory> before snapshot/record')
        game = validate_game(args.game)
        if args.command == "snapshot":
            output = ensure_output(game, args.output, "baseline")
            snapshot(game, output)
            print((output / "summary.txt").read_text(encoding="utf-8"))
        else:
            output = record(args, game)
        print(f"Saved: {output}")
        return 0
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

