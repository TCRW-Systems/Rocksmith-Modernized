#pragma once
#include "input_classifier.hpp"

namespace IntroSkip {
inline void* getter=nullptr;
inline bool installed=false;
inline DWORD error=0;
inline volatile LONG completions=0;
inline const float finished=4.0f;
inline void (*logger)(const char*,unsigned)=nullptr;
inline void __cdecl Notify() {InterlockedIncrement(&completions);if(logger)logger("native_intro_completion_requested",completions);}

// Original custom ABI: movie in EAX, engine string in ECX, float result in ST0.
// Only the intro controller's completion query is redirected. Missing/unready
// state 0 and unknown values are preserved; initialized states 1..3 become 4.
inline __declspec(naked) void Query() {
    __asm {
        call dword ptr [getter]
        pushfd
        push eax
        sub esp,4
        fst dword ptr [esp]
        mov eax,dword ptr [esp]
        cmp eax,03f800000h
        je complete
        cmp eax,040000000h
        je complete
        cmp eax,040400000h
        jne done
    complete:
        fstp st(0)
        mov dword ptr [esp],040800000h
        pushad
        call Notify
        popad
        fld dword ptr [esp]
    done:
        add esp,4
        pop eax
        popfd
        ret
    }
}
inline bool InstallAt(BYTE* call,void* original) {
    error=1;if(installed || call[0]!=0xe8)return false;
    int32_t displacement=0;memcpy(&displacement,call+1,4);
    error=2;if(call+5+displacement!=original)return false;
    HMODULE module;error=3;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Query),&module))return false;
    DWORD previous;error=4;
    if(!InputClassifier::ProtectTarget(call,5,PAGE_EXECUTE_READWRITE,&previous))return false;
    BYTE before[5];memcpy(before,call,5);getter=original;
    BYTE patch[5]={0xe8,0,0,0,0}; // Preserve CALL semantics: Query returns to this exact site.
    int32_t relative=static_cast<int32_t>(reinterpret_cast<BYTE*>(&Query)-(call+5));
    memcpy(patch+1,&relative,4);memcpy(call,patch,5);
    DWORD ignored;error=5;
    if(!InputClassifier::ProtectTarget(call,5,previous,&ignored)) {
        memcpy(call,before,5);FlushInstructionCache(GetCurrentProcess(),call,5);
        InputClassifier::ProtectTarget(call,5,previous,&ignored);getter=nullptr;return false;
    }
    FlushInstructionCache(GetCurrentProcess(),call,5);installed=true;error=0;return true;
}
inline bool InstallForGame(void (*log)(const char*,unsigned)) {
    error=10;if(!InputClassifier::PrepareHost())return false;
    BYTE* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    constexpr BYTE expected[]={0xe8,0x70,0x1a,0x67,0,0xd9,0x9d,0x28,0xff,0xff,0xff,0x8b,0xce,0xe8,0x63,0xec,0x3a,0,0xd9,0x85,0x28,0xff,0xff,0xff,0xd9,0xc0};
    constexpr BYTE getterPrefix[]={0x55,0x8b,0xec,0x8b,0x40,0x58,0x83,0xec,0x24,0x33,0xd2,0x56,0x3b,0xc2,0x0f,0x84,0x87,0,0,0};
    error=11;if(memcmp(base+0x1a6c9b,expected,sizeof(expected)))return false;
    error=12;if(memcmp(base+0x818710,getterPrefix,sizeof(getterPrefix)))return false;
    error=13;if(memcmp(base+0xdad828,"mPlaybackState",15))return false;
    logger=log;return InstallAt(base+0x1a6c9b,base+0x818710);
}
}
