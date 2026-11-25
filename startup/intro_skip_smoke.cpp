#include <cstdio>
#include "intro_skip.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static float state=0;
static unsigned movie=0,name=0;
static __declspec(naked) void Getter() {
    __asm {mov dword ptr [movie],eax
        mov dword ptr [name],ecx
        fld dword ptr [state]
        ret}
}
static float Invoke(void* function) {
    float result;
    __asm {mov eax,012345678h
        mov ecx,087654321h
        call dword ptr [function]
        fstp dword ptr [result]}
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
    for(int i=-1;i<=5;++i) {
        state=static_cast<float>(i);float actual=Invoke(call);
        CHECK(actual==((i>=1 && i<=3)?4.0f:state));
        CHECK(movie==0x12345678 && name==0x87654321);
    }
    state=1.5f;CHECK(Invoke(call)==state); // Unexpected values do not force completion.
    CHECK(IntroSkip::completions==3);
    MEMORY_BASIC_INFORMATION mem{};CHECK(VirtualQuery(call,&mem,sizeof(mem)));CHECK(mem.Protect==PAGE_EXECUTE_READ);
    CHECK(!IntroSkip::InstallAt(site,reinterpret_cast<void*>(&Getter)));
    puts("PASS: intro-only CALL redirection, EAX/ECX inputs, x87 return, unready/unknown fallback, completion mapping, target/duplicate guards and protection restoration");
    return 0;
}
