#pragma once
#include <windows.h>
#include "native_protection.hpp"

// Process-local protection changes using Windows APIs only. Resolve the native
// fallback outside DllMain; no third-party module, fixed address or syscall ID.
namespace MemoryProtection {
using NativeProtect=LONG(NTAPI*)(HANDLE,PVOID*,SIZE_T*,ULONG,PULONG);
using WinProtect=BOOL(WINAPI*)(LPVOID,SIZE_T,DWORD,PDWORD);
inline WinProtect winProtect=&VirtualProtect;
inline NativeProtect nativeProtect=nullptr;
inline bool nativeResolved=false;
inline NativeProtect directProtect=nullptr;
inline bool directResolved=false;
inline bool preferDirect=false;
inline DWORD windowsError=0;
inline LONG nativeStatus=0;
inline bool usedNative=false;
inline unsigned nativeCalls=0;
inline unsigned directCalls=0;

inline NativeProtect ResolveNative() {
    if(!nativeResolved) {
        HMODULE module=GetModuleHandleW(L"ntdll.dll");
        if(module)nativeProtect=reinterpret_cast<NativeProtect>(GetProcAddress(module,"NtProtectVirtualMemory"));
        nativeResolved=true;
    }
    return nativeProtect;
}
inline bool PrepareForGame() {
    if(!directResolved){directProtect=NativeProtection::Resolve();directResolved=true;}
    preferDirect=directProtect!=nullptr;return preferDirect;
}
inline bool Protect(void* address,SIZE_T length,DWORD protection,DWORD* previous) {
    windowsError=0;nativeStatus=0;usedNative=false;
    if(!address || !length || !previous){windowsError=ERROR_INVALID_PARAMETER;return false;}
    if(preferDirect && directProtect) {
        PVOID region=address;SIZE_T size=length;ULONG old=0;
        usedNative=true;++directCalls;
        nativeStatus=directProtect(GetCurrentProcess(),&region,&size,protection,&old);
        if(nativeStatus<0)return false;
        *previous=old;return true;
    }
    DWORD old=0;
    if(winProtect(address,length,protection,&old)){*previous=old;return true;}
    windowsError=GetLastError();
    auto native=ResolveNative();
    PVOID region=address;SIZE_T size=length;ULONG nativeOld=0;
    usedNative=true;
    nativeStatus=static_cast<LONG>(0xc000007a);
    if(native) {
        ++nativeCalls;
        nativeStatus=native(GetCurrentProcess(),&region,&size,protection,&nativeOld);
        if(nativeStatus>=0){*previous=nativeOld;return true;}
    }
    if(!directResolved){directProtect=NativeProtection::Resolve();directResolved=true;}
    if(!directProtect)return false;
    region=address;size=length;nativeOld=0;++directCalls;
    nativeStatus=directProtect(GetCurrentProcess(),&region,&size,protection,&nativeOld);
    if(nativeStatus>=0){*previous=nativeOld;return true;}
    return false;
}
}
