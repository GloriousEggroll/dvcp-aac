// Guarded startup-only bridges from QuickTime/Matroska readers to codec plugins.
// No executable/library files are written; there are no fixed Resolve addresses.
#include "bridge.h"
#include "diagnostics.h"
#include "aac_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <dlfcn.h>
#include <link.h>
#include <new>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
void aac_qt_entry();
void aac_qt_entry_21_1();
void aac_mkv_gate();
void aac_mkv_probe();
void aac_mkv_dispatch();
uintptr_t aac_qt_fallback=0;
uintptr_t aac_qt_ready=0;
uintptr_t aac_mkv_accept=0,aac_mkv_gate_fallback=0;
uintptr_t aac_mkv_probe_fallback=0,aac_mkv_finalize=0;
uintptr_t aac_mkv_other=0,aac_mkv_mp3=0,aac_mkv_ready=0,aac_mkv_failure=0;
}
namespace AACBridge {
static Targets active;
static bool installed=false;
static void log(const char* message) {
    aacLog("%s",message);
}
static std::vector<int> pattern(const char* text) {
    std::vector<int> out;
    while(*text) {
        while(*text==' ')text++;
        if(!*text)break;
        if(*text=='?') {out.push_back(-1);text+=2;}
        else {unsigned b=0;if(sscanf(text,"%2x",&b)!=1)return {};out.push_back(int(b));text+=2;}
    }
    return out;
}
static const uint8_t* read(const std::vector<Region>& rs,uintptr_t va,size_t n) {
    for(const auto& r:rs)
        if(va>=r.address && va-r.address<=r.size && n<=r.size-(va-r.address)) return r.bytes+(va-r.address);
    return nullptr;
}
static bool code(const std::vector<Region>& rs,uintptr_t va,size_t n=1) {
    for(const auto& r:rs)
        if(r.executable && va>=r.address && va-r.address<=r.size && n<=r.size-(va-r.address))return true;
    return false;
}
static bool match(const uint8_t* b,const std::vector<int>& p) {
    for(size_t i=0;i<p.size();i++)if(p[i]>=0 && b[i]!=p[i])return false;
    return true;
}
static std::vector<uintptr_t> find(const std::vector<Region>& rs,const char* text) {
    auto p=pattern(text);std::vector<uintptr_t> hits;
    if(p.empty()||p[0]<0)return hits;
    for(const auto& r:rs) {
        if(!r.executable || r.size<p.size())continue;
        size_t offset=0;
        while(offset<=r.size-p.size()) {
            const auto* b=static_cast<const uint8_t*>(memchr(r.bytes+offset,p[0],r.size-p.size()-offset+1));
            if(!b)break;
            offset=size_t(b-r.bytes);
            if(match(b,p))hits.push_back(r.address+offset);
            offset++;
        }
    }
    return hits;
}
static uintptr_t relative(const std::vector<Region>& rs,uintptr_t pc) {
    const uint8_t* b=read(rs,pc,5);if(!b || (b[0]!=0xe8 && b[0]!=0xe9))return 0;
    int32_t rel;memcpy(&rel,b+1,4);return pc+5+rel;
}
static uintptr_t branch(const std::vector<Region>& rs,uintptr_t pc) {
    const uint8_t* b=read(rs,pc,6);if(!b || b[0]!=0x0f || (b[1]&0xf0)!=0x80)return 0;
    int32_t rel;memcpy(&rel,b+2,4);return pc+6+rel;
}
bool discover(const std::vector<Region>& rs,Targets& out,std::string& error) {
    // Register assignments, virtual call slot and the ALAC fallback are checked.
    // Each layout describes one known compiler output of the QuickTime reader.
    struct Layout {
        const char *name,*dispatch,*gate,*ctor,*integerFormat;
        size_t readyJump,nativeAlloc,vtableImmediate;
    };
    static const Layout layouts[]={
        {"21.0.4",
         "49 8b 04 24 4c 89 e3 4c 89 e7 89 ee ff 50 60 49 89 c4 48 85 c0 0f 94 c0 41 81 ff 63 61 6c 61 0f 95 c1 08 c1 0f 85 ?? ?? ?? ?? bf ?? 00 00 00 e8 ?? ?? ?? ?? 49 89 c5 b0 01 48 89 44 24 20 4c 89 ef 4c 89 e6 e8 ?? ?? ?? ?? 49 89 dc e9 ?? ?? ?? ??",
         "44 89 f8 25 00 ff ff ff 3d 00 63 61 61 0f 95 c0 41 81 e7 ff ff ff 00 41 81 ff 63 61 61 00 0f 95 c1 84 c8 0f 85",
         "55 41 57 41 56 41 55 41 54 53 48 83 ec ?? 49 89 f7 48 89 fb 48 89 77 08 48 c7 c0 ?? ?? ?? ?? 48 83 c0 10 48 89 07 c6 47 10 00 48 c7 47 18 00 00 00 00",
         "55 41 57 41 56 53 50 39 b7 f0 00 00 00 0f 84 ?? ?? ?? ?? 48 89 fb 89 b7 f0 00 00 00 bf 78 00 00 00 89 f5 e8 ?? ?? ?? ?? f2 0f 10 43 40 f2 0f 59 05 ?? ?? ?? ?? 49 89 c6 8b 83 dc 00 00 00 f2 48 0f 2a c8 8b 83 d8 00 00 00 f2 48 0f 2a d0 f2 0f 58 d1 f2 0f 58 d0 f2 48 0f 2c f2 8b 4b 58 4c 89 f7 89 ea e8 ?? ?? ?? ?? 4c 8b bb d0 00 00 00 4c 89 b3 d0 00 00 00 4d 85 ff 74 ?? 4c 89 ff e8 ?? ?? ?? ?? be 78 00 00 00 4c 89 ff e8 ?? ?? ?? ?? 66 0f 57 c0 66 0f 11 83 e0 00 00 00 b0 01 48 83 c4 08 5b 41 5e 41 5f 5d c3",
         76,47,27},
        // 21.1.1: reader r13, fourcc r12d, stream rbx, codec r15, flag [rsp+8];
        // IOPluginAudioCodec gained a std::string at +0x10 (fields moved by 0x18).
        {"21.1.1",
         "49 8b 45 00 4c 89 ef 89 ee ff 50 60 48 89 c3 48 85 c0 0f 94 c0 41 81 fc 63 61 6c 61 0f 95 c1 08 c1 75 ?? bf ?? 00 00 00 e8 ?? ?? ?? ?? 49 89 c7 b0 01 48 89 44 24 08 4c 89 ff 48 89 de e8 ?? ?? ?? ?? e9 ?? ?? ?? ??",
         "44 89 e0 25 00 ff ff ff 3d 00 63 61 61 0f 95 c0 41 81 e4 ff ff ff 00 41 81 fc 63 61 61 00 0f 95 c1 84 c8 0f 85",
         "55 41 57 41 56 41 55 41 54 53 48 83 ec ?? 49 89 f7 48 89 fb 48 89 77 08 48 8d 47 18 66 0f ef c0 f3 0f 7f 47 18 48 89 47 10 48 c7 c0 ?? ?? ?? ?? 48 83 c0 10 48 89 07 c6 47 28 00 48 c7 47 30 00 00 00 00",
         "55 41 57 41 56 53 50 39 b7 08 01 00 00 0f 84 ?? ?? ?? ?? 48 89 fb 89 b7 08 01 00 00 bf 78 00 00 00 89 f5 e8 ?? ?? ?? ?? f2 0f 10 43 58 f2 0f 59 05 ?? ?? ?? ?? 49 89 c6 8b 83 f4 00 00 00 f2 48 0f 2a c8 8b 83 f0 00 00 00 f2 48 0f 2a d0 f2 0f 58 d1 f2 0f 58 d0 f2 48 0f 2c f2 8b 4b 70 4c 89 f7 89 ea e8 ?? ?? ?? ?? 4c 8b bb e8 00 00 00 4c 89 b3 e8 00 00 00 4d 85 ff 74 ?? 4c 89 ff e8 ?? ?? ?? ?? be 78 00 00 00 4c 89 ff e8 ?? ?? ?? ?? 66 0f 57 c0 66 0f 11 83 f8 00 00 00 b0 01 48 83 c4 08 5b 41 5e 41 5f 5d c3",
         66,40,44},
    };
    const Layout* layout=nullptr;std::vector<uintptr_t> sites;
    for(const auto& l:layouts) {
        auto hits=find(rs,l.dispatch);
        if(hits.empty())continue;
        if(layout||hits.size()!=1) {error="QuickTime dispatch signature is absent or ambiguous";return false;}
        layout=&l;sites=hits;
    }
    if(!layout) {error="QuickTime dispatch signature is absent or ambiguous";return false;}
    auto allocations=find(rs,"bf ?? ?? ?? ?? e8 ?? ?? ?? ?? 49 89 c4 48 89 c7 48 89 de e8 ?? ?? ?? ?? 40 84 ed 75 09 49 8b 06");
    if(allocations.size()!=1) {error="Audio plugin constructor call is absent or ambiguous";return false;}
    Targets t;t.layout=size_t(layout-layouts);t.vtableImmediate=layout->vtableImmediate;
    t.dispatch=sites[0];t.ready=relative(rs,t.dispatch+layout->readyJump);
    auto gate=pattern(layout->gate);
    auto ready=read(rs,t.ready,gate.size());
    if(!ready||!code(rs,t.ready,gate.size())||!match(ready,gate)) {error="AAC delay-wrapper continuation does not match";return false;}
    uintptr_t a=allocations[0];uint32_t objectSize=0;
    memcpy(&objectSize,read(rs,a+1,4),4);t.objectSize=objectSize;
    t.allocator=relative(rs,a+5);t.constructor=relative(rs,a+19);
    if(t.objectSize<0x100||t.objectSize>0x1000||!code(rs,t.allocator,6)) {error="Invalid decoder allocation size or allocator";return false;}
    // Validate the constructor's input/field layout, then identify its C++ RTTI.
    auto ctor=pattern(layout->ctor);
    const auto* c=read(rs,t.constructor,ctor.size());
    if(!c||!code(rs,t.constructor,ctor.size())||!match(c,ctor)) {error="Decoder constructor ABI is not recognized";return false;}
    int32_t vtable=0;memcpy(&vtable,c+layout->vtableImmediate,4);t.vtable=uintptr_t(intptr_t(vtable))+16;
    const auto* v=read(rs,t.vtable-8,8);uintptr_t type=0,name=0;
    if(v)memcpy(&type,v,8);
    const auto* rtti=read(rs,type+8,8);if(rtti)memcpy(&name,rtti,8);
    const auto* text=read(rs,name,sizeof("18IOPluginAudioCodec"));
    if(!text||memcmp(text,"18IOPluginAudioCodec",sizeof("18IOPluginAudioCodec"))) {error="Constructor is not IOPluginAudioCodec";return false;}
    for(size_t i=0;i<32;i++) {
        uintptr_t fn=0;const auto* slot=read(rs,t.vtable+8*i,8);
        if(!slot)break;memcpy(&fn,slot,8);if(!code(rs,fn))break;t.vtableEntries++;
    }
    if(t.vtableEntries<8||t.vtableEntries>=32) {error="Audio decoder vtable shape is not recognized";return false;}
    uintptr_t format=0;memcpy(&format,read(rs,t.vtable+16,8),8);
    auto integerFormat=pattern(layout->integerFormat);
    const auto* fmt=read(rs,format,integerFormat.size());
    if(!fmt||!match(fmt,integerFormat)) {error="Plugin PCM format implementation changed; conversion must be reviewed";return false;}
    // The allocator must be the same one the neighboring native branch uses.
    if(relative(rs,t.dispatch+layout->nativeAlloc)!=t.allocator) {error="Native and plugin allocation functions disagree";return false;}
    auto mkv=find(rs,"49 8d 54 24 79 8b 32 bf 41 5f 41 41 31 fe 0f b6 52 04 83 f2 43 09 f2 0f 84 ?? ?? ?? ?? f6 c1 01 0f 84 ?? ?? ?? ??");
    auto descriptor=find(rs,"b8 03 00 00 00 31 db 48 8d 4c 24 38 66 0f ef c0 f3 0f 7f 01 c6 41 10 00 b9 ff ff ff ff 48 89 4c 24 4c c7 44 24 54 00 00 00 00 66 c7 44 24 58 01 00 4c 89 64 24 30 89 44 24 38 31 ed eb 18 0f 1f 80 00 00 00 00 48 8d 45 01 48 83 fd 08 48 89 c5 0f 87 ?? ?? ?? ??");
    auto mkvCodecs=find(rs,"41 8b 47 08 83 f8 02 0f 84 ?? ?? ?? ?? 83 f8 03 0f 84 ?? ?? ?? ?? 83 f8 07 0f 85 ?? ?? ?? ?? bf ?? 00 00 00 e8 ?? ?? ?? ?? 49 89 c5 48 89 c7 48 8b 74 24 20 e8 ?? ?? ?? ?? e9 ?? ?? ?? ??");
    auto privateData=find(rs,"4c 89 64 24 30 c7 44 24 38 07 00 00 00 49 8d b4 24 90 00 00 00 4c 8d 44 24 30 e8 ?? ?? ?? ?? e9 ?? ?? ?? ??");
    if(mkv.size()!=1 || descriptor.size()!=1 || mkvCodecs.size()!=1 || privateData.size()!=1) {
        error="Matroska AAC metadata/decoder signatures are absent or ambiguous";return false;
    }
    t.mkvGate=mkv[0]+23;t.mkvAccept=descriptor[0]+5;t.mkvProbe=descriptor[0]+58;
    t.mkvProbeFallback=t.mkvProbe+28;t.mkvFinalize=branch(rs,t.mkvProbe+22);
    t.mkvDispatch=mkvCodecs[0]+4;t.mkvOther=t.mkvDispatch+9;
    t.mkvMP3=branch(rs,t.mkvDispatch+3);t.mkvFailure=branch(rs,t.mkvDispatch+21);t.mkvReady=t.mkvMP3+26;
    auto finalPattern=pattern("48 8b 44 24 30 48 85 c0 0f 84 ?? ?? ?? ?? 48 83 38 00 0f 84 ?? ?? ?? ?? 83 7c 24 3c 00 0f 84 ?? ?? ?? ?? 83 7c 24 40 00 0f 84 ?? ?? ?? ?? 83 7c 24 44 00 0f 84");
    auto formatPattern=pattern("bf ?? 00 00 00 e8 ?? ?? ?? ?? 49 89 c5 48 89 c7 48 8b 74 24 20 e8 ?? ?? ?? ?? 41 8b 77 10 41 8b 57 14 41 0f b6 4f 18 49 8b 45 00 4c 89 ef ff 50 10");
    auto finish=read(rs,t.mkvFinalize,finalPattern.size());auto fmtMkv=read(rs,t.mkvMP3,formatPattern.size());
    if(!finish || !match(finish,finalPattern) || !fmtMkv || !match(fmtMkv,formatPattern) ||
       relative(rs,t.mkvMP3+5)!=t.allocator || relative(rs,privateData[0]+31)!=t.mkvFinalize ||
       descriptor[0]<mkv[0] || descriptor[0]-mkv[0]>1024 || privateData[0]<mkv[0] || privateData[0]>descriptor[0] ||
       !code(rs,t.mkvFailure,8) || !code(rs,t.mkvProbeFallback,8)) {
        error="Matroska descriptor or PCM ABI relationships changed";return false;
    }
    out=t;return true;
}
static int collect(dl_phdr_info* info,size_t,void* arg) {
    if(info->dlpi_name && info->dlpi_name[0])return 0;
    auto& rs=*static_cast<std::vector<Region>*>(arg);
    for(int i=0;i<info->dlpi_phnum;i++) {
        auto& p=info->dlpi_phdr[i];
        if(p.p_type==PT_LOAD && (p.p_flags&PF_R)) {
            uintptr_t a=info->dlpi_addr+p.p_vaddr;
            rs.push_back({a,reinterpret_cast<const uint8_t*>(a),size_t(p.p_memsz),bool(p.p_flags&PF_X)});
        }
    }
    return 1;
}
bool install() {
    if(installed)return true;
    std::vector<Region> rs;dl_iterate_phdr(collect,&rs);
    Targets t;std::string error;
    if(!discover(rs,t,error)) {log(("INACTIVE: "+error).c_str());return false;}
    char info[256];snprintf(info,sizeof(info),"discovered layout=%zu dispatch=%p continuation=%p constructor=%p vtable=%p size=%zu",t.layout,(void*)t.dispatch,(void*)t.ready,(void*)t.constructor,(void*)t.vtable,t.objectSize);log(info);
    // Keep bridge code loaded for the lifetime of the process. Installation is
    // only attempted by msgPluginStart, before Resolve opens any media/project.
    Dl_info self;
    if(!dladdr(reinterpret_cast<void*>(&install),&self) || !dlopen(self.dli_fname,RTLD_NOW|RTLD_NODELETE)) {log("INACTIVE: cannot pin plugin");return false;}
    long pageSize=sysconf(_SC_PAGESIZE);if(pageSize<=0)return false;
    // Short MKV branch sites use rel32 jumps to tiny nearby absolute jump stubs.
    // MAP_32BIT never replaces an existing mapping. Reject any out-of-range site.
    uint8_t* stubs=static_cast<uint8_t*>(mmap(nullptr,size_t(pageSize),PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0));
    if(stubs==MAP_FAILED){log("INACTIVE: cannot allocate nearby jump stubs");return false;}
    struct Edit {uintptr_t address;std::vector<uint8_t> bytes,original;};
    std::vector<Edit> edits;
    uint8_t qt[15]={0xff,0x25,0,0,0,0};uintptr_t qtEntry=reinterpret_cast<uintptr_t>(t.layout==1?&aac_qt_entry_21_1:&aac_qt_entry);
    memcpy(qt+6,&qtEntry,8);qt[14]=0x90;
    edits.push_back({t.dispatch,std::vector<uint8_t>(qt,qt+15),{}});
    uintptr_t sites[]={t.mkvGate,t.mkvProbe,t.mkvDispatch};size_t sizes[]={6,11,9};
    uintptr_t entries[]={reinterpret_cast<uintptr_t>(&aac_mkv_gate),reinterpret_cast<uintptr_t>(&aac_mkv_probe),reinterpret_cast<uintptr_t>(&aac_mkv_dispatch)};
    for(size_t i=0;i<3;i++) {
        uint8_t* stub=stubs+16*i;stub[0]=0xff;stub[1]=0x25;memset(stub+2,0,4);memcpy(stub+6,&entries[i],8);
        intptr_t distance=reinterpret_cast<uintptr_t>(stub)-(sites[i]+5);
        if(distance<INT32_MIN || distance>INT32_MAX) {munmap(stubs,pageSize);log("INACTIVE: jump stub is out of range");return false;}
        std::vector<uint8_t> patch(sizes[i],0x90);patch[0]=0xe9;int32_t rel=int32_t(distance);memcpy(patch.data()+1,&rel,4);
        edits.push_back({sites[i],patch,{}});
    }
    if(mprotect(stubs,pageSize,PROT_READ|PROT_EXEC)!=0){munmap(stubs,pageSize);return false;}
    std::vector<uintptr_t> pages;
    for(auto& edit:edits) {
        edit.original.assign(reinterpret_cast<uint8_t*>(edit.address),reinterpret_cast<uint8_t*>(edit.address)+edit.bytes.size());
        uintptr_t first=edit.address & ~(uintptr_t(pageSize)-1);
        uintptr_t last=(edit.address+edit.bytes.size()-1) & ~(uintptr_t(pageSize)-1);
        for(uintptr_t page=first;page<=last;page+=pageSize)pages.push_back(page);
    }
    std::sort(pages.begin(),pages.end());pages.erase(std::unique(pages.begin(),pages.end()),pages.end());
    size_t unlocked=0;
    for(;unlocked<pages.size();unlocked++)
        if(mprotect(reinterpret_cast<void*>(pages[unlocked]),pageSize,PROT_READ|PROT_WRITE|PROT_EXEC)!=0)break;
    if(unlocked!=pages.size()) {
        for(size_t i=0;i<unlocked;i++)mprotect(reinterpret_cast<void*>(pages[i]),pageSize,PROT_READ|PROT_EXEC);
        munmap(stubs,pageSize);log("INACTIVE: code page protection change failed");return false;
    }
    active=t;aac_qt_fallback=t.dispatch+15;aac_qt_ready=t.ready;
    aac_mkv_accept=t.mkvAccept;aac_mkv_gate_fallback=t.mkvGate+6;
    aac_mkv_probe_fallback=t.mkvProbeFallback;aac_mkv_finalize=t.mkvFinalize;
    aac_mkv_other=t.mkvOther;aac_mkv_mp3=t.mkvMP3;aac_mkv_ready=t.mkvReady;aac_mkv_failure=t.mkvFailure;
    for(auto& edit:edits) {
        memcpy(reinterpret_cast<void*>(edit.address),edit.bytes.data(),edit.bytes.size());
        __builtin___clear_cache(reinterpret_cast<char*>(edit.address),reinterpret_cast<char*>(edit.address+edit.bytes.size()));
    }
    bool protectedAgain=true;
    for(uintptr_t page:pages)if(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC)!=0)protectedAgain=false;
    if(!protectedAgain) {
        // Restore the original instructions while startup is still single-threaded.
        bool writable=true;
        for(uintptr_t page:pages)if(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_WRITE|PROT_EXEC)!=0)writable=false;
        if(writable)for(auto& edit:edits)memcpy(reinterpret_cast<void*>(edit.address),edit.original.data(),edit.original.size());
        for(uintptr_t page:pages)mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC);
        if(writable){munmap(stubs,pageSize);log("INACTIVE: executable protection restoration failed");return false;}
        // Keep live stubs mapped if the OS refused rollback; never jump into freed memory.
        log("ERROR: operating system refused code-page rollback");
    }
    installed=true;log("ACTIVE: AAC QuickTime and Matroska bridges installed in memory; disk files unchanged");return true;

}

