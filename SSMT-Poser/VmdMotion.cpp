#include "VmdMotion.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace poser::vmd {
namespace {
constexpr size_t MaxFile=256*1024*1024,MaxKeys=1000000,MaxTracks=4096;
struct Reader {
    std::span<const uint8_t> data; size_t cursor=0;
    size_t Remaining() const { return data.size()-cursor; }
    std::span<const uint8_t> Take(size_t size) {
        if (size>Remaining()) throw std::runtime_error("VMD_TRUNCATED");
        const auto result=data.subspan(cursor,size); cursor+=size; return result;
    }
    template<class T> T Value() { T value{}; const auto bytes=Take(sizeof(T)); std::memcpy(&value,bytes.data(),sizeof(T)); return value; }
    std::wstring Text(size_t width) {
        const auto bytes=Take(width);
        size_t length=0; while (length<width && bytes[length]) ++length;
        if (!length) return {};
        const auto count=MultiByteToWideChar(932,0,reinterpret_cast<const char*>(bytes.data()),int(length),nullptr,0);
        if (!count) throw std::runtime_error("VMD_NAME_ENCODING");
        std::wstring text(count,L'\0');
        MultiByteToWideChar(932,0,reinterpret_cast<const char*>(bytes.data()),int(length),text.data(),count);
        return text;
    }
    uint32_t Count(size_t recordSize) {
        const auto count=Value<uint32_t>();
        if (count>MaxKeys || count>Remaining()/recordSize) throw std::runtime_error("VMD_RECORD_COUNT");
        return count;
    }
    uint32_t SkipOptional(size_t recordSize) {
        if (!Remaining()) return 0;
        const auto count=Count(recordSize); Take(size_t(count)*recordSize); return count;
    }
};
bool Finite(const Quaternion& q) { return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w); }
}
Quaternion Normalize(Quaternion q) {
    const auto norm=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
    if (!Finite(q) || !std::isfinite(norm) || norm<1e-12 || norm>1e12) return {0,0,0,0};
    const auto scale=float(1/std::sqrt(norm));
    return {q.x*scale,q.y*scale,q.z*scale,q.w*scale};
}
Quaternion Multiply(const Quaternion& a,const Quaternion& b) {
    return Normalize({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
Quaternion Inverse(const Quaternion& q) { return {-q.x,-q.y,-q.z,q.w}; }
Quaternion Slerp(Quaternion a,Quaternion b,float t) {
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if (dot<0) { b={-b.x,-b.y,-b.z,-b.w}; dot=-dot; }
    dot=std::clamp(dot,0.0f,1.0f); t=std::clamp(t,0.0f,1.0f);
    float left=1-t,right=t;
    if (dot<0.9995f) { const auto angle=std::acos(dot),denom=std::sin(angle); left=std::sin((1-t)*angle)/denom; right=std::sin(t*angle)/denom; }
    return Normalize({left*a.x+right*b.x,left*a.y+right*b.y,left*a.z+right*b.z,left*a.w+right*b.w});
}
float Bezier(const std::array<uint8_t,64>& interpolation,unsigned channel,float fraction) {
    if (fraction<=0) return 0; if (fraction>=1) return 1;
    const auto offset=channel*16;
    if (offset>=64) return fraction;
    const auto x1=interpolation[offset]/127.0f,y1=interpolation[offset+4]/127.0f,
        x2=interpolation[offset+8]/127.0f,y2=interpolation[offset+12]/127.0f;
    float low=0,high=1,t=0.5f;
    for (unsigned i=0;i<20;++i) {
        t=(low+high)/2; const auto u=1-t,x=3*u*u*t*x1+3*u*t*t*x2+t*t*t;
        if (x<fraction) low=t; else high=t;
    }
    const auto u=1-t; return 3*u*u*t*y1+3*u*t*t*y2+t*t*t;
}
Quaternion Sample(const Track& track,double frame) {
    if (track.keys.empty()) return {0,0,0,1};
    if (frame<=track.keys.front().frame) return track.keys.front().rotation;
    if (frame>=track.keys.back().frame) return track.keys.back().rotation;
    const auto next=std::upper_bound(track.keys.begin(),track.keys.end(),frame,[](double value,const Key& key){return value<key.frame;});
    const auto& previous=*(next-1);
    const auto fraction=float((frame-previous.frame)/(next->frame-previous.frame));
    // 下一关键帧定义进入该帧的曲线；旋转块为 48/52/56/60，不能使用被部分工具改写的冗余首块。
    return Slerp(previous.rotation,next->rotation,Bezier(next->interpolation,3,fraction));
}
int FindTrack(const Clip& clip,std::wstring_view name) {
    for (size_t i=0;i<clip.tracks.size();++i) if (clip.tracks[i].name==name) return int(i);
    return -1;
}
bool Parse(std::span<const uint8_t> bytes,Clip& out,std::string& error,HANDLE stop) {
    out={}; error.clear();
    try {
        if (bytes.size()>MaxFile) throw std::runtime_error("VMD_FILE_LIMIT");
        Reader reader{bytes};
        const auto signature=reader.Take(30);
        constexpr char Header[]="Vocaloid Motion Data 0002";
        if (std::memcmp(signature.data(),Header,sizeof(Header)-1)) throw std::runtime_error("VMD_HEADER_UNSUPPORTED");
        Clip clip; clip.bytes=bytes.size(); clip.model=reader.Text(20);
        clip.boneKeys=reader.Count(111);
        std::unordered_map<std::wstring,int> tracks;
        for (uint32_t i=0;i<clip.boneKeys;++i) {
            if (!(i%1024) && stop && WaitForSingleObject(stop,0)==WAIT_OBJECT_0) throw std::runtime_error("VMD_CANCELLED");
            const auto name=reader.Text(15);
            if (name.empty()) throw std::runtime_error("VMD_BONE_NAME_EMPTY");
            Key key; key.frame=reader.Value<uint32_t>();
            if (key.frame>1080000) throw std::runtime_error("VMD_FRAME_LIMIT");
            for (auto& value:key.position) { value=reader.Value<float>(); if (!std::isfinite(value)) throw std::runtime_error("VMD_POSITION_NONFINITE"); }
            key.rotation=reader.Value<Quaternion>();
            if (!Finite(key.rotation)) throw std::runtime_error("VMD_ROTATION_NONFINITE");
            // VMD 常见全零 Quaternion 按 identity 解读，与 MMD 的文件兼容行为一致。
            if (!key.rotation.x&&!key.rotation.y&&!key.rotation.z&&!key.rotation.w) key.rotation={0,0,0,1};
            key.rotation=Normalize(key.rotation);
            if (!bone_detail::Valid(key.rotation)) throw std::runtime_error("VMD_ROTATION_INVALID");
            const auto controls=reader.Take(64); std::copy(controls.begin(),controls.end(),key.interpolation.begin());
            for (unsigned channel=0;channel<4;++channel) for (unsigned component : {0u,4u,8u,12u})
                if (key.interpolation[channel*16+component]>127) throw std::runtime_error("VMD_BEZIER_INVALID");
            const auto found=tracks.find(name);
            int track=found==tracks.end()?-1:found->second;
            if (track<0) { if (clip.tracks.size()>=MaxTracks) throw std::runtime_error("VMD_TRACK_LIMIT"); track=int(clip.tracks.size()); clip.tracks.push_back({name,{}}); tracks.emplace(name,track); }
            clip.tracks[track].keys.push_back(key); clip.lastFrame=std::max(clip.lastFrame,key.frame);
        }
        for (auto& track:clip.tracks) {
            if (stop && WaitForSingleObject(stop,0)==WAIT_OBJECT_0) throw std::runtime_error("VMD_CANCELLED");
            std::stable_sort(track.keys.begin(),track.keys.end(),[](const Key& a,const Key& b){return a.frame<b.frame;});
            // 同名同帧使用文件中最后一条；采样区间因此始终有非零长度。
            std::vector<Key> unique; unique.reserve(track.keys.size());
            for (const auto& key:track.keys) { if (!unique.empty()&&unique.back().frame==key.frame) unique.back()=key; else unique.push_back(key); }
            track.keys=std::move(unique);
        }
        clip.morphKeys=reader.SkipOptional(23);
        clip.cameraKeys=reader.SkipOptional(61);
        clip.lightKeys=reader.SkipOptional(28);
        reader.SkipOptional(9); // self shadow。
        if (reader.Remaining()) {
            clip.ikKeys=reader.Count(9);
            for (uint32_t i=0;i<clip.ikKeys;++i) {
                if (!(i%1024) && stop && WaitForSingleObject(stop,0)==WAIT_OBJECT_0) throw std::runtime_error("VMD_CANCELLED");
                reader.Take(5); const auto count=reader.Count(21); reader.Take(size_t(count)*21);
            }
        }
        if (reader.Remaining()) throw std::runtime_error("VMD_TRAILING_DATA");
        out=std::move(clip); return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
bool Load(const std::filesystem::path& path,Clip& out,std::string& error,HANDLE stop) {
    try {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if (!file) throw std::runtime_error("VMD_FILE_UNREADABLE");
        const auto size=file.tellg();
        if (size<0 || size>std::streamoff(MaxFile)) throw std::runtime_error("VMD_FILE_LIMIT");
        std::vector<uint8_t> bytes(static_cast<size_t>(size)); file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("VMD_FILE_READ_FAILED");
        return Parse(bytes,out,error,stop);
    } catch (const std::exception& e) { error=e.what(); return false; }
}
}
