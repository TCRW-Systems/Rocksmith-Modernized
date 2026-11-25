#pragma once
#include "input_classifier.hpp"

// Game layout observations were cross-checked with RSMods' public offsets and
// QualityOfLife.cpp. This independent, bounded startup fallback tracks requests at
// dispatch (rather than keeping a registry of every menu movie).
namespace TitleRetry {
inline void* requestRelay=nullptr;
inline void* updateRelay=nullptr;
inline uintptr_t movieVtable=0;
inline bool installed=false,deferred=false;
inline DWORD error=0,thread=0;
inline unsigned frames=0;
inline void (*logger)(const char*,unsigned)=nullptr;
struct Pending {
    uintptr_t movie=0;uint64_t owner=0;unsigned id0=0,id1=0;
    ULONGLONG started=0,last=0;unsigned frame=0,retries=0;
};
inline Pending requests[32]{};
inline ULONGLONG acceptingUntil=0;
template<class T> inline bool Read(uintptr_t address,T& value) {
    if(address<0x10000 || address>0xffffffffu-sizeof(T))return false;
    __try {value=*reinterpret_cast<const T*>(address);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline unsigned U32(uintptr_t p){unsigned v=0;Read(p,v);return v;}
inline unsigned U8(uintptr_t p){BYTE v=0;Read(p,v);return v;}
inline uint64_t Owner(uintptr_t movie) {
    uintptr_t p=U32(movie+0xc);
    return static_cast<uint64_t>(U32(p+0x54))<<32 | U32(p+0x50);
}
inline void __cdecl Track(uintptr_t movie,unsigned id0,unsigned id1) {
    ULONGLONG now=GetTickCount64();
    if(now>acceptingUntil || U32(movie)!=movieVtable || !Owner(movie))return;
    if(!thread)thread=GetCurrentThreadId();
    if(thread!=GetCurrentThreadId())return;
    Pending* slot=nullptr;
    for(auto& p:requests) {
        if(p.movie==movie){slot=&p;break;}
        if(!p.movie && !slot)slot=&p;
    }
    if(!slot){if(logger)logger("startup_movie_retry_queue_full",32);return;}
    *slot={movie,Owner(movie),id0,id1,now,now,frames,0};
    if(logger)logger("startup_movie_request_tracked",static_cast<unsigned>(slot-requests));
}
inline void Send(uintptr_t movie,unsigned id0,unsigned id1) {
    __asm {push id1
        push id0
        mov eax,movie
        call dword ptr [requestRelay]}
}
inline bool Eligible(ULONGLONG now,unsigned frame,const Pending& p) {
    return now-p.last>=1000 && frame-p.frame>=5 && now-p.started<=30000;
}
inline void __cdecl Poll(uintptr_t loader) {
    if(thread!=GetCurrentThreadId())return;
    ++frames;ULONGLONG now=GetTickCount64();
    for(auto& p:requests) {
        uintptr_t movie=p.movie;if(!movie)continue;
        if(U32(movie)!=movieVtable || Owner(movie)!=p.owner || U32(movie+0x40)!=p.id0 ||
           U32(movie+0x44)!=p.id1 || U32(movie+0x58) || now-p.started>30000) {
            if(logger)logger(U32(movie+0x58)?"startup_movie_ready":"startup_movie_retry_cancelled",p.retries);
            p={};continue;
        }
        if(!Eligible(now,frames,p))continue;
        unsigned first=U32(loader+0x144),end=U32(loader+0x148);
        if(!first || end<first || (end-first)%4 || end-first>256*4)continue;
        // Retry only while the loader still owns a matching, object-ready/movie-missing entry.
        for(unsigned at=first;at<end;at+=4) {
            unsigned handle=U32(at);
            unsigned table=U32(loader+0x10+((handle>>12)&255)*12);
            unsigned record=U32(table+(handle&4095)*4),state=U32(record+0xc);
            if(!state)continue;
            uint64_t owner=static_cast<uint64_t>(U32(state+0x1c))<<32 | U32(state+0x18);
            if(owner!=p.owner || !U8(state+0x10) || U8(state+0x11))continue;
            p.last=now;p.frame=frames;++p.retries;
            if(logger)logger("startup_movie_request_retried",p.retries);
            Send(movie,p.id0,p.id1);break;
        }
    }
}
inline __declspec(naked) void RequestHook() {
    __asm {pushfd
        pushad
        push dword ptr [esp+44]
        push dword ptr [esp+44]
        push dword ptr [esp+36]
        call Track
        add esp,12
        popad
        popfd
        jmp dword ptr [requestRelay]}
}
inline __declspec(naked) void UpdateHook() {
    __asm {pushfd
        pushad
        push dword ptr [esp+40]
        call Poll
        add esp,4
        popad
        popfd
        jmp dword ptr [updateRelay]}
}
struct Detour {BYTE* site=nullptr;BYTE* relay=nullptr;BYTE original[6]{};};
inline Detour requestPatch{},updatePatch{};
inline bool Patch(Detour& d,BYTE* site,void* hook,void*& relay) {
    if(memcmp(site,InputClassifier::prefix,6))return false;
    BYTE* memory=static_cast<BYTE*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!memory)return false;
    memcpy(memory,site,6);memory[6]=0xe9;
    int32_t back=static_cast<int32_t>((site+6)-(memory+11));memcpy(memory+7,&back,4);
    DWORD old=0;if(!VirtualProtect(memory,32,PAGE_EXECUTE_READ,&old)){VirtualFree(memory,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),memory,11);
    if(!InputClassifier::ProtectTarget(site,6,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(memory,0,MEM_RELEASE);return false;}
    d.site=site;d.relay=memory;memcpy(d.original,site,6);relay=memory;
    BYTE patch[6]={0xe9,0,0,0,0,0x90};int32_t relative=static_cast<int32_t>(reinterpret_cast<BYTE*>(hook)-(site+5));memcpy(patch+1,&relative,4);memcpy(site,patch,6);
    DWORD ignored=0;
    if(!InputClassifier::ProtectTarget(site,6,old,&ignored)) {
        memcpy(site,d.original,6);FlushInstructionCache(GetCurrentProcess(),site,6);
        InputClassifier::ProtectTarget(site,6,old,&ignored);relay=nullptr;VirtualFree(memory,0,MEM_RELEASE);d={};return false;
    }
    FlushInstructionCache(GetCurrentProcess(),site,6);return true;
}
// Recognize the tested external recovery owner, rather than assuming every
// existing JMP implements request retries. No external function is called.
inline bool KnownRecoveryOwner(BYTE* update) {
    if(update[0]!=0xe9)return false;
    int32_t displacement=0;memcpy(&displacement,update+1,4);
    const BYTE* destination=update+5+displacement;
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(destination),&module))return false;
    wchar_t path[MAX_PATH];DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
    if(!length || length>=MAX_PATH)return false;
    constexpr BYTE expected[]={0xae,0x15,0x66,0x43,0x4f,0x39,0x63,0x4c,0xee,0x8c,0x51,0x92,0xda,0x80,0x9c,0x79,0x7a,0xc9,0x76,0x65,0x7a,0xec,0x9f,0xa0,0xed,0x42,0x02,0x6e,0xc2,0x1b,0x10,0x24};
    return InputClassifier::ExactFileHash(path,expected);
}
inline bool Install(void (*log)(const char*,unsigned)) {
    logger=log;error=1;if(installed || !InputClassifier::PrepareHost())return false;
    BYTE* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    BYTE* update=base+0x19ef80;BYTE* request=base+0x8189e0;
    // Defer only to the tested recovery implementation; unknown hooks fail closed.
    if(KnownRecoveryOwner(update)){deferred=true;error=0;if(logger)logger("title_movie_retry_deferred_existing_hook",0);return true;}
    constexpr BYTE a[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x14,0x8b,0x45,0x08,0x53,0x8b,0x58,0x0c};
    constexpr BYTE b[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x08,0x56,0x8b,0xf0,0x8b,0x4e,0x38};
    error=2;if(memcmp(update,a,sizeof(a)) || memcmp(request,b,sizeof(b)))return false;
    HMODULE pinned;error=3;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&RequestHook),&pinned))return false;
    movieVtable=reinterpret_cast<uintptr_t>(base+0xe1fa30);
    acceptingUntil=GetTickCount64()+30000;
    // The update hook is harmless without a tracked request, even if the second install fails.
    error=4;if(!Patch(updatePatch,update,reinterpret_cast<void*>(&UpdateHook),updateRelay))return false;
    error=5;if(!Patch(requestPatch,request,reinterpret_cast<void*>(&RequestHook),requestRelay))return false;
    installed=true;error=0;return true;
}
}
