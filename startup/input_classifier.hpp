#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <vector>

namespace InputClassifier {
using Function=BOOL(__cdecl*)(const GUID*);
inline Function original=nullptr;
struct KnownDevice {bool active=false;GUID product={};};
inline thread_local KnownDevice knownProduct;
inline volatile LONG bypassCount=0;
inline DWORD lastGuardError=0;
inline DWORD lastHashError=0;
inline DWORD lastHookError=0;
inline DWORD lastHookWindowsError=0;
inline LONG lastProtectionStatus=0;
inline DWORD lastRestoreError=0;
inline DWORD lastRsmodsGuard=0;
inline void* rsmodsProtect=nullptr;
inline BYTE observedDigest[32]={};
inline constexpr BYTE prefix[]={0x55,0x8b,0xec,0x83,0xe4,0xf8};

// This pinned RSMods release uses an LTCG internal ABI: address in ECX,
// length in EDX, then protection and oldProtection on the caller-cleaned stack.
// Verified against its matching PDB and all 222 bytes of disassembly.
inline LONG CallRsmodsProtect(void* address,SIZE_T byteCount,DWORD protection,DWORD* previous) {
    void* function=rsmodsProtect;
    LONG status;
    __asm {
        push previous
        push protection
        mov ecx,address
        mov edx,byteCount
        call function
        add esp,8
        mov status,eax
    }
    return status;
}

inline bool ProtectTarget(void* address,SIZE_T length,DWORD protection,DWORD* previous) {
    if(!rsmodsProtect)return VirtualProtect(address,length,protection,previous)!=FALSE;
    lastProtectionStatus=CallRsmodsProtect(address,length,protection,previous);
    return lastProtectionStatus>=0;
}

struct KnownDeviceScope {
    KnownDevice previous;
    explicit KnownDeviceScope(const GUID& product):previous(knownProduct){knownProduct={true,product};}
    ~KnownDeviceScope(){knownProduct=previous;}
    KnownDeviceScope(const KnownDeviceScope&)=delete;
    KnownDeviceScope& operator=(const KnownDeviceScope&)=delete;
};

inline BOOL __cdecl Hook(const GUID* product) {
    if(knownProduct.active && product && IsEqualGUID(knownProduct.product,*product)) {
        InterlockedIncrement(&bypassCount);
        return FALSE; // A device already identified as keyboard/mouse is not XInput.
    }
    return original(product);
}

inline void Jump(BYTE* from,const void* to) {
    from[0]=0xe9;
    DWORD displacement=static_cast<DWORD>(reinterpret_cast<uintptr_t>(to)-reinterpret_cast<uintptr_t>(from)-5);
    memcpy(from+1,&displacement,4);
}

// The six copied bytes are three complete, position-independent instructions.
// Install only before game input enumeration starts, on its initialization thread.
inline bool InstallAt(BYTE* target) {
    lastHookError=1;
    if(original || memcmp(target,prefix,sizeof(prefix))!=0)return false;
    BYTE* trampoline=static_cast<BYTE*>(VirtualAlloc(nullptr,16,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    lastHookError=2;
    if(!trampoline){lastHookWindowsError=GetLastError();return false;}
    memcpy(trampoline,prefix,sizeof(prefix));
    Jump(trampoline+sizeof(prefix),target+sizeof(prefix));
    DWORD previous;
    lastHookError=3;
    if(!VirtualProtect(trampoline,16,PAGE_EXECUTE_READ,&previous)) {lastHookWindowsError=GetLastError();VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    HMODULE self;
    lastHookError=4;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Hook),&self)) {lastHookWindowsError=GetLastError();VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    lastHookError=5;
    if(!ProtectTarget(target,sizeof(prefix),PAGE_EXECUTE_READWRITE,&previous)) {lastHookWindowsError=GetLastError();VirtualFree(trampoline,0,MEM_RELEASE);return false;}
    original=reinterpret_cast<Function>(trampoline);
    BYTE patch[sizeof(prefix)]={0xe9,0,0,0,0,0x90};
    DWORD displacement=static_cast<DWORD>(reinterpret_cast<uintptr_t>(&Hook)-reinterpret_cast<uintptr_t>(target)-5);
    memcpy(patch+1,&displacement,4);
    memcpy(target,patch,sizeof(patch));
    DWORD ignored;lastRestoreError=ProtectTarget(target,sizeof(prefix),previous,&ignored)?0:1;
    FlushInstructionCache(GetCurrentProcess(),trampoline,16);
    FlushInstructionCache(GetCurrentProcess(),target,sizeof(prefix));
    lastHookError=0;return true;
}

inline bool ExactFileHash(const wchar_t* path,const BYTE (&expected)[32]) {
    lastHashError=1;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size;
    if(!GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>64*1024*1024){CloseHandle(file);return false;}
    // Hundreds of small API calls were expensive inside this protected process.
    // Bound allocation and use one read/hash update for each pinned image.
    std::vector<BYTE> buffer(static_cast<size_t>(size.QuadPart));
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    BYTE digest[32];bool okay=false;
    lastHashError=2;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 && (lastHashError=3,BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0)) {
        DWORD count=0;
        lastHashError=4;
        okay=ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)!=FALSE && count==buffer.size();
        if(okay){lastHashError=5;okay=BCryptHashData(hash,buffer.data(),count,0)>=0;}
        if(okay) {
            lastHashError=6;
            okay=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
            if(okay){memcpy(observedDigest,digest,sizeof(digest));lastHashError=7;okay=memcmp(digest,expected,sizeof(digest))==0;}
        }
    }
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);if(okay)lastHashError=0;return okay;
}

