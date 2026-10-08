#pragma once
#include "LegIK.h"

namespace poser::reference {
// Unity bindposes 为列主序的 mesh→bone 逆矩阵。这里只接受无 shear、正 scale 的仿射矩阵。
struct Matrix { float value[16]; };
struct Pose { Quaternion rotation{0,0,0,1}; Vector3 position; };
inline bool Decode(const Matrix& inverseBind,Pose& out) {
    const auto* m=inverseBind.value;
    for (auto value:inverseBind.value) if (!std::isfinite(value)) return false;
    if (std::fabs(m[3])+std::fabs(m[7])+std::fabs(m[11])+std::fabs(m[15]-1)>0.0001f) return false;
    Vector3 x{m[0],m[1],m[2]},y{m[4],m[5],m[6]},z{m[8],m[9],m[10]};
    const auto sx=ik::Length(x),sy=ik::Length(y),sz=ik::Length(z);
    if (sx<0.00001f||sy<0.00001f||sz<0.00001f||sx>100000||sy>100000||sz>100000) return false;
    x=ik::Scale(x,1/sx);y=ik::Scale(y,1/sy);z=ik::Scale(z,1/sz);
    if (std::fabs(ik::Dot(x,y))+std::fabs(ik::Dot(x,z))+std::fabs(ik::Dot(y,z))>0.002f || ik::Dot(ik::Cross(x,y),z)<0.999f) return false;
    const auto trace=x.x+y.y+z.z;
    Quaternion inverseRotation;
    if (trace>0) {
        const auto s=2*std::sqrt(1+trace);
        inverseRotation={(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,s/4};
    } else if (x.x>y.y&&x.x>z.z) {
        const auto s=2*std::sqrt(1+x.x-y.y-z.z);
        inverseRotation={s/4,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s};
    } else if (y.y>z.z) {
        const auto s=2*std::sqrt(1+y.y-x.x-z.z);
        inverseRotation={(y.x+x.y)/s,s/4,(z.y+y.z)/s,(z.x-x.z)/s};
    } else {
        const auto s=2*std::sqrt(1+z.z-x.x-y.y);
        inverseRotation={(z.x+x.z)/s,(z.y+y.z)/s,s/4,(x.y-y.x)/s};
    }
    out.rotation=vmd::Inverse(vmd::Normalize(inverseRotation));
    // 求 inverse(R*S) 的平移，避免把带 scale 的逆绑定矩阵当成纯旋转矩阵。
    const Vector3 translation{m[12],m[13],m[14]};
    out.position={-ik::Dot(x,translation)/sx,-ik::Dot(y,translation)/sy,-ik::Dot(z,translation)/sz};
    return bone_detail::Valid(out.rotation)&&ik::Finite(out.position);
}
}
