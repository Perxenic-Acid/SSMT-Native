#pragma once
#include "SecondaryPhysics.h"

namespace poser::physics {
// FK / 足 IK / 身体可达适配语义改变时递增，拒绝复用旧驱动生成的烘焙。
inline constexpr uint32_t MotionDriverVersion=7;
struct Source { int parent=-1,track=-1; };
struct Leg {
    int thigh=-1,calf=-1,foot=-1,toe=-1,track=-1,parentTrack=-1,toggleTrack=-1;
    int toeTrack=-1,toeToggleTrack=-1,ankleTrack=-1;
    ik::HingeReference hinge;
    bool chainReady=false,hingeReady=false;
};
// 离线物理驱动只引用不可变 VMD 与静态 profile，不含 managed/native 对象地址。
struct Motion {
    const Profile* profile=nullptr;
    const vmd::Clip* clip=nullptr;
    int actorRoot=-1,motionRoot=-1,allTrack=-1,centerTrack=-1,grooveTrack=-1;
    unsigned sourceCount=0;
    std::array<Source,128> sources{};
    std::array<int,MaxBones> sourceForBone=[] {std::array<int,MaxBones> values;values.fill(-1);return values;}();
    std::array<int,MaxBones> jointFollower=[] {std::array<int,MaxBones> values;values.fill(-1);return values;}();
    std::array<Leg,2> legs{};
    float positionScale=.1f,ikScale=.1f;
    bool footIK=false;
};
bool SampleMotion(void* context,double seconds,std::span<Pose> poses);
// 同父级、同静态关节枢轴的蒙皮叶骨跟随身体关节；不靠角色 / 膝辅助骨名称识别。
void StaticJointFollowers(std::span<const InputBone> bones,std::span<int> out);
}
