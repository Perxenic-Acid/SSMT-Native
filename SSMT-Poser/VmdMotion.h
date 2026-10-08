#pragma once
#include "BoneWriteProbe.h"
#include <array>
#include <filesystem>
#include <span>
#include <string_view>

namespace poser::vmd {
struct Key {
    uint32_t frame=0;
    std::array<float,3> position{};
    Quaternion rotation{0,0,0,1};
    std::array<uint8_t,64> interpolation{};
};
struct Track { std::wstring name; std::vector<Key> keys; };
struct Clip {
    std::wstring model;
    std::vector<Track> tracks;
    uint32_t boneKeys=0,morphKeys=0,cameraKeys=0,lightKeys=0,ikKeys=0,lastFrame=0;
    size_t bytes=0;
};
bool Parse(std::span<const uint8_t> bytes,Clip& out,std::string& error,HANDLE stop=nullptr);
bool Load(const std::filesystem::path& path,Clip& out,std::string& error,HANDLE stop=nullptr);
int FindTrack(const Clip& clip,std::wstring_view name);
Quaternion Normalize(Quaternion value);
Quaternion Multiply(const Quaternion& a,const Quaternion& b);
Quaternion Inverse(const Quaternion& q);
Quaternion Slerp(Quaternion a,Quaternion b,float t);
float Bezier(const std::array<uint8_t,64>& interpolation,unsigned channel,float fraction);
Quaternion Sample(const Track& track,double frame);
Vector3 SamplePosition(const Track& track,double frame);
}
