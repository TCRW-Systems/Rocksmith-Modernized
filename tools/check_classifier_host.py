"""Inspect loaded module identity for the classifier; reads only, no dumps."""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import sys
import argparse
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from rocksmith_diag import WindowsProcess

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--game',type=Path,required=True);args=parser.parse_args()
    api=WindowsProcess();found=api.find(args.game.resolve())
    if not found:raise SystemExit('Game not running')
    handle,pid=found;k=api.k
    class Module(c.Structure):
        _fields_=[('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.Module32NextW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32NextW.restype=w.BOOL
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    def read(address,size):
        buffer=c.create_string_buffer(size);n=c.c_size_t()
        if not k.ReadProcessMemory(handle,address,buffer,size,c.byref(n)) or n.value!=size:raise c.WinError(c.get_last_error())
        return buffer.raw
    snapshot=k.CreateToolhelp32Snapshot(0x18,pid)
    try:
        entry=Module();entry.dwSize=c.sizeof(entry);ok=k.Module32FirstW(snapshot,c.byref(entry));rows=[]
        while ok:
            if entry.szModule.lower() in ('xinput1_3.dll','d3dx9_42.dll','dinput8.dll'):
                row=dict(name=entry.szModule,path=entry.szExePath,base=hex(entry.modBaseAddr),size=entry.modBaseSize,sha256=hashlib.sha256(Path(entry.szExePath).read_bytes()).hexdigest())
                header=read(entry.modBaseAddr,4096);pe=struct.unpack_from('<I',header,60)[0]
                row.update(timestamp=hex(struct.unpack_from('<I',header,pe+8)[0]),pe_size=hex(struct.unpack_from('<I',header,pe+24+56)[0]))
                if entry.modBaseSize>0x9d4c6:row['helper_prefix']=read(entry.modBaseAddr+0x9d4c0,6).hex()
                rows.append(row)
            ok=k.Module32NextW(snapshot,c.byref(entry))
        print(json.dumps(rows,indent=2))
    finally:k.CloseHandle(snapshot);k.CloseHandle(handle)

if __name__=='__main__':main()
