#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
namespace AACBridge {
struct Region {uintptr_t address;const uint8_t* bytes;size_t size;bool executable;};
struct Targets {
    uintptr_t dispatch=0,ready=0,allocator=0,constructor=0,vtable=0;
    size_t objectSize=0,vtableEntries=0,layout=0,vtableImmediate=0;
    uintptr_t mkvGate=0,mkvAccept=0,mkvProbe=0,mkvProbeFallback=0,mkvFinalize=0;
    uintptr_t mkvDispatch=0,mkvOther=0,mkvMP3=0,mkvReady=0,mkvFailure=0;
};
bool discover(const std::vector<Region>& regions,Targets& result,std::string& error);
bool install();
void setAudioChannels(uint32_t channels);
}
