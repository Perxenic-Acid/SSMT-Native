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
inline Vector3 ModelPosition(Vector3 vmd) {return {-vmd.x,vmd.y,-vmd.z};}
// VMD position 是相对静态骨位置的局部偏移；此函数仅合成无旋转父轨道的偏移，
// 不能把结果直接当成脚踝绝对位置，也不能减去动作第 0 帧。
inline bool ModelTarget(Vector3 target,Vector3 parent,Vector3& out) {
    if (!Finite(target)||!Finite(parent)) return false;
    out=Add(ModelPosition(target),ModelPosition(parent));
    return Finite(out);
}
// Derive a stable pole direction from the bind-time three-bone geometry.  A
// nearly collinear chain is rejected instead of inventing a bend direction;
// callers may then keep the write path disabled until a verified pole exists.
inline bool PoleDirection(Vector3 hip,Vector3 knee,Vector3 foot,Vector3& out) {
    if (!Finite(hip)||!Finite(knee)||!Finite(foot)) return false;
    const auto leg=Sub(foot,hip); const auto length=Length(leg);
    if (length<1e-6f) return false;
    const auto direction=Scale(leg,1/length);
    const auto bend=Sub(Sub(knee,hip),Scale(direction,Dot(Sub(knee,hip),direction)));
    if (Length(bend)<1e-6f) return false;
    out=Unit(bend);
    return Finite(out)&&Length(out)>0.5f;
}
// Ground calibration helper. Both inputs must already be in the same world
// space; this only adjusts a candidate and never touches a Unity Transform.
// MODEL_Y_180 uses +Y as up.
inline bool GroundCandidate(Vector3 target,float groundY,float soleOffset,Vector3& out,bool& clamped) {
    if (!Finite(target)||!std::isfinite(groundY)||!std::isfinite(soleOffset)) return false;
    const auto floor=groundY+soleOffset;
    out=target; clamped=target.y<floor;
    if (clamped) out.y=floor;
    return Finite(out);
}
// 旧调查用的相对位移数学，仅保留给本机历史回归；正式 MMD 写入不能使用
// live 脚锚点或 frame0 subtraction，必须走下方的静态 Controller 分支。
inline Vector3 Goal(Vector3 anchorWorld,Quaternion actorRotation,Vector3 rootDelta,
    Vector3 ikDelta,float ikScale) {
    return Add(anchorWorld,Rotate(actorRotation,Add(rootDelta,Scale(ikDelta,ikScale))));
}
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
inline Quaternion AxisRotation(Vector3 axis,float angle) {
    axis=Unit(axis);const auto s=std::sin(angle*0.5f);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(angle*0.5f)};
}
struct ControllerPose { Vector3 position{};Quaternion rotation{0,0,0,1}; };
// 标准腿链的控制器分支为 全ての親 -> 足IK親 -> 足IK -> つま先IK。
// Center/Groove/下半身只驱动身体分支，不能再加到这条目标分支。
// 静态枢轴必须由调用方提供；VMD 没有这些数据，未知枢轴不能猜测。
inline ControllerPose FootController(Vector3 restFoot,Vector3 restParent,
    ControllerPose all,ControllerPose parent,ControllerPose foot) {
    return {Add(all.position,Rotate(all.rotation,Add(Add(restParent,parent.position),
        Rotate(parent.rotation,Add(Sub(restFoot,restParent),foot.position))))),
        vmd::Multiply(all.rotation,vmd::Multiply(parent.rotation,foot.rotation))};
}
inline Vector3 ToeController(ControllerPose foot,Vector3 restFoot,Vector3 restToe,Vector3 offset) {
    return Add(foot.position,Rotate(foot.rotation,Add(Sub(restToe,restFoot),offset)));
}
// Bip001 的直接父级是 Actor。由同一段 local/world 位移测量场景放置的统一缩放，
// 不使用趴伏/待机时的腿长比例去改变两侧目标，姿态变化不会改变这个比值。
inline bool PlacementScale(Vector3 actor,Quaternion rotation,Vector3 rootWorld,Vector3 rootLocal,float& out) {
    if(!Finite(actor)||!Finite(rootWorld)||!Finite(rootLocal))return false;
    const auto localLength=Length(rootLocal);const auto worldDelta=Sub(rootWorld,actor);
    if(!std::isfinite(localLength)||localLength<1e-5f)return false;
    out=Length(worldDelta)/localLength;
    return std::isfinite(out)&&out>=0.001f&&out<=100&&
        Length(Sub(worldDelta,Rotate(rotation,Scale(rootLocal,out))))<0.0001f;
}
struct HingeReference {Vector3 axis{};float restAngle=0;};
inline bool Hinge(Vector3 upper,Vector3 lower,HingeReference& out) {
    if(!Finite(upper)||!Finite(lower)||Length(upper)<1e-5f||Length(lower)<1e-5f)return false;
    const auto cross=Cross(Unit(upper),Unit(lower));
    // MODEL_Y_180 下标准 MMD 膝盖的负 X 屈曲映射为模型 +X。
    // 用目标静态几何校准 hinge，拒绝与标准腿链相反的参考姿态。
    // 标准 MMD 直腿的膝轴由模型 +X 定义，不依赖某一帧恰好存在弯曲平面。
    out.axis=Length(cross)<1e-5f?Unit(Sub(Vector3{1,0,0},Scale(Unit(upper),Unit(upper).x))):Unit(cross);
    out.restAngle=std::atan2(Length(cross),Dot(Unit(upper),Unit(lower)));
    return out.axis.x>0.5f&&out.restAngle<0.5f;
}
struct HingeSolution {
    Quaternion thighDelta{0,0,0,1},kneeDelta{0,0,0,1};
    Vector3 knee{},ankle{};float kneeAngle=0;bool clamped=false;
};
struct ReachConstraint {
    Vector3 hip{},goal{};
    float upper=0,lower=0;
    bool enabled=false;
};
struct RootReachSolution {Vector3 offset{};float residual=0;bool feasible=true;};
// 重定向到不同骨长的角色时，不能只投影脚目标、让身体继续沿不可达轨迹走。
// 两腿共享一个平移量：求原点到两个可达球交集的最近点；骨长、足目标和旋转不变。
// 输入髋位置与骨长来自本帧静态 FK；开关关闭的腿不约束身体。没有可达交集时
// 返回两侧等量超出的折中点，并明确报告 infeasible，后续腿求解仍保留不可达投影。
inline bool SolveRootReach(std::span<const ReachConstraint> legs,RootReachSolution& out) {
    out={};
    std::array<Vector3,2> centers{};std::array<float,2> radii{};unsigned count=0;
    for(const auto& leg:legs)if(leg.enabled) {
        if(count==2||!Finite(leg.hip)||!Finite(leg.goal)||!std::isfinite(leg.upper)||!std::isfinite(leg.lower)||
            leg.upper<1e-5f||leg.lower<1e-5f)return false;
        centers[count]=Sub(leg.goal,leg.hip);
        radii[count]=leg.upper+leg.lower-std::min(leg.upper,leg.lower)*.0001f;
        if(!Finite(centers[count])||!std::isfinite(Length(centers[count]))||!std::isfinite(radii[count]))return false;
        ++count;
    }
    if(!count)return true;
    const auto contains=[&](unsigned i,Vector3 point) {return Length(Sub(point,centers[i]))<=radii[i]+.000001f;};
    const auto project=[&](unsigned i) {
        const auto distance=Length(centers[i]);
        return distance<=radii[i]?Vector3{}:Scale(centers[i],1-radii[i]/distance);
    };
    if(contains(0,{})&&(count==1||contains(1,{})))return true;
    const auto first=project(0);
    if(count==1||contains(1,first))out.offset=first;
    else {
        const auto second=project(1);
        if(contains(0,second))out.offset=second;
        else {
            const auto separation=Sub(centers[1],centers[0]);const auto distance=Length(separation);
            if(!std::isfinite(distance)||distance<1e-7f)return false;
            const auto axis=Scale(separation,1/distance);
            if(distance>radii[0]+radii[1]) {
                out.offset=Add(centers[0],Scale(axis,(distance+radii[0]-radii[1])*.5f));
                out.feasible=false;
            }else {
                const auto along=(radii[0]*radii[0]-radii[1]*radii[1]+distance*distance)/(2*distance);
                const auto circle=Add(centers[0],Scale(axis,along));
                const auto radius=std::sqrt(std::max(0.0f,radii[0]*radii[0]-along*along));
                auto direction=Sub(Scale(axis,Dot(circle,axis)),circle);
                if(Length(direction)<1e-7f)direction=Cross(axis,std::fabs(axis.x)<.8f?Vector3{1,0,0}:Vector3{0,1,0});
                out.offset=Add(circle,Scale(Unit(direction),radius));
            }
        }
    }
    for(unsigned i=0;i<count;++i)out.residual=std::max(out.residual,Length(Sub(out.offset,centers[i]))-radii[i]);
    out.feasible=out.feasible&&out.residual<.00001f;
    return Finite(out.offset)&&std::isfinite(out.residual);
}
// 只允许膝盖在静态局部 hinge 上屈曲。大腿保留 FK 的 twist 后做最短 swing；
// 不能分别对 thigh/calf 做世界 FromTo，那会让 calf 获得额外的 Y/Z 自转。
inline bool SolveHinge(Vector3 hip,Vector3 goal,Quaternion fkThighDelta,Vector3 upper,
    Vector3 lower,const HingeReference& hinge,HingeSolution& out) {
    const auto a=Length(upper),b=Length(lower),distance=Length(Sub(goal,hip));
    const auto rotationNorm=fkThighDelta.x*fkThighDelta.x+fkThighDelta.y*fkThighDelta.y+
        fkThighDelta.z*fkThighDelta.z+fkThighDelta.w*fkThighDelta.w;
    if(!Finite(hip)||!Finite(goal)||!Finite(upper)||!Finite(lower)||!Finite(hinge.axis)||
        !std::isfinite(hinge.restAngle)||!std::isfinite(rotationNorm)||std::fabs(rotationNorm-1)>0.001f||
        std::fabs(Length(hinge.axis)-1)>0.001f||a<1e-5f||b<1e-5f||!std::isfinite(distance)||distance<1e-7f)return false;
    const auto epsilon=std::min(a,b)*0.0001f;
    const auto reach=std::clamp(distance,std::fabs(a-b)+epsilon,a+b-epsilon);
    const auto flex=std::acos(std::clamp((reach*reach-a*a-b*b)/(2*a*b),-1.0f,1.0f));
    out.kneeAngle=flex-hinge.restAngle;
    out.kneeDelta=AxisRotation(hinge.axis,out.kneeAngle);
    const auto localEnd=Add(upper,Rotate(out.kneeDelta,lower));
    const auto swing=FromTo(Rotate(fkThighDelta,localEnd),Sub(goal,hip));
    out.thighDelta=vmd::Multiply(swing,fkThighDelta);
    out.knee=Add(hip,Rotate(out.thighDelta,upper));
    out.ankle=Add(out.knee,Rotate(out.thighDelta,Rotate(out.kneeDelta,lower)));
    out.clamped=std::fabs(reach-distance)>epsilon;
    return Finite(out.knee)&&Finite(out.ankle);
}
struct Solution {Vector3 knee,ankle;bool clamped=false;};
// 通用自由 pole 几何检查，供历史探针使用；MMD 运行路径只使用 SolveHinge。
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
