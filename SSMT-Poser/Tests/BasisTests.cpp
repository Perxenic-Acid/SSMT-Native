#include "BasisRetarget.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>

using namespace poser;
namespace {
void Require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
// 独立 Rodrigues oracle：避免用与生产实现相同的四元数复合检验轴方向。
Vector3 Rodrigues(Vector3 vector,Vector3 axis,float angle) {
    return ik::Add(ik::Add(ik::Scale(vector,std::cos(angle)),ik::Scale(ik::Cross(axis,vector),std::sin(angle))),ik::Scale(axis,ik::Dot(axis,vector)*(1-std::cos(angle))));
}
void CheckBasis(Quaternion parent,Quaternion bind) {
    const auto local=basis::BindLocal(parent,bind);
    Require(basis::Angle(basis::Local(local,bind,{0,0,0,1}),local)<0.001f,"identity changed static local reference");
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.0f,1.0f}) {
        const float angle=sign*0.5235987756f,s=std::sin(angle/2),c=std::cos(angle/2);
        const Quaternion input{axis==0?s:0,axis==1?s:0,axis==2?s:0,c};
        const Vector3 modelAxis{axis==0?1.0f:0,axis==1?1.0f:0,axis==2?1.0f:0};
        const auto converted=basis::Local(local,bind,input);
        for(Vector3 vector: {Vector3{1,0,0},Vector3{0,1,0},Vector3{0,0,1}}) {
            const auto actual=ik::Rotate(parent,ik::Rotate(converted,vector));
            const auto expected=Rodrigues(ik::Rotate(bind,vector),modelAxis,angle);
            Require(ik::Length(ik::Sub(actual,expected))<0.00002f,"single axis conversion failed Rodrigues oracle");
        }
        Require(basis::Angle(converted,{-converted.x,-converted.y,-converted.z,-converted.w})<0.001f,"quaternion sign invariance failed");
        // 父骨动画通过真实 hierarchy 累积，局部增量不应额外重复父旋转。
        const Quaternion extra{0,0.382683432f,0,0.923879533f};
        const auto movingParent=vmd::Multiply(extra,parent);
        const auto actual=ik::Rotate(movingParent,ik::Rotate(converted,{0,0,1}));
        const auto expected=ik::Rotate(extra,Rodrigues(ik::Rotate(bind,{0,0,1}),modelAxis,angle));
        Require(ik::Length(ik::Sub(actual,expected))<0.00002f,"parent rotation duplicated in bone basis");
    }
}
struct Record {int index,parent;wchar_t name[128];reference::Matrix inverse;};
struct Item {Record record{};reference::Pose bind;bool valid=false;int source=-1;};
constexpr unsigned Limit=256;
const wchar_t* target[]={L"Bip001 Pelvis",L"Bip001 Spine",L"Bip001 Spine2",L"Bip001 Neck",L"Bip001 Head",L"Bip001 L Clavicle",L"Bip001 L UpperArm",L"Bip001 L Forearm",L"Bip001 L Hand",L"Bip001 R Clavicle",L"Bip001 R UpperArm",L"Bip001 R Forearm",L"Bip001 R Hand"};
const wchar_t* source[]={L"下半身",L"上半身",L"上半身2",L"首",L"頭",L"左肩",L"左腕",L"左ひじ",L"左手首",L"右肩",L"右腕",L"右ひじ",L"右手首"};
const int sourceParent[]={-1,-1,1,2,3,2,5,6,7,2,9,10,11};
void Dump(const wchar_t* fixture,const wchar_t* motion,const wchar_t* destination) {
    Item items[Limit];std::ifstream input(fixture,std::ios::binary);Record record;unsigned decoded=0,localCount=0;
    Require(bool(input),"reference fixture unavailable");
    while(input.read(reinterpret_cast<char*>(&record),sizeof(record))) {
        Require(record.index>=0&&record.index<int(Limit)&&record.parent>=-1&&record.parent<int(Limit)&&record.name[127]==0,"fixture record invalid");
        auto& item=items[record.index];Require(!item.valid,"duplicate fixture bone");item.record=record;
        Require(reference::Decode(record.inverse,item.bind),"actual bind matrix rejected");item.valid=true;++decoded;
        for(unsigned i=0;i<13;++i)if(std::wstring_view(record.name)==target[i])item.source=int(i);
    }
    Require(input.eof()&&input.gcount()==0&&decoded==137,"truncated or wrong character bind fixture");
    for(auto& item:items)if(item.valid&&item.record.parent>=0&&items[item.record.parent].valid) {
        CheckBasis(items[item.record.parent].bind.rotation,item.bind.rotation);++localCount;
    }
    vmd::Clip clip;std::string error;Require(vmd::Load(motion,clip,error),error.c_str());
    std::ofstream out(destination,std::ios::binary);out<<std::setprecision(9)<<"{\"source\":\"OFFLINE_NICOLE_READONLY_BIND_FIXTURE\",\"unity_write\":false,\"visual_verdict\":\"PENDING\",\"decoded_bind_bones\":"<<decoded<<",\"local_invariants_checked\":"<<localCount<<",\"missing_reference\":[\"Bip001 Pelvis\"],\"axis_alignment\":\"S=I_EXPERIMENT_ASSUMPTION\",\"frames\":[";
    auto jq=[&](Quaternion q){out<<'['<<q.x<<','<<q.y<<','<<q.z<<','<<q.w<<']';};bool firstFrame=true;
    for(double frame:{0.0,30.0,120.0,480.0}) {
        Quaternion sampled[13]{},cumulative[13]{};Quaternion legacy[Limit]{},converted[Limit]{};bool visited[Limit]{};
        for(unsigned i=0;i<13;++i) {
            int track=vmd::FindTrack(clip,source[i]);if(track<0&&(i==7||i==11))track=vmd::FindTrack(clip,i==7?L"左肘":L"右肘");
            sampled[i]=track>=0?vmd::Sample(clip.tracks[track],frame):Quaternion{0,0,0,1};
            cumulative[i]=sourceParent[i]>=0?vmd::Multiply(cumulative[sourceParent[i]],sampled[i]):sampled[i];
        }
        // 使用真实目标父子关系；绝不依赖数组中的 skin index 排序。
        auto evaluate=[&](auto&& self,int index)->void {
            if(visited[index])return;visited[index]=true;auto& item=items[index];if(!item.valid)return;
            const auto parent=item.record.parent;const bool known=parent>=0&&items[parent].valid;if(known)self(self,parent);
            const auto bind=item.bind.rotation;
            const auto bindLocal=known?basis::BindLocal(items[parent].bind.rotation,bind):Quaternion{0,0,0,1};
            const auto q=item.source>=0?sampled[item.source]:Quaternion{0,0,0,1};
            legacy[index]=item.source>=0?vmd::Multiply(cumulative[item.source],bind):known?vmd::Multiply(legacy[parent],bindLocal):bind;
            converted[index]=known?vmd::Multiply(converted[parent],basis::Local(bindLocal,bind,q)):vmd::Multiply(q,bind);
        };
        for(unsigned i=0;i<Limit;++i)if(items[i].valid)evaluate(evaluate,int(i));
        if(!firstFrame)out<<',';firstFrame=false;out<<"{\"frame\":"<<frame<<",\"bones\":[";bool first=true;float maxAngle=0;
        for(unsigned i=0;i<Limit;++i) {
            const auto& item=items[i];if(!item.valid||item.source<0)continue;const auto parent=item.record.parent;const bool known=parent>=0&&items[parent].valid;
            if(!first)out<<',';first=false;char name[512]{};WideCharToMultiByte(CP_UTF8,0,item.record.name,-1,name,sizeof(name),nullptr,nullptr);
            out<<"{\"bone\":\""<<name<<"\",\"vmd_quaternion\":";jq(sampled[item.source]);out<<",\"target_bind_model\":";jq(item.bind.rotation);
            out<<",\"reference_basis\":";jq(vmd::Inverse(item.bind.rotation));out<<",\"target_bind_local\":";
            if(known)jq(basis::BindLocal(items[parent].bind.rotation,item.bind.rotation));else out<<"null";
            out<<",\"legacy_local\":";if(known)jq(vmd::Multiply(vmd::Inverse(legacy[parent]),legacy[i]));else out<<"null";
            out<<",\"basis_local\":";if(known)jq(vmd::Multiply(vmd::Inverse(converted[parent]),converted[i]));else out<<"null";
            out<<",\"angular_difference_deg\":";
            if(known) {const auto angle=basis::Angle(vmd::Multiply(vmd::Inverse(legacy[parent]),legacy[i]),vmd::Multiply(vmd::Inverse(converted[parent]),converted[i]));out<<angle;maxAngle=std::max(angle,maxAngle);}else out<<"null";
            out<<'}';
        }
        out<<"],\"max_local_difference_deg\":"<<maxAngle<<'}';
    }
    out<<"]}";out.close();Require(bool(out),"diagnostic write failed");
    std::cout<<"Actual bind matrices="<<decoded<<"; identity and +/-XYZ oracle checked="<<localCount<<"; frame0 preserved; Unity writes and visual verdict pending.\n";
}
}
int RunBasisTests(int argc,wchar_t** argv) {
    try {
        CheckBasis({0,0,0,1},{0,0,0,1});
        CheckBasis({0.382683432f,0,0,0.923879533f},{0.183012702f,0.683012702f,0.183012702f,0.683012702f});
        basis::Settings settings;settings.enabled=true;settings.pose=basis::Pose::X;
        Require(basis::Angle(basis::Input(settings,L"左腕",{0,0,0,1}),{0.258819045f,0,0,0.965925826f})<0.001f,"single bone input missing");
        Require(basis::Angle(basis::Input(settings,L"右腕",{0,0,1,0}),{0,0,0,1})<0.001f,"single bone test leaked to another bone");
        Require(!basis::MainFk(L"左足")&&!basis::MainFk(L"左肩P")&&!basis::MainFk(L"グルーブ"),"FK scope includes excluded track");
        Require(basis::Stage(0)==0&&basis::Stage(2999)==0&&basis::Stage(3000)==1&&basis::Stage(15000)==5&&basis::Stage(39000)==10&&basis::Stage(UINT64_MAX)==10,"sequence boundaries or final manual hold invalid");
        for(unsigned stage=5;stage<11;stage+=2) {
            const auto a=basis::StageSettings(stage),b=basis::StageSettings(stage+1);
            Require(a.route==basis::Route::Legacy&&b.route==basis::Route::PerBone&&a.frame==b.frame&&a.pose==b.pose,"A/B sequence changed frame between routes");
        }
        if(argc==4)Dump(argv[1],argv[2],argv[3]);else Require(argc==1,"usage: PoserBasisTests [Nicole fixture VMD output.json]");
        std::cout<<"Basis identity, sign invariance, single-axis Rodrigues oracle and hierarchy tests passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
