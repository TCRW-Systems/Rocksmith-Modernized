#include <cstdio>
#include <cstring>
#include "memory_protection.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static BOOL WINAPI Denied(LPVOID,SIZE_T,DWORD,PDWORD) {SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static LONG NTAPI NativeDenied(HANDLE,PVOID*,SIZE_T*,ULONG,PULONG) {return static_cast<LONG>(0xc0000022);}
int main() {
    BYTE* page=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(page);
    DWORD old=0;
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_EXECUTE_READ,&old));CHECK(old==PAGE_READWRITE);
    MEMORY_BASIC_INFORMATION memory{};CHECK(VirtualQuery(page,&memory,sizeof(memory)));CHECK(memory.Protect==PAGE_EXECUTE_READ);
    CHECK(!MemoryProtection::usedNative);
    // Exercise the real Windows native fallback with an unaligned address/size.
    MemoryProtection::winProtect=Denied;
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_READWRITE,&old));CHECK(old==PAGE_EXECUTE_READ);
    CHECK(MemoryProtection::usedNative && MemoryProtection::nativeStatus>=0 && MemoryProtection::windowsError==ERROR_ACCESS_DENIED);
    page[19]=42;
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_EXECUTE_READ,&old));CHECK(old==PAGE_READWRITE);
    CHECK(VirtualQuery(page,&memory,sizeof(memory)));CHECK(memory.Protect==PAGE_EXECUTE_READ);
    auto native=MemoryProtection::nativeProtect;
    MemoryProtection::nativeProtect=NativeDenied;
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_READWRITE,&old));
    CHECK(MemoryProtection::directCalls==1 && NativeProtection::error==0);
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_EXECUTE_READ,&old));
    CHECK(old==PAGE_READWRITE && MemoryProtection::directCalls==2);
    CHECK(MemoryProtection::PrepareForGame());
    unsigned exportedCalls=MemoryProtection::nativeCalls;
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_READWRITE,&old));
    CHECK(MemoryProtection::Protect(page+19,7,PAGE_EXECUTE_READ,&old));
    CHECK(MemoryProtection::directCalls==4 && MemoryProtection::nativeCalls==exportedCalls);
    MemoryProtection::preferDirect=false;
    MemoryProtection::directResolved=true;MemoryProtection::directProtect=nullptr;
    old=0x12345678;
    CHECK(!MemoryProtection::Protect(page,5,PAGE_READWRITE,&old));CHECK(old==0x12345678);
    CHECK(VirtualQuery(page,&memory,sizeof(memory)));CHECK(memory.Protect==PAGE_EXECUTE_READ);
    MemoryProtection::nativeProtect=nullptr;
    CHECK(!MemoryProtection::Protect(page,5,PAGE_READWRITE,&old));CHECK(old==0x12345678);
    MemoryProtection::nativeProtect=native;
    CHECK(!MemoryProtection::Protect(nullptr,5,PAGE_READWRITE,&old));
    CHECK(!MemoryProtection::Protect(page,0,PAGE_READWRITE,&old));
    CHECK(!MemoryProtection::Protect(page,5,PAGE_READWRITE,nullptr));
    CHECK(VirtualFree(page,0,MEM_RELEASE));
    DWORD service=0x12345678;BYTE malformed[64]={};
    CHECK(!NativeProtection::ReadService(malformed,sizeof(malformed),service));CHECK(service==0x12345678);
    puts("PASS: Win32/exported-native/own WoW64 protection, dynamic service ID, unaligned regions, RX restoration and failure guards");
    return 0;
}
