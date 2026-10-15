#include "PhysicsMotion.h"
#include "AvatarReference.h"
#include "BasisRetarget.h"
#include "SkinnedSurfaceFixture.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <cstring>

using namespace poser;
namespace {
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Record {avatar_reference::Node node;avatar_reference::Local local;uint32_t name,path;};
struct Bind {int index,parent;wchar_t name[128];reference::Matrix inverse;};
}
void PhysicsMotionOracle() {
    const Quaternion yaw{0,.707106781f,0,.707106781f};
    vmd::Clip clip;clip.lastFrame=60;
    for(const auto name:{L"全ての親",L"センター",L"グルーブ"}) {vmd::Track track;track.name=name;track.keys.push_back({});clip.tracks.push_back(track);}
    clip.tracks[0].keys[0].position={10,0,0};clip.tracks[0].keys[0].rotation=yaw;
    clip.tracks[1].keys[0].position={0,1,2};clip.tracks[1].keys[0].rotation=yaw;
    clip.tracks[2].keys[0].position={1,0,0};
    const physics::InputBone bones[]={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,false},
        {L"Bip001",0,{{0,1,0},{0,0,0,1}},{0,1,0},true,true},
        {L"Bone_Hair",1,{{.1f,1,0},{0,0,0,1}},{.1f,0,0},true,false}
    };
    const unsigned selected[]={2};physics::Profile profile;std::string error;
    Check(physics::Build(bones,selected,1,profile,error),error.c_str());
    physics::Motion motion;motion.profile=&profile;motion.clip=&clip;motion.actorRoot=0;motion.motionRoot=1;
    motion.allTrack=0;motion.centerTrack=1;motion.grooveTrack=2;motion.sourceCount=3;motion.sources[0]={-1,0};motion.sources[1]={0,1};motion.sources[2]={1,2};
    motion.sourceForBone.fill(-1);motion.sourceForBone[1]=0;
    std::array<physics::Pose,3> poses{};
    Check(physics::SampleMotion(&motion,0,poses),"motion oracle sampling rejected");
    Check(ik::Length(ik::Sub(poses[1].position,{-1.1f,1.1f,0}))<.00001f,"physics body driver lost opening/root hierarchy");
    Check(physics::SampleMotion(&motion,1,poses)&&std::fabs(poses[1].rotation.y-yaw.y)<.00001f,"mother rotation missing in physics driver");
    Check(physics::SampleMotion(&motion,0,poses)&&std::fabs(poses[1].rotation.w-1)<.00001f,
        "opening pose blend differs from live body driver");
    Check(physics::SampleMotion(&motion,.175,poses),"opening hierarchy sample rejected");
    const auto inherited=vmd::Multiply(vmd::Inverse(poses[1].rotation),poses[2].rotation);
    Check(std::fabs(inherited.w)> .999999f,"unmapped child blended an already blended parent a second time");
    physics::Bake bake;Check(physics::Generate(profile,123,2,physics::SampleMotion,&motion,nullptr,nullptr,nullptr,bake,error),error.c_str());
    physics::State live;
    for(unsigned frame=0;frame<bake.frames;++frame) {
        Check(physics::Advance(profile,physics::SampleMotion,&motion,frame,live),"motion live advance rejected");
        const auto& a=bake.rotations[frame];const auto& b=live.local[0];
        Check(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.999999f,"motion bake/live driver differs");
    }
    // 同一个身体平移必须同时满足双足，且衣物驱动跟随适配后的根；重播不累计偏移。
    vmd::Clip reachClip;reachClip.lastFrame=60;
    for(const auto name:{L"センター",L"左足ＩＫ",L"右足ＩＫ"}) {vmd::Track track;track.name=name;track.keys.push_back({});reachClip.tracks.push_back(track);}
    reachClip.tracks[0].keys[0].position={0,5,0};reachClip.tracks[1].keys[0].position={4,0,0};reachClip.tracks[2].keys[0].position={-4,0,0};
    const physics::InputBone reachBones[]={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,false},
        {L"Bip001",0,{{0,2,0},{0,0,0,1}},{0,2,0},true,true},
        {L"Bip001 Pelvis",1,{{0,2,0},{0,0,0,1}},{},true,true},
        {L"Bip001 L Thigh",2,{{-.1f,2,0},{0,0,0,1}},{-.1f,0,0},true,true},
        {L"Bip001 L Calf",3,{{-.1f,1,0},{0,0,0,1}},{0,-1,0},true,true},
        {L"Bip001 L Foot",4,{{-.1f,0,0},{0,0,0,1}},{0,-1,0},true,true},
        {L"Bip001 R Thigh",2,{{.1f,2,0},{0,0,0,1}},{.1f,0,0},true,true},
        {L"Bip001 R Calf",6,{{.1f,1,0},{0,0,0,1}},{0,-1,0},true,true},
        {L"Bip001 R Foot",7,{{.1f,0,0},{0,0,0,1}},{0,-1,0},true,true},
        {L"Bone_Amice01",1,{{.2f,2,0},{0,0,0,1}},{.2f,0,0},true,false},
        {L"Bone_Amice02",9,{{.2f,1.8f,0},{0,0,0,1}},{0,-.2f,0},true,false},
        {L"UnlabelledKneeHelper",3,{{-.1f,1,0},ik::AxisRotation({1,0,0},.7f)},{0,-1,0},true,false}
    };
    const unsigned reachSelected[]={9,10};physics::Profile reachProfile;
    Check(physics::Build(reachBones,reachSelected,1,reachProfile,error),error.c_str());
    physics::Motion reachMotion;reachMotion.profile=&reachProfile;reachMotion.clip=&reachClip;reachMotion.actorRoot=0;reachMotion.motionRoot=1;
    physics::StaticJointFollowers(reachBones,reachMotion.jointFollower);
    reachMotion.centerTrack=0;reachMotion.footIK=true;
    for(unsigned side=0;side<2;++side) {
        auto& leg=reachMotion.legs[side];leg.thigh=side?6:3;leg.calf=side?7:4;leg.foot=side?8:5;leg.track=side?2:1;
        leg.chainReady=true;leg.hingeReady=ik::Hinge({0,-1,0},{0,-1,0},leg.hinge);
    }
    std::array<physics::Pose,std::size(reachBones)> reachPoses{};
    Check(physics::SampleMotion(&reachMotion,1,reachPoses),"root reach driver failed");
    const auto expectedKneeHelper=vmd::Multiply(reachPoses[4].rotation,reachBones[11].rest.rotation),actualKneeHelper=reachPoses[11].rotation;
    Check(std::fabs(expectedKneeHelper.x*actualKneeHelper.x+expectedKneeHelper.y*actualKneeHelper.y+
        expectedKneeHelper.z*actualKneeHelper.z+expectedKneeHelper.w*actualKneeHelper.w)>.999999f,
        "shared static pivot helper did not follow final IK");
    Check(ik::Length(ik::Sub(reachPoses[5].position,{-.5f,0,0}))<.00001f&&
        ik::Length(ik::Sub(reachPoses[8].position,{.5f,0,0}))<.00001f,"root driver misses original bilateral targets");
    Check(reachPoses[1].position.y<2&&ik::Length(ik::Sub(reachPoses[9].position,ik::Add(reachPoses[1].position,{.2f,0,0})))<.00001f,
        "cloth driver did not inherit corrected root");
    const auto corrected=reachPoses[1].position;
    Check(physics::SampleMotion(&reachMotion,0,reachPoses)&&physics::SampleMotion(&reachMotion,1,reachPoses)&&
        ik::Length(ik::Sub(reachPoses[1].position,corrected))<.00001f,"root reach accumulated previous playback offset");
    physics::Bake reachBake;Check(physics::Generate(reachProfile,124,2,physics::SampleMotion,&reachMotion,nullptr,nullptr,nullptr,reachBake,error),error.c_str());
    physics::State reachState;
    for(unsigned tick=0;tick<reachBake.frames;++tick) {
        Check(physics::Advance(reachProfile,physics::SampleMotion,&reachMotion,tick,reachState),"root reach live physics failed");
        for(unsigned j=0;j<reachProfile.jointCount;++j) {
            const auto& a=reachBake.rotations[size_t(tick)*reachProfile.jointCount+j];const auto& b=reachState.local[j];
            Check(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)>.999999f,"adapted root differs in live/bake physics");
        }
    }
    reachMotion.footIK=false;Check(physics::SampleMotion(&reachMotion,1,reachPoses)&&std::fabs(reachPoses[1].position.y-2.5f)<.00001f,
        "disabled foot IK changed authored body trajectory");
    const physics::InputBone helperBones[]={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
        {L"BodyJoint",0,{{0,1,0},{0,0,0,1}},{0,1,0},true,true},
        {L"UnlabelledLeaf",0,{{0,1,0},ik::AxisRotation({1,0,0},.7f)},{0,1,0},true,false}
    };
    std::array<int,3> followers;physics::StaticJointFollowers(helperBones,followers);
    Check(followers[0]<0&&followers[1]<0&&followers[2]==1,"static shared pivot helper not linked");
    physics::Profile helperProfile;Check(physics::Build(helperBones,{},1,helperProfile,error),error.c_str());
    auto helperMotion=motion;helperMotion.profile=&helperProfile;helperMotion.sourceForBone[2]=-1;helperMotion.jointFollower[2]=1;
    std::array<physics::Pose,3> helperPoses{};Check(physics::SampleMotion(&helperMotion,1,helperPoses),"joint helper driver rejected");
    const auto expected=vmd::Multiply(helperPoses[1].rotation,helperBones[2].rest.rotation),actual=helperPoses[2].rotation;
    Check(std::fabs(expected.x*actual.x+expected.y*actual.y+expected.z*actual.z+expected.w*actual.w)>.999999f,
        "joint helper lost its static orientation");
    std::cout<<"Bilateral root reach: planted targets, cloth driver, replay and live/bake parity PASS\n";
}
void PhysicsMotionFixture(const wchar_t* avatarPath,const wchar_t* bodyPath,const wchar_t* motionPath,bool reachOnly,const wchar_t* nativeSelection,const wchar_t* skinPath,const wchar_t* posePath,bool inspectOnly,double diagnosticSeconds,bool topologyOnly) {
    std::ifstream avatar(avatarPath,std::ios::binary);uint32_t count=0;avatar.read(reinterpret_cast<char*>(&count),sizeof(count));
    Check(count>0&&count<=physics::MaxBones,"physics Avatar fixture size invalid");
    std::vector<Record> records(count);avatar.read(reinterpret_cast<char*>(records.data()),std::streamsize(count*sizeof(Record)));
    Check(bool(avatar)&&avatar.peek()==EOF,"physics Avatar fixture truncated");
    std::vector<avatar_reference::Node> nodes(count);std::vector<avatar_reference::Local> locals(count);
    std::vector<reference::Pose> model(count);std::vector<uint8_t> required(count);std::vector<std::wstring> names(count);
    for(unsigned i=0;i<count;++i){nodes[i]=records[i].node;locals[i]=records[i].local;names[i]=L"静态中间骨 "+std::to_wstring(i);}
    const auto locate=[&](std::wstring_view name) {
        char utf8[512]{};const auto size=WideCharToMultiByte(CP_UTF8,0,name.data(),int(name.size()),utf8,sizeof(utf8),nullptr,nullptr);
        const auto hash=avatar_reference::Hash(std::string_view(utf8,size));int found=-1;
        for(unsigned i=0;i<count;++i)if(records[i].name==hash){Check(found<0,"duplicate fixture CRC");found=int(i);}return found;
    };
    const auto mark=[&](int index) {while(index>=0){Check(unsigned(index)<count,"bad fixture ancestor");required[index]=1;index=nodes[index].parent;}};
    std::ifstream body(bodyPath,std::ios::binary);std::vector<Bind> binds;Bind bind{};
    while(body.read(reinterpret_cast<char*>(&bind),sizeof(bind))) {const auto index=locate(bind.name);Check(index>=0,"fixture bind missing Avatar node");names[index]=bind.name;mark(index);binds.push_back(bind);}
    Check(body.eof()&&!body.gcount()&&!binds.empty()&&binds.size()<=physics::MaxBones,"physics Body fixture truncated");
    for(const auto name:{L"Bip001",L"Bip001 Pelvis",L"Bip001 Spine",L"Bip001 Spine2",L"Bip001 Neck",L"Bip001 Head",L"Body"}) {
        const auto index=locate(name);Check(index>=0,"required fixture body node missing");names[index]=name;mark(index);
    }
    Check(avatar_reference::Compose(nodes,locals,model,required),"physics actual static TRS invalid");
    for(const auto& item:binds) {
        reference::Pose expected;Check(reference::Decode(item.inverse,expected),"physics bind decode invalid");
        const auto& actual=model[locate(item.name)];
        Check(basis::Angle(actual.rotation,expected.rotation)<.01f&&ik::Length(ik::Sub(actual.position,expected.position))<.0001f,"physics reference does not match Body");
    }
    std::vector<physics::InputBone> bones(count);
    for(unsigned i=0;i<count;++i)bones[i]={names[i],nodes[i].parent,{model[i].position,model[i].rotation},
        {locals[i].position[0],locals[i].position[1],locals[i].position[2]},required[i]!=0,false};
    for(auto& bone:bones)bone.skinned=false;
    for(const auto& item:binds)bones[locate(item.name)].skinned=true;
    bones[0].name=L"Actor";vmd::Clip clip;std::string error;Check(vmd::Load(motionPath,clip,error),error.c_str());
    Check(std::isfinite(diagnosticSeconds)&&diagnosticSeconds>=0,"invalid diagnostic duration");
    if(diagnosticSeconds>0){clip.lastFrame=std::min(clip.lastFrame,unsigned(diagnosticSeconds*30));std::cout<<"DIAGNOSTIC_WINDOW seconds="<<diagnosticSeconds<<" full_motion_acceptance=0\n";}
    const auto track=[&](std::wstring name) {
        auto index=vmd::FindTrack(clip,name);if(index>=0)return index;
        for(auto& ch:name)if(ch>=L'０'&&ch<=L'９')ch=ch-L'０'+L'0';
        const auto ik=name.find(L"ＩＫ");if(ik!=std::wstring::npos)name.replace(ik,2,L"IK");return vmd::FindTrack(clip,name);
    };
    physics::Motion motion;motion.clip=&clip;motion.actorRoot=0;motion.motionRoot=locate(L"Bip001");motion.sourceForBone.fill(-1);
    motion.allTrack=track(L"全ての親");motion.centerTrack=track(L"センター");motion.grooveTrack=track(L"グルーブ");
    const auto source=[&](const std::wstring& name,const std::wstring& target,int parent) {
        const auto index=int(motion.sourceCount++);Check(motion.sourceCount<=motion.sources.size(),"fixture source limit");
        motion.sources[index]={parent,track(name)};const auto bone=target.empty()?-1:locate(target);
        // UpperArm 等无 Body 蒙皮权重的静态中间骨仍提供碰撞体，按 Avatar CRC
        // 恢复已核验的通用映射名称，离线输入须与实时完整骨架一致。
        if(bone>=0){names[bone]=target;bones[bone].name=names[bone];}
        if(bone>=0&&motion.sources[index].track>=0){motion.sourceForBone[bone]=index;bones[bone].controlled=true;}return index;
    };
    const auto all=source(L"全ての親",L"Bip001",-1),center=source(L"センター",L"",all),groove=source(L"グルーブ",L"",center),waist=source(L"腰",L"",groove);
    const auto lower=source(L"下半身",L"Bip001 Pelvis",waist),upper=source(L"上半身",L"Bip001 Spine",waist);
    const auto upper2=source(L"上半身2",L"Bip001 Spine2",upper),upper3=source(L"上半身3",L"",upper2);
    const auto neck=source(L"首",L"Bip001 Neck",upper3);source(L"頭",L"Bip001 Head",neck);
    for(unsigned side=0;side<2;++side) {
        const std::wstring prefix=side?L"右":L"左",target=side?L"Bip001 R ":L"Bip001 L ";
        const auto shoulderP=source(prefix+L"肩P",L"",upper3),shoulder=source(prefix+L"肩",target+L"Clavicle",shoulderP),shoulderC=source(prefix+L"肩C",L"",shoulder);
        const auto arm=source(prefix+L"腕",target+L"UpperArm",shoulderC),twist=source(prefix+L"腕捩",L"",arm),elbow=source(prefix+L"ひじ",target+L"Forearm",twist);
        const auto handTwist=source(prefix+L"手捩",L"",elbow),hand=source(prefix+L"手首",target+L"Hand",handTwist);
        for(unsigned finger=0;finger<5;++finger) {
            int parent=hand;
            for(unsigned joint=0;joint<3;++joint) {
                auto name=target+L"Finger"+std::to_wstring(finger);if(joint)name+=std::to_wstring(joint);
                parent=source(prefix+std::wstring(fingers::Names[finger])+wchar_t(L'０'+joint+(finger?1:0)),name,parent);
            }
        }
        const auto thigh=source(prefix+L"足",target+L"Thigh",lower),knee=source(prefix+L"ひざ",target+L"Calf",thigh),foot=source(prefix+L"足首",target+L"Foot",knee);
        source(prefix+L"つま先",target+L"Toe0",foot);
        auto& leg=motion.legs[side];leg.thigh=locate(target+L"Thigh");leg.calf=locate(target+L"Calf");leg.foot=locate(target+L"Foot");leg.toe=locate(target+L"Toe0");
        leg.track=track(prefix+L"足ＩＫ");leg.parentTrack=track(prefix+L"足ＩＫ親");leg.toeTrack=track(prefix+L"つま先ＩＫ");leg.ankleTrack=motion.sources[foot].track;
        leg.toggleTrack=vmd::FindIkTrack(clip,prefix+L"足ＩＫ");leg.toeToggleTrack=vmd::FindIkTrack(clip,prefix+L"つま先ＩＫ");
        leg.chainReady=leg.thigh>=0&&leg.calf>=0&&leg.foot>=0;
        Check(leg.chainReady,"fixture leg chain incomplete");
        leg.hingeReady=ik::Hinge(ik::Sub(model[leg.calf].position,model[leg.thigh].position),ik::Sub(model[leg.foot].position,model[leg.calf].position),leg.hinge);
        Check(leg.hingeReady,"fixture static hinge invalid");
    }
    const auto helper=locate(L"+PelvisTwist CF A01");if(helper>=0)motion.sourceForBone[helper]=lower;
    motion.footIK=true;
    for(unsigned i=0;i<count;++i)bones[i].controlled=motion.sourceForBone[i]>=0;
    physics::StaticJointFollowers(bones,motion.jointFollower);
    for(unsigned i=0;i<count;++i)if(motion.jointFollower[i]>=0) {
        bones[i].controlled=true;
        motion.sourceForBone[i]=motion.sourceForBone[motion.jointFollower[i]];
    }
    std::vector<physics::Candidate> candidates;Check(physics::Candidates(bones,candidates,error),error.c_str());
    std::vector<unsigned> selected;for(const auto& candidate:candidates)if(candidate.defaultSelected)selected.push_back(candidate.bone);
    std::vector<unsigned> pinned;
    if(nativeSelection) {
        selected.clear();std::ifstream native(std::filesystem::path(nativeSelection),std::ios::binary);Check(native.good(),"native selection fixture missing");
        std::string line;
        while(std::getline(native,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            Check(line.size()>2&&(line[0]=='A'||line[0]=='D')&&line[1]=='\t',"native selection row invalid");
            const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data()+2,int(line.size()-2),nullptr,0);
            Check(size>0,"native bone name is not UTF-8");std::wstring name(size,L'\0');
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data()+2,int(line.size()-2),name.data(),size);
            const auto index=locate(name);Check(index>=0,"native particle is absent from STATIC fixture");
            Check(std::find(selected.begin(),selected.end(),unsigned(index))==selected.end(),"duplicate native particle selection");
            selected.push_back(unsigned(index));if(line[0]=='A')pinned.push_back(unsigned(index));
        }
        Check(!selected.empty(),"native selection is empty");
    }
    physics::Profile profile;const bool built=physics::Build(bones,selected,1,profile,error);Check(built,error.c_str());motion.profile=&profile;
    if(nativeSelection) {
        // 只读原生粒子图定义参与骨与固定位置的根；不缩小代理或放宽现有碰撞门控。
        for(auto& joint:std::span(profile.joints.data(),profile.jointCount))if(std::find(pinned.begin(),pinned.end(),joint.bone)!=pinned.end())joint.translationLimit=0;
        profile.key=physics::HashBytes(profile.key,profile.joints.data(),profile.jointCount*sizeof(physics::Joint));
        std::cout<<"Native particle participation joints="<<selected.size()<<" pinned_roots="<<pinned.size()<<'\n';
    }
    if(inspectOnly) {
        struct Pair {unsigned a,b;float depth;};std::vector<Pair> overlaps;
        std::array<int,physics::MaxJoints> parent{};
        for(unsigned j=0;j<profile.jointCount;++j) {
            parent[j]=-1;
            for(auto ancestor=profile.bones[profile.joints[j].bone].parent;ancestor>=0;ancestor=profile.bones[ancestor].parent)
                if(profile.bones[ancestor].joint>=0){parent[j]=profile.bones[ancestor].joint;break;}
            for(unsigned k=0;k<j;++k) {
                if(parent[j]==int(k)||parent[k]==int(j)||bones[profile.joints[j].bone].parent==bones[profile.joints[k].bone].parent)continue;
                const auto a=physics::CollisionSegment(profile.joints[j],bones[profile.joints[j].bone].rest);
                const auto b=physics::CollisionSegment(profile.joints[k],bones[profile.joints[k].bone].rest);
                const auto depth=profile.joints[j].radius+profile.joints[k].radius-physics::SegmentClearance(a[0],a[1],b[0],b[1]);
                if(depth>0)overlaps.push_back({j,k,depth});
            }
        }
        std::sort(overlaps.begin(),overlaps.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
        std::cout<<"STATIC self proxy overlaps="<<overlaps.size()<<'\n';
        for(unsigned i=0;i<std::min(size_t(12),overlaps.size());++i) {
            const auto& pair=overlaps[i];
            const auto utf8=[](std::wstring_view name){char buffer[512]{};const auto n=WideCharToMultiByte(CP_UTF8,0,name.data(),int(name.size()),buffer,sizeof(buffer),nullptr,nullptr);return std::string(buffer,n);};
            std::cout<<"STATIC overlap="<<utf8(bones[profile.joints[pair.a].bone].name)<<'/'<<utf8(bones[profile.joints[pair.b].bone].name)<<" depth="<<pair.depth<<'\n';
        }
        return;
    }
    validation::SkinnedSurfaceFixture surface;
    surface.Load(skinPath,std::span<const Bind>(binds),locate,profile,candidates);
    if(topologyOnly){std::cout<<"TOPOLOGY_ONLY full_motion_acceptance=0\n";return;}
    if(posePath) {
        std::cout<<"Stored skin pose diagnostic: current motion driver is not resampled\n";
        surface.ReplayPose(posePath,profile);
        Check(!surface.HasNewCrossings(),"cached skinned triangles gain body intersections relative to STATIC");
        return;
    }
    unsigned unreachable=0,remaining=0,windowUnreachable=0,windowRemaining=0;
    float maxBefore=0,maxAfter=0,maxOffset=0,windowBefore=0,windowAfter=0;
    std::array<physics::Pose,physics::MaxBones> drivers{};
    for(unsigned frame=11;frame<=clip.lastFrame;++frame) {
        Check(physics::SampleMotion(&motion,double(frame)/30,std::span(drivers.data(),count)),"actual static reach driver failed");
        const auto controllerAt=[&](int index) {
            if(index<0)return ik::ControllerPose{};
            return ik::ControllerPose{ik::Scale(ik::ModelPosition(vmd::SamplePosition(clip.tracks[index],frame)),.1f),
                basis::ModelInput(vmd::Sample(clip.tracks[index],frame))};
        };
        const auto allPose=controllerAt(motion.allTrack),centerPose=controllerAt(motion.centerTrack),groovePose=controllerAt(motion.grooveTrack);
        const auto unadaptedRoot=ik::Add(ik::Rotate(allPose.rotation,bones[motion.motionRoot].rest.position),
            ik::Add(allPose.position,ik::Rotate(allPose.rotation,ik::Add(centerPose.position,ik::Rotate(centerPose.rotation,groovePose.position)))));
        const auto offset=ik::Sub(drivers[motion.motionRoot].position,unadaptedRoot);maxOffset=std::max(maxOffset,ik::Length(offset));
        for(unsigned side=0;side<2;++side) {
            const auto& leg=motion.legs[side];
            if(leg.toggleTrack>=0&&!vmd::SampleIkEnabled(clip,clip.ikTracks[leg.toggleTrack].name,frame))continue;
            const auto goal=ik::FootController(bones[leg.foot].rest.position,{},allPose,controllerAt(leg.parentTrack),controllerAt(leg.track)).position;
            const auto reach=ik::Length(ik::Sub(bones[leg.calf].rest.position,bones[leg.thigh].rest.position))+
                ik::Length(ik::Sub(bones[leg.foot].rest.position,bones[leg.calf].rest.position));
            const auto before=std::max(0.0f,ik::Length(ik::Sub(goal,ik::Sub(drivers[leg.thigh].position,offset)))-reach);
            const auto after=ik::Length(ik::Sub(goal,drivers[leg.foot].position));
            unreachable+=before>.0001f;remaining+=after>.0001f;maxBefore=std::max(maxBefore,before);maxAfter=std::max(maxAfter,after);
            if(frame>=300&&frame<=600) {
                windowUnreachable+=before>.0001f;windowRemaining+=after>.0001f;
                windowBefore=std::max(windowBefore,before);windowAfter=std::max(windowAfter,after);
            }
        }
    }
    std::cout<<"Static body validation nodes="<<count<<" Body_binds="<<binds.size()<<" physics_joints="<<profile.jointCount<<'\n';
    std::cout<<"Root reach original_unreachable="<<unreachable<<" remaining="<<remaining<<" max_before="<<maxBefore
        <<" max_original_target_error="<<maxAfter<<" max_root_offset="<<maxOffset<<'\n';
    std::cout<<"Root reach 10-20s original_unreachable="<<windowUnreachable<<" remaining="<<windowRemaining
        <<" max_before="<<windowBefore<<" max_original_target_error="<<windowAfter<<'\n';
    std::array<physics::Pose,physics::MaxBones> previousDriver{};float maxDriverPosition=0,maxDriverRotation=0;
    unsigned driverPositionTick=0,driverRotationTick=0,driverRotationBone=0;
    for(unsigned tick=0;tick<physics::FrameCount(double(clip.lastFrame)/30);++tick) {
        Check(physics::SampleMotion(&motion,double(tick)/physics::Hz,std::span(drivers.data(),count)),"body continuity sampling failed");
        if(tick)for(unsigned i=0;i<count;++i)if(bones[i].controlled) {
            const auto parent=profile.bones[i].parent;
            const auto current=parent>=0?vmd::Multiply(vmd::Inverse(drivers[parent].rotation),drivers[i].rotation):drivers[i].rotation;
            const auto prior=parent>=0?vmd::Multiply(vmd::Inverse(previousDriver[parent].rotation),previousDriver[i].rotation):previousDriver[i].rotation;
            const auto change=basis::Angle(prior,current);
            if(change>maxDriverRotation){maxDriverRotation=change;driverRotationTick=tick;driverRotationBone=i;}
        }
        if(tick) {
            const auto change=ik::Length(ik::Sub(drivers[motion.motionRoot].position,previousDriver[motion.motionRoot].position));
            if(change>maxDriverPosition){maxDriverPosition=change;driverPositionTick=tick;}
        }
        if(reachOnly&&skinPath&&tick%4==0)surface.Observe(profile,std::span(drivers.data(),count),tick);
        previousDriver=drivers;
    }
    std::cout<<"Body driver continuity max_root_step="<<maxDriverPosition<<" root_tick="<<driverPositionTick
        <<" max_local_angle_deg="<<maxDriverRotation<<" angle_tick="<<driverRotationTick<<" bone="<<driverRotationBone<<std::endl;
    if(reachOnly){
        surface.Report(false);auto counts=surface.InvertedBoneCounts();
        for(unsigned k=0;k<8;++k){const auto i=unsigned(std::max_element(counts.begin(),counts.end())-counts.begin());if(!counts[i])break;char name[512]{};const auto n=WideCharToMultiByte(CP_UTF8,0,bones[i].name.data(),int(bones[i].name.size()),name,sizeof(name),nullptr,nullptr);std::cout<<"Body inversion influence bone="<<std::string(name,n)<<" samples="<<counts[i]<<'\n';counts[i]=0;}
        return;
    }
    const auto started=std::chrono::steady_clock::now();
    const auto progress=[](void* context,unsigned percent) {
        auto& previous=*static_cast<unsigned*>(context);
        if(percent>=previous+10){previous=percent;std::cout<<"Physics bake progress="<<percent<<"%\n"<<std::flush;}
    };
    unsigned lastProgress=0;
    physics::Bake bake;Check(physics::Generate(profile,123,double(clip.lastFrame)/30,physics::SampleMotion,&motion,nullptr,progress,&lastProgress,bake,error),error.c_str());
    const auto generated=std::chrono::steady_clock::now();
    const auto cache=std::filesystem::temp_directory_path()/(L"ssmt-motion-physics-"+std::to_wstring(GetCurrentProcessId())+L".bin");
    physics::Bake loaded;
    const auto cacheVerified=physics::Save(cache,bake,error)&&physics::Load(cache,profile,123,double(clip.lastFrame)/30,loaded,error)&&
        loaded.positions.size()==bake.positions.size()&&loaded.rotations.size()==bake.rotations.size()&&
        !std::memcmp(loaded.positions.data(),bake.positions.data(),bake.positions.size()*sizeof(Vector3))&&
        !std::memcmp(loaded.rotations.data(),bake.rotations.data(),bake.rotations.size()*sizeof(Quaternion));
    if(!cacheVerified)std::cout<<"Cache validation FAIL reason="<<error<<'\n';
    std::filesystem::remove(cache);
    physics::State state;float maximumError=0,maximumPositionError=0,maximumRootDisplacement=0,maximumPaletteError=0;std::array<float,physics::MaxJoints> penetration{};
#ifdef SSMT_POSER_TRACE_PHYSICS
    physics::PhaseTrace phases;state.trace=&phases;
#endif
    unsigned displacedJoint=0,displacedTick=0;float maximumChainDrift=0;
    std::array<Quaternion,physics::MaxColliders> previousCollider{},previousWorldAxis{};
    float maximumColliderAngle=0,maximumWorldAxisAngle=0;
    unsigned colliderAngleTick=0,worldAxisAngleTick=0,colliderAngleIndex=0,worldAxisAngleIndex=0;
    float maximumSelfGeometry=0;unsigned geometryPairA=0,geometryPairB=0;
    std::array<int,physics::MaxJoints> physicalParent{};
    for(unsigned j=0;j<profile.jointCount;++j) {
        physicalParent[j]=-1;
        for(auto ancestor=profile.bones[profile.joints[j].bone].parent;ancestor>=0;ancestor=profile.bones[ancestor].parent)
            if(profile.bones[ancestor].joint>=0){physicalParent[j]=profile.bones[ancestor].joint;break;}
    }
    std::array<unsigned,physics::MaxJoints> worstCollider{},worstTick{};
    std::array<float,physics::MaxJoints> worstAnchorDistance{};
    std::array<Vector3,physics::MaxJoints> worstRootDelta{};
    for(unsigned tick=0;tick<bake.frames;++tick) {
        Check(physics::Advance(profile,physics::SampleMotion,&motion,tick,state),"actual motion live step rejected");
        const auto seconds=double(tick)/physics::Hz;
        Check(physics::SampleMotion(&motion,seconds,std::span(drivers.data(),count)),"actual motion palette driver failed");
        for(unsigned j=0;j<profile.jointCount;++j)if(profile.joints[j].translationLimit==0)maximumChainDrift=std::max(maximumChainDrift,ik::Length(state.positionDelta[j]));
        for(unsigned c=0;c<profile.colliderCount;++c) {
            const auto& collider=profile.colliders[c];
            const auto rotation=physics::BodyColliderPose(profile,c,std::span(drivers.data(),count)).rotation;
            const auto worldAxis=ik::FromTo({0,1,0},ik::Sub(drivers[collider.b].position,drivers[collider.a].position));
            if(tick) {
                const auto current=basis::Angle(rotation,previousCollider[c]),legacy=basis::Angle(worldAxis,previousWorldAxis[c]);
                if(current>maximumColliderAngle){maximumColliderAngle=current;colliderAngleTick=tick;colliderAngleIndex=c;}
                if(legacy>maximumWorldAxisAngle){maximumWorldAxisAngle=legacy;worldAxisAngleTick=tick;worldAxisAngleIndex=c;}
            }
            previousCollider[c]=rotation;previousWorldAxis[c]=worldAxis;
        }
        std::array<physics::Pose,physics::MaxBones> rebuilt{};
        for(unsigned ordinal=0;ordinal<count;++ordinal) {
            const auto i=profile.order[ordinal];const auto& node=profile.bones[i];if(!node.valid)continue;
            if(node.parent<0){rebuilt[i]=drivers[i];continue;}
            const auto inverse=vmd::Inverse(drivers[node.parent].rotation);
            const auto rotation=node.joint>=0?bake.rotations[size_t(tick)*profile.jointCount+node.joint]:vmd::Multiply(inverse,drivers[i].rotation);
            const auto position=node.joint>=0?ik::Add(node.localPosition,bake.positions[size_t(tick)*profile.jointCount+node.joint]):
                ik::Rotate(inverse,ik::Sub(drivers[i].position,drivers[node.parent].position));
            const auto& parent=rebuilt[node.parent];
            rebuilt[i]={ik::Add(parent.position,ik::Rotate(parent.rotation,position)),vmd::Multiply(parent.rotation,rotation)};
            maximumPaletteError=std::max(maximumPaletteError,ik::Length(ik::Sub(rebuilt[i].position,state.poses[i].position)));
            Check(maximumPaletteError<.00001f&&basis::Angle(rebuilt[i].rotation,state.poses[i].rotation)<.01f,
                "cached local TRS does not reconstruct the geometry used by physical collision");
        }
        // 表面按 VMD 的 30Hz 检查；求解连续性仍逐个 120Hz 样本检查。
        if(tick%4==0)surface.Observe(profile,std::span(rebuilt.data(),count),tick);
        for(unsigned j=0;j<profile.jointCount;++j) {
            const auto& a=bake.rotations[size_t(tick)*profile.jointCount+j];const auto& b=state.local[j];
            const auto errorValue=std::max(0.0f,1-std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w));maximumError=std::max(maximumError,errorValue);
            Check(errorValue<.000001f,"actual motion live/bake mismatch");
            maximumPositionError=std::max(maximumPositionError,ik::Length(ik::Sub(bake.positions[size_t(tick)*profile.jointCount+j],state.positionDelta[j])));
            Check(maximumPositionError<.000001f,"actual motion live/bake position mismatch");
            if(ik::Length(state.positionDelta[j])>maximumRootDisplacement){maximumRootDisplacement=ik::Length(state.positionDelta[j]);displacedJoint=j;displacedTick=tick;}
            const auto& joint=profile.joints[j];
            const auto segment=physics::CollisionSegment(joint,state.poses[joint.bone]);
            for(unsigned c=0;c<profile.colliderCount;++c)if(joint.colliders&(1u<<c)) {
                const auto& collider=profile.colliders[c];
                const auto residual=joint.collisionRadius[c]-physics::SegmentClearance(segment[0],segment[1],
                    state.poses[collider.a].position,state.poses[collider.b].position,joint.collisionStart[c]);
                if(residual>penetration[j]) {
                    penetration[j]=residual;worstCollider[j]=c;worstTick[j]=tick;
                    auto root=joint.bone;
                    for(auto ancestor=profile.bones[root].parent;ancestor>=0;ancestor=profile.bones[ancestor].parent)
                        if(profile.bones[ancestor].joint>=0)root=unsigned(ancestor);
                    worstRootDelta[j]=state.positionDelta[profile.bones[root].joint];
                    worstAnchorDistance[j]=physics::SegmentClearance(state.poses[joint.bone].position,state.poses[joint.bone].position,
                        state.poses[collider.a].position,state.poses[collider.b].position);
                }
            }
            for(unsigned other=0;other<j;++other) {
                if(physicalParent[j]==int(other)||physicalParent[other]==int(j)||
                    profile.bones[joint.bone].parent==profile.bones[profile.joints[other].bone].parent)continue;
                const auto opposing=physics::CollisionSegment(profile.joints[other],state.poses[profile.joints[other].bone]);
                const auto residual=joint.radius+profile.joints[other].radius-physics::SegmentClearance(segment[0],segment[1],opposing[0],opposing[1]);
                if(residual>maximumSelfGeometry){maximumSelfGeometry=residual;geometryPairA=j;geometryPairB=other;}
            }
        }
    }
    std::cout<<"Physics Avatar nodes="<<count<<" body_binds="<<binds.size()<<" joints="<<profile.jointCount<<" colliders="<<profile.colliderCount
        <<" frames="<<bake.frames<<" bytes="<<bake.rotations.size()*(sizeof(Quaternion)+sizeof(Vector3))<<" contacts="<<state.contacts<<" max_quaternion_dot_error="<<maximumError
        <<" max_position_error="<<maximumPositionError<<" max_root_displacement="<<maximumRootDisplacement<<" max_palette_error="<<maximumPaletteError<<'\n';
    const auto finished=std::chrono::steady_clock::now();
    std::cout<<"Physics collision unresolved="<<state.unresolved<<" max_penetration="<<state.maxPenetration<<" max_merge_offset="<<state.maxMergeOffset<<'\n';
    std::cout<<"Physics continuity max_angular_step_rad="<<state.maxAngularStep<<" max_root_step="<<state.maxRootStep<<'\n';
    std::cout<<"Physics fixed attachment max_local_position_drift="<<maximumChainDrift<<'\n';
    std::cout<<"Body collider rotation max_step_deg="<<maximumColliderAngle<<" collider="<<colliderAngleIndex<<" tick="<<colliderAngleTick
        <<" legacy_world_axis_deg="<<maximumWorldAxisAngle<<" legacy_collider="<<worldAxisAngleIndex<<" legacy_tick="<<worldAxisAngleTick<<'\n';
    const auto utf8=[&](std::wstring_view name) {
        char buffer[512]{};const auto size=WideCharToMultiByte(CP_UTF8,0,name.data(),int(name.size()),buffer,sizeof(buffer),nullptr,nullptr);return std::string(buffer,size);
    };
