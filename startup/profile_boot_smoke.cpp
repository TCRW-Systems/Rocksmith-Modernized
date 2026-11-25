#include <cstdio>
#include "profile_boot.hpp"
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned calls=0;
static void __fastcall FixtureTick(void*,void*,void*) {++calls;}
static void StringAt(BYTE* object,const char* text,char* heap=nullptr) {
    memset(object,0,24);size_t length=strlen(text);
    char* start=reinterpret_cast<char*>(object);
    if(length>=16){start=heap;memcpy(object,&heap,sizeof(heap));}
    memcpy(start,text,length+1);
    uintptr_t end=reinterpret_cast<uintptr_t>(start)+length;
    uintptr_t capacity=heap?end+1:reinterpret_cast<uintptr_t>(object)+16;
    memcpy(object+16,&end,4);memcpy(object+20,&capacity,4);
}
static void VectorAt(BYTE* object,unsigned offset,BYTE* rows,unsigned count) {
    uintptr_t begin=reinterpret_cast<uintptr_t>(rows),end=begin+24*count;
    memcpy(object+offset,&begin,4);memcpy(object+offset+4,&end,4);memcpy(object+offset+8,&end,4);
}
int main() {
    BYTE object[0x300]={},rows[72]={};
    StringAt(rows,"New profile");StringAt(rows+24,"second");StringAt(rows+48,"Uj profil");
    StringAt(object+0x1fc,"New profile");
    for(unsigned offset:{0x214u,0x220u})VectorAt(object,offset,rows,3);
    CHECK(ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object))); // English creation words can be a real saved name.
    StringAt(object+0x1fc,"Uj profil");CHECK(!ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object)));
    StringAt(object+0x1fc,"second");CHECK(!ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object))); // User moved selection.
    for(unsigned offset:{0x214u,0x220u})VectorAt(object,offset,rows,1);
    StringAt(object+0x1fc,"New profile");CHECK(!ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object))); // Creation-only list.
    for(unsigned offset:{0x214u,0x220u})VectorAt(object,offset,rows,3);
    StringAt(rows+48,"New profile");CHECK(!ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object))); // Ambiguous labels.
    StringAt(rows+48,"Uj profil");
    char firstHeap[256],selectedHeap[256];
    const char* unicode="\xc3\x81rv\xc3\xadzt\xc5\xb1r\xc5\x91 profile with a long name";
    StringAt(rows,unicode,firstHeap);StringAt(object+0x1fc,unicode,selectedHeap);
    CHECK(ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object)));
    VectorAt(object,0x220,rows,2);CHECK(!ProfileBoot::HasProfileAt(reinterpret_cast<uintptr_t>(object))); // Incoherent vectors.
    CHECK(!ProfileBoot::HasProfileAt(0));
    ProfileBoot::Router r;
    CHECK(!r.Step("TitleScreen",true,100));
    CHECK(!r.Step("SelectionListDialog",true,200));
    CHECK(!r.Step("UplayLoginDialog",true,1000));
    CHECK(!r.Step("ProfileSelect",false,2000));
    CHECK(!r.Step("ProfileSelect",false,3000));
    CHECK(r.Step("ProfileSelect",true,3100));
    CHECK(!r.Step("ProfileSelect",true,3150));
    CHECK(r.Step("ProfileSelect",true,3450));
    CHECK(!r.Step("SelectionListDialog",true,3500));
    CHECK(r.done && !r.Step("ProfileSelect",true,5000));
    ProfileBoot::Router manual;
    CHECK(!manual.Step("MainMenu",false,1));
    CHECK(!manual.Step("ProfileSelect",true,2000));
    ProfileBoot::Router missing;
    CHECK(!missing.Step("ProfileSelect",false,0));
    CHECK(!missing.Step("ProfileSelect",false,5000) && missing.done);
    CHECK(!missing.Step("ProfileSelect",true,6000));
    ProfileBoot::Router failed;
    CHECK(!failed.Step("ProfileSelect",true,100));
    CHECK(failed.Step("ProfileSelect",true,700));
    CHECK(failed.Step("ProfileSelect",true,1000));
    CHECK(failed.Step("ProfileSelect",true,1300));
    CHECK(!failed.Step("ProfileSelect",true,1600) && failed.done);
    ProfileBoot::Router dismissed;
    CHECK(!dismissed.Step("ProfileSelect",false,100));
    CHECK(!dismissed.Step("TitleScreen",false,200) && dismissed.done);

    BYTE* host=static_cast<BYTE*>(VirtualAlloc(nullptr,0x80000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(host);
    void** table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(table);
    unsigned rvas[]={0x23830,0x7df30,0x7df50,0x26ef0,0x24b80,0x24b90,0x24b90,0x24b90,0x24b90,0x24b90,0x7df90,0x24b90,0x24b90,0x24b90,0x24b90};
    for(unsigned i=0;i<15;++i)table[i]=host+rvas[i];
    DWORD old;CHECK(VirtualProtect(table,4096,PAGE_READONLY,&old));
    CHECK(ProfileBoot::InstallAt(table,host));
    MEMORY_BASIC_INFORMATION mem{};CHECK(VirtualQuery(table,&mem,sizeof(mem)));CHECK(mem.Protect==PAGE_READONLY);
    for(unsigned i=0;i<15;++i)if(i!=2 && i!=10)CHECK(table[i]==host+rvas[i]);
    using Enabled=bool(__thiscall*)(void*,void*);
    CHECK(reinterpret_cast<Enabled>(table[2])(nullptr,nullptr));
    ProfileBoot::router.done=true;
    CHECK(!reinterpret_cast<Enabled>(table[2])(nullptr,nullptr));
    CHECK(!ProfileBoot::InstallAt(table,host));
    ProfileBoot::installed=false;
    CHECK(!ProfileBoot::InstallAt(table,host)); // A changed vtable is rejected.
    ProfileBoot::originalTick=reinterpret_cast<ProfileBoot::Tick>(&FixtureTick);
    ProfileBoot::originalTick(nullptr,nullptr);CHECK(calls==1);
    puts("PASS: x86 virtual-call ABI, exact vtable guard, read-only protection restoration, boot-only routing, connection/auth exclusions and manual re-entry");
    return 0;
}
