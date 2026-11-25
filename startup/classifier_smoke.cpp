#include <windows.h>
#include <cstdio>
#include "input_classifier.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned protectionCalls=0;
static LONG __cdecl FixtureProtect(void* address,SIZE_T length,DWORD protection,DWORD* previous) {
    ++protectionCalls;
    return VirtualProtect(address,length,protection,previous)?0:static_cast<LONG>(0xc0000001);
}
// Model the pinned release's custom register arguments and caller stack cleanup.
static __declspec(naked) void FixtureProtectAbi() {
    __asm {
        push ebp
        mov ebp,esp
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
        push edx
        push ecx
        call FixtureProtect
        add esp,16
        pop ebp
        ret
    }
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && wcscmp(argv[1],L"--host-check")==0) {
        // Map the pinned image without running DllMain or resolving imports.
        // Never call the helper in this fixture; only validate host selection.
        wchar_t host[MAX_PATH];wcscpy_s(host,argv[2]);wchar_t* slash=wcsrchr(host,L'\\');CHECK(slash);wcscpy_s(slash+1,MAX_PATH-(slash+1-host),L"xinput1_3.dll");
        HMODULE mapped=LoadLibraryExW(host,nullptr,DONT_RESOLVE_DLL_REFERENCES);
        CHECK(mapped);
        bool okay=InputClassifier::ResolveRsmodsProtect(argv[2]);
        printf("RSMods host accepted: %d; stage: %lu; hash stage: %lu\n",okay,InputClassifier::lastRsmodsGuard,InputClassifier::lastHashError);
        return okay?0:1;
    }
    if(argc==3 && wcscmp(argv[1],L"--game-check")==0) {
        bool okay=InputClassifier::ExactGameHash(argv[2]);
        printf("Exact game hash accepted: %d; reason: %lu; observed: ",okay,InputClassifier::lastHashError);
        for(BYTE value:InputClassifier::observedDigest)printf("%02x",value);
        puts("");return okay?0:1;
    }
    if(argc!=1){puts("Usage: classifier_smoke [--host-check|--game-check <absolute game executable>]");return 2;}
    // Standalone native fixture returns TRUE and has the verified six-byte prologue.
    BYTE code[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0xb8,1,0,0,0,0x8b,0xe5,0x5d,0xc3};
    BYTE* target=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(target);
    memcpy(target,code,sizeof(code));DWORD old;CHECK(VirtualProtect(target,4096,PAGE_EXECUTE_READ,&old));
    auto function=reinterpret_cast<InputClassifier::Function>(target);
    InputClassifier::rsmodsProtect=reinterpret_cast<void*>(&FixtureProtectAbi);
    GUID mouse={1},other={2};
    CHECK(function(&mouse)==TRUE);
    CHECK(InputClassifier::InstallAt(target));
    CHECK(protectionCalls==2 && InputClassifier::lastProtectionStatus==0 && InputClassifier::lastRestoreError==0);
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
    puts("PASS: x86 trampoline, known-device scope, unknown-device fallback, nested scopes, version and duplicate-install guards");
    // The trampoline intentionally stays alive until process exit, as in the game.
    return 0;
}
