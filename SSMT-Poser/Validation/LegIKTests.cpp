#include "LegIK.h"
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace poser;
namespace {
constexpr float Pi=3.14159265358979323846f;
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void Near(Vector3 a,Vector3 b,float epsilon=0.00002f) {
    Check(ik::Length(ik::Sub(a,b))<epsilon,"position mismatch");
}
Quaternion ModelRotation(Quaternion q) {return {-q.x,q.y,-q.z,q.w};}
ik::ControllerPose TrackPose(const vmd::Clip& clip,const wchar_t* name,double frame,float scale) {
    const auto index=vmd::FindTrack(clip,name);
    if(index<0)return {};
    return {ik::Scale(ik::ModelPosition(vmd::SamplePosition(clip.tracks[size_t(index)],frame)),scale),
        ModelRotation(vmd::Sample(clip.tracks[size_t(index)],frame))};
}
struct LegFixture {Vector3 hip,knee,foot,toe;};
// native-live-71 的 Body inverse bind matrix 独立求逆后的静态几何；不含 live Transform。
const LegFixture Legs[]={
    {{-.0760919875f,.8742456285f,.0385008235f},{-.0513505266f,.4798935999f,.0233096298f},
     {-.0246901974f,.0684419356f,-.0110251100f},{-.0350120514f,-.0010385466f,.0616211817f}},
    {{.0760919893f,.8742456157f,.0385010343f},{.0513506526f,.4798937625f,.0233097752f},
     {.0246904766f,.0684419347f,-.0110250406f},{.0350121361f,-.0010385466f,.0616212886f}}
};
void ControllerTests() {
    const auto yaw=ik::AxisRotation({0,1,0},Pi/2);
    const auto initial=ik::FootController({1,0,2},{1,0,0},{},{{.5f,0,0},yaw},{{0,0,1},yaw});
    // 已知親枢轴 (1,0,0)：(0,0,3) 经 +90Y 变为 (3,0,0)。
    Near(initial.position,{4.5f,0,0});
    const auto root=ik::FootController({1,0,2},{1,0,0},{{10,0,20},yaw},{{.5f,0,0},yaw},{{0,0,1},yaw});
    Near(root.position,{10,0,15.5f});
    const auto rest=ik::FootController({1,0,2},{},{},{},{});
    const auto opening=ik::FootController({1,0,2},{},{},{},{{.5f,.1f,-.3f},yaw});
    Near(opening.position,{1.5f,.1f,1.7f});
    Check(ik::Length(ik::Sub(opening.position,rest.position))>.5f,"opening key was subtracted");
    // controller 旋转不改变自身位置；只影响子控制器的静态偏移和局部平移。
    const auto toe=ik::ToeController(opening,{1,0,2},{1,0,3},{0,0,.5f});
    Near(toe,{3,.1f,1.7f});
    const auto rootToe=ik::ToeController(root,{1,0,2},{1,0,3},{});
    Near(rootToe,{9,0,15.5f});
    for(const Vector3 rootLocal: {Vector3{0,.875f,0},Vector3{-.15f,.2f,.3f},Vector3{.3f,1.4f,-.2f}}) {
        float scale=0;
        Check(ik::PlacementScale({10,20,30},yaw,ik::Add(Vector3{10,20,30},ik::Rotate(yaw,ik::Scale(rootLocal,1.25f))),rootLocal,scale),
            "placement scale rejected a different native posture");
        Check(std::fabs(scale-1.25f)<.00002f,"native posture changed model target scale");
    }
    float scale=0;
    Check(!ik::PlacementScale({},{0,0,0,1},{2,1,1},{1,1,1},scale),"nonuniform placement was treated as a pose reference");
}
void HingeTests() {
    ik::HingeReference hinge;
    Check(ik::Hinge({0,-1,0},{0,-1,0},hinge),"standard straight knee rejected");
    for(int heading=0;heading<=360;++heading) {
        const auto yaw=ik::AxisRotation({0,1,0},heading*Pi/180);
        ik::HingeSolution s;
        const auto goal=ik::Rotate(yaw,{0,-1,0});
        Check(ik::SolveHinge({},goal,yaw,{0,-1,0},{0,-1,0},hinge,s),"turn solve failed");
        Near(s.ankle,goal);
        // 等长腿终点距离为 1 时膝盖屈曲为 120°，与角色世界朝向无关。
        Check(std::fabs(s.kneeAngle-2*Pi/3)<0.00001f,"knee flex changed with heading");
        Near(ik::Rotate(vmd::Multiply(vmd::Inverse(s.thighDelta),
            vmd::Multiply(s.thighDelta,s.kneeDelta)),{1,0,0}),{1,0,0});
        const auto localKnee=ik::Rotate(vmd::Inverse(yaw),s.knee);
        Near(localKnee,{0,-.5f,.866025404f});
    }
    for(const auto& fixture:Legs) {
        const auto upper=ik::Sub(fixture.knee,fixture.hip),lower=ik::Sub(fixture.foot,fixture.knee);
        Check(ik::Hinge(upper,lower,hinge),"static avatar hinge rejected");
        for(int heading=0;heading<=720;++heading) {
            ik::HingeSolution s;
            const auto yaw=ik::AxisRotation({0,1,0},heading*Pi/180);
            const auto goal=ik::Add(fixture.hip,{.03f,-.65f,.08f});
            Check(ik::SolveHinge(fixture.hip,goal,yaw,upper,lower,hinge,s),"body turn with planted foot failed");
            Near(s.ankle,goal);
            Check(ik::Length(ik::Cross({s.kneeDelta.x,s.kneeDelta.y,s.kneeDelta.z},hinge.axis))<.00001f,
                "calf acquired forbidden local swing");
            Check(s.kneeAngle+hinge.restAngle>=0,"knee bent backwards");
            Check(std::fabs(ik::Length(ik::Sub(s.knee,fixture.hip))-ik::Length(upper))<.00001f,"upper leg stretched");
        }
        ik::HingeSolution s;
        Check(ik::SolveHinge(fixture.hip,ik::Add(fixture.hip,{0,-4,0}),{0,0,0,1},upper,lower,hinge,s)&&s.clamped,
            "unreachable target stretched leg");
        Check(!ik::SolveHinge(fixture.hip,fixture.hip,{0,0,0,1},upper,lower,hinge,s),"zero target accepted");
        Check(!ik::SolveHinge(fixture.hip,fixture.foot,{0,0,0,0},upper,lower,hinge,s),"invalid rotation accepted");
        Check(!ik::SolveHinge(fixture.hip,{0,std::numeric_limits<float>::quiet_NaN(),0},{0,0,0,1},upper,lower,hinge,s),"NaN accepted");
    }
}
void RootReachTests() {
    std::array<ik::ReachConstraint,2> legs={{{{-.1f,1.5f,0},{-.5f,0,0},.5f,.5f,true},
        {{.1f,1.5f,0},{.5f,0,0},.5f,.5f,true}}};
    ik::RootReachSolution result;
    Check(ik::SolveRootReach(legs,result)&&result.feasible,"bilateral reachable intersection rejected");
    const auto radius=1-.5f*.0001f;
    Near(result.offset,{0,-1.5f+std::sqrt(radius*radius-.16f),0});
    for(const auto& leg:legs)Check(ik::Length(ik::Sub(leg.goal,ik::Add(leg.hip,result.offset)))<=radius+.000002f,"adapted hip cannot reach foot");
    const auto expected=result.offset;
    std::swap(legs[0],legs[1]);Check(ik::SolveRootReach(legs,result),"swapped legs rejected");Near(result.offset,expected);
    const auto yaw=ik::AxisRotation({0,1,0},1.234f);const Vector3 placement{23,230,-31};
    for(auto& leg:legs) {leg.hip=ik::Add(placement,ik::Rotate(yaw,leg.hip));leg.goal=ik::Add(placement,ik::Rotate(yaw,leg.goal));}
    Check(ik::SolveRootReach(legs,result)&&result.feasible,"world placement changed reach feasibility");Near(result.offset,ik::Rotate(yaw,expected));
    legs={{{{0,1.5f,0},{0,0,0},.5f,.5f,true},{{0,1.5f,0},{4,0,0},.5f,.5f,false}}};
    Check(ik::SolveRootReach(legs,result)&&result.feasible,"disabled foot constrained pelvis");Near(result.offset,{0,-.5f-.5f*.0001f,0});
    legs[0].enabled=false;Check(ik::SolveRootReach(legs,result)&&result.feasible,"disabled IK root adaptation rejected");Near(result.offset,{});
    legs={{{{-.1f,.5f,0},{-.1f,0,0},.5f,.5f,true},{{.1f,.5f,0},{.1f,0,0},.5f,.5f,true}}};
    Check(ik::SolveRootReach(legs,result),"feasible native motion rejected");Near(result.offset,{});
    legs={{{{0,0,0},{-1.5f,0,0},.5f,.5f,true},{{0,0,0},{1.5f,0,0},.5f,.5f,true}}};
    Check(ik::SolveRootReach(legs,result)&&!result.feasible&&result.residual>.5f,"disjoint feet reported as feasible");Near(result.offset,{});
    legs[0].upper=std::numeric_limits<float>::quiet_NaN();Check(!ik::SolveRootReach(legs,result),"invalid static reach accepted");
}
void MotionTests(const std::filesystem::path& path) {
    vmd::Clip clip;std::string error;
    Check(vmd::Load(path,clip,error),error.c_str());
    unsigned solves=0,clamped=0,adapted=0,oldUnreachable=0;float maxResidual=0,maxGoalResidual=0,maxRootOffset=0;
    for(unsigned side=0;side<2;++side) {
        const auto& leg=Legs[side];const auto upper=ik::Sub(leg.knee,leg.hip),lower=ik::Sub(leg.foot,leg.knee);
        ik::HingeReference hinge;Check(ik::Hinge(upper,lower,hinge),"fixture hinge failed");
        for(unsigned frame=0;frame<=clip.lastFrame;++frame) {
            const auto all=TrackPose(clip,L"全ての親",frame,.1f);
            const auto parent=TrackPose(clip,side?L"右足IK親":L"左足IK親",frame,.1f);
            const auto foot=TrackPose(clip,side?L"右足ＩＫ":L"左足ＩＫ",frame,.1f);
            const auto controller=ik::FootController(leg.foot,{},all,parent,foot);
            const auto center=TrackPose(clip,L"センター",frame,.1f),groove=TrackPose(clip,L"グルーブ",frame,.1f);
            const auto lowerBody=TrackPose(clip,L"下半身",frame,.1f);
            const auto waist=TrackPose(clip,L"腰",frame,.1f);
            const auto thigh=TrackPose(clip,side?L"右足":L"左足",frame,.1f);
            const auto body=vmd::Multiply(all.rotation,vmd::Multiply(center.rotation,
                vmd::Multiply(groove.rotation,vmd::Multiply(waist.rotation,lowerBody.rotation))));
            const auto rootPosition=ik::Add(all.position,ik::Rotate(all.rotation,
                ik::Add(center.position,ik::Rotate(center.rotation,groove.position))));
            const Vector3 pelvis{0,.875f,0};
            const auto hip=ik::Add(rootPosition,ik::Add(pelvis,ik::Rotate(body,ik::Sub(leg.hip,pelvis))));
            // 两侧必须共同适配，同一身体位移不能在左腿/右腿依次解算时互相推翻。
            std::array<ik::ReachConstraint,2> constraints{};
            for(unsigned other=0;other<2;++other) {
                const auto& geometry=Legs[other];
                const auto otherHip=ik::Add(rootPosition,ik::Add(pelvis,ik::Rotate(body,ik::Sub(geometry.hip,pelvis))));
                const auto otherGoal=ik::FootController(geometry.foot,{},all,
                    TrackPose(clip,other?L"右足IK親":L"左足IK親",frame,.1f),TrackPose(clip,other?L"右足ＩＫ":L"左足ＩＫ",frame,.1f)).position;
                constraints[other]={otherHip,otherGoal,ik::Length(ik::Sub(geometry.knee,geometry.hip)),
                    ik::Length(ik::Sub(geometry.foot,geometry.knee)),vmd::SampleIkEnabled(clip,other?L"右足ＩＫ":L"左足ＩＫ",frame)};
            }
            const auto oldReach=ik::Length(ik::Sub(controller.position,hip));oldUnreachable+=oldReach>ik::Length(upper)+ik::Length(lower);
            ik::RootReachSolution rootReach;Check(ik::SolveRootReach(constraints,rootReach),"VMD root reach failed");
            adapted+=ik::Length(rootReach.offset)>.000001f;maxRootOffset=std::max(maxRootOffset,ik::Length(rootReach.offset));
            const auto correctedHip=ik::Add(hip,rootReach.offset);
            ik::HingeSolution s;
            Check(ik::SolveHinge(correctedHip,controller.position,vmd::Multiply(body,thigh.rotation),upper,lower,hinge,s),"VMD frame solve failed");
            const auto reach=ik::Length(ik::Sub(controller.position,correctedHip));
            const auto minReach=std::fabs(ik::Length(upper)-ik::Length(lower)),maxReach=ik::Length(upper)+ik::Length(lower);
            if(reach>minReach+.0001f&&reach<maxReach-.0001f) {
                const auto residual=ik::Length(ik::Sub(s.ankle,controller.position));
                Check(residual<.00002f,"reachable VMD target missed");maxResidual=std::max(maxResidual,residual);
            }
            const auto goalResidual=ik::Length(ik::Sub(s.ankle,controller.position));maxGoalResidual=std::max(maxGoalResidual,goalResidual);
            if(rootReach.feasible)Check(goalResidual<.00002f,"adapted VMD foot still misses original target");
            const auto toe=ik::ToeController(controller,leg.foot,leg.toe,
                TrackPose(clip,side?L"右つま先ＩＫ":L"左つま先ＩＫ",frame,.1f).position);
            const auto calf=vmd::Multiply(s.thighDelta,s.kneeDelta);
            const auto oldToe=ik::Rotate(calf,ik::Sub(leg.toe,leg.foot));
            const auto wantedToe=ik::Sub(toe,s.ankle);
            if(ik::Length(wantedToe)>1e-6f)Near(ik::Unit(ik::Rotate(ik::FromTo(oldToe,wantedToe),oldToe)),ik::Unit(wantedToe));
            Check(ik::Length(ik::Cross({s.kneeDelta.x,s.kneeDelta.y,s.kneeDelta.z},hinge.axis))<.00001f,"VMD knee twisted");
            ++solves;clamped+=s.clamped;
        }
    }
    std::cout<<"VMD frames="<<clip.lastFrame+1<<" leg_solves="<<solves<<" old_unreachable="<<oldUnreachable<<" adapted="<<adapted
        <<" clamped="<<clamped<<" max_root_offset="<<maxRootOffset<<" max_reachable_residual="<<maxResidual<<" max_original_goal_residual="<<maxGoalResidual<<'\n';
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        ControllerTests();HingeTests();RootReachTests();
        if(argc==2)MotionTests(argv[1]);
        std::cout<<"MMD leg IK validation PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
