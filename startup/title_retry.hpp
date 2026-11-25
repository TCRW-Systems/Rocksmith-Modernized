#pragma once
#include "input_classifier.hpp"

// Game layout observations were cross-checked with RSMods' public offsets and
// QualityOfLife.cpp. This independent, title-only fallback tracks requests at
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
inline Pending pending{};
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
inline bool IsTitle(uintptr_t movie) {
    uintptr_t name=movie+0x8c,start=U32(name+0x14)==name+0x10?name:U32(name);
    uintptr_t end=U32(name+0x10);
    if(end<start || end-start>128)return false;
    constexpr char target[]="titlescreen";
    for(unsigned i=0;i+sizeof(target)-1<=end-start;++i) {
        bool equal=true;
        for(unsigned j=0;j<sizeof(target)-1;++j) {
            BYTE c=0;if(!Read(start+i+j,c))return false;
            if(c>='A' && c<='Z')c+=32;
            if(c!=target[j]){equal=false;break;}
        }
        if(equal)return true;
    }
    return false;
}
inline void __cdecl Track(uintptr_t movie,unsigned id0,unsigned id1) {
    if(U32(movie)!=movieVtable || !IsTitle(movie))return;
    if(!thread)thread=GetCurrentThreadId();
    if(thread!=GetCurrentThreadId())return;
    pending={movie,Owner(movie),id0,id1,GetTickCount64(),GetTickCount64(),frames,0};
    if(logger)logger("title_movie_request_tracked",0);
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
    if(!pending.movie || thread!=GetCurrentThreadId())return;
    ++frames;ULONGLONG now=GetTickCount64();
    uintptr_t movie=pending.movie;
    if(U32(movie)!=movieVtable || Owner(movie)!=pending.owner || U32(movie+0x40)!=pending.id0 ||
       U32(movie+0x44)!=pending.id1 || U32(movie+0x58) || now-pending.started>30000) {
        if(logger)logger(U32(movie+0x58)?"title_movie_ready":"title_movie_retry_cancelled",pending.retries);
        pending={};return;
    }
    if(!Eligible(now,frames,pending))return;
    unsigned first=U32(loader+0x144),end=U32(loader+0x148);
    if(!first || end<first || (end-first)%4 || end-first>256*4)return;
    // Retry only while the loader still owns a matching, object-ready/movie-missing entry.
    for(unsigned at=first;at<end;at+=4) {
        unsigned handle=U32(at);
        unsigned table=U32(loader+0x10+((handle>>12)&255)*12);
        unsigned record=U32(table+(handle&4095)*4),state=U32(record+0xc);
        uint64_t owner=static_cast<uint64_t>(U32(state+0x1c))<<32 | U32(state+0x18);
        if(owner!=pending.owner || !U8(state+0x10) || U8(state+0x11))continue;
        pending.last=now;pending.frame=frames;++pending.retries;
        if(logger)logger("title_movie_request_retried",pending.retries);
        Send(movie,pending.id0,pending.id1);return;
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
inline bool Install(void (*log)(const char*,unsigned)) {
    logger=log;error=1;if(installed || !InputClassifier::PrepareHost())return false;
    BYTE* base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    BYTE* update=base+0x19ef80;BYTE* request=base+0x8189e0;
    // Another mod already owns the menu-update hook. Leave its recovery path alone.
    if(update[0]==0xe9){deferred=true;error=0;if(logger)logger("title_movie_retry_deferred_existing_hook",0);return true;}
    constexpr BYTE a[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x14,0x8b,0x45,0x08,0x53,0x8b,0x58,0x0c};
    constexpr BYTE b[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x08,0x56,0x8b,0xf0,0x8b,0x4e,0x38};
    error=2;if(memcmp(update,a,sizeof(a)) || memcmp(request,b,sizeof(b)))return false;
    HMODULE pinned;error=3;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&RequestHook),&pinned))return false;
    movieVtable=reinterpret_cast<uintptr_t>(base+0xe1fa30);
    // The update hook is harmless without a tracked request, even if the second install fails.
    error=4;if(!Patch(updatePatch,update,reinterpret_cast<void*>(&UpdateHook),updateRelay))return false;
    error=5;if(!Patch(requestPatch,request,reinterpret_cast<void*>(&RequestHook),requestRelay))return false;
    installed=true;error=0;return true;
}
}
