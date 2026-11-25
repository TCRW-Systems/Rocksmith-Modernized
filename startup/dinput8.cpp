#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <new>
#include <cstdio>
#include <cwchar>
#include "input_classifier.hpp"
#include "profile_boot.hpp"
#include "early_qos.hpp"
#include "intro_skip.hpp"

// Process-local input/profile/intro adapters. Hooks initialize at normal export
// calls. Early QoS uses bounded Kernel32-only attach work; its restoration timer
// is armed outside DllMain. No executable or asset files are patched on disk.
static HMODULE systemDll;
static INIT_ONCE initOnce = INIT_ONCE_STATIC_INIT;
static bool skipGamepads;
static bool skipKnownDeviceQueries;
static bool autoLastProfile;
static bool skipIntro;
static wchar_t startupConfig[MAX_PATH];
static wchar_t logPath[MAX_PATH];
static FILETIME attachedAt;

static void Log(const char* action, unsigned value=0) {
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a{}, b{};
    a.LowPart=created.dwLowDateTime; a.HighPart=created.dwHighDateTime;
    b.LowPart=now.dwLowDateTime; b.HighPart=now.dwHighDateTime;
    char line[256];
    int length=snprintf(line,sizeof(line),"%.3f %s %u\r\n",(b.QuadPart-a.QuadPart)/1e7,action,value);
    if(length<=0 || length>=sizeof(line)) return;
    HANDLE f=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE) {DWORD written; WriteFile(f,line,length,&written,nullptr); CloseHandle(f);}
}

static BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n || n>MAX_PATH-13) return FALSE;
    wcscat_s(path,L"\\dinput8.dll");
    systemDll=LoadLibraryW(path); // Absolute system path; cannot recurse into us.
    if(!systemDll) return FALSE;
    wchar_t config[MAX_PATH];
    DWORD size=GetModuleFileNameW(nullptr,config,MAX_PATH);
    if(!size || size>=MAX_PATH) return TRUE; // Forward only on path failure.
    wchar_t* slash=wcsrchr(config,L'\\');
    if(!slash) return TRUE;
    bool target=_wcsicmp(slash+1,L"Rocksmith2014.exe")==0;
    slash[1]=0;
    wcscpy_s(logPath,config);
    wcscat_s(logPath,L"RSStartup.log");
    wcscat_s(config,L"RSStartup.ini");
    wcscpy_s(startupConfig,config);
    skipGamepads=target && GetPrivateProfileIntW(L"Startup",L"SkipGamepadEnumeration",0,config)!=0;
    skipKnownDeviceQueries=skipGamepads && GetPrivateProfileIntW(L"Startup",L"SkipKnownKeyboardMouseXInputChecks",0,config)!=0;
    autoLastProfile=target && GetPrivateProfileIntW(L"Startup",L"AutoLoadLastProfile",0,config)!=0;
    skipIntro=target && GetPrivateProfileIntW(L"Startup",L"SkipIntro",0,config)!=0;
    if(target) {
        FILETIME created, exited, kernel, user;
        if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) {
            ULARGE_INTEGER a{},b{};a.LowPart=created.dwLowDateTime;a.HighPart=created.dwHighDateTime;
            b.LowPart=attachedAt.dwLowDateTime;b.HighPart=attachedAt.dwHighDateTime;
            if(b.QuadPart>=a.QuadPart)Log("proxy_process_attach_age_ms",static_cast<unsigned>((b.QuadPart-a.QuadPart)/10000));
        }
        Log("proxy_initialized_skip_gamepads",skipGamepads);
        EarlyQos::Initialize(Log);
    }
    return TRUE;
}

static FARPROC Resolve(const char* name) {
    if(!InitOnceExecuteOnce(&initOnce,Initialize,nullptr,nullptr)) return nullptr;
    return GetProcAddress(systemDll,name);
}

