#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <vector>

// Own x86/WoW64 native-call adapter. The service ID is obtained from the exact
// Windows ntdll file named by its loaded module; no Windows code is distributed.
namespace NativeProtection {
using Function=LONG(NTAPI*)(HANDLE,PVOID*,SIZE_T*,ULONG,PULONG);
inline DWORD service=0;
inline void* transition=nullptr;
inline unsigned error=0;

inline bool ReadService(const BYTE* data,SIZE_T length,DWORD& result) {
    auto read=[&](SIZE_T offset,void* value,SIZE_T count)->bool {
        if(offset>length || count>length-offset)return false;
        memcpy(value,data+offset,count);return true;
    };
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!read(0,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0)return false;
    SIZE_T pe=static_cast<SIZE_T>(dos.e_lfanew);
    if(!read(pe,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE ||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER32) ||
       nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT ||
       !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>96)return false;
    SIZE_T sections=pe+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
    auto rva=[&](DWORD address,SIZE_T count)->const BYTE* {
        if(address<nt.OptionalHeader.SizeOfHeaders && address<=length && count<=length-address)return data+address;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            IMAGE_SECTION_HEADER section{};
            if(!read(sections+i*sizeof(section),&section,sizeof(section)))return nullptr;
            if(address<section.VirtualAddress)continue;
            DWORD delta=address-section.VirtualAddress;
            if(delta>=section.SizeOfRawData || count>section.SizeOfRawData-delta)continue;
            SIZE_T offset=static_cast<SIZE_T>(section.PointerToRawData)+delta;
            if(offset<=length && count<=length-offset)return data+offset;
        }
        return nullptr;
    };
    auto directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!directory.VirtualAddress || directory.Size<sizeof(IMAGE_EXPORT_DIRECTORY))return false;
    auto exportBytes=rva(directory.VirtualAddress,sizeof(IMAGE_EXPORT_DIRECTORY));if(!exportBytes)return false;
    IMAGE_EXPORT_DIRECTORY exports{};memcpy(&exports,exportBytes,sizeof(exports));
    if(!exports.NumberOfNames || exports.NumberOfNames>length/4 || !exports.NumberOfFunctions || exports.NumberOfFunctions>length/4)return false;
    auto names=rva(exports.AddressOfNames,static_cast<SIZE_T>(exports.NumberOfNames)*4);
    auto ordinals=rva(exports.AddressOfNameOrdinals,static_cast<SIZE_T>(exports.NumberOfNames)*2);
    auto functions=rva(exports.AddressOfFunctions,static_cast<SIZE_T>(exports.NumberOfFunctions)*4);
    if(!names || !ordinals || !functions)return false;
    constexpr char wanted[]="NtProtectVirtualMemory";
    for(DWORD i=0;i<exports.NumberOfNames;++i) {
        DWORD name=0;memcpy(&name,names+i*4,4);
        auto text=rva(name,sizeof(wanted));if(!text || memcmp(text,wanted,sizeof(wanted)))continue;
        WORD ordinal=0;memcpy(&ordinal,ordinals+i*2,2);if(ordinal>=exports.NumberOfFunctions)return false;
        DWORD address=0;memcpy(&address,functions+ordinal*4,4);
        if(address>=directory.VirtualAddress && address-directory.VirtualAddress<directory.Size)return false;
        auto stub=rva(address,15);
        // Recognize this WoW64 ABI, including its five-argument stack cleanup.
        if(!stub || stub[0]!=0xb8 || stub[5]!=0xba || stub[10]!=0xff || stub[11]!=0xd2 ||
           stub[12]!=0xc2 || stub[13]!=0x14 || stub[14]!=0)return false;
        DWORD number=0;memcpy(&number,stub+1,4);
        if(!number || number>=0x10000)return false;
        result=number;return true;
    }
    return false;
}

inline __declspec(naked) LONG NTAPI Protect(HANDLE,PVOID*,SIZE_T*,ULONG,PULONG) {
    __asm {
        mov eax,dword ptr [service]
        mov edx,dword ptr [transition]
        call edx
        ret 14h
    }
}
inline Function Resolve() {
    error=1;BOOL wow64=FALSE;
    SYSTEM_INFO system{};GetNativeSystemInfo(&system);
    if(system.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64 ||
       !IsWow64Process(GetCurrentProcess(),&wow64) || !wow64)return nullptr;
    // WOW32Reserved in the x86 TEB supplies the OS-owned WoW64 transition.
    void* gate=reinterpret_cast<void*>(__readfsdword(0xc0));
    MEMORY_BASIC_INFORMATION memory{};error=2;
    if(!gate || !VirtualQuery(gate,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
       !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
       (memory.Protect&PAGE_GUARD))return nullptr;
    wchar_t path[MAX_PATH];HMODULE module=GetModuleHandleW(L"ntdll.dll");error=3;
    DWORD count=module?GetModuleFileNameW(module,path,MAX_PATH):0;if(!count || count>=MAX_PATH)return nullptr;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    error=4;if(file==INVALID_HANDLE_VALUE)return nullptr;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<sizeof(IMAGE_DOS_HEADER) || size.QuadPart>32*1024*1024){CloseHandle(file);return nullptr;}
    std::vector<BYTE> bytes(static_cast<SIZE_T>(size.QuadPart));DWORD got=0;
    bool okay=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr) && got==bytes.size();CloseHandle(file);
    error=5;DWORD number=0;if(!okay || !ReadService(bytes.data(),bytes.size(),number))return nullptr;
    service=number;transition=gate;error=0;return &Protect;
}
}
