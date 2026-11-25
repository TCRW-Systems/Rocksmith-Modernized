#include <cstdio>
#include "title_retry.hpp"
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned movie[80]{},owner[32]{},loader[100]{},table[1]{},record[8]{},state[16]{},handles[1]{};
static unsigned sent=0,arg0=0,arg1=0,object=0;
static void __cdecl Received(unsigned m,unsigned first,unsigned second){++sent;object=m;arg0=first;arg1=second;}
static unsigned Address(void* p){return reinterpret_cast<unsigned>(p);}
static void Call(void* function) {
    __asm {push 022222222h
        push 011111111h
        lea eax,movie
        call function}
}
int main() {
    TitleRetry::movieVtable=0x12345678;movie[0]=0x12345678;movie[3]=Address(owner);
    owner[0x50/4]=0x42;owner[0x54/4]=0x24;
    movie[0x40/4]=0x11111111;movie[0x44/4]=0x22222222;
    const char* name="gfxassets/views/titlescreen.gfx";
    movie[0x8c/4]=reinterpret_cast<unsigned>(name);movie[0x9c/4]=reinterpret_cast<unsigned>(name+strlen(name));movie[0xa0/4]=movie[0x9c/4]+1;
    CHECK(TitleRetry::IsTitle(Address(movie)));CHECK(!TitleRetry::IsTitle(0));
    BYTE* code=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(code);
    // Original six-byte aligned-frame prologue followed by a receiver and RET 8.
    BYTE body[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0xff,0x75,0x0c,0xff,0x75,0x08,0x50,0xe8,0,0,0,0,0x83,0xc4,0x0c,0x8b,0xe5,0x5d,0xc2,0x08,0};
    int rel=static_cast<int>(reinterpret_cast<BYTE*>(&Received)-(code+18));memcpy(body+14,&rel,4);memcpy(code,body,sizeof(body));
    DWORD old;CHECK(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    Call(code);CHECK(sent==1 && object==Address(movie) && arg0==0x11111111 && arg1==0x22222222);
    CHECK(TitleRetry::Patch(TitleRetry::requestPatch,code,reinterpret_cast<void*>(&TitleRetry::RequestHook),TitleRetry::requestRelay));
    Call(code);CHECK(sent==2 && TitleRetry::pending.movie==Address(movie));
    CHECK(TitleRetry::pending.id0==arg0 && TitleRetry::pending.id1==arg1);
    loader[0x144/4]=Address(handles);loader[0x148/4]=Address(handles+1);loader[0x10/4]=Address(table);
    table[0]=Address(record);record[3]=Address(state);state[4]=1;state[6]=0x42;state[7]=0x24;
    TitleRetry::Poll(Address(loader));CHECK(sent==2); // No immediate duplicate.
    TitleRetry::pending.started=GetTickCount64()-2000;TitleRetry::pending.last=GetTickCount64()-1500;
    TitleRetry::frames=10;
    TitleRetry::Poll(Address(loader));CHECK(sent==3 && TitleRetry::pending.retries==1);
    TitleRetry::Poll(Address(loader));CHECK(sent==3);
    movie[0x58/4]=1;TitleRetry::Poll(Address(loader));CHECK(!TitleRetry::pending.movie && sent==3);
    movie[0x58/4]=0;Call(code);CHECK(sent==4);
    movie[0x40/4]=3;TitleRetry::Poll(Address(loader));CHECK(!TitleRetry::pending.movie && sent==4);
    CHECK(!TitleRetry::Eligible(2000,4,TitleRetry::Pending{}));
    CHECK(!TitleRetry::Eligible(31000,100,TitleRetry::Pending{}));
    CHECK(TitleRetry::Eligible(2000,10,TitleRetry::Pending{}));
    MEMORY_BASIC_INFORMATION info{};CHECK(VirtualQuery(code,&info,sizeof(info)));CHECK(info.Protect==PAGE_EXECUTE_READ);
    puts("PASS: title request hook ABI, title-only capture, retry delay/frame gates, native dispatch, completion/id cancellation and RX restoration");
    return 0;
}
