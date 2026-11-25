#pragma once
#include "input_classifier.hpp"

// Narrow adapter for the pinned RSMods AutoLoadProfileMod. Its original tick is
// invoked only at the initial profile picker, never at a connection/auth dialog.
namespace ProfileBoot {
using Tick=void(__thiscall*)(void*,void*);
inline Tick originalTick=nullptr;
inline uintptr_t gameBase=0;
inline bool installed=false;
inline DWORD error=0;
inline void (*logger)(const char*,unsigned)=nullptr;

struct Router {
    bool done=false, sent=false;
    bool waiting=false;
    unsigned attempts=0;
    ULONGLONG entered=0, lastSend=0;
    bool Expire(ULONGLONG now) {
        if(waiting && now-entered>=5000)done=true;
        return done;
    }
    bool Step(const char* menu,bool validProfile,ULONGLONG now) {
        if(Expire(now))return false;
        if(strcmp(menu,"MainMenu")==0){done=true;return false;}
        if(strcmp(menu,"ProfileSelect")!=0) {
            // A successful selection consumes this boot's allowance. Failed
            // loads and later Change Profile flows are deliberately manual.
            if(sent)done=true;
            if(waiting)done=true;
            return false;
        }
        if(!waiting){entered=now;waiting=true;}
        if(attempts>=3){done=true;return false;}
        if(!validProfile || now-entered<600 || (sent && now-lastSend<300))return false;
        sent=true;++attempts;lastSend=now;return true;
    }
};
inline Router router;

inline bool Read(uintptr_t address,void* value,SIZE_T size) {
    SIZE_T got=0;
    return address>=0x10000 && address<=UINTPTR_MAX-size &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),value,size,&got) && got==size;
}
inline uintptr_t Pointer(uintptr_t address) {uintptr_t value=0;Read(address,&value,sizeof(value));return value;}
inline bool Text(uintptr_t address,char* out,SIZE_T size) {
    if(!Read(address,out,size))return false;
    return memchr(out,0,size)!=nullptr;
}
inline bool Menu(char (&out)[64]) {
    uintptr_t level=Pointer(Pointer(gameBase+0xf6062c)+0x28)+0x8c;
    return Text(Pointer(level),out,sizeof(out)) || Text(level,out,sizeof(out));
}
// Pinned engine string: 16-byte inline buffer (or heap pointer), end, capacity.
// Use the engine's length rather than treating its object header as C text.
inline bool ProfileText(uintptr_t address,char (&out)[256]) {
    uintptr_t end=Pointer(address+16), capacity=Pointer(address+20);
    uintptr_t start=capacity==address+16?address:Pointer(address);
    if(!start || end<start || capacity<end || end-start==0 || end-start>=sizeof(out))return false;
    SIZE_T length=end-start;
    if(!Read(start,out,length+1) || out[length]!=0 || memchr(out,0,length))return false;
    return true;
}
inline bool HasProfileAt(uintptr_t object) {
    // Two mirrored row vectors in this build: all saved profiles followed by
    // the creation action. Require coherent bounds and agreement between both.
    char selected[256]={},first[256]={},creation[256]={};
    if(!ProfileText(object+0x1fc,selected))return false;
    uintptr_t count=0;
    for(unsigned offset:{0x214u,0x220u}) {
        uintptr_t begin=Pointer(object+offset),end=Pointer(object+offset+4),capacity=Pointer(object+offset+8);
        if(!begin || end<begin || capacity<end || (end-begin)%24 || (capacity-begin)%24)return false;
        uintptr_t rows=(end-begin)/24;
        if(rows<2 || rows>256 || (capacity-begin)/24>256 || (count && count!=rows))return false;
        count=rows;
        if(!ProfileText(begin,first) || !ProfileText(end-24,creation))return false;
        // An ambiguous label is left manual, including a saved profile whose
        // name happens to equal the localized creation action.
        if(strcmp(selected,first) || !strcmp(first,creation))return false;
    }
    return true;
}
inline bool HasProfile() {
    uintptr_t address=gameBase+0xf6062c;
    constexpr unsigned offsets[]={0x18,0x3c,0x28};
    for(unsigned offset:offsets){address=Pointer(address);if(!address)return false;address+=offset;}
    return HasProfileAt(Pointer(address));
}
inline bool __fastcall Enabled(void*,void*,void*) {return !router.done;}
inline void __fastcall OnTick(void* self,void*,void* context) {
    if(router.Expire(GetTickCount64()))return;
    char menu[64]={};
    if(!Menu(menu))return;
    bool wasDone=router.done;
    if(router.Step(menu,strcmp(menu,"ProfileSelect")==0 && HasProfile(),GetTickCount64())) {
        if(logger)logger("profile_boot_select_last_used",0);
        originalTick(self,context);
    }
    if(!wasDone && router.done && logger)logger("profile_boot_finished_manual_selection_restored",0);
}

// Slots are verified in full against the matching PDB and pinned DLL. Install
// before RSMods initializes its registry, on the input initialization thread.
inline bool InstallAt(void** table,BYTE* host) {
    constexpr unsigned rvas[]={0x23830,0x7df30,0x7df50,0x26ef0,0x24b80,
        0x24b90,0x24b90,0x24b90,0x24b90,0x24b90,0x7df90,0x24b90,0x24b90,0x24b90,0x24b90};
    error=1;if(installed)return false;
    for(unsigned i=0;i<15;++i){error=10+i;if(table[i]!=host+rvas[i])return false;}
    HMODULE self;
    error=30;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&OnTick),&self))return false;
    DWORD previous=0;
    error=31;if(!InputClassifier::ProtectTarget(table,15*sizeof(void*),PAGE_READWRITE,&previous))return false;
    originalTick=reinterpret_cast<Tick>(table[10]);
    InterlockedExchangePointer(table+10,reinterpret_cast<void*>(&OnTick));
    InterlockedExchangePointer(table+2,reinterpret_cast<void*>(&Enabled));
    DWORD ignored=0;
    error=32;
    if(!InputClassifier::ProtectTarget(table,15*sizeof(void*),previous,&ignored)) {
        InterlockedExchangePointer(table+2,host+rvas[2]);
        InterlockedExchangePointer(table+10,host+rvas[10]);
        InputClassifier::ProtectTarget(table,15*sizeof(void*),previous,&ignored);
        originalTick=nullptr;return false;
    }
    installed=true;error=0;return true;
}
inline bool InstallForGame(const wchar_t* config,void (*log)(const char*,unsigned)) {
    // Leave the upstream feature off. Missing/unknown adapter => normal picker.
    wchar_t ini[MAX_PATH];wcscpy_s(ini,config);
    wchar_t* slash=wcsrchr(ini,L'\\');if(!slash)return false;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-ini),L"RSMods.ini");
    wchar_t forced[32],profile[256];
    GetPrivateProfileStringW(L"Toggle Switches",L"ForceProfileLoad",L"missing",forced,32,ini);
    GetPrivateProfileStringW(L"Toggle Switches",L"ProfileToLoad",L"missing",profile,256,ini);
    error=40;if(_wcsicmp(forced,L"off") || profile[0])return false;
    // Profile selection requires the exact host, independently of input options.
    error=41;if(!InputClassifier::PrepareHost())return false;
    BYTE* host=static_cast<BYTE*>(InputClassifier::rsmodsProtect)-0x9d4c0;
    gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));logger=log;
    return InstallAt(reinterpret_cast<void**>(host+0x19af6c),host);
}
}
