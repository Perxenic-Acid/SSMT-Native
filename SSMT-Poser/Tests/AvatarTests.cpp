#include "AvatarReference.h"
#include "BasisRetarget.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>

using namespace poser;
namespace {
void Check(bool passed,const char* reason){if(!passed)throw std::runtime_error(reason);}
struct Record {avatar_reference::Node node;avatar_reference::Local local;uint32_t name,path;};
struct Bind {int index,parent;wchar_t name[128];reference::Matrix inverse;};
void Math() {
    Check(avatar_reference::Hash("Bip001 Pelvis")==1485247078u,"Unity name CRC fixture mismatch");
    avatar_reference::Node nodes[]={{-1,-1},{0,-1}};
    avatar_reference::Local locals[]={{{1,0,0,0},{0,0,0.707106781f,0.707106781f},{1,1,1,0}},{{2,0,0,0},{0.258819045f,0,0,0.965925826f},{1,1,1,0}}};
    reference::Pose model[2];
    Check(avatar_reference::Compose(nodes,locals,model),"valid static hierarchy rejected");
    Check(ik::Length(ik::Sub(model[1].position,{1,2,0}))<0.00001f,"local translation used as model position");
    const auto direction=ik::Rotate(model[1].rotation,{0,1,0});
    Check(ik::Length(ik::Sub(direction,{-0.866025404f,0,0.5f}))<0.00001f,"TRS rotation order incorrect");
    nodes[1].parent=1;Check(!avatar_reference::Compose(nodes,locals,model),"cycle accepted");nodes[1].parent=0;
    locals[1].scale[1]=2;Check(!avatar_reference::Compose(nodes,locals,model),"unsupported scale silently discarded");locals[1].scale[1]=1;
    locals[1].rotation={0,0,0,0};Check(!avatar_reference::Compose(nodes,locals,model),"invalid Quaternion accepted");
    const uint8_t rootOnly[]={1,0},unclosed[]={0,1};
    Check(avatar_reference::Compose(nodes,locals,model,rootOnly),"unrelated unused branch blocked known reference");
    Check(!avatar_reference::Compose(nodes,locals,model,unclosed),"required child lost parent reference");
    for(unsigned i=0;i<6;++i){const auto s=basis::FkStageSettings(i,basis::Pose::FkSequence);Check(s.route==basis::Route::Legacy&&s.fullFK==bool(i%2)&&s.frame==(i<2?0:i<4?30:120),"FK comparison changed route/frame");}
    Check(basis::FkStage(5999,basis::Pose::FkSequence)==0&&basis::FkStage(6000,basis::Pose::FkSequence)==1&&basis::FkStage(UINT64_MAX,basis::Pose::FkSequence)==5,"manual final hold invalid");
    const Quaternion input{0.258819045f,0,0,0.965925826f};
    auto upper=basis::FkStageSettings(0,basis::Pose::FkSequence),full=basis::FkStageSettings(1,basis::Pose::FkSequence);
    Check(basis::Angle(basis::Input(upper,L"下半身",input),{0,0,0,1})<0.001f,"upper baseline took over Pelvis");
    Check(basis::Angle(basis::Input(full,L"左足",input),input)<0.001f,"full FK lost thigh track");
    for(const auto name:{L"センター",L"グルーブ",L"左足ＩＫ",L"左足D",L"腰",L"左肩P"})Check(basis::Angle(basis::Input(full,name,input),{0,0,0,1})<0.001f,"excluded track leaked into FK");
    for(unsigned stage=1;stage<13;++stage){auto axis=basis::FkStageSettings(stage,basis::Pose::FkAxes);Check(axis.axisBone==int((stage-1)/3)&&axis.axis==int((stage-1)%3),"axis sequence mixed bone and axis");}
    const auto dance=basis::FkStageSettings(0,basis::Pose::FkDance);
    Check(dance.fullFK&&dance.route==basis::Route::Legacy&&basis::FkTest(dance),"continuous preview lost complete FK scope");
    Check(basis::FkStage(999,basis::Pose::FkDance)==0&&basis::FkStage(1000,basis::Pose::FkDance)==1&&basis::FkStage(4000,basis::Pose::FkDance)==2&&basis::SampleFrame(dance,1000)==30&&basis::SampleFrame(dance,4000)==120,"continuous preview held a fixed frame");
    const auto sample30=basis::FkStageSettings(1,basis::Pose::FkDance),sample120=basis::FkStageSettings(2,basis::Pose::FkDance);
    Check(basis::SampleFrame(sample30,1016,true)==30&&std::fabs(basis::SampleFrame(sample30,1016)-30.48)<.00001&&basis::SampleFrame(sample120,4016,true)==120&&std::fabs(basis::SampleFrame(sample120,4016)-120.48)<.00001,"reference sample froze continuous playback");
    Check(basis::SampleFrame(full,4000)==0&&basis::SampleFrame(basis::FkStageSettings(5,basis::Pose::FkSequence),4000)==120,"fixed-frame control advanced with time");
    Check(basis::Angle(basis::Input(dance,L"下半身",input),input)<0.001f&&basis::Angle(basis::Input(dance,L"左足",input),input)<0.001f,"continuous preview lost pelvis or thigh input");
}
void Fixture(const wchar_t* avatarPath,const wchar_t* bindPath) {
    std::ifstream in(avatarPath,std::ios::binary);uint32_t count=0;in.read(reinterpret_cast<char*>(&count),4);Check(count>0&&count<=avatar_reference::Limit,"bad Avatar fixture count");
    Record records[avatar_reference::Limit];avatar_reference::Node nodes[avatar_reference::Limit];avatar_reference::Local locals[avatar_reference::Limit];reference::Pose model[avatar_reference::Limit];
    in.read(reinterpret_cast<char*>(records),count*sizeof(Record));Check(bool(in)&&in.peek()==EOF,"truncated Avatar fixture");
    for(unsigned i=0;i<count;++i){nodes[i]=records[i].node;locals[i]=records[i].local;}
    uint8_t required[avatar_reference::Limit]{};
    std::ifstream selection(bindPath,std::ios::binary);Bind selected;
    auto mark=[&](uint32_t hash){for(unsigned i=0;i<count;++i)if(records[i].name==hash){auto index=int(i);while(index>=0){Check(unsigned(index)<count,"fixture parent invalid");required[index]=1;index=nodes[index].parent;}}};
    while(selection.read(reinterpret_cast<char*>(&selected),sizeof(selected))){char name[512]{};Check(WideCharToMultiByte(CP_UTF8,0,selected.name,-1,name,sizeof(name),nullptr,nullptr)>0,"fixture name invalid");mark(avatar_reference::Hash(name));}
    for(const auto name:{"Bip001","Bip001 Pelvis","Body"})mark(avatar_reference::Hash(name));
    Check(avatar_reference::Compose({nodes,count},{locals,count},{model,count},{required,count}),"actual Avatar default pose rejected");
    std::ifstream binds(bindPath,std::ios::binary);Bind bind;unsigned compared=0;float maxAngle=0,maxPosition=0;
    while(binds.read(reinterpret_cast<char*>(&bind),sizeof(bind))) {
        char name[512]{};Check(WideCharToMultiByte(CP_UTF8,0,bind.name,-1,name,sizeof(name),nullptr,nullptr)>0,"fixture name invalid");
        const auto hash=avatar_reference::Hash(name);int found=-1;
        for(unsigned i=0;i<count;++i)if(records[i].name==hash){Check(found<0,"duplicate Avatar name hash");found=int(i);}
        Check(found>=0,"Body bind missing from Avatar default pose");reference::Pose expected;Check(reference::Decode(bind.inverse,expected),"bad actual bind matrix");
        const auto angle=basis::Angle(expected.rotation,model[found].rotation),position=ik::Length(ik::Sub(expected.position,model[found].position));
        Check(angle<0.01f&&position<0.0001f,"Avatar default pose does not reproduce Body binds");
        maxAngle=std::max(maxAngle,angle);maxPosition=std::max(maxPosition,position);++compared;
    }
    Check(binds.eof()&&binds.gcount()==0&&compared==137,"wrong or truncated Body fixture");
    std::cout<<"Avatar nodes="<<count<<" Body bind comparisons="<<compared<<" max_angle_deg="<<maxAngle<<" max_position="<<maxPosition<<'\n';
}
}
int RunAvatarTests(int argc,wchar_t** argv) {
    try{Math();if(argc==3)Fixture(argv[1],argv[2]);else Check(argc==1,"usage: --avatar [Avatar.fixture Body.fixture]");std::cout<<"Avatar static TRS, malformed hierarchy, FK scope and manual sequence tests passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
