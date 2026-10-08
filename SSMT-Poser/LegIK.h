#pragma once
#include "BoneWriteProbe.h"
#include "VmdMotion.h"
#include <algorithm>
#include <cmath>

namespace poser::ik {
inline Vector3 Add(Vector3 a,Vector3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector3 Sub(Vector3 a,Vector3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector3 Scale(Vector3 a,float s) {return {a.x*s,a.y*s,a.z*s};}
inline float Dot(Vector3 a,Vector3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vector3 Cross(Vector3 a,Vector3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Length(Vector3 a) {return std::sqrt(Dot(a,a));}
inline bool Finite(Vector3 a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vector3 Unit(Vector3 a) {const auto n=Length(a);return n>1e-7f?Scale(a,1/n):Vector3{};}
inline Vector3 Rotate(Quaternion q,Vector3 p) {const auto t=Scale(Cross({q.x,q.y,q.z},p),2);return Add(p,Add(Scale(t,q.w),Cross({q.x,q.y,q.z},t)));}
inline Quaternion FromTo(Vector3 from,Vector3 to) {
    from=Unit(from);to=Unit(to);const auto d=std::clamp(Dot(from,to),-1.0f,1.0f);
    auto axis=Cross(from,to);const auto sine=Length(axis);
    if (sine<1e-7f) {
        if (d>=0) return {0,0,0,1};
        axis=Unit(Cross(from,std::fabs(from.x)<0.8f?Vector3{1,0,0}:Vector3{0,1,0}));return {axis.x,axis.y,axis.z,0};
    }
    // dot 在小角度时会舍入为 1；保留 cross 的精度，避免把毫米级修正误判为无旋转。
    const auto half=std::atan2(sine,d)*0.5f;axis=Scale(axis,std::sin(half)/sine);
    return vmd::Normalize({axis.x,axis.y,axis.z,std::cos(half)});
}
struct Solution {Vector3 knee,ankle;bool clamped=false;};
inline bool Solve(Vector3 hip,Vector3 goal,Vector3 pole,float upper,float lower,Solution& out) {
    if (!Finite(hip)||!Finite(goal)||!Finite(pole)||!std::isfinite(upper)||!std::isfinite(lower)||upper<1e-5f||lower<1e-5f) return false;
    const auto delta=Sub(goal,hip);const auto distance=Length(delta);if (distance<1e-7f) return false;
    const auto direction=Scale(delta,1/distance);
    const auto epsilon=std::min(upper,lower)*0.0001f;
    const auto reach=std::clamp(distance,std::fabs(upper-lower)+epsilon,upper+lower-epsilon);
    auto bend=Sub(pole,Scale(direction,Dot(pole,direction)));
    if (Length(bend)<1e-6f) bend=Cross(direction,std::fabs(direction.x)<0.8f?Vector3{1,0,0}:Vector3{0,1,0});
    bend=Unit(bend);
    const auto along=(upper*upper+reach*reach-lower*lower)/(2*reach);
    const auto height=std::sqrt(std::max(0.0f,upper*upper-along*along));
    out={Add(hip,Add(Scale(direction,along),Scale(bend,height))),Add(hip,Scale(direction,reach)),std::fabs(reach-distance)>epsilon};
    return Finite(out.knee)&&Finite(out.ankle);
}
}