static void TryFeatures() {
    static unsigned attempts=0;
    if((!skipKnownDeviceQueries || InputClassifier::original) && (!autoLastProfile || ProfileBoot::installed) && (!skipIntro || IntroSkip::installed))return;
    if(attempts>=4)return;
    // Retry only a host-not-loaded rejection, before a game callback can use it.
    if(attempts && !(InputClassifier::lastGuardError==204 && InputClassifier::lastRsmodsGuard==2))return;
    ++attempts;
    ULONGLONG before=GetTickCount64();
    bool ready=InputClassifier::PrepareHost();
    if(!ready) {Log("startup_host_guard_rejected",InputClassifier::lastGuardError);return;}
    if(skipKnownDeviceQueries && !InputClassifier::original) {
    bool installed=InputClassifier::InstallForGame();
    Log(installed?"known_keyboard_mouse_classifier_installed":"known_keyboard_mouse_classifier_guard_rejected",InputClassifier::lastGuardError);
    Log("classifier_guard_duration_ms",static_cast<unsigned>(GetTickCount64()-before));
    Log("classifier_rsmods_guard_stage",InputClassifier::lastRsmodsGuard);
    Log("classifier_hash_error",InputClassifier::lastHashError);
    Log("classifier_hook_failure_stage",InputClassifier::lastHookError);
    Log("classifier_hook_windows_error",InputClassifier::lastHookWindowsError);
    Log("classifier_protection_status",static_cast<unsigned>(InputClassifier::lastProtectionStatus));
    Log("classifier_protection_restore_error",InputClassifier::lastRestoreError);
    }
    if(autoLastProfile && !ProfileBoot::installed) {
        bool profileInstalled=ProfileBoot::InstallForGame(startupConfig,Log);
        Log(profileInstalled?"profile_boot_adapter_installed":"profile_boot_adapter_guard_rejected",ProfileBoot::error);
    }
    if(skipIntro && !IntroSkip::installed) {
        bool introInstalled=IntroSkip::InstallForGame(Log);
        Log(introInstalled?"native_intro_adapter_installed":"native_intro_adapter_guard_rejected",IntroSkip::error);
    }
}

template<class Interface, class Device, class Instance, class Callback, class Char,
         class ActionFormat, class SemanticsCallback, class ConfigureParams>
class Input final: public Interface {
    Interface* real;
    IID ownIid;
    volatile LONG refs=1;
    struct EnumContext {Callback callback; void* context;};
    struct SemanticsContext {SemanticsCallback callback; void* context;};
    static bool KeyboardOrMouse(DWORD type) {
        DWORD base=GET_DIDEVICE_TYPE(type);
        return base==DI8DEVTYPE_MOUSE || base==DI8DEVTYPE_KEYBOARD;
    }
    static BOOL CALLBACK Filter(const Instance* instance,void* data) {
        auto c=static_cast<EnumContext*>(data);
        if(skipGamepads && !KeyboardOrMouse(instance->dwDevType)) {Log("filtered_game_device",instance->dwDevType);return DIENUM_CONTINUE;}
        TryFeatures();
        if(!skipKnownDeviceQueries || !KeyboardOrMouse(instance->dwDevType))return c->callback(instance,c->context);
        InputClassifier::KnownDeviceScope scope(instance->guidProduct);
        return c->callback(instance,c->context);
    }
    static BOOL CALLBACK FilterSemantics(const Instance* instance,Device* device,DWORD flags,DWORD remaining,void* data) {
        auto c=static_cast<SemanticsContext*>(data);
        if(skipGamepads && !KeyboardOrMouse(instance->dwDevType)) {Log("filtered_semantics_game_device",instance->dwDevType);return DIENUM_CONTINUE;}
        TryFeatures();
        if(!skipKnownDeviceQueries || !KeyboardOrMouse(instance->dwDevType))return c->callback(instance,device,flags,remaining,c->context);
        InputClassifier::KnownDeviceScope scope(instance->guidProduct);
        return c->callback(instance,device,flags,remaining,c->context);
    }
public:
    Input(Interface* p,REFIID iid):real(p),ownIid(iid) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out) return E_POINTER;
        if(iid==IID_IUnknown || iid==ownIid) { *out=static_cast<Interface*>(this); AddRef(); return S_OK; }
        return real->QueryInterface(iid,out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count=InterlockedDecrement(&refs);
        if(!count) {real->Release(); delete this;} return count;
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID id,Device** out,IUnknown* outer) override {
        HRESULT hr=real->CreateDevice(id,out,outer);
        Log(id==GUID_SysKeyboard?"create_keyboard":id==GUID_SysMouse?"create_mouse":"create_other_device",hr);
        if(InputClassifier::original)Log("known_device_queries_bypassed",InputClassifier::bypassCount);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type,Callback callback,void* context,DWORD flags) override {
        TryFeatures();
        if(skipGamepads && type==DI8DEVCLASS_GAMECTRL) {Log("skipped_gamepad_enumeration",type); return DI_OK;}
        if(callback) {
            EnumContext c{callback,context};
            Log("filtered_all_enumeration",type);
            return real->EnumDevices(type,Filter,&c,flags);
        }
        Log("forward_enumeration",type);
        return real->EnumDevices(type,callback,context,flags);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID id) override {return real->GetDeviceStatus(id);}
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwnd,DWORD flags) override {return real->RunControlPanel(hwnd,flags);}
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance,DWORD version) override {return real->Initialize(instance,version);}
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID id,const Char* name,GUID* out) override {return real->FindDevice(id,name,out);}
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(const Char* user,ActionFormat* format,SemanticsCallback callback,void* context,DWORD flags) override {
        TryFeatures();
        Log("filtered_semantics_enumeration",flags);
        if(!callback) return real->EnumDevicesBySemantics(user,format,callback,context,flags);
        SemanticsContext c{callback,context};
        return real->EnumDevicesBySemantics(user,format,FilterSemantics,&c,flags);
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK callback,ConfigureParams* params,DWORD flags,void* context) override {
        return real->ConfigureDevices(callback,params,flags,context);
    }
};
using InputA=Input<IDirectInput8A,IDirectInputDevice8A,DIDEVICEINSTANCEA,LPDIENUMDEVICESCALLBACKA,char,DIACTIONFORMATA,LPDIENUMDEVICESBYSEMANTICSCBA,DICONFIGUREDEVICESPARAMSA>;
using InputW=Input<IDirectInput8W,IDirectInputDevice8W,DIDEVICEINSTANCEW,LPDIENUMDEVICESCALLBACKW,wchar_t,DIACTIONFORMATW,LPDIENUMDEVICESBYSEMANTICSCBW,DICONFIGUREDEVICESPARAMSW>;

