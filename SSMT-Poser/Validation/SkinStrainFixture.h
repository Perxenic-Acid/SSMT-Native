#pragma once
#include "SecondaryPhysics.h"
#include <algorithm>
#include <iostream>

namespace poser::validation {
// 测量最终蒙皮的源网格边，不用骨段长度或两块代理的质心距离替代布面拉伸。
class SkinStrainFixture {
    struct Edge {unsigned a,b;float length;};
    std::vector<Edge> edges;
    size_t pointCount=0;
public:
    float maximumRatio=1,minimumRatio=1;
    unsigned worstTick=0;
    unsigned worstA=0,worstB=0;
    float worstRestLength=0,worstLength=0;
    uint64_t samplesAbove20Percent=0;
    bool Initialize(std::span<const std::array<uint32_t,3>> faces,std::span<const uint8_t> physical,std::span<const Vector3> rest) {
        *this={};
        if(rest.size()!=physical.size()||rest.size()>200000||faces.size()>400000)return false;
        for(const auto point:rest)if(!ik::Finite(point))return false;
        std::vector<uint64_t> keys;keys.reserve(faces.size()*3);
        for(const auto& face:faces) {
            for(auto v:face)if(v>=rest.size())return false;
            for(unsigned k=0;k<3;++k) {
                const auto a=std::min(face[k],face[(k+1)%3]),b=std::max(face[k],face[(k+1)%3]);
                if(physical[a]||physical[b])keys.push_back((uint64_t(a)<<32)|b);
            }
        }
        std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
        for(const auto key:keys) {
            const auto a=unsigned(key>>32),b=unsigned(key&0xffffffff);
            const auto length=ik::Length(ik::Sub(rest[a],rest[b]));
            // 低于 0.5 mm 的重合 / 退化边不适合作为相对应变分母。
            if(length>=.0005f)edges.push_back({a,b,length});
        }
        pointCount=rest.size();return true;
    }
    bool Observe(std::span<const Vector3> points,unsigned tick) {
        if(points.size()!=pointCount)return false;
        for(const auto point:points)if(!ik::Finite(point))return false;
        for(const auto& edge:edges) {
            const auto length=ik::Length(ik::Sub(points[edge.a],points[edge.b]));const auto ratio=length/edge.length;
            if(ratio>maximumRatio){maximumRatio=ratio;worstTick=tick;worstA=edge.a;worstB=edge.b;worstRestLength=edge.length;worstLength=length;}
            minimumRatio=std::min(minimumRatio,ratio);samplesAbove20Percent+=ratio>1.2f;
        }
        return true;
    }
    size_t EdgeCount()const{return edges.size();}
    void Report()const {
        if(!pointCount)return;
        std::cout<<"Actual skin edges count="<<edges.size()<<" max_length_ratio="<<maximumRatio<<" min_length_ratio="<<minimumRatio
            <<" samples_above_20_percent="<<samplesAbove20Percent<<" tick="<<worstTick
            <<" edge="<<worstA<<','<<worstB<<" rest_length="<<worstRestLength<<" length="<<worstLength<<'\n';
    }
};
}