#ifdef SSMT_POSER_TRACE_PHYSICS
    constexpr const char* phaseNames[]={"INTEGRATION_AND_FK","BODY_PROJECTION","SELF_PROJECTION","ROOT_PROJECTION"};
    for(unsigned i=0;i<phases.phases.size();++i) {
        const auto& peak=phases.phases[i];
        std::cout<<"Physics phase="<<phaseNames[i]<<" angle_rad="<<peak.angle<<" angle_bone="<<utf8(bones[profile.joints[peak.angleJoint].bone].name)
            <<" angle_tick="<<peak.angleStep<<" position="<<peak.position<<" position_bone="<<utf8(bones[profile.joints[peak.positionJoint].bone].name)
            <<" position_tick="<<peak.positionStep<<'\n';
    }
    std::cout<<"Physics single correction angle_rad="<<phases.correction.angle
        <<" bone="<<utf8(bones[profile.joints[phases.correction.angleJoint].bone].name)<<" tick="<<phases.correction.angleStep<<'\n';
#endif
    std::cout<<"Physics body unresolved="<<state.bodyUnresolved<<" max_penetration="<<state.bodyMaxPenetration
        <<" self_unresolved="<<state.selfUnresolved<<" self_max_penetration="<<state.selfMaxPenetration
        <<" self_pair="<<utf8(bones[profile.joints[state.selfPairA].bone].name)<<'/'<<utf8(bones[profile.joints[state.selfPairB].bone].name)<<'\n';
    std::cout<<"Physics self geometry maximum="<<maximumSelfGeometry<<" pair="<<utf8(bones[profile.joints[geometryPairA].bone].name)<<'/'<<utf8(bones[profile.joints[geometryPairB].bone].name)<<'\n';
    std::cout<<"Physics root displacement bone="<<utf8(bones[profile.joints[displacedJoint].bone].name)<<" tick="<<displacedTick<<" maximum="<<maximumRootDisplacement<<'\n';
    std::cout<<"Physics timing bake_ms="<<std::chrono::duration<double,std::milli>(generated-started).count()
        <<" live_sample_us="<<std::chrono::duration<double,std::micro>(finished-generated).count()/bake.frames<<'\n';
    for(unsigned rank=0;rank<5;++rank) {
        const auto worst=unsigned(std::max_element(penetration.begin(),penetration.begin()+profile.jointCount)-penetration.begin());
        const auto name=bones[profile.joints[worst].bone].name;
        const auto& joint=profile.joints[worst];const auto c=worstCollider[worst];
        std::cout<<"Collision residual bone="<<utf8(name)<<" maximum="<<penetration[worst]<<" collider="<<c<<" tick="<<worstTick[worst]
            <<" anchor_distance="<<worstAnchorDistance[worst]<<" radius="<<joint.collisionRadius[c]<<" trim="<<joint.collisionTrim
            <<" length="<<ik::Length(joint.tailLocal)<<" proxy_offset="<<ik::Length(joint.collisionOffset)
            <<" root_delta="<<worstRootDelta[worst].x<<','<<worstRootDelta[worst].y<<','<<worstRootDelta[worst].z<<'\n';penetration[worst]=-1;
    }
    surface.Report();
    Check(cacheVerified,"actual motion cache lost physical local TRS");
    Check(state.unresolved==0&&maximumSelfGeometry<.002f,"actual motion retains a physical penetration above 2 mm after local TRS writeback");
    Check(surface.maximumAdded<.002f,"actual skinned surface gains penetration above 2 mm relative to STATIC");
    Check(!surface.HasNewCrossings(),"actual skinned triangles gain body intersections relative to STATIC");
    Check(state.maxAngularStep<24.0f/physics::Hz+.00001f&&
        state.maxRootStep<4.0f/physics::Hz+.00001f,"actual motion has a discontinuous local physics pose");
}