extern "C" HRESULT WINAPI ProxyCreate(HINSTANCE instance,DWORD version,REFIID iid,void** out,IUnknown* outer) {
    using Fn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
    auto fn=reinterpret_cast<Fn>(Resolve("DirectInput8Create"));
    if(!fn) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    HRESULT hr=fn(instance,version,iid,out,outer);
    if(skipKnownDeviceQueries)Log("directinput_create_returned",hr);
    if(FAILED(hr) || !out || !*out || (!skipGamepads && !autoLastProfile && !skipIntro) || outer) return hr;
    TryFeatures();
    void* wrapper=nullptr;
    if(iid==IID_IDirectInput8A) wrapper=new(std::nothrow) InputA(static_cast<IDirectInput8A*>(*out),iid);
    else if(iid==IID_IDirectInput8W) wrapper=new(std::nothrow) InputW(static_cast<IDirectInput8W*>(*out),iid);
    else return hr;
    if(!wrapper) {static_cast<IUnknown*>(*out)->Release(); *out=nullptr; return E_OUTOFMEMORY;}
    *out=wrapper;
    Log("wrapped_directinput8");
    return hr;
}
extern "C" HRESULT WINAPI ProxyCanUnload() {using Fn=HRESULT(WINAPI*)();auto f=(Fn)Resolve("DllCanUnloadNow");return f?f():S_FALSE;}
extern "C" HRESULT WINAPI ProxyGetClass(REFCLSID cls,REFIID iid,void** out) {using Fn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto f=(Fn)Resolve("DllGetClassObject");return f?f(cls,iid,out):CLASS_E_CLASSNOTAVAILABLE;}
extern "C" HRESULT WINAPI ProxyRegister() {using Fn=HRESULT(WINAPI*)();auto f=(Fn)Resolve("DllRegisterServer");return f?f():E_NOTIMPL;}
extern "C" HRESULT WINAPI ProxyUnregister() {using Fn=HRESULT(WINAPI*)();auto f=(Fn)Resolve("DllUnregisterServer");return f?f():E_NOTIMPL;}
extern "C" const DIDATAFORMAT* WINAPI ProxyJoystick() {using Fn=const DIDATAFORMAT*(WINAPI*)();auto f=(Fn)Resolve("GetdfDIJoystick");return f?f():nullptr;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void* reserved) {
    if(reason==DLL_PROCESS_ATTACH) {GetSystemTimeAsFileTime(&attachedAt);EarlyQos::Attach();DisableThreadLibraryCalls(instance);}
    else if(reason==DLL_PROCESS_DETACH && !reserved && !EarlyQos::timer)EarlyQos::Restore();
    return TRUE;
}