// QuickTime/Matroska request float PCM; IOPluginAudioCodec's SetFormat ignores that
// flag and always caches integer PCM. Adapt only objects created by this bridge.
// The original vtable and all native object fields remain otherwise unchanged.
struct AudioAdapter {
    std::vector<uintptr_t> table;
    uintptr_t original=0;
    uint32_t channels=0,bits=0;
    bool floating=false;
    unsigned logged=0;
};
static thread_local AudioAdapter* constructing=nullptr;
static std::mutex adaptersMutex;
static std::unordered_map<void*,std::shared_ptr<AudioAdapter>> adapters;
void setAudioChannels(uint32_t channels) {if(constructing)constructing->channels=channels;}
static std::shared_ptr<AudioAdapter> adapter(void* object,bool erase=false) {
    std::lock_guard<std::mutex> lock(adaptersMutex);
    auto it=adapters.find(object);if(it==adapters.end())return {};
    auto p=it->second;if(erase)adapters.erase(it);return p;
}
static void destruct(void* object) {
    auto p=adapter(object,true);if(!p)return;
    reinterpret_cast<void(*)(void*)>(reinterpret_cast<uintptr_t*>(p->original)[0])(object);
}
static void deleteObject(void* object) {
    auto p=adapter(object,true);if(!p)return;
    reinterpret_cast<void(*)(void*)>(reinterpret_cast<uintptr_t*>(p->original)[1])(object);
}
static bool setFormat(void* object,uint32_t bits,uint32_t rate,uint8_t floating) {
    auto p=adapter(object);if(!p)return false;
    if(floating && (bits!=32 || p->channels==0 || p->channels>8))return false;
    using Fn=bool(*)(void*,uint32_t,uint32_t,uint8_t);
    bool ok=reinterpret_cast<Fn>(reinterpret_cast<uintptr_t*>(p->original)[2])(object,bits,rate,floating);
    if(ok){p->bits=bits;p->floating=floating!=0;}
    char line[160];snprintf(line,sizeof(line),"FORMAT adapter=%p bits=%u rate=%u channels=%u float=%u success=%d",object,bits,rate,p->channels,floating,int(ok));log(line);
    return ok;
}
static bool decode(void* object,uint64_t position,uint32_t* samples,uint8_t* data) {
    auto p=adapter(object);if(!p)return false;
    using Fn=bool(*)(void*,uint64_t,uint32_t*,uint8_t*);
    bool ok=reinterpret_cast<Fn>(reinterpret_cast<uintptr_t*>(p->original)[3])(object,position,samples,data);
    if(ok && p->floating && data && samples) {
        size_t count=size_t(*samples)*p->channels;
        for(size_t i=0;i<count;i++) {
            int32_t integer;memcpy(&integer,data+4*i,4);
            float value=integer*(1.0f/2147483648.0f);
            memcpy(data+4*i,&value,4);
        }
        if(p->logged<4 && count>256) {
            double energy=0;float peak=0;
            for(size_t i=0;i<count;i++) {float f;memcpy(&f,data+4*i,4);energy+=double(f)*f;peak=std::max(peak,std::fabs(f));}
            char line[180];snprintf(line,sizeof(line),"PCM adapter=%p position=%llu frames=%u rms=%.8f peak=%.8f",object,(unsigned long long)position,*samples,std::sqrt(energy/count),peak);log(line);p->logged++;
        }
    }
    return ok;
}
static void attach(void* object,std::shared_ptr<AudioAdapter> p) {
    p->original=*reinterpret_cast<uintptr_t*>(object);
    const uintptr_t* original=reinterpret_cast<uintptr_t*>(p->original);
    p->table.assign(original-2,original+active.vtableEntries);
    p->table[2]=reinterpret_cast<uintptr_t>(&destruct);
    p->table[3]=reinterpret_cast<uintptr_t>(&deleteObject);
    p->table[4]=reinterpret_cast<uintptr_t>(&setFormat);
    p->table[5]=reinterpret_cast<uintptr_t>(&decode);
    std::lock_guard<std::mutex> lock(adaptersMutex);
    adapters.emplace(object,p);
    *reinterpret_cast<uintptr_t*>(object)=reinterpret_cast<uintptr_t>(p->table.data()+2);
}
}
extern "C" __attribute__((visibility("hidden"))) void* aac_stream_make(void* stream) {
    const auto& t=AACBridge::active;
    if(!stream)return nullptr;
    auto allocate=reinterpret_cast<void*(*)(size_t)>(t.allocator);
    auto construct=reinterpret_cast<void(*)(void*,void*)>(t.constructor);
    void* codec=nullptr;bool constructed=false;
    auto adapter=std::make_shared<AACBridge::AudioAdapter>();
    auto previous=AACBridge::constructing;
    AACBridge::constructing=adapter.get();
    try {
        codec=allocate(t.objectSize);
        construct(codec,stream);
        constructed=true;
        AACBridge::constructing=previous;
        AACBridge::attach(codec,adapter);
        return codec;
    } catch(...) {
        AACBridge::constructing=previous;
        if(constructed)reinterpret_cast<void(*)(void*)>(reinterpret_cast<uintptr_t*>(t.vtable)[1])(codec);
        else if(codec)::operator delete(codec);
        AACBridge::log("AAC decoder construction failed; using original fallback");
        return nullptr;
    }
}

