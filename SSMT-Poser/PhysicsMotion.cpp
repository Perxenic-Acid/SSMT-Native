#include "PhysicsMotion.h"
#include "BasisRetarget.h"

namespace poser::physics {
namespace {
Quaternion ModelRotation(Quaternion q) {return {-q.x,q.y,-q.z,q.w};}
bool Toggle(const vmd::Clip& clip,int index,double frame) {
    if(index<0)return true;
    const auto& keys=clip.ikTracks[size_t(index)].keys;
    const auto next=std::upper_bound(keys.begin(),keys.end(),frame,[](double f,const vmd::IkToggle& key){return f<key.frame;});
    return next==keys.begin()||std::prev(next)->enabled;
}
Vector3 RootMotion(const Motion& model,double frame) {
    const auto sample=[&](int track) {
        if(track<0)return ik::ControllerPose{};
        return ik::ControllerPose{ik::ModelPosition(vmd::SamplePosition(model.clip->tracks[track],frame)),ModelRotation(vmd::Sample(model.clip->tracks[track],frame))};
    };
    const auto all=sample(model.allTrack),center=sample(model.centerTrack),groove=sample(model.grooveTrack);
    return ik::Scale(ik::Add(all.position,ik::Rotate(all.rotation,ik::Add(center.position,ik::Rotate(center.rotation,groove.position)))),model.positionScale);
}
void Propagate(const Motion& model,unsigned index,Quaternion rotation,std::span<physics::Pose> poses,
    const std::array<Quaternion,MaxBones>& locals) {
    const auto& profile=*model.profile;
    std::array<bool,MaxBones> changed{};changed[index]=true;poses[index].rotation=rotation;
    for(unsigned ordinal=0;ordinal<profile.boneCount;++ordinal) {
        const auto i=profile.order[ordinal];const auto& node=profile.bones[i];
        if(i==index||!node.valid||node.parent<0||!changed[node.parent])continue;
        poses[i].position=ik::Add(poses[node.parent].position,ik::Rotate(poses[node.parent].rotation,node.localPosition));
        poses[i].rotation=vmd::Multiply(poses[node.parent].rotation,locals[i]);changed[i]=true;
    }
}
}
void StaticJointFollowers(std::span<const InputBone> bones,std::span<int> out) {
    std::fill(out.begin(),out.end(),-1);if(out.size()<bones.size())return;
    for(unsigned i=0;i<bones.size();++i) {
        const auto& bone=bones[i];if(!bone.valid||!bone.skinned||bone.controlled||bone.parent<0||Classify(bone.name)!=Kind::None)continue;
        if(std::any_of(bones.begin(),bones.end(),[&](const InputBone& child){return child.valid&&child.parent==int(i);}))continue;
        int match=-1;bool ambiguous=false;
        for(unsigned other=0;other<bones.size();++other) {
            const auto& joint=bones[other];if(other==i||!joint.valid||!joint.controlled||joint.parent!=bone.parent)continue;
            if(ik::Length(ik::Sub(joint.rest.position,bone.rest.position))>.0002f)continue;
            if(match>=0){ambiguous=true;break;}match=int(other);
        }
        if(!ambiguous)out[i]=match;
    }
}
bool SampleMotion(void* context,double seconds,std::span<physics::Pose> poses) {
    const auto& model=*static_cast<const Motion*>(context);
    if(!model.profile||!model.clip||model.motionRoot<0||poses.size()!=model.profile->boneCount)return false;
    const auto& profile=*model.profile;
    const auto frame=std::min(double(model.clip->lastFrame),seconds*30);const auto blend=std::min(1.0f,float(seconds/.35));
    std::array<Quaternion,128> worlds{};
    for(unsigned i=0;i<model.sourceCount;++i) {
        const auto& source=model.sources[i];const auto input=source.track>=0?ModelRotation(vmd::Sample(model.clip->tracks[source.track],frame)):Quaternion{0,0,0,1};
        worlds[i]=source.parent>=0?vmd::Multiply(worlds[source.parent],input):input;
    }
    for(unsigned ordinal=0;ordinal<profile.boneCount;++ordinal) {
        const auto i=profile.order[ordinal];const auto& node=profile.bones[i];const auto& bone=profile.bones[i];if(!node.valid)continue;
        if(int(i)==model.actorRoot){poses[i]={};continue;}
        if(node.parent<0||!profile.bones[node.parent].valid)return false;
        poses[i].position=ik::Add(poses[node.parent].position,ik::Rotate(poses[node.parent].rotation,node.localPosition));
        if(model.sourceForBone[i]>=0)
            poses[i].rotation=vmd::Slerp(bone.rest.rotation,vmd::Multiply(worlds[model.sourceForBone[i]],bone.rest.rotation),blend);
        else
            // 父姿态已经混合；静态局部旋转直接继承，逐层再混合会产生 t² / t³ 的延迟和蒙皮扭曲。
            poses[i].rotation=vmd::Multiply(poses[node.parent].rotation,node.localRotation);
        if(int(i)==model.motionRoot) {
            const auto allRotation=model.allTrack>=0?ModelRotation(vmd::Sample(model.clip->tracks[model.allTrack],frame)):Quaternion{0,0,0,1};
            poses[i].position=ik::Add(ik::Rotate(allRotation,bone.rest.position),RootMotion(model,frame));
        }
    }
    const auto followers=[&] {
        for(unsigned i=0;i<profile.boneCount;++i)if(model.jointFollower[i]>=0) {
            const auto followed=unsigned(model.jointFollower[i]);
            poses[i].rotation=vmd::Multiply(vmd::Multiply(poses[followed].rotation,vmd::Inverse(profile.bones[followed].rest.rotation)),profile.bones[i].rest.rotation);
        }
    };
    if(!model.footIK){followers();return true;}
    std::array<ik::ControllerPose,2> controllers{};
    std::array<Vector3,2> goals{};
    std::array<ik::ReachConstraint,2> reach{};
    const auto sample=[&](int track,float scale) {
        if(track<0)return ik::ControllerPose{};
        return ik::ControllerPose{ik::Scale(ik::ModelPosition(vmd::SamplePosition(model.clip->tracks[track],frame)),scale),ModelRotation(vmd::Sample(model.clip->tracks[track],frame))};
    };
    for(unsigned side=0;side<2;++side) {
        const auto& leg=model.legs[side];if(!leg.chainReady||leg.track<0||!leg.hingeReady)return false;
        const auto parent=sample(leg.parentTrack,model.ikScale);if(basis::Angle(parent.rotation,{0,0,0,1})>.001f)return false;
        const auto& thigh=profile.bones[leg.thigh].rest;const auto& calf=profile.bones[leg.calf].rest;const auto& foot=profile.bones[leg.foot].rest;
        controllers[side]=ik::FootController(foot.position,{},sample(model.allTrack,model.positionScale),parent,sample(leg.track,model.ikScale));
        goals[side]=ik::Add(poses[leg.foot].position,ik::Scale(ik::Sub(controllers[side].position,poses[leg.foot].position),blend));
        reach[side]={poses[leg.thigh].position,goals[side],ik::Length(ik::Sub(calf.position,thigh.position)),
            ik::Length(ik::Sub(foot.position,calf.position)),Toggle(*model.clip,leg.toggleTrack,frame)};
    }
    ik::RootReachSolution rootReach;if(!ik::SolveRootReach(reach,rootReach))return false;
    std::array<bool,MaxBones> shifted{};
    for(unsigned ordinal=0;ordinal<profile.boneCount;++ordinal) {
        const auto i=profile.order[ordinal];const auto& node=profile.bones[i];
        shifted[i]=int(i)==model.motionRoot||(node.parent>=0&&shifted[node.parent]);
        if(node.valid&&shifted[i])poses[i].position=ik::Add(poses[i].position,rootReach.offset);
    }
    std::array<Quaternion,MaxBones> locals{};
    for(unsigned i=0;i<profile.boneCount;++i)if(profile.bones[i].valid) {
        const auto parent=profile.bones[i].parent;
        locals[i]=parent>=0?vmd::Multiply(vmd::Inverse(poses[parent].rotation),poses[i].rotation):poses[i].rotation;
    }
    for(unsigned side=0;side<2;++side) {
        const auto& leg=model.legs[side];
        const auto& restThigh=profile.bones[leg.thigh].rest;const auto& restCalf=profile.bones[leg.calf].rest;const auto& restFoot=profile.bones[leg.foot].rest;
        const auto& controller=controllers[side];
        auto footRotation=poses[leg.foot].rotation;
        if(Toggle(*model.clip,leg.toggleTrack,frame)) {
            ik::HingeSolution solution;
            if(!ik::SolveHinge(poses[leg.thigh].position,goals[side],vmd::Multiply(poses[leg.thigh].rotation,vmd::Inverse(restThigh.rotation)),
                ik::Sub(restCalf.position,restThigh.position),ik::Sub(restFoot.position,restCalf.position),leg.hinge,solution))return false;
            Propagate(model,unsigned(leg.thigh),vmd::Multiply(solution.thighDelta,restThigh.rotation),poses,locals);
            const auto calfDelta=vmd::Multiply(solution.thighDelta,solution.kneeDelta);
            Propagate(model,unsigned(leg.calf),vmd::Multiply(calfDelta,restCalf.rotation),poses,locals);
            const auto ankleTrack=leg.ankleTrack;
            footRotation=vmd::Multiply(calfDelta,vmd::Multiply(vmd::Slerp({0,0,0,1},sample(ankleTrack,model.ikScale).rotation,blend),restFoot.rotation));
        }
        if(leg.toe>=0&&leg.toeTrack>=0&&(leg.toeToggleTrack<0||vmd::SampleIkEnabled(*model.clip,model.clip->ikTracks[leg.toeToggleTrack].name,frame))) {
            const auto restVector=ik::Sub(profile.bones[leg.toe].rest.position,restFoot.position);
            const auto toe=ik::ToeController(controller,restFoot.position,profile.bones[leg.toe].rest.position,sample(leg.toeTrack,model.ikScale).position);
            const auto current=ik::Rotate(vmd::Multiply(footRotation,vmd::Inverse(restFoot.rotation)),restVector),wanted=ik::Sub(toe,poses[leg.foot].position);
            if(ik::Length(wanted)>1e-6f&&ik::Length(current)>1e-6f)footRotation=vmd::Multiply(vmd::Slerp({0,0,0,1},ik::FromTo(current,wanted),blend),footRotation);
        }
        Propagate(model,unsigned(leg.foot),footRotation,poses,locals);
    }followers();return true;
}
}
