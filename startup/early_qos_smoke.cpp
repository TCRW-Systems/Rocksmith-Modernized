#include <cstdio>
#include <cstring>
#include "early_qos.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static EarlyQos::PowerState current{1,0,0};
static bool failSet=false;
static BOOL WINAPI Read(HANDLE,int,void* data,DWORD){*static_cast<EarlyQos::PowerState*>(data)=current;return TRUE;}
static BOOL WINAPI Write(HANDLE,int,void* data,DWORD){if(failSet)return FALSE;current=*static_cast<EarlyQos::PowerState*>(data);return TRUE;}
static bool Enabled(const char* input) {char text[2048];strcpy_s(text,input);return EarlyQos::EnabledText(text);}
int main() {
    CHECK(Enabled("[Startup]\nStartupHighQoS=1\n"));
    CHECK(Enabled(" [sTaRtUp] \r\n StartupHighQoS = 1 ; enabled\r\n"));
    CHECK(!Enabled("[Other]\nStartupHighQoS=1\n"));
    CHECK(!Enabled("[Startup]\nStartupHighQoS=0\n"));
    CHECK(!Enabled("[Startup]\nStartupHighQoS=1\nStartupHighQoS=0\n"));
    CHECK(!Enabled("[Startup]\nStartupHighQoS=10\n"));
    EarlyQos::getInfo=Read;EarlyQos::setInfo=Write;
    CHECK(EarlyQos::Apply() && current.ControlMask==1 && current.StateMask==0);
    current.ControlMask|=2;current.StateMask|=2; // Unrelated later hint survives restoration.
    CHECK(EarlyQos::Restore() && current.ControlMask==2 && current.StateMask==2);
    CHECK(EarlyQos::Restore());
    current={1,1,0};CHECK(!EarlyQos::Apply() && !EarlyQos::active);
    current={1,1,1};CHECK(EarlyQos::Apply());CHECK(EarlyQos::Restore() && current.ControlMask==1 && current.StateMask==1);
    current={1,0,0};CHECK(EarlyQos::Apply());failSet=true;CHECK(!EarlyQos::Restore() && EarlyQos::active);failSet=false;CHECK(EarlyQos::Restore());
    current={1,0,0};CHECK(EarlyQos::Apply());EarlyQos::attachedTick=GetTickCount64()-EarlyQos::durationMs;EarlyQos::Initialize(nullptr);CHECK(!EarlyQos::active && current.ControlMask==0);
    current={1,0,0};CHECK(EarlyQos::Apply());EarlyQos::attachedTick=GetTickCount64()-EarlyQos::durationMs+100;EarlyQos::Initialize(nullptr);CHECK(EarlyQos::timer);
    for(unsigned i=0;i<100 && EarlyQos::active;++i)Sleep(10);
    CHECK(!EarlyQos::active && current.ControlMask==0);
    WaitForThreadpoolTimerCallbacks(EarlyQos::timer,FALSE);CloseThreadpoolTimer(EarlyQos::timer);
    puts("PASS: opt-in parsing, early hint ownership, unrelated-bit preservation, restoration failure/retry, late initialization and native timer restoration");
    return 0;
}
