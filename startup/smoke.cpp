#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <initializer_list>
#include <cstring>
static BOOL CALLBACK AnyA(const DIDEVICEINSTANCEA*,void* n) {++*static_cast<int*>(n);return DIENUM_CONTINUE;}
static BOOL CALLBACK AnyW(const DIDEVICEINSTANCEW*,void* n) {++*static_cast<int*>(n);return DIENUM_CONTINUE;}
static BOOL CALLBACK CountA(const DIDEVICEINSTANCEA* instance,void* n) {if(GET_DIDEVICE_TYPE(instance->dwDevType)!=DI8DEVTYPE_MOUSE && GET_DIDEVICE_TYPE(instance->dwDevType)!=DI8DEVTYPE_KEYBOARD) return DIENUM_STOP; ++*static_cast<int*>(n);return DIENUM_CONTINUE;}
static BOOL CALLBACK CountW(const DIDEVICEINSTANCEW* instance,void* n) {if(GET_DIDEVICE_TYPE(instance->dwDevType)!=DI8DEVTYPE_MOUSE && GET_DIDEVICE_TYPE(instance->dwDevType)!=DI8DEVTYPE_KEYBOARD) return DIENUM_STOP; ++*static_cast<int*>(n);return DIENUM_CONTINUE;}
#define CHECK(x) do {if(!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(int argc,char** argv) {
    bool passthrough=argc==2 && strcmp(argv[1],"--passthrough")==0;
    bool early=argc==2 && strcmp(argv[1],"--early-qos")==0;
    wchar_t config[MAX_PATH];CHECK(GetModuleFileNameW(nullptr,config,MAX_PATH));
    wchar_t* slash=wcsrchr(config,L'\\');CHECK(slash);wcscpy_s(slash+1,MAX_PATH-(slash+1-config),L"RSStartup.ini");
    HANDLE ini=CreateFileW(config,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(ini!=INVALID_HANDLE_VALUE);
    struct Cleanup {const wchar_t* path;~Cleanup(){DeleteFileW(path);}} cleanup{config};
    const char* settings=early?"[Startup]\r\nStartupHighQoS=1\r\n":passthrough?"[Startup]\r\nSkipGamepadEnumeration=0\r\nAutoLoadLastProfile=1\r\n":"[Startup]\r\nSkipGamepadEnumeration=1\r\n";
    DWORD written=0;CHECK(WriteFile(ini,settings,static_cast<DWORD>(strlen(settings)),&written,nullptr));CloseHandle(ini);
    struct Power {ULONG version,control,state;};Power before{1,0,0},after{1,0,0};
    using ReadInfo=BOOL(WINAPI*)(HANDLE,int,void*,DWORD);
    auto readInfo=reinterpret_cast<ReadInfo>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcessInformation"));
    if(early){CHECK(readInfo);CHECK(readInfo(GetCurrentProcess(),4,&before,sizeof(before)));}
    HMODULE dll=LoadLibraryW(L".\\dinput8.dll"); CHECK(dll);
    if(early) {
        CHECK(readInfo(GetCurrentProcess(),4,&after,sizeof(after)));CHECK((after.control&1) && !(after.state&1));
        CHECK(FreeLibrary(dll));CHECK(readInfo(GetCurrentProcess(),4,&after,sizeof(after)));
        CHECK(before.control==after.control && before.state==after.state);
        puts("PASS: actual DLL-attach opt-in read, native HighQoS readback and explicit-unload restoration without calling an export");return 0;
    }
    using Fn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
    auto create=(Fn)GetProcAddress(dll,"DirectInput8Create"); CHECK(create);
    wchar_t system[MAX_PATH];CHECK(GetSystemDirectoryW(system,MAX_PATH));wcscat_s(system,L"\\dinput8.dll");
    HMODULE original=LoadLibraryW(system);CHECK(original);
    auto realCreate=(Fn)GetProcAddress(original,"DirectInput8Create");CHECK(realCreate);
    for(bool wide:{false,true}) {
        void* p=nullptr;
        CHECK(SUCCEEDED(create(GetModuleHandle(nullptr),0x0800,wide?IID_IDirectInput8W:IID_IDirectInput8A,&p,nullptr)));
        auto u=static_cast<IUnknown*>(p);
        IUnknown* identity=nullptr;
        CHECK(SUCCEEDED(u->QueryInterface(IID_IUnknown,(void**)&identity)));
        CHECK(identity==u); identity->Release();
        int count=0;
        if(passthrough) {
            void* baseline=nullptr;
            CHECK(SUCCEEDED(realCreate(GetModuleHandle(nullptr),0x0800,wide?IID_IDirectInput8W:IID_IDirectInput8A,&baseline,nullptr)));
            for(DWORD kind:{DI8DEVCLASS_GAMECTRL,DI8DEVCLASS_ALL}) {
                int realCount=0,proxyCount=0;
                if(wide) {
                    CHECK(SUCCEEDED(static_cast<IDirectInput8W*>(baseline)->EnumDevices(kind,AnyW,&realCount,DIEDFL_ATTACHEDONLY)));
                    CHECK(SUCCEEDED(static_cast<IDirectInput8W*>(p)->EnumDevices(kind,AnyW,&proxyCount,DIEDFL_ATTACHEDONLY)));
                } else {
                    CHECK(SUCCEEDED(static_cast<IDirectInput8A*>(baseline)->EnumDevices(kind,AnyA,&realCount,DIEDFL_ATTACHEDONLY)));
                    CHECK(SUCCEEDED(static_cast<IDirectInput8A*>(p)->EnumDevices(kind,AnyA,&proxyCount,DIEDFL_ATTACHEDONLY)));
                }
                CHECK(realCount==proxyCount);
            }
            static_cast<IUnknown*>(baseline)->Release();CHECK(u->Release()==0);continue;
        }
        if(wide) {
            auto i=static_cast<IDirectInput8W*>(p);
            CHECK(SUCCEEDED(i->EnumDevices(DI8DEVCLASS_GAMECTRL,CountW,&count,DIEDFL_ATTACHEDONLY))); CHECK(count==0);
            CHECK(SUCCEEDED(i->EnumDevices(DI8DEVCLASS_KEYBOARD,CountW,&count,DIEDFL_ATTACHEDONLY))); CHECK(count>0);
            count=0; CHECK(SUCCEEDED(i->EnumDevices(DI8DEVCLASS_ALL,CountW,&count,DIEDFL_ATTACHEDONLY))); CHECK(count==2);
            IDirectInputDevice8W* device=nullptr;
            CHECK(SUCCEEDED(i->CreateDevice(GUID_SysKeyboard,&device,nullptr))); device->Release();
            CHECK(SUCCEEDED(i->CreateDevice(GUID_SysMouse,&device,nullptr))); device->Release();
        } else {
            auto i=static_cast<IDirectInput8A*>(p);
            CHECK(SUCCEEDED(i->EnumDevices(DI8DEVCLASS_GAMECTRL,CountA,&count,DIEDFL_ATTACHEDONLY))); CHECK(count==0);
            CHECK(SUCCEEDED(i->EnumDevices(DI8DEVCLASS_KEYBOARD,CountA,&count,DIEDFL_ATTACHEDONLY))); CHECK(count>0);
            count=0; CHECK(SUCCEEDED(i->EnumDevices(DI8DEVCLASS_ALL,CountA,&count,DIEDFL_ATTACHEDONLY))); CHECK(count==2);
            IDirectInputDevice8A* device=nullptr;
            CHECK(SUCCEEDED(i->CreateDevice(GUID_SysKeyboard,&device,nullptr))); device->Release();
            CHECK(SUCCEEDED(i->CreateDevice(GUID_SysMouse,&device,nullptr))); device->Release();
        }
        CHECK(u->Release()==0);
    }
    puts(passthrough?"PASS: profile-only configuration preserves ANSI/Unicode gamepad and all-device enumeration":"PASS: ANSI/Unicode interfaces, COM identity/refcounts, gamepad filter, keyboard enumeration, keyboard/mouse creation");
    return 0;
}
