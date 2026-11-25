#include <cstdio>
#include "intro_skip.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static float state=0;
static unsigned movie=0,name=0;
static unsigned controller[32]={};
static float rate=0.85f;
static unsigned writes=0;
static void* expectedMovie=reinterpret_cast<void*>(0x12345678);
static unsigned __cdecl MockRead(void* object,IntroSkip::PropertyName* property) {
    if(object!=expectedMovie || strcmp(property->data,"mScene.introTimeline.timeScale") ||
       property->end!=property->data+strlen(property->data))return 0;
    unsigned bits;memcpy(&bits,&rate,4);return bits;
}
static __declspec(naked) void PropertyGetter() {
    __asm {push ecx
        push eax
        call MockRead
        add esp,8
        push eax
        fld dword ptr [esp]
        add esp,4
        ret}
}
static unsigned __cdecl MockWrite(void* object,IntroSkip::PropertyName* property,float* value) {
    if(object!=expectedMovie || strcmp(property->data,"mScene.introTimeline.timeScale") || *value!=100.0f)return 0;
    rate=*value;++writes;return 1;
}
static __declspec(naked) void Setter() {
    __asm {push dword ptr [esp+4]
        push ecx
        push eax
        call MockWrite
        add esp,12
        ret 4}
}
static __declspec(naked) void Getter() {
    __asm {cmp ecx,087654321h
        jne property
        mov dword ptr [movie],eax
        mov dword ptr [name],ecx
        fld dword ptr [state]
        ret
    property:
        jmp PropertyGetter}
}
static float Invoke(void* function) {
    float result;
    __asm {push ebx
        lea ebx,controller
        mov eax,012345678h
        mov ecx,087654321h
        call dword ptr [function]
        fstp dword ptr [result]
        pop ebx}
    return result;
}
int main() {
    BYTE* call=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(call);
    // A frame with live locals catches an accidental tail JMP in place of CALL.
    const BYTE body[]={0x55,0x8b,0xec,0x83,0xec,0x10,0xe8,0,0,0,0,0x83,0xc4,0x10,0x5d,0xc3};
    memcpy(call,body,sizeof(body));BYTE* site=call+6;
    int32_t relative=static_cast<int32_t>(reinterpret_cast<BYTE*>(&Getter)-(site+5));memcpy(site+1,&relative,4);
    DWORD old;CHECK(VirtualProtect(call,4096,PAGE_EXECUTE_READ,&old));
    state=2;CHECK(Invoke(call)==2);
    CHECK(!IntroSkip::InstallAt(site,reinterpret_cast<void*>(&Invoke)));
    CHECK(IntroSkip::InstallAt(site,reinterpret_cast<void*>(&Getter)));
    CHECK(site[0]==0xe8);
    IntroSkip::setter=reinterpret_cast<void*>(&Setter);
    controller[0x34/4]=0xbf800000u; // Asset callbacks have not selected a screen.
    for(int i=-1;i<=5;++i) {state=static_cast<float>(i);CHECK(Invoke(call)==state);}
    CHECK(writes==0 && IntroSkip::attempts==0);
    controller[0x34/4]=0;state=0;CHECK(Invoke(call)==0);CHECK(writes==0);
    state=1.5f;CHECK(Invoke(call)==state);CHECK(writes==0);
    state=1;CHECK(Invoke(call)==1);CHECK(writes==1 && rate==100.0f);
    for(int i=-1;i<=5;++i) {
        state=static_cast<float>(i);CHECK(Invoke(call)==state);
        CHECK(movie==0x12345678 && name==0x87654321);
    }
    CHECK(writes==1 && IntroSkip::completions==1);
    MEMORY_BASIC_INFORMATION mem{};CHECK(VirtualQuery(call,&mem,sizeof(mem)));CHECK(mem.Protect==PAGE_EXECUTE_READ);
    CHECK(!IntroSkip::InstallAt(site,reinterpret_cast<void*>(&Getter)));
    puts("PASS: intro CALL ABI, original playback preservation, asset gate, timeline setter ABI and single acceleration, target guards and RX restoration");
    return 0;
}
