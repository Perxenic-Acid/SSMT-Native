#include "VmdMotion.h"
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace poser;
namespace {
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class T>void Add(std::vector<uint8_t>& bytes,const T& value){const auto p=reinterpret_cast<const uint8_t*>(&value);bytes.insert(bytes.end(),p,p+sizeof(value));}
std::vector<uint8_t> MorphOnly() {
    std::vector<uint8_t> bytes(50);const char* signature="Vocaloid Motion Data 0002";std::memcpy(bytes.data(),signature,std::strlen(signature));
    Add(bytes,uint32_t(0));Add(bytes,uint32_t(4));
    for(const auto key:{vmd::MorphKey{30,.8f},vmd::MorphKey{0,0},vmd::MorphKey{30,1},vmd::MorphKey{60,0}}) {
        char name[15]="blink";const auto p=reinterpret_cast<const uint8_t*>(name);bytes.insert(bytes.end(),p,p+15);Add(bytes,key.frame);Add(bytes,key.weight);
    }return bytes;
}
}
void FaceOracle() {
    auto bytes=MorphOnly();vmd::Clip clip;std::string error;
    Check(vmd::Parse(bytes,clip,error)&&clip.tracks.empty()&&clip.morphKeys==4&&clip.morphTracks.size()==1&&clip.lastFrame==60,"morph-only VMD rejected or lost duration");
    const auto& track=clip.morphTracks[0];Check(track.keys.size()==3&&track.keys[1].weight==1,"duplicate morph frame did not use last record");
    Check(vmd::SampleMorph(track,15)==.5f&&vmd::SampleMorph(track,45)==.5f&&vmd::SampleMorph(track,-1)==0&&vmd::SampleMorph(track,90)==0,"morph sample interpolation/boundaries incorrect");
    const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(bytes.data()+54+4+15+4,&nan,4);
    Check(!vmd::Parse(bytes,clip,error)&&error=="VMD_MORPH_WEIGHT_NONFINITE"&&clip.morphTracks.empty(),"non-finite morph accepted or partial clip published");
    bytes=MorphOnly();bytes.pop_back();Check(!vmd::Parse(bytes,clip,error),"truncated morph VMD accepted");
    bytes=MorphOnly();const uint32_t excessive=1080001;std::memcpy(bytes.data()+54+4+15,&excessive,4);Check(!vmd::Parse(bytes,clip,error)&&error=="VMD_FRAME_LIMIT","morph frame limit bypassed");
    std::cout<<"Face VMD: morph-only, duplicate frames, linear weights, duration, truncation and finite gates PASS\n";
}
void FaceFixture(const wchar_t* path) {
    vmd::Clip clip;std::string error;Check(vmd::Load(path,clip,error)&&!clip.morphTracks.empty(),"face fixture load failed");
    for(const auto& track:clip.morphTracks)for(double frame=0;frame<=clip.lastFrame;frame+=.25)Check(std::isfinite(vmd::SampleMorph(track,frame)),"actual facial track sampled non-finite weight");
    std::cout<<"Face fixture morph_keys="<<clip.morphKeys<<" tracks="<<clip.morphTracks.size()<<" last_frame="<<clip.lastFrame<<" fps=30 samples=FINITE PASS\n";
}