inline bool ExactGameHash(const wchar_t* path) {
    constexpr BYTE expected[]={0xbb,0x05,0x69,0x59,0xc0,0xc6,0x37,0x1d,0x4e,0xcf,0x78,0xf8,0x4c,0x5d,0x27,0xe2,0xfa,0xe9,0x3a,0x9d,0x02,0x58,0x83,0x0c,0x2a,0x36,0xf3,0x3e,0x6a,0x27,0x78,0xeb};
    return ExactFileHash(path,expected);
}

inline bool ResolveRsmodsProtect(const wchar_t* gamePath) {
    lastRsmodsGuard=1;
    wchar_t path[MAX_PATH],expectedPath[MAX_PATH];
    wcscpy_s(expectedPath,gamePath);
    wchar_t* slash=wcsrchr(expectedPath,L'\\');if(!slash)return false;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-expectedPath),L"xinput1_3.dll");
    // RSMods also loads the system XInput DLL with the same basename.
    // Resolve the full path so Windows cannot select that other module.
    HMODULE module=GetModuleHandleW(expectedPath);
    lastRsmodsGuard=2;
    if(!module)return false;
    DWORD n=GetModuleFileNameW(module,path,MAX_PATH);
    if(!n || n>=MAX_PATH)return false;
    lastRsmodsGuard=3;if(_wcsicmp(path,expectedPath))return false;
    constexpr BYTE hash[]={0xae,0x15,0x66,0x43,0x4f,0x39,0x63,0x4c,0xee,0x8c,0x51,0x92,0xda,0x80,0x9c,0x79,0x7a,0xc9,0x76,0x65,0x7a,0xec,0x9f,0xa0,0xed,0x42,0x02,0x6e,0xc2,0x1b,0x10,0x24};
    lastRsmodsGuard=4;if(!ExactFileHash(path,hash))return false;
    BYTE* base=reinterpret_cast<BYTE*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    lastRsmodsGuard=5;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    lastRsmodsGuard=6;if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=0x6add9a9c || nt->OptionalHeader.SizeOfImage!=0x1e5000)return false;
    constexpr BYTE helperPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x10};
    lastRsmodsGuard=7;if(memcmp(base+0x9d4c0,helperPrefix,sizeof(helperPrefix)))return false;
    HMODULE pinned;
    lastRsmodsGuard=8;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(base+0x9d4c0),&pinned))return false;
    rsmodsProtect=base+0x9d4c0;lastRsmodsGuard=0;return true;
}

// Shared compatibility validation; it never installs the classifier hook.
// A missing RSMods module may be retried at later input initialization calls.
inline bool PrepareHost() {
    if(rsmodsProtect)return true;
    wchar_t path[MAX_PATH];DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    lastGuardError=1;
    if(!n || n>=MAX_PATH)return false;
    static int gameAccepted=0;
    if(!gameAccepted)gameAccepted=ExactGameHash(path)?1:-1;
    if(gameAccepted<0){lastGuardError=100+lastHashError;return false;}
    lastGuardError=204;if(!ResolveRsmodsProtect(path))return false;
    lastGuardError=0;return true;
}

inline bool InstallForGame() {
    if(!PrepareHost())return false;
    BYTE* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    constexpr BYTE call1[]={0xe8,0x2f,0x11,0,0,0x83,0xc4,4,0x85,0xc0};
    constexpr BYTE call2[]={0xe8,0x10,0x10,0,0,0x83,0xc4,4,0x85,0xc0};
    lastGuardError=201;if(memcmp(base+0x800d4c,call1,sizeof(call1)))return false;
    lastGuardError=202;if(memcmp(base+0x800e6b,call2,sizeof(call2)))return false;
    lastGuardError=203;
    if(!InstallAt(base+0x801e80))return false;
    lastGuardError=0;return true;
}
}
