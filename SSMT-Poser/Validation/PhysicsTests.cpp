#include "SecondaryPhysics.h"
#include "TriangleSurfaceFixture.h"
#include "SkinStrainFixture.h"
#include "PlayerGirlAmiceFixture.h"
#include "OdetteBodyFixture.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <limits>

void PhysicsMotionOracle();
void SkinSurfaceOracle();
void FaceOracle();
void FaceFixture(const wchar_t* path);
void PhysicsMotionFixture(const wchar_t* avatar,const wchar_t* body,const wchar_t* motion,bool reachOnly=false,const wchar_t* nativeSelection=nullptr,const wchar_t* skinPath=nullptr,const wchar_t* posePath=nullptr,bool inspectOnly=false,double diagnosticSeconds=0,bool topologyOnly=false);

using namespace poser;
namespace {
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void SkinStrainOracle() {
    const std::array<Vector3,6> rest{{{0,0,0},{1,0,0},{0,1,0},{3,0,0},{4,0,0},{3,1,0}}};
    const std::array<uint8_t,6> physical{{1,1,1,0,0,0}};
    const std::array<std::array<uint32_t,3>,3> faces{{{0,1,2},{2,1,0},{3,4,5}}};
    validation::SkinStrainFixture strain;Check(strain.Initialize(faces,physical,rest)&&strain.EdgeCount()==3,"cloth edges were duplicated or included body-only edges");
    auto moved=rest;const auto rotation=ik::AxisRotation({0,1,0},1.1f);
    for(auto& point:moved)point=ik::Add(ik::Rotate(rotation,point),{2,3,4});
    Check(strain.Observe(moved,1)&&strain.maximumRatio<1.00001f&&strain.minimumRatio>.99999f,"rigid body motion counted as fabric strain");
    moved=rest;moved[1].x=3;moved[4].x=100;
    Check(strain.Observe(moved,7)&&std::fabs(strain.maximumRatio-3)<1e-6f&&strain.worstTick==7&&strain.samplesAbove20Percent==2,"final cloth stretching was not measured independently of body geometry");
    auto invalid=faces;invalid[0][0]=unsigned(rest.size());
    Check(!strain.Initialize(invalid,physical,rest),"cloth edge topology accepted an out-of-range source index");
}
void AnimatedColliderFrame() {
    physics::Profile profile;profile.boneCount=2;profile.colliderCount=1;
    profile.bones[0].rest={{0,1,0},ik::AxisRotation({0,0,1},.31f)};
    const Vector3 local{0,-.4f,0};
    profile.bones[1].rest={ik::Add(profile.bones[0].rest.position,ik::Rotate(profile.bones[0].rest.rotation,local)),{0,0,0,1}};
    profile.colliders[0]={0,1,.05f};
    const auto rest=physics::BodyColliderPose(profile,0);auto previous=rest.rotation;
    for(unsigned tick=0;tick<=360;++tick) {
        const auto animated=ik::AxisRotation({0,1,0},float(tick)*.02f);
        std::array<physics::Pose,2> drivers;
        for(unsigned bone=0;bone<2;++bone)drivers[bone]={ik::Rotate(animated,profile.bones[bone].rest.position),vmd::Multiply(animated,profile.bones[bone].rest.rotation)};
        const auto pose=physics::BodyColliderPose(profile,0,drivers);
        const auto expected=vmd::Multiply(animated,rest.rotation);
        Check(bone_detail::Same(pose.rotation,expected),"axis-symmetric body collider lost the animated bone twist");
        Check(ik::Length(ik::Sub(ik::Rotate(pose.rotation,{0,1,0}),ik::Unit(ik::Sub(drivers[1].position,drivers[0].position))))<1e-5f,
            "collider frame changed its capsule axis");
        const auto dot=std::fabs(previous.x*pose.rotation.x+previous.y*pose.rotation.y+previous.z*pose.rotation.z+previous.w*pose.rotation.w);
        Check(dot>.9999f,"continuous body rotation spun the kinematic collider discontinuously");previous=pose.rotation;
    }
    std::cout<<"Animated collider frame: STATIC alignment, full turning circle and continuous axial twist PASS\n";
}
void ActualTriangleContacts() {
    std::array<Vector3,6> points={Vector3{0,-1,-1},{0,1,-1},{0,0,1},{1,0,0},{1,.5f,0},{1,0,.5f}};
    const std::array<uint8_t,6> body={1,1,1,0,0,0},physical={0,0,0,1,1,1};
    const std::array<validation::Triangle,2> faces={validation::Triangle{0,1,2},{3,4,5}};
    validation::TriangleSurfaceFixture surface;surface.Initialize(faces,body,physical,points);
    Check(surface.staticCrossings==0,"disjoint STATIC triangles counted as crossings");
    const auto nearest=surface.Closest({.2f,0,0},points,{});
    Check(std::fabs(nearest.distanceSquared-.04f)<1e-6f&&ik::Length(nearest.point)<1e-6f&&nearest.normal.x>.999f,
        "nearest body triangle lost face projection or outward normal");
    const auto edge=surface.Closest({.2f,2,-1},points,{});
    Check(ik::Length(ik::Sub(edge.point,points[1]))<1e-6f&&std::fabs(edge.barycentric.y-1)<1e-6f,
        "nearest triangle vertex region was not clamped");
    const std::array<uint8_t,1> excluded={0};
    Check(surface.Closest({},points,{},excluded).distanceSquared==FLT_MAX,"excluded body faces remained eligible");
    points[3].x=-1;surface.Observe(points,4);
    Check(surface.AddedCrossings()==1&&surface.NewPairs(points)==1,"actual crossed triangles were missed");
    std::vector<validation::TriangleSurfaceFixture::Contact> contacts;surface.Contacts(points,contacts);
    Check(contacts.size()==1&&contacts[0].vertex==3&&std::fabs(contacts[0].depth-1)<1e-6f&&contacts[0].normal.x>.999f,
        "triangle contact did not preserve outward plane and penetration depth");
    points[3].x=1;
    const std::array<validation::Triangle,2> seam={validation::Triangle{0,1,2},{0,3,4}};
    validation::TriangleSurfaceFixture shared;shared.Initialize(seam,body,physical,points);
    Check(shared.staticCrossings==0,"shared STATIC endpoint counted as penetration");
    points[3].x=-1;shared.Observe(points,4);
    Check(shared.AddedCrossings()==1,"a shared endpoint incorrectly exempted the entire garment triangle");
    // STATIC 已与一块面相交，不能因此豁免此衣物三角形与其他身体面的新增相交。
    std::array<Vector3,9> pairPoints={Vector3{0,-1,-1},{0,1,-1},{0,0,1},
        {2,-1,-1},{2,1,-1},{2,0,1},{-1,0,0},{1,.5f,0},{1,0,.5f}};
    const std::array<uint8_t,9> pairBody={1,1,1,1,1,1,0,0,0},pairPhysical={0,0,0,0,0,0,1,1,1};
    const std::array<validation::Triangle,3> pairFaces={validation::Triangle{0,1,2},{3,4,5},{6,7,8}};
    validation::TriangleSurfaceFixture pairs;pairs.Initialize(pairFaces,pairBody,pairPhysical,pairPoints);
    Check(pairs.staticCrossings==1,"STATIC intersecting pair fixture invalid");
    for(unsigned i=0;i<3;++i){pairPoints[i].x=-3;pairPoints[i+3].x=0;}
    pairs.Observe(pairPoints,8);
    Check(pairs.AddedCrossings()==0&&pairs.maximumNewPairs==1,
        "a STATIC clothing intersection hid a new body triangle pair");
}
std::vector<physics::InputBone> Fixture() {
    return {
        {L"Actor",-1,{{},{0,0,0,1}},{},true,false},
        {L"Bip001 Head",0,{{0,1,0},{0,0,0,1}},{0,1,0},true,true},
        {L"Bone_HairA01",1,{{.12f,1,0},{0,0,0,1}},{.12f,0,0},true,false},
        {L"Bone_HairA02",2,{{.12f,.8f,0},{0,0,0,1}},{0,-.2f,0},true,false},
        {L"Bone_HairA03",3,{{.12f,.6f,0},{0,0,0,1}},{0,-.2f,0},true,false},
        {L"Bone_Dress",0,{{-.2f,.5f,0},{0,0,0,1}},{-.2f,.5f,0},true,false},
        {L"Bone_Ribbon",1,{{-.1f,1,0},{0,0,0,1}},{-.1f,0,0},true,false},
        {L"Bone_Hair_Vmd",1,{{0,1,.1f},{0,0,0,1}},{0,0,.1f},true,true},
    };
}
bool Moving(void* context,double seconds,std::span<physics::Pose> out) {
    const auto& profile=*static_cast<const physics::Profile*>(context);
    for(unsigned ordinal=0;ordinal<profile.boneCount;++ordinal) {
        const auto index=profile.order[ordinal];const auto& node=profile.bones[index];if(!node.valid)continue;
        out[index].rotation=node.parent>=0?vmd::Multiply(out[node.parent].rotation,node.localRotation):node.localRotation;
        out[index].position=node.parent>=0?ik::Add(out[node.parent].position,ik::Rotate(out[node.parent].rotation,node.localPosition)):node.localPosition;
        if(index==1) {
            out[index].position.x+=.15f*float(std::sin(seconds*3));
            out[index].rotation=ik::AxisRotation({0,1,0},.6f*float(std::sin(seconds*2)));
        }
    }return true;
}
struct Cancellation {unsigned calls=0;};
bool Cancel(void* context) {return ++static_cast<Cancellation*>(context)->calls>50;}
bool TurningCloth(void* context,double seconds,std::span<physics::Pose> out) {
    const auto& profile=*static_cast<const physics::Profile*>(context);
    for(unsigned ordinal=0;ordinal<profile.boneCount;++ordinal) {
        const auto i=profile.order[ordinal];const auto& node=profile.bones[i];
        if(node.parent<0) {
            const auto yaw=float(seconds<2?seconds*3.141592653589793:2*3.141592653589793);
            out[i]={ik::Add(node.rest.position,{.2f*float(std::sin(std::min(seconds,2.0)*3)),0,0}),
                vmd::Multiply(ik::AxisRotation({0,1,0},yaw),node.rest.rotation)};
        }else out[i]={ik::Add(out[node.parent].position,ik::Rotate(out[node.parent].rotation,node.localPosition)),
            vmd::Multiply(out[node.parent].rotation,node.localRotation)};
    }return true;
}
void PlayerGirlAmice() {
    const auto& fixture=validation::PlayerGirlAmice;std::array<physics::InputBone,fixture.size()> bones{};
    for(unsigned i=0;i<fixture.size();++i) {
        reference::Pose rest;Check(reference::Decode(fixture[i].inverse,rest),"PlayerGirl Amice bind decode failed");
        bones[i]={fixture[i].name,fixture[i].parent,{rest.position,rest.rotation},i?fixture[i].local:rest.position,true,i==0};
    }
    std::string error;std::vector<physics::Candidate> candidates;
    Check(physics::Candidates(bones,candidates,error)&&candidates.size()==12,"PlayerGirl neck cloth chain omitted");
    std::vector<unsigned> selection;for(const auto& candidate:candidates)selection.push_back(candidate.bone);
    for(unsigned preset=0;preset<3;++preset) {
        physics::Profile profile;Check(physics::Build(bones,selection,preset,profile,error),error.c_str());
        physics::Bake baked;Check(physics::Generate(profile,73,8,TurningCloth,&profile,nullptr,nullptr,nullptr,baked,error),"PlayerGirl cloth bake failed");
        physics::State live;float finalMotion=0;unsigned worst=0;
        for(unsigned tick=0;tick<baked.frames;++tick) {
            Check(physics::Advance(profile,TurningCloth,&profile,tick,live),"PlayerGirl cloth step failed");
            for(unsigned j=0;j<profile.jointCount;++j) {
                const auto& a=baked.rotations[size_t(tick)*profile.jointCount+j];const auto& b=live.local[j];
                Check(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.999999f,"PlayerGirl cloth live/bake mismatch");
                Check(std::fabs(ik::Length(ik::Sub(live.particles[j].current,live.poses[profile.joints[j].bone].position))-
                    ik::Length(profile.joints[j].tailLocal))<.00001f,"PlayerGirl long cloth stretched");
                if(tick>physics::Hz*7) {
                    const auto speed=ik::Length(ik::Sub(live.particles[j].current,live.particles[j].previous));
                    if(speed>finalMotion){finalMotion=speed;worst=j;}
                }
            }
        }
        std::cout<<"PlayerGirl Amice joints="<<profile.jointCount<<" preset="<<preset<<" final_tip_step="<<finalMotion<<" worst_joint="<<worst
            <<" merge="<<live.maxMergeOffset<<" contacts="<<live.contacts<<" penetration="<<live.maxPenetration<<'\n';
        Check(finalMotion<.0001f,"PlayerGirl stopped cloth keeps bouncing");
    }
}
void ClothSettling() {
    float worstOvershoot=0;
    for(const auto length:{.02f,.12f,.25f,.5f})for(unsigned preset=0;preset<3;++preset) {
        const physics::InputBone bones[]={
            {L"Attachment",-1,{{0,1,0},{0,0,0,1}},{0,1,0},true,true},
            {L"Bone_AmiceA01_L",0,{{0,1,0},{0,0,0,1}},{},true,false},
            {L"Bone_AmiceA02_L",1,{{0,1-length,0},{0,0,0,1}},{0,-length,0},true,false}
        };
        const unsigned selected[]={1};physics::Profile profile;std::string error;
        Check(physics::Build(bones,selected,preset,profile,error),"Amice cloth profile rejected");
        std::array<physics::Pose,3> drivers{};for(unsigned i=0;i<3;++i)drivers[i]=bones[i].rest;
        auto released=drivers;released[1].rotation=ik::AxisRotation({0,0,1},.6f);
        released[2].position=ik::Add(released[1].position,ik::Rotate(released[1].rotation,{0,-length,0}));
        physics::State state;Check(physics::Initialize(profile,released,state),"cloth settling initialization failed");
        auto& particle=state.particles[0];
        float last=.6f,lateMotion=0;
        for(unsigned tick=0;tick<physics::Hz*6;++tick) {
            Check(physics::Step(profile,drivers,state),"cloth settling step failed");
            const auto offset=ik::Sub(particle.current,drivers[1].position);const auto angle=std::atan2(offset.x,-offset.y);
            worstOvershoot=std::max(worstOvershoot,-angle);
            if(std::fabs(angle)>=.65f)std::cout<<"Release failure length="<<length<<" preset="<<preset<<" tick="<<tick<<" angle="<<angle<<'\n';
            Check(std::fabs(angle)<.65f,"released cloth gains angular energy");
            Check(std::fabs(ik::Length(offset)-length)<.00001f,"released cloth stretches");last=angle;
            if(tick>physics::Hz*5)lateMotion=std::max(lateMotion,ik::Length(ik::Sub(particle.current,particle.previous)));
        }
        std::cout<<"Cloth release length="<<length<<" preset="<<preset<<" final_angle="<<last<<" late_tip_step="<<lateMotion<<'\n';
        Check(std::fabs(last)<.005f,"cloth damping froze the bone instead of settling");
        Check(lateMotion<.0001f,"released cloth keeps rebounding");
    }
    std::cout<<"Cloth release lengths=4 presets=3 overshoot_rad="<<worstOvershoot<<" settling PASS\n";
}
void StructuralCandidates() {
    const auto& fixture=validation::OdetteBody;std::vector<physics::InputBone> bones(fixture.size());
    for(unsigned i=0;i<fixture.size();++i) {
        reference::Pose rest;Check(reference::Decode(fixture[i].inverse,rest),"Odette bind invalid");
        bones[i]={fixture[i].name,fixture[i].parent,{rest.position,rest.rotation},
            fixture[i].parent<0?rest.position:fixture[i].local,true,false};
    }
    std::vector<physics::Candidate> candidates;std::string error;
    Check(physics::Candidates(bones,candidates,error),error.c_str());
    unsigned automatic=0,inferred=0,hem=0,wing=0,let=0;
    std::vector<std::wstring> renamed(bones.size());auto generic=bones;
    for(const auto& candidate:candidates) {
        const auto name=bones[candidate.bone].name;
        automatic+=candidate.defaultSelected;inferred+=candidate.inferred;
        hem+=name.starts_with(L"Bone_Hem");wing+=name.starts_with(L"Bone_Wing");let+=name.starts_with(L"Bone_Let");
        Check(name.find(L"Twist")==std::wstring_view::npos&&name.find(L"Shape")==std::wstring_view::npos&&
            name.find(L"_Pos_")==std::wstring_view::npos&&name.find(L"_Root_")==std::wstring_view::npos&&name.find(L"Rot01")==std::wstring_view::npos,
            "corrective/position/twist helper selected as secondary physics");
        renamed[candidate.bone]=L"UnknownSegment_"+std::to_wstring(candidate.bone);generic[candidate.bone].name=renamed[candidate.bone];
    }
    Check(hem==12&&wing==12&&let==10,"Odette branches still depend on per-garment keyword exceptions");
    std::vector<physics::Candidate> genericCandidates;
    Check(physics::Candidates(generic,genericCandidates,error)&&genericCandidates.size()==candidates.size(),"renaming garment bones changes eligibility");
    for(unsigned i=0;i<candidates.size();++i) {
        Check(genericCandidates[i].bone==candidates[i].bone&&
            ik::Length(ik::Sub(genericCandidates[i].tailLocal,candidates[i].tailLocal))<.000001f,"renaming truncates static branch geometry");
        if(!candidates[i].virtualTail)Check(genericCandidates[i].defaultSelected,"unknown continuous branch requires a new name exception");
    }
    std::vector<unsigned> selected;for(const auto& candidate:candidates)if(candidate.defaultSelected)selected.push_back(candidate.bone);
    physics::Profile profile;Check(physics::Build(bones,selected,1,profile,error),error.c_str());
    physics::Bake baked;Check(physics::Generate(profile,74,3,TurningCloth,&profile,nullptr,nullptr,nullptr,baked,error),error.c_str());
    physics::State live;
    for(unsigned frame=0;frame<baked.frames;++frame) {
        Check(physics::Advance(profile,TurningCloth,&profile,frame,live),"Odette structural branch step failed");
        for(unsigned j=0;j<profile.jointCount;++j) {
            const auto a=baked.rotations[size_t(frame)*profile.jointCount+j],b=live.local[j];
            Check(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.999999f,"Odette live/bake differs");
        }
    }
    auto unknown=bones;const auto bone=candidates.front().bone;unknown[bone].skinned=false;
    Check(physics::Candidates(unknown,genericCandidates,error)&&
        std::none_of(genericCandidates.begin(),genericCandidates.end(),[&](const physics::Candidate& c){return c.bone==bone;}),"unskinned helper proposed as physics");
    std::vector<physics::InputBone> many(132);
    many[0]={L"Actor",-1,{{},{0,0,0,1}},{},true,true};
    for(unsigned i=1;i<many.size();++i)many[i]={L"Unlabelled",0,{{.03f,.01f*float(i),0},{0,0,0,1}},{.03f,.01f*float(i),0},true,false};
    Check(physics::Candidates(many,genericCandidates,error)&&genericCandidates.size()==131,"large candidate inventory cleared instead of allowing selection");
    const unsigned one[]={1};Check(physics::Build(many,one,1,profile,error),"valid selection from large candidate inventory rejected");
    std::cout<<"Odette static Body bones="<<bones.size()<<" candidates="<<candidates.size()<<" automatic="<<automatic<<" inferred="<<inferred
        <<" Hem/Wing/Let="<<hem<<'/'<<wing<<'/'<<let<<" renamed branches/live-bake PASS\n";
}
void StaticShape() {
    float worstSag=0;
    for(const auto length:{.02f,.12f,.5f}) {
        float softSag=0;
        for(unsigned preset=0;preset<3;++preset) {
            const physics::InputBone bones[]={
                {L"Attachment",-1,{{0,1,0},{0,0,0,1}},{0,1,0},true,true},
                {L"Bone_Cloth01",0,{{0,1,0},{0,0,0,1}},{},true,false},
                {L"Bone_Cloth02",1,{{length,1,0},{0,0,0,1}},{length,0,0},true,false}
            };
            const unsigned selected[]={1};physics::Profile profile;std::string error;
            Check(physics::Build(bones,selected,preset,profile,error),"horizontal cloth profile rejected");
            std::array<physics::Pose,3> drivers{};for(unsigned i=0;i<3;++i)drivers[i]=bones[i].rest;
            physics::State state;Check(physics::Initialize(profile,drivers,state),"horizontal cloth initialize failed");
            for(unsigned tick=0;tick<physics::Hz*4;++tick)Check(physics::Step(profile,drivers,state),"horizontal cloth step failed");
            const auto offset=ik::Sub(state.particles[0].current,state.particles[0].anchor);
            const auto sag=std::atan2(-offset.y,offset.x);worstSag=std::max(worstSag,sag);
            std::cout<<"Static shape length="<<length<<" preset="<<preset<<" sag_rad="<<sag<<'\n';
            Check(sag>=0&&sag<.7853982f,"small garment collapses to its angular limit under gravity");
            if(!preset)softSag=sag;else Check(sag<softSag,"firmness does not preserve static clothing shape");
        }
    }
    std::cout<<"Static horizontal garment worst_sag_rad="<<worstSag<<" shape retention PASS\n";
}
void SegmentCollisions() {
    std::vector<physics::InputBone> bones={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
        {L"Bip001 Neck",0,{{0,-.1f,0},{0,0,0,1}},{0,-.1f,0},true,true},
        {L"Bip001 Head",1,{{},{0,0,0,1}},{0,.1f,0},true,true},
        {L"Attachment",0,{{-.3f,0,0},{0,0,0,1}},{-.3f,0,0},true,true},
        {L"UnlabelledA",3,{{-.3f,0,0},{0,0,0,1}},{},true,false},
        {L"UnlabelledB",4,{{-.3f,-.6f,0},{0,0,0,1}},{0,-.6f,0},true,false}
    };
    const unsigned selected[]={4};std::string error;physics::Profile profile;
    Check(physics::Build(bones,selected,1,profile,error)&&profile.colliderCount==1,error.c_str());
    auto& joint=profile.joints[0];joint.frequency=joint.gravity=joint.damping=0;joint.maxAngle=2.9f;
    std::vector<physics::Pose> drivers;for(const auto& bone:bones)drivers.push_back(bone.rest);
    const auto anchor=drivers[4].position;const Vector3 crossing{.3f,0,.002f};
    Check(ik::Length(anchor)>joint.collisionRadius[0]&&ik::Length(crossing)>joint.collisionRadius[0]&&
        physics::SegmentClearance(anchor,crossing,{},{})<.002f,"middle-only collision fixture invalid");
    auto crossed=drivers;crossed[4].rotation=ik::FromTo({0,-1,0},ik::Sub(crossing,anchor));crossed[5].position=crossing;
    physics::State state;Check(physics::Initialize(profile,crossed,state),"segment collision initialize failed");
    for(unsigned tick=0;tick<120;++tick)Check(physics::Step(profile,drivers,state),"segment collision step failed");
    const auto sphereGap=physics::SegmentClearance(state.particles[0].anchor,state.particles[0].current,{},{})-joint.collisionRadius[0];
    std::cout<<"Sphere contact gap="<<sphereGap<<" contacts="<<state.contacts<<" merge="<<state.maxMergeOffset<<'\n';
    Check(state.contacts>0&&sphereGap>-.002f,"middle of segment collision missed after bone merge");
    Check(std::fabs(ik::Length(ik::Sub(state.particles[0].current,state.particles[0].anchor))-.6f)<.00001f,"body collision stretched bone");
    Check(physics::SegmentClearance({-.3f,0,0},{.3f,0,0},{0,-.2f,.1f},{0,.2f,.1f})>.09999f&&
        physics::SegmentClearance({-.3f,0,0},{.3f,0,0},{-.2f,.2f,0},{.2f,.2f,0})>.19999f,
        "skew/parallel segment clearance invalid");
    auto cylinder=profile;cylinder.colliders[0]={1,2,.1f};cylinder.joints[0].collisionRadius[0]=.115f;
    cylinder.bones[1].rest.position={0,-.2f,0};cylinder.bones[2].rest.position={0,.2f,0};
    auto cylinderDrivers=drivers;cylinderDrivers[1].position={0,-.2f,0};cylinderDrivers[2].position={0,.2f,0};
    auto crossedCylinder=cylinderDrivers;crossedCylinder[4].rotation=crossed[4].rotation;crossedCylinder[5].position=crossing;
    physics::State cylinderState;Check(physics::Initialize(cylinder,crossedCylinder,cylinderState),"cylinder collision initialization failed");
    for(unsigned tick=0;tick<120;++tick)Check(physics::Step(cylinder,cylinderDrivers,cylinderState),"cylinder collision step failed");
    Check(cylinderState.contacts>0&&physics::SegmentClearance(cylinderState.particles[0].anchor,cylinderState.particles[0].current,{0,-.2f,0},{0,.2f,0})>=.113f,
        "bone/capsule cylinder interior contact missed after bone merge");
    auto pairProfile=profile;pairProfile.colliderCount=2;pairProfile.colliders[0]={3,3,.055f};pairProfile.colliders[1]={2,2,.1f};
    pairProfile.joints[0].colliders=3;pairProfile.joints[0].collisionStart[0]=.85f;pairProfile.joints[0].collisionStart[1]=0;
    pairProfile.joints[0].collisionRadius[0]=.07f;pairProfile.joints[0].collisionRadius[1]=.115f;
    auto pairDrivers=drivers;pairDrivers[2].position={-.27f,-.2f,0};physics::State pairState;
    Check(physics::Initialize(pairProfile,pairDrivers,pairState),"pair-specific garment collision initialization failed");
    for(unsigned tick=0;tick<120;++tick)Check(physics::Step(pairProfile,pairDrivers,pairState),"pair-specific garment collision step failed");
    Check(pairState.contacts>0&&physics::SegmentClearance(pairState.particles[0].anchor,pairState.particles[0].current,pairDrivers[2].position,pairDrivers[2].position)>=.113f,
        "attachment exclusion removed garment interior collision against another body part");
    Check(physics::Initialize(profile,drivers,state),"moving capsule initialize failed");
    for(unsigned tick=1;tick<=120;++tick) {
        drivers[2].position=ik::Scale({-.25f,-.3f,0},std::min(1.0f,float(tick)/30));
        Check(physics::Step(profile,drivers,state),"moving capsule step failed");
    }
    Check(physics::SegmentClearance(state.particles[0].anchor,state.particles[0].current,drivers[2].position,drivers[2].position)>=
        joint.collisionRadius[0]-.002f,"moving body did not collide with garment interior");
    bones[1].rest.position=bones[1].localPosition={-.22f,-.4f,0};bones[2].rest.position={-.22f,-.3f,0};
    Check(physics::Build(bones,selected,1,profile,error),"overlapping static garment profile rejected");
    auto& calibrated=profile.joints[0];calibrated.frequency=calibrated.gravity=calibrated.damping=0;
    Check(calibrated.colliders==1&&std::fabs(calibrated.collisionRadius[0]-(profile.colliders[0].radius+calibrated.radius))<.000001f,
        "static overlap shrank the body collision radius");
    for(unsigned i=0;i<bones.size();++i)drivers[i]=bones[i].rest;
    state={};Check(physics::Initialize(profile,drivers,state),"static overlap initialization failed");
    for(unsigned tick=0;tick<120;++tick)Check(physics::Step(profile,drivers,state),"static overlap step failed");
    auto fittedSegment=physics::CollisionSegment(calibrated,state.poses[calibrated.bone]);
    Check(ik::Length(calibrated.collisionOffset)>.02f&&physics::SegmentClearance(fittedSegment[0],fittedSegment[1],drivers[2].position,drivers[2].position)>=
        calibrated.collisionRadius[0]-.002f,"static-overlap rigid-body offset stayed intersecting");
    drivers[2].position={-.29f,-.3f,0};
    for(unsigned tick=0;tick<120;++tick)Check(physics::Step(profile,drivers,state),"reactivated overlap step failed");
    fittedSegment=physics::CollisionSegment(calibrated,state.poses[calibrated.bone]);
    Check(physics::SegmentClearance(fittedSegment[0],fittedSegment[1],drivers[2].position,drivers[2].position)>=
        calibrated.collisionRadius[0]-.002f,"fitted static-overlap pair never reactivated");
    profile.colliders[0].radius=1;calibrated.collisionStart[0]=0;calibrated.translationLimit=0;drivers[2].position=anchor;state={};
    Check(physics::Initialize(profile,drivers,state)&&physics::Step(profile,drivers,state)&&state.unresolved>0&&state.maxPenetration>.9f,
        "infeasible body/anchor collision silently reported as solved");
    std::cout<<"Segment collision: middle-only, moving body, static-overlap calibration, final constraint validation PASS\n";
}
void StaticRigidBodyOffsets() {
    std::vector<physics::InputBone> bones={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
        {L"Bip001 Neck",0,{{0,-.1f,0},{0,0,0,1}},{0,-.1f,0},true,true},
        {L"Bip001 Head",1,{{},{0,0,0,1}},{0,.1f,0},true,true},
        {L"UnlabelledA",0,{{.03f,0,0},{0,0,0,1}},{.03f,0,0},true,false},
        {L"UnlabelledB",3,{{.03f,-.04f,0},{0,0,0,1}},{0,-.04f,0},true,false}
    };
    const unsigned selected[]={3};physics::Profile profile;std::string error;
    Check(physics::Build(bones,selected,1,profile,error),error.c_str());const auto& joint=profile.joints[0];
    Check(ik::Length(joint.collisionOffset)>.05f&&joint.colliders==1&&joint.collisionStart[0]==0,"embedded sleeve root retained impossible zero-offset collision geometry");
    const auto restSegment=physics::CollisionSegment(joint,bones[3].rest);
    Check(physics::SegmentClearance(restSegment[0],restSegment[1],{},{})>=joint.radius+profile.colliders[0].radius+.0009f,"static rigid-body fit did not clear its full capsule");
    Check(ik::Length(ik::Sub(profile.bones[3].rest.position,bones[3].rest.position))<.000001f&&
        ik::Length(ik::Sub(profile.bones[3].localPosition,bones[3].localPosition))<.000001f,"rigid-body fit moved the static skeleton");
    auto placed=bones;const auto rotation=ik::AxisRotation(ik::Unit({1,2,3}),.7f);const Vector3 translation{2,3,-1};
    for(auto& bone:placed){bone.rest.position=ik::Add(translation,ik::Rotate(rotation,bone.rest.position));bone.rest.rotation=vmd::Multiply(rotation,bone.rest.rotation);}
    placed[0].localPosition=placed[0].rest.position;
    physics::Profile other;Check(physics::Build(placed,selected,1,other,error),error.c_str());
    Check(ik::Length(ik::Sub(joint.collisionOffset,other.joints[0].collisionOffset))<.00001f,"collision fit depends on world placement or rotation");
    std::vector<physics::Pose> drivers;for(const auto& bone:bones)drivers.push_back(bone.rest);
    physics::State state;Check(physics::Initialize(profile,drivers,state),"embedded sleeve initialization failed");
    for(unsigned tick=0;tick<physics::Hz;++tick)Check(physics::Step(profile,drivers,state),"embedded sleeve step failed");
    auto segment=physics::CollisionSegment(joint,state.poses[joint.bone]);
    Check(ik::Length(ik::Sub(state.poses[joint.bone].position,bones[3].rest.position))<.01f&&
        physics::SegmentClearance(segment[0],segment[1],{},{})>=joint.collisionRadius[0]-.002f,"fixed sleeve root lost its fitted collision after bone writeback");
    for(unsigned tick=1;tick<=physics::Hz;++tick) {
        drivers[2].position={.07f*std::min(1.0f,float(tick)/60),0,0};
        Check(physics::Step(profile,drivers,state),"moving body against fitted sleeve failed");
    }
    segment=physics::CollisionSegment(joint,state.poses[joint.bone]);
    Check(state.contacts>0&&physics::SegmentClearance(segment[0],segment[1],drivers[2].position,drivers[2].position)>=joint.collisionRadius[0]-.002f,
        "offset rigid-body collider was only moved out at bind time and ignored subsequent body collision");
    Check(ik::Length(state.positionDelta[0])>.01f&&ik::Length(state.positionDelta[0])<joint.translationLimit*1.733f,
        "embedded moving body discarded the bounded dynamic root translation");
    for(unsigned tick=1;tick<=physics::Hz*3;++tick) {
        drivers[2].position={.07f*std::max(0.0f,1-float(tick)/physics::Hz),0,0};
        Check(physics::Step(profile,drivers,state),"dynamic root contact release failed");
    }
    Check(ik::Length(state.positionDelta[0])<.01f,"released sleeve root stayed detached from its static attachment");
    const auto sampler=+[](void* context,double seconds,std::span<physics::Pose> poses) {
        const auto& p=*static_cast<physics::Profile*>(context);for(unsigned i=0;i<p.boneCount;++i)poses[i]=p.bones[i].rest;
        poses[2].position.x=.07f*float(std::clamp(seconds,0.0,1.0));return true;
    };
    physics::Bake baked;Check(physics::Generate(profile,601,2,sampler,&profile,nullptr,nullptr,nullptr,baked,error),"dynamic root bake failed");
    physics::State live;std::array<Quaternion,physics::MaxJoints> rotations{};std::array<Vector3,physics::MaxJoints> positions{};
    for(unsigned tick=0;tick<baked.frames;++tick) {
        Check(physics::Advance(profile,sampler,&profile,tick,live)&&physics::Sample(baked,profile,double(tick)/physics::Hz,rotations,positions),"dynamic root timeline failed");
        Check(ik::Length(ik::Sub(positions[0],live.positionDelta[0]))<.00001f,"dynamic root live/bake positions disagree");
        const auto reconstructed=ik::Add(profile.bones[3].localPosition,positions[0]);
        Check(ik::Length(ik::Sub(reconstructed,live.poses[3].position))<.00001f,"dynamic root palette does not reconstruct the physical transform");
        const auto childLocal=ik::Rotate(vmd::Inverse(live.poses[3].rotation),ik::Sub(live.poses[4].position,live.poses[3].position));
        Check(ik::Length(ik::Sub(childLocal,profile.bones[4].localPosition))<.00001f,"root contact stretched a child bone");
    }
    const auto cache=std::filesystem::temp_directory_path()/(L"ssmt-root-physics-"+std::to_wstring(GetCurrentProcessId())+L".bin");physics::Bake loaded;
    Check(physics::Save(cache,baked,error)&&physics::Load(cache,profile,601,2,loaded,error)&&
        loaded.positions.size()==baked.positions.size()&&!std::memcmp(loaded.positions.data(),baked.positions.data(),baked.positions.size()*sizeof(Vector3)),"cache lost root contact translation");
    auto invalid=baked;invalid.positions[0].x=std::numeric_limits<float>::quiet_NaN();
    Check(!physics::Save(cache,invalid,error),"nonfinite root translation was persisted");
    invalid=baked;invalid.positions[0].x=joint.translationLimit+.1f;
    Check(physics::Save(cache,invalid,error)&&!physics::Load(cache,profile,601,2,loaded,error)&&error=="PHYSICS_CACHE_POSITION_REJECTED",
        "cache accepted a finite root translation outside its physical limit");
    std::filesystem::remove(cache);
    std::cout<<"MMD-style rigid-body offsets: embedded root, full capsule, static reference, world placement and moving collision PASS\n";
}
void MultiBodyContacts() {
    const std::vector<physics::InputBone> bones={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
        {L"Bip001 ColliderLeft",0,{{-.06f,0,0},{0,0,0,1}},{-.06f,0,0},true,true},
        {L"Bip001 ColliderRight",0,{{.06f,0,0},{0,0,0,1}},{.06f,0,0},true,true},
        {L"UnlabelledA",0,{{0,0,.2f},{0,0,0,1}},{0,0,.2f},true,false},
        {L"UnlabelledB",3,{{0,-.08f,.2f},{0,0,0,1}},{0,-.08f,0},true,false},
        {L"UnlabelledC",4,{{0,-.16f,.2f},{0,0,0,1}},{0,-.08f,0},true,false}
    };
    const unsigned selected[]={3,4};physics::Profile profile;std::string error;
    Check(physics::Build(bones,selected,1,profile,error),"multi-body branch profile failed");
    profile.colliderCount=2;profile.colliders[0]={1,1,.1f};profile.colliders[1]={2,2,.1f};
    for(auto& joint:std::span(profile.joints.data(),profile.jointCount)) {
        joint.gravity=0;joint.maxAngle=.05f;joint.colliders=3;
        joint.collisionRadius[0]=joint.collisionRadius[1]=.1f+joint.radius;
    }
    profile.joints[0].translationLimit=.25f;
    std::vector<physics::Pose> drivers;for(const auto& bone:bones)drivers.push_back(bone.rest);
    // 两个重叠身体代理把根夹在反向法线之间；单独推出一个会进入另一个。
    drivers[1].position.z=drivers[2].position.z=.2f;
    physics::State state;Check(physics::Initialize(profile,drivers,state),"multi-body initialization failed");
    for(unsigned j=0;j<profile.jointCount;++j) {
        const auto& joint=profile.joints[j];const auto segment=physics::CollisionSegment(joint,state.poses[joint.bone]);
        for(unsigned c=0;c<2;++c)Check(physics::SegmentClearance(segment[0],segment[1],drivers[c+1].position,drivers[c+1].position)>=joint.collisionRadius[c]-.002f,
            "opening frame / seek published an intersecting physical branch");
    }
    for(unsigned tick=0;tick<physics::Hz;++tick) {
        Check(physics::Step(profile,drivers,state),"multi-body branch step failed");
        for(unsigned j=0;j<profile.jointCount;++j) {
            const auto& joint=profile.joints[j];const auto segment=physics::CollisionSegment(joint,state.poses[joint.bone]);
            for(unsigned c=0;c<2;++c)Check(physics::SegmentClearance(segment[0],segment[1],drivers[c+1].position,drivers[c+1].position)>=joint.collisionRadius[c]-.002f,
                "branch contact correction cleared one body by penetrating another");
        }
        Check(ik::Length(state.positionDelta[1])<.00001f,"multi-body correction stretched the interior joint");
    }
    std::cout<<"Multi-body root contact: opposing normals, full chain clearance and fixed interior length PASS\n";
}
void GarmentSelfContact() {
    const std::vector<physics::InputBone> bones={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
        {L"AttachmentA",0,{{-.05f,0,0},{0,0,0,1}},{-.05f,0,0},true,true},
        {L"UnlabelledA01",1,{{-.05f,0,0},{0,0,0,1}},{},true,false},
        {L"UnlabelledA02",2,{{-.05f,-.2f,0},{0,0,0,1}},{0,-.2f,0},true,false},
        {L"AttachmentB",0,{{.05f,0,0},{0,0,0,1}},{.05f,0,0},true,true},
        {L"UnlabelledB01",4,{{.05f,0,0},{0,0,0,1}},{},true,false},
        {L"UnlabelledB02",5,{{.05f,-.2f,0},{0,0,0,1}},{0,-.2f,0},true,false}
    };
    const unsigned selected[]={2,3,5,6};physics::Profile profile;std::string error;
    Check(physics::Build(bones,selected,1,profile,error),"self-contact profile rejected");
    for(auto& joint:std::span(profile.joints.data(),profile.jointCount))joint.gravity=0;
    profile.joints[profile.bones[2].joint].translationLimit=profile.joints[profile.bones[5].joint].translationLimit=.15f;
    std::vector<physics::Pose> drivers;for(const auto& bone:bones)drivers.push_back(bone.rest);
    auto crossed=drivers;
    crossed[2].rotation=ik::FromTo({0,-1,0},{.1f,-.2f,0});crossed[5].rotation=ik::FromTo({0,-1,0},{-.1f,-.2f,0});
    for(const auto pair:{std::pair{2u,3u},std::pair{5u,6u}}) {
        crossed[pair.second].position=ik::Add(crossed[pair.first].position,ik::Rotate(crossed[pair.first].rotation,{0,-.2f,0}));
        crossed[pair.second].rotation=crossed[pair.first].rotation;
    }
    physics::State state;Check(physics::Initialize(profile,crossed,state),"crossed garments failed initialization");
    for(unsigned tick=0;tick<physics::Hz;++tick) {
        if(tick)Check(physics::Step(profile,drivers,state),"garment self-contact step failed");
        for(const auto a:{unsigned(profile.bones[2].joint),unsigned(profile.bones[3].joint)})
        for(const auto b:{unsigned(profile.bones[5].joint),unsigned(profile.bones[6].joint)}) {
            const auto first=physics::CollisionSegment(profile.joints[a],state.poses[profile.joints[a].bone]);
            const auto last=physics::CollisionSegment(profile.joints[b],state.poses[profile.joints[b].bone]);
            Check(physics::SegmentClearance(first[0],first[1],last[0],last[1])>=profile.joints[a].radius+profile.joints[b].radius-.002f,
                "garment branches intersect after root / static-length writeback");
        }
        Check(ik::Length(state.positionDelta[profile.bones[3].joint])<.00001f&&ik::Length(state.positionDelta[profile.bones[6].joint])<.00001f,"self-contact stretched the physical interior");
    }
    std::cout<<"Garment self-contact: crossed independent chains, initialization and static-length writeback PASS\n";
}
void FoldedBranchContact() {
    for(const auto attachmentX:{.07f,.06f}) {
        const std::vector<physics::InputBone> bones={
            {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
            {L"Attachment",0,{{},{0,0,0,1}},{},true,true},
            {L"UnlabelledFan",1,{{},{0,0,0,1}},{},true,false},
            {L"UnlabelledUnused",2,{{0,-.1f,0},{0,0,0,1}},{0,-.1f,0},true,false},
            {L"UnlabelledBranch",2,{{attachmentX,-.05f,0},{0,0,0,1}},{attachmentX,-.05f,0},true,false},
            {L"UnlabelledTip",4,{{attachmentX,-.13f,0},{0,0,0,1}},{0,-.08f,0},true,false}
        };
        const unsigned selected[]={2,4,5};physics::Profile profile;std::string error;
        Check(physics::Build(bones,selected,1,profile,error),"folded garment profile failed");
        for(auto& joint:std::span(profile.joints.data(),profile.jointCount))joint.gravity=joint.frequency=0;
        std::vector<physics::Pose> drivers;for(const auto& bone:bones)drivers.push_back(bone.rest);
        drivers[4].rotation=ik::AxisRotation({0,0,1},-55*3.14159265f/180);
        drivers[5].position=ik::Add(drivers[4].position,ik::Rotate(drivers[4].rotation,{0,-.08f,0}));drivers[5].rotation=drivers[4].rotation;
        const auto root=unsigned(profile.bones[2].joint),tip=unsigned(profile.bones[5].joint);
        auto first=physics::CollisionSegment(profile.joints[root],drivers[2]);auto last=physics::CollisionSegment(profile.joints[tip],drivers[5]);
        Check(physics::SegmentClearance(first[0],first[1],drivers[5].position,drivers[5].position)<profile.joints[root].radius+profile.joints[tip].radius-.002f,
            "folded fixture does not embed the fixed tip joint in its ancestor capsule");
        physics::State state;Check(physics::Initialize(profile,drivers,state),"folded branch initialization failed");
        for(unsigned tick=0;tick<30;++tick) {
            if(tick)Check(physics::Step(profile,drivers,state),"folded branch contact step failed");
            first=physics::CollisionSegment(profile.joints[root],state.poses[2]);last=physics::CollisionSegment(profile.joints[tip],state.poses[5]);
            Check(physics::SegmentClearance(first[0],first[1],last[0],last[1])>=profile.joints[root].radius+profile.joints[tip].radius-.002f,
                "contact inside a fixed tip joint was not repaired through its movable ancestor");
            for(unsigned j=0;j<profile.jointCount;++j)Check(ik::Length(state.positionDelta[j])<.00001f,"same-chain collision displaced a fixed joint");
            Check(bone_detail::Same(state.poses[2].rotation,drivers[2].rotation),"self-contact rotated both objects through their common ancestor");
        }
    }
    std::cout<<"Folded garment contact: embedded tip joint, ancestor correction, blocked normal, tangent escape and unchanged shared root PASS\n";
}
void Test() {
    auto bones=Fixture();std::string error;std::vector<physics::Candidate> candidates;
    Check(physics::Candidates(bones,candidates,error),error.c_str());Check(candidates.size()==5,"candidate scope includes an animated or body bone");
    Check(physics::Classify(L"Bip001 Hair")==physics::Kind::None&&physics::Classify(L"+Hair Helper")==physics::Kind::None,"helper was treated as physics");
    Check(physics::Classify(L"BONE_HAIR")==physics::Kind::Hair,"case-folded hair name missing");
    Check(physics::Classify(L"Bone_AmiceA01_L")==physics::Kind::Clothing&&physics::Classify(L"Bone_RibbonA01_L")==physics::Kind::Clothing,"neck cloth missed or given accessory stiffness");
    const unsigned selection[]={2,3,4,5,6};physics::Profile profile;
    Check(physics::Build(bones,selection,1,profile,error),error.c_str());
    const unsigned badSelection[]={2,7};physics::Profile rejected;
    Check(!physics::Build(bones,badSelection,1,rejected,error),"VMD-driven bone accepted");
    auto broken=bones;broken[3].parent=4;Check(!physics::Candidates(broken,candidates,error),"cyclic physics rig accepted");
    broken=bones;broken[3].localPosition.x=.2f;Check(!physics::Candidates(broken,candidates,error),"live local position used as static reference");
    Vector3 point{.1f,.5f,0};Check(physics::ProjectCapsule(point,{},{0,1,0},.25f,{1,0,0}),"capsule contact missed");
    Check(std::fabs(point.x-.25f)<.00001f&&std::fabs(point.y-.5f)<.00001f,"capsule projection incorrect");
    point={0,.5f,0};Check(physics::ProjectCapsule(point,{},{0,1,0},.25f,{0,0,1})&&std::fabs(point.z-.25f)<.00001f,"capsule center produced NaN");
    physics::Bake baked;Check(physics::Generate(profile,42,3,Moving,&profile,nullptr,nullptr,nullptr,baked,error),error.c_str());
    Check(baked.frames==361,"bake omitted endpoint");physics::State live;
    float maximumDeviation=0;
    for(unsigned tick=0;tick<baked.frames;++tick) {
        Check(physics::Advance(profile,Moving,&profile,tick,live),"realtime step rejected");
        std::array<Quaternion,physics::MaxJoints> sampled{};
        Check(physics::Sample(baked,profile,double(tick)/physics::Hz,sampled),"bake sample rejected");
        for(unsigned j=0;j<profile.jointCount;++j) {
            const auto& a=sampled[j];const auto& b=live.local[j];
            const auto dot=std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
            Check(dot>.999999f,"live/bake mismatch on fixed timeline");
            const auto& joint=profile.joints[j];const auto& pose=live.poses[joint.bone];
            Check(std::fabs(ik::Length(ik::Sub(live.particles[j].current,pose.position))-ik::Length(joint.tailLocal))<.00001f,"physics stretched a segment");
            const auto rest=profile.bones[joint.bone].localRotation;
            const auto angle=2*std::acos(std::clamp(std::fabs(a.x*rest.x+a.y*rest.y+a.z*rest.z+a.w*rest.w),0.0f,1.0f));
            Check(angle<=joint.maxAngle+.001f,"cone limit exceeded");maximumDeviation=std::max(maximumDeviation,angle);
        }
    }
    Check(maximumDeviation>.05f,"moving attachment produced no secondary motion");
    std::array<physics::Pose,physics::MaxBones> preparedDrivers{};
    Check(Moving(&profile,0,std::span(preparedDrivers.data(),profile.boneCount)),"prepared driver rejected");
    physics::State prepared;Check(physics::Prepare(profile,std::span(preparedDrivers.data(),profile.boneCount),prepared),"worker physics prepare failed");
    const auto* preparedWorld=prepared.simulation.get();
    for(unsigned replay=0;replay<2;++replay) {
        physics::Reset(prepared);
        for(unsigned tick=0;tick<baked.frames;++tick) {
            Check(physics::Advance(profile,Moving,&profile,tick,prepared)&&prepared.simulation.get()==preparedWorld,"replay rebuilt its physics world");
            for(unsigned j=0;j<profile.jointCount;++j) {
                const auto& a=baked.rotations[size_t(tick)*profile.jointCount+j];const auto& b=prepared.local[j];
                Check(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.999999f,"prepared/replayed world differs from offline bake");
            }
        }
    }
    // 只选链首和链尾：未选中间骨保留局部驱动，仍继承物理父骨，验证烘焙稀疏调色板可还原整条链。
    const unsigned sparseSelection[]={2,4};physics::Profile sparse;
    Check(physics::Build(bones,sparseSelection,1,sparse,error),"sparse physics selection rejected");
    physics::Bake sparseBake;Check(physics::Generate(sparse,42,3,Moving,&sparse,nullptr,nullptr,nullptr,sparseBake,error),"sparse bake rejected");
    physics::State sparseLive;
    for(unsigned tick=0;tick<sparseBake.frames;++tick) {
        const auto seconds=double(tick)/physics::Hz;
        std::array<physics::Pose,physics::MaxBones> drivers{},rebuilt{};std::array<Quaternion,physics::MaxJoints> palette{};
        Check(Moving(&sparse,seconds,std::span(drivers.data(),sparse.boneCount))&&physics::Advance(sparse,Moving,&sparse,tick,sparseLive)&&
            physics::Sample(sparseBake,sparse,seconds,palette),"sparse timeline rejected");
        for(unsigned ordinal=0;ordinal<sparse.boneCount;++ordinal) {
            const auto i=sparse.order[ordinal];const auto& node=sparse.bones[i];const auto parent=node.parent;
            if(parent<0){rebuilt[i]=drivers[i];continue;}
            const auto rotation=node.joint>=0?palette[node.joint]:vmd::Multiply(vmd::Inverse(drivers[parent].rotation),drivers[i].rotation);
            const auto position=ik::Rotate(vmd::Inverse(drivers[parent].rotation),ik::Sub(drivers[i].position,drivers[parent].position));
            rebuilt[i]={ik::Add(rebuilt[parent].position,ik::Rotate(rebuilt[parent].rotation,position)),vmd::Multiply(rebuilt[parent].rotation,rotation)};
            const auto& actual=sparseLive.poses[i].rotation;const auto& expected=rebuilt[i].rotation;
            Check(std::fabs(actual.x*expected.x+actual.y*expected.y+actual.z*expected.z+actual.w*expected.w)>.999999f&&
                ik::Length(ik::Sub(rebuilt[i].position,sparseLive.poses[i].position))<.00001f,"sparse bridge lost physical parent inheritance");
        }
    }
    Check(physics::Advance(profile,Moving,&profile,900,live)&&live.resets==1,"long stall did not reset bounded simulation");
    Check(physics::Advance(profile,Moving,&profile,1,live)&&live.resets==2,"rewind retained stale velocity");
    physics::Bake repeated;Check(physics::Generate(profile,42,3,Moving,&profile,nullptr,nullptr,nullptr,repeated,error),"repeat bake rejected");
    Check(repeated.rotations.size()==baked.rotations.size()&&!std::memcmp(repeated.rotations.data(),baked.rotations.data(),baked.rotations.size()*sizeof(Quaternion)),"bake is nondeterministic");
    Cancellation cancellation;Check(!physics::Generate(profile,42,3,Moving,&profile,Cancel,nullptr,&cancellation,repeated,error)&&error=="PHYSICS_BAKE_CANCELLED"&&repeated.frames==361,"cancel published partial bake");
    Check(!physics::Generate(profile,42,1e6,Moving,&profile,nullptr,nullptr,nullptr,repeated,error)&&error=="PHYSICS_BAKE_SIZE_LIMIT","unbounded bake allocation accepted");
    const auto path=std::filesystem::temp_directory_path()/(L"ssmt-physics-test-"+std::to_wstring(GetCurrentProcessId())+L".bin");
    Check(physics::Save(path,baked,error),error.c_str());physics::Bake loaded;
    Check(physics::Load(path,profile,42,3,loaded,error),error.c_str());
    auto statistics=baked;statistics.contacts=31;statistics.unresolved=7;statistics.maxPenetration=.003f;statistics.maxMergeOffset=.004f;
    Check(physics::Save(path,statistics,error)&&physics::Load(path,profile,42,3,loaded,error)&&
        loaded.contacts==31&&loaded.unresolved==7&&loaded.maxPenetration==.003f&&loaded.maxMergeOffset==.004f,"cache lost collision statistics");
    for(uint32_t version=1;version<physics::Version;++version) {
        {std::fstream legacy(path,std::ios::binary|std::ios::in|std::ios::out);
            legacy.seekp(8);legacy.write(reinterpret_cast<const char*>(&version),sizeof(version));}
        Check(!physics::Load(path,profile,42,3,loaded,error)&&error=="PHYSICS_CACHE_HEADER_REJECTED","old point-only collision cache accepted");
    }
    Check(physics::Save(path,baked,error),"current physics cache rewrite failed");
    Check(!physics::Load(path,profile,43,3,loaded,error),"bake from another motion accepted");
    physics::Profile changed;Check(physics::Build(bones,selection,2,changed,error),"preset rejected");
    Check(!physics::Load(path,changed,42,3,loaded,error),"bake from another profile accepted");
    {std::fstream corrupt(path,std::ios::binary|std::ios::in|std::ios::out);corrupt.seekg(-1,std::ios::end);char byte=0;corrupt.read(&byte,1);byte^=1;corrupt.seekp(-1,std::ios::end);corrupt.write(&byte,1);}
    Check(!physics::Load(path,profile,42,3,loaded,error)&&error=="PHYSICS_CACHE_CHECKSUM_REJECTED","corrupt bake accepted");
    std::filesystem::remove(path);
    std::cout<<"Physics joints="<<profile.jointCount<<" steps="<<baked.frames<<" max_swing_rad="<<maximumDeviation<<" live/bake parity PASS\n";
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if((argc==6||argc==7)&&std::wstring_view(argv[1])==L"--skin-topology-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,argc==7?argv[6]:nullptr,argv[5],nullptr,false,0,true);return 0;
        }
        if(argc==7&&std::wstring_view(argv[1])==L"--skin-window-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,nullptr,argv[5],nullptr,false,std::stod(argv[6]));return 0;
        }
        if(argc==3&&std::wstring_view(argv[1])==L"--face-fixture"){FaceOracle();FaceFixture(argv[2]);return 0;}
        if(argc==5&&std::wstring_view(argv[1])==L"--profile-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,nullptr,nullptr,nullptr,true);return 0;
        }
        if(argc==7&&std::wstring_view(argv[1])==L"--skin-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,argv[5],argv[6]);return 0;
        }
        if(argc==6&&std::wstring_view(argv[1])==L"--skin-default-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,nullptr,argv[5]);return 0;
        }
        if(argc==6&&std::wstring_view(argv[1])==L"--skin-driver-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],true,nullptr,argv[5]);return 0;
        }
        if(argc==8&&std::wstring_view(argv[1])==L"--skin-pose-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,std::wstring_view(argv[5])==L"-"?nullptr:argv[5],argv[6],argv[7]);return 0;
        }
        if(argc==6&&std::wstring_view(argv[1])==L"--native-fixture") {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],false,argv[5]);return 0;
        }
        FaceOracle();if(argc==5&&(std::wstring_view(argv[1])==L"--fixture"||std::wstring_view(argv[1])==L"--reach-fixture")) {
            PhysicsMotionFixture(argv[2],argv[3],argv[4],std::wstring_view(argv[1])==L"--reach-fixture");return 0;}
        Check(argc==1||argc==4,"usage: PoserPhysicsTests [--fixture] [Avatar.fixture Body.fixture Motion.vmd]");SkinStrainOracle();ActualTriangleContacts();AnimatedColliderFrame();
        Test();ClothSettling();StaticShape();PlayerGirlAmice();StructuralCandidates();SegmentCollisions();StaticRigidBodyOffsets();MultiBodyContacts();GarmentSelfContact();FoldedBranchContact();PhysicsMotionOracle();SkinSurfaceOracle();
        if(argc==4)PhysicsMotionFixture(argv[1],argv[2],argv[3]);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
