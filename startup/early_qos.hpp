#pragma once
#include <windows.h>

// Only bounded file reads and Kernel32 APIs run at process attach. No CRT,
// module loading, threads, timers, hashing, COM or waits under the loader lock.
// The restoration timer is created on the first exported call, outside it.
namespace EarlyQos {
struct PowerState {ULONG Version,ControlMask,StateMask;};
using GetInfo=BOOL(WINAPI*)(HANDLE,int,void*,DWORD);
using SetInfo=BOOL(WINAPI*)(HANDLE,int,void*,DWORD);
inline GetInfo getInfo=nullptr;
inline SetInfo setInfo=nullptr;
inline PowerState original{};
inline ULONGLONG attachedTick=0;
inline DWORD attachError=0;
inline volatile LONG active=0,busy=0;
inline unsigned retries=0;
inline PTP_TIMER timer=nullptr;
inline void (*logger)(const char*,unsigned)=nullptr;
inline constexpr DWORD durationMs=50000;

inline char Lower(char c){return c>='A' && c<='Z'?static_cast<char>(c+32):c;}
inline bool Equal(const char* a,const char* b){while(*a && Lower(*a)==Lower(*b)){++a;++b;}return *a==*b;}
inline char* Trim(char* s){while(*s==' '||*s=='\t')++s;char* end=s;while(*end)++end;while(end>s && (end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'))--end;*end=0;return s;}
inline bool EnabledText(char* text) {
    bool section=false,seen=false,enabled=false;
    char* line=text;
    while(*line) {
        char* next=line;while(*next && *next!='\n')++next;
        if(*next)*next++=0;
        char* s=Trim(line);
        if(*s=='[') {
            char* end=s;while(*end && *end!=']')++end;
            if(*end==']'){*end=0;section=Equal(Trim(s+1),"Startup");}else section=false;
        } else if(section && *s && *s!=';' && *s!='#') {
            char* equals=s;while(*equals && *equals!='=')++equals;
            if(*equals=='=') {
                *equals=0;
                if(Equal(Trim(s),"StartupHighQoS")) {
                    if(seen)return false; // Ambiguous settings leave the feature off.
                    seen=true;char* value=Trim(equals+1);char* comment=value;
                    while(*comment && *comment!=';' && *comment!='#')++comment;*comment=0;
                    value=Trim(value);if(value[0]=='1' && !value[1])enabled=true;
                    else if(value[0]!='0' || value[1])return false;
                }
            }
        }
        line=next;
    }
    return seen && enabled;
}
inline bool ReadEnabled() {
    wchar_t path[MAX_PATH];DWORD count=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!count || count>=MAX_PATH)return false;
    DWORD slash=count;while(slash && path[slash-1]!=L'\\')--slash;
    const wchar_t name[]=L"Rocksmith2014.exe";
    unsigned i=0;while(name[i] && slash+i<count) {
        wchar_t c=path[slash+i],n=name[i];if(c>=L'A' && c<=L'Z')c+=32;if(n>=L'A' && n<=L'Z')n+=32;
        if(c!=n)return false;++i;
    }
    if(name[i] || slash+i!=count || slash+14>=MAX_PATH)return false;
    const wchar_t ini[]=L"RSStartup.ini";i=0;do{path[slash+i]=ini[i];}while(ini[i++]);
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};char raw[8192]={};DWORD got=0;
    bool okay=GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<sizeof(raw) &&
        ReadFile(file,raw,static_cast<DWORD>(size.QuadPart),&got,nullptr) && got==size.QuadPart;
    CloseHandle(file);if(!okay)return false;
    if(got>=2 && static_cast<unsigned char>(raw[0])==0xff && static_cast<unsigned char>(raw[1])==0xfe) {
        if(got%2)return false;DWORD out=0;
        for(DWORD at=2;at<got;at+=2)raw[out++]=raw[at+1]?'?':raw[at];raw[out]=0;
    } else if(got>=3 && static_cast<unsigned char>(raw[0])==0xef && static_cast<unsigned char>(raw[1])==0xbb && static_cast<unsigned char>(raw[2])==0xbf) {
        for(DWORD at=0;at+3<=got;++at)raw[at]=raw[at+3];
    }
    return EnabledText(raw);
}
inline bool Apply() {
    if(!getInfo || !setInfo)return false;
    original={1,0,0};attachError=1;
    if(!getInfo(GetCurrentProcess(),4,&original,sizeof(original))){attachError=GetLastError();return false;}
    // The external helper (or another owner) may already hold this request.
    if((original.ControlMask&1) && !(original.StateMask&1)){attachError=0;return false;}
    PowerState requested={1,original.ControlMask|1,original.StateMask&~1ul};
    if(!setInfo(GetCurrentProcess(),4,&requested,sizeof(requested))){attachError=GetLastError();return false;}
    active=1;PowerState actual{1,0,0};
    if(!getInfo(GetCurrentProcess(),4,&actual,sizeof(actual)) || !(actual.ControlMask&1) || (actual.StateMask&1)) {attachError=2;return false;}
    attachError=0;return true;
}
inline void Attach() {
    attachedTick=GetTickCount64();
    wchar_t helper[4]={};
    if(GetEnvironmentVariableW(L"RSM_STARTUP_HELPER_ACTIVE",helper,4)==1 && helper[0]==L'1')return;
    if(!ReadEnabled())return;
    HMODULE kernel=GetModuleHandleW(L"kernel32.dll");if(!kernel)return;
    getInfo=reinterpret_cast<GetInfo>(GetProcAddress(kernel,"GetProcessInformation"));
    setInfo=reinterpret_cast<SetInfo>(GetProcAddress(kernel,"SetProcessInformation"));
    Apply();
}
inline bool Restore() {
    if(!active)return true;
    if(InterlockedCompareExchange(&busy,1,0))return false;
    PowerState current{1,0,0};bool okay=getInfo && setInfo && getInfo(GetCurrentProcess(),4,&current,sizeof(current));
    if(okay) {
        // Restore our execution-speed bit, preserving unrelated concurrent hints.
        PowerState restore={1,(current.ControlMask&~1ul)|(original.ControlMask&1),(current.StateMask&~1ul)|(original.StateMask&1)};
        okay=setInfo(GetCurrentProcess(),4,&restore,sizeof(restore)) && getInfo(GetCurrentProcess(),4,&current,sizeof(current)) &&
            (current.ControlMask&1)==(original.ControlMask&1) && (current.StateMask&1)==(original.StateMask&1);
    }
    if(okay)InterlockedExchange(&active,0);
    InterlockedExchange(&busy,0);return okay;
}
inline void Schedule(DWORD milliseconds) {
    LARGE_INTEGER due;due.QuadPart=-static_cast<LONGLONG>(milliseconds?milliseconds:1)*10000;
    SetThreadpoolTimer(timer,reinterpret_cast<FILETIME*>(&due),0,0);
}
inline void CALLBACK Timer(PTP_CALLBACK_INSTANCE,void*,PTP_TIMER) {
    bool okay=Restore();if(logger)logger(okay?"dll_startup_qos_restored_verified":"dll_startup_qos_restore_failed",0);
    if(!okay && ++retries<3)Schedule(1000);
}
inline void Initialize(void (*log)(const char*,unsigned)) {
    logger=log;if(logger){logger("dll_startup_qos_active",active);logger("dll_startup_qos_attach_error",attachError);}
    if(!active)return;
    HMODULE module;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Timer),&module)) {Restore();return;}
    ULONGLONG elapsed=GetTickCount64()-attachedTick;
    if(attachError || elapsed>=durationMs) {bool okay=Restore();if(logger)logger(okay?"dll_startup_qos_restored_verified":"dll_startup_qos_restore_failed",0);return;}
    timer=CreateThreadpoolTimer(Timer,nullptr,nullptr);
    if(!timer){Restore();if(logger)logger("dll_startup_qos_timer_unavailable",GetLastError());return;}
    Schedule(static_cast<DWORD>(durationMs-elapsed));
}
}
