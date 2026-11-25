#include <windows.h>
#include <cstdio>
#include "input_classifier.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned protectionCalls=0;
static BOOL WINAPI FailRestore(LPVOID address,SIZE_T length,DWORD value,PDWORD old) {
    if(++protectionCalls==2){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return VirtualProtect(address,length,value,old);
}
static LONG NTAPI NativeDenied(HANDLE,PVOID*,SIZE_T*,ULONG,PULONG) {return static_cast<LONG>(0xc0000022);}
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && wcscmp(argv[1],L"--game-check")==0) {
        bool okay=InputClassifier::ExactGameHash(argv[2]);
        printf("Exact game hash accepted: %d; reason: %lu; observed: ",okay,InputClassifier::lastHashError);
        for(BYTE value:InputClassifier::observedDigest)printf("%02x",value);
        puts("");return okay?0:1;
    }
    if(argc!=1){puts("Usage: classifier_smoke [--game-check <absolute game executable>]");return 2;}
    // Standalone native fixture returns TRUE and has the verified six-byte prologue.
    BYTE code[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0xb8,1,0,0,0,0x8b,0xe5,0x5d,0xc3};
    BYTE* target=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(target);
    memcpy(target,code,sizeof(code));DWORD old;CHECK(VirtualProtect(target,4096,PAGE_EXECUTE_READ,&old));
    auto function=reinterpret_cast<InputClassifier::Function>(target);
    GUID mouse={1},other={2};
    CHECK(function(&mouse)==TRUE);
    MemoryProtection::winProtect=FailRestore;
    MemoryProtection::nativeResolved=true;MemoryProtection::nativeProtect=NativeDenied;
    MemoryProtection::directResolved=true;MemoryProtection::directProtect=nullptr;
    CHECK(!InputClassifier::InstallAt(target));CHECK(InputClassifier::original==nullptr);
    CHECK(!memcmp(target,code,sizeof(code)));CHECK(function(&mouse)==TRUE);
    MEMORY_BASIC_INFORMATION failedMemory{};CHECK(VirtualQuery(target,&failedMemory,sizeof(failedMemory)));CHECK(failedMemory.Protect==PAGE_EXECUTE_READ);
    MemoryProtection::winProtect=&VirtualProtect;
    MemoryProtection::nativeResolved=false;MemoryProtection::nativeProtect=nullptr;
    MemoryProtection::directResolved=false;
    CHECK(InputClassifier::InstallAt(target));
    CHECK(InputClassifier::lastRestoreError==0);
    MEMORY_BASIC_INFORMATION memory={};CHECK(VirtualQuery(target,&memory,sizeof(memory))==sizeof(memory));CHECK(memory.Protect==PAGE_EXECUTE_READ);
    CHECK(function(&mouse)==TRUE); // Unknown contexts must reach the original code.
    {
        InputClassifier::KnownDeviceScope scope(mouse);
        CHECK(function(&mouse)==FALSE);
        CHECK(function(&other)==TRUE);
        {InputClassifier::KnownDeviceScope nested(other);CHECK(function(&other)==FALSE);CHECK(function(&mouse)==TRUE);}
        CHECK(function(&mouse)==FALSE);
    }
    CHECK(function(&mouse)==TRUE);
    CHECK(!InputClassifier::InstallAt(target));
    CHECK(!InputClassifier::ExactGameHash(L"classifier_smoke.exe"));
    puts("PASS: standalone x86 trampoline, scope/fallback/nesting, failed-restore rollback, RX restoration and version/duplicate guards");
    // The trampoline intentionally stays alive until process exit, as in the game.
    return 0;
}
