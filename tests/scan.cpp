#include "bridge.h"
#include <elf.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cassert>
int main(int argc,char**argv) {
    if(argc!=2&&argc!=3)return 2;
    std::ifstream f(argv[1],std::ios::binary);if(!f)return 2;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
    if(b.size()<sizeof(Elf64_Ehdr))return 2;
    Elf64_Ehdr e;memcpy(&e,b.data(),sizeof(e));
    if(memcmp(e.e_ident,ELFMAG,SELFMAG)||e.e_ident[EI_CLASS]!=ELFCLASS64||e.e_machine!=EM_X86_64||e.e_phentsize!=sizeof(Elf64_Phdr))return 2;
    if(e.e_phoff>b.size()||e.e_phnum>(b.size()-e.e_phoff)/sizeof(Elf64_Phdr))return 2;
    std::vector<AACBridge::Region> rs;
    for(unsigned i=0;i<e.e_phnum;i++) {
        Elf64_Phdr p;memcpy(&p,b.data()+e.e_phoff+i*sizeof(p),sizeof(p));
        if(p.p_type!=PT_LOAD||!(p.p_flags&PF_R))continue;
        if(p.p_offset>b.size()||p.p_filesz>b.size()-p.p_offset)return 2;
        rs.push_back({p.p_vaddr,b.data()+p.p_offset,size_t(p.p_filesz),bool(p.p_flags&PF_X)});
    }
    AACBridge::Targets t;std::string error;
    if(!AACBridge::discover(rs,t,error)){fprintf(stderr,"Refused: %s\n",error.c_str());return 1;}
    printf("layout=%zu dispatch=%lx ready=%lx allocator=%lx constructor=%lx vtable=%lx size=%zu\n",t.layout,t.dispatch,t.ready,t.allocator,t.constructor,t.vtable,t.objectSize);
    printf("Matroska gate=%lx metadata=%lx finalize=%lx dispatch=%lx ready=%lx\n",t.mkvGate,t.mkvProbe,t.mkvFinalize,t.mkvDispatch,t.mkvReady);
    if(argc==3 && std::string(argv[2])=="--test") {
        auto ptr=[&](uintptr_t va)->uint8_t* {
            for(auto& r:rs)if(va>=r.address&&va-r.address<r.size)return const_cast<uint8_t*>(r.bytes+va-r.address);
            return nullptr;
        };
        for(uintptr_t va:{t.dispatch,t.ready,t.constructor,t.mkvGate,t.mkvProbe,t.mkvFinalize,t.mkvDispatch,t.mkvReady}) {
            uint8_t* p=ptr(va);assert(p);*p^=1;
            AACBridge::Targets ignored;
            assert(!AACBridge::discover(rs,ignored,error));*p^=1;
        }
        auto duplicate=rs;duplicate.push_back({0x60000000,ptr(t.dispatch),81,true});
        AACBridge::Targets ignored;assert(!AACBridge::discover(duplicate,ignored,error));
        auto duplicateMkv=rs;duplicateMkv.push_back({0x61000000,ptr(t.mkvGate-23),41,true});
        assert(!AACBridge::discover(duplicateMkv,ignored,error));
        uintptr_t type=0,name=0;memcpy(&type,ptr(t.vtable-8),8);memcpy(&name,ptr(type+8),8);
        *ptr(name)^=1;assert(!AACBridge::discover(rs,ignored,error));*ptr(name)^=1;
        // Move every virtual region and the absolute RTTI pointers. rel32 calls
        // and jumps retain their offsets. The scan must find relocated targets.
        const uintptr_t shift=0x1000000;
        auto add64=[&](uintptr_t va){uint64_t n;memcpy(&n,ptr(va),8);n+=shift;memcpy(ptr(va),&n,8);};
        add64(t.vtable-8);add64(type+8);
        for(size_t i=0;i<t.vtableEntries;i++)add64(t.vtable+8*i);
        uint32_t vt;memcpy(&vt,ptr(t.constructor+t.vtableImmediate),4);vt+=shift;memcpy(ptr(t.constructor+t.vtableImmediate),&vt,4);
        for(auto& r:rs)r.address+=shift;
        AACBridge::Targets moved;assert(AACBridge::discover(rs,moved,error));
        assert(moved.dispatch==t.dispatch+shift&&moved.constructor==t.constructor+shift&&moved.ready==t.ready+shift&&moved.vtable==t.vtable+shift);
        assert(moved.mkvGate==t.mkvGate+shift&&moved.mkvAccept==t.mkvAccept+shift&&moved.mkvProbe==t.mkvProbe+shift&&moved.mkvProbeFallback==t.mkvProbeFallback+shift&&moved.mkvFinalize==t.mkvFinalize+shift);
        assert(moved.mkvDispatch==t.mkvDispatch+shift&&moved.mkvOther==t.mkvOther+shift&&moved.mkvMP3==t.mkvMP3+shift&&moved.mkvReady==t.mkvReady+shift&&moved.mkvFailure==t.mkvFailure+shift);
        puts("PASS: changed QuickTime/Matroska instructions, duplicate matches and wrong RTTI rejected; relocated targets discovered");
    }
}