extern "C" __attribute__((visibility("hidden"))) void* aac_qt_make(void* reader,uint32_t track) {
    auto vtable=*reinterpret_cast<void***>(reader);
    auto getStream=reinterpret_cast<void*(*)(void*,uint32_t)>(vtable[0x60/8]);
    return aac_stream_make(getStream(reader,track));
}
extern "C" __attribute__((visibility("hidden"))) void aac_mkv_config(void* track,void* descriptor) {
    uintptr_t start=0,end=0;
    memcpy(&start,static_cast<uint8_t*>(track)+0x90,8);
    memcpy(&end,static_cast<uint8_t*>(track)+0x98,8);
    AACBridge::AudioConfig config;
    if(!start || end<start || end-start>1024*1024 ||
       !AACBridge::parseAudioConfig(reinterpret_cast<uint8_t*>(start),end-start,config)) {
        AACBridge::log("MKV: unsupported or malformed AAC configuration; track left disabled");return;
    }
    auto* d=static_cast<uint8_t*>(descriptor);
    uint32_t bits=32,layout=config.channels==1?1:config.channels==2?2:uint32_t(-1);
    memcpy(d+0x0c,&config.channels,4);memcpy(d+0x10,&bits,4);
    memcpy(d+0x14,&config.rate,4);d[0x18]=1;
    memcpy(d+0x1c,&layout,4);memcpy(d+0x24,&config.frameSamples,4);
    aacLog("MKV AAC metadata rate=%u channels=%u frameSamples=%u",config.rate,config.channels,config.frameSamples);
}
