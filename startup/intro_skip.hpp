#pragma once
#include "input_classifier.hpp"

namespace IntroSkip {
inline void* getter=nullptr;
inline bool installed=false;
inline DWORD error=0;
inline volatile LONG completions=0;
inline void (*logger)(const char*,unsigned)=nullptr;
inline void* setter=nullptr;
inline void* acceleratedMovie=nullptr;
inline unsigned attempts=0;

// Non-owning engine string, using the verified external-buffer layout. The
// native property accessors only read it; no engine allocation is transferred.
struct PropertyName {
    const char* data;char reserved[12]{};const char* end;const char* capacity;
    explicit PropertyName(const char* text):data(text),end(text+strlen(text)),capacity(end+1){}
};
static_assert(sizeof(PropertyName)==24,"Engine string layout");
inline float Read(void* movie,PropertyName* name) {
    float value;
    __asm {mov eax,movie
        mov ecx,name
        call dword ptr [getter]
        fstp dword ptr [value]}
    return value;
}
inline bool Write(void* movie,PropertyName* name,float value) {
    bool result;
    __asm {lea edx,value
        push edx
        mov eax,movie
        mov ecx,name
        call dword ptr [setter]
        mov result,al}
    return result;
}
inline void __cdecl Accelerate(void* movie,unsigned playback,unsigned screen) {
    if(!setter || acceleratedMovie==movie || attempts>=8 || screen>0x40a00000u ||
       (playback!=0x3f800000u && playback!=0x40000000u && playback!=0x40400000u))return;
    ++attempts;
    PropertyName name("mScene.introTimeline.timeScale");
    float current=Read(movie,&name);
    if(!(current>0.0f && current<=100.0f))return;
    if(Write(movie,&name,100.0f)) {
        acceleratedMovie=movie;InterlockedIncrement(&completions);
        if(logger)logger("native_intro_timeline_accelerated",completions);
    }
}

// EAX: movie, ECX: engine string, ST0: float result. Keep the original playback
// state and let the intro's own timeline fire its asset/transition callbacks.
// EBX+0x34 is the controller's just-updated mScreenState copy.
inline __declspec(naked) void Query() {
    __asm {
        push eax
        call dword ptr [getter]
        pushfd
        sub esp,4
        fstp dword ptr [esp]
        pushad
        push dword ptr [ebx+034h]
        push dword ptr [esp+36]
        push dword ptr [esp+48]
        call Accelerate
        add esp,12
        popad
        fld dword ptr [esp]
        add esp,4
        popfd
        lea esp,[esp+4]
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
    constexpr BYTE setterPrefix[]={0x55,0x8b,0xec,0x8b,0x40,0x58,0x83,0xec,0x20,0x53,0x85,0xc0,0x74,0x5b,0x8b,0x55,0x08};
    error=14;if(memcmp(base+0x818690,setterPrefix,sizeof(setterPrefix)))return false;
    setter=base+0x818690;
    logger=log;return InstallAt(base+0x1a6c9b,base+0x818710);
}
}
