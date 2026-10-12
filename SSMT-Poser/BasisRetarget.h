#pragma once
#include "ReferencePose.h"
#include "FingerFK.h"

namespace poser::basis {
enum class Route { Legacy, PerBone };
enum class Pose { Dance, Identity, X, Y, Z, Frame, Sequence, FkSequence, FkAxes, FkDance, FingerTest, FingerFrames };
struct Settings { bool enabled=false; Route route=Route::Legacy; Pose pose=Pose::Frame; double frame=0; bool fullFK=false; int axisBone=-1,axis=0; double fingerFrames[3]{0,30,120}; };
inline bool FkTest(const Settings& settings){return settings.enabled&&(settings.pose==Pose::FkSequence||settings.pose==Pose::FkAxes||settings.pose==Pose::FkDance||settings.pose==Pose::FingerTest||settings.pose==Pose::FingerFrames);}
inline double SampleFrame(const Settings& settings,uint64_t milliseconds,bool first=false){return settings.enabled&&(settings.pose!=Pose::Dance&&settings.pose!=Pose::FkDance||(settings.pose==Pose::FkDance&&first))?settings.frame:double(milliseconds)*0.03;}
inline bool LegsFk(std::wstring_view name){return name==L"左足"||name==L"右足"||name==L"左ひざ"||name==L"右ひざ"||name==L"左足首"||name==L"右足首";}
inline bool MainFk(std::wstring_view name) {
    return name==L"下半身"||name==L"上半身"||name==L"上半身2"||name==L"首"||name==L"頭"||
        name==L"左肩"||name==L"右肩"||name==L"左腕"||name==L"右腕"||
        name==L"左ひじ"||name==L"右ひじ"||name==L"左手首"||name==L"右手首";
}
// 静态肩/肘/腕及拇指侧共同确认标准 MMD→当前模型为 S=Ry(pi)。
// 全部受控 FK 来源必须统一变换；只变换手指会让父子使用不同的模型坐标约定。
// q'=S*q*S^-1，不是 Quaternion 求逆，也不交换左右骨。
inline Quaternion ModelInput(Quaternion q) {return {-q.x,q.y,-q.z,q.w};}
// 旧 Basis A/B 保留原实验的 S=I；当前完整 FK 路线在 Input 中统一校正源增量。
// R 为 bone→model 静态旋转：C=R^-1，delta=C*q*C^-1，local=bindLocal*delta。
inline Quaternion Delta(Quaternion modelBind,Quaternion vmdLocal) {
    return vmd::Multiply(vmd::Inverse(modelBind),vmd::Multiply(vmdLocal,modelBind));
}
inline Quaternion BindLocal(Quaternion parentBind,Quaternion modelBind) {
    return vmd::Multiply(vmd::Inverse(parentBind),modelBind);
}
inline Quaternion Local(Quaternion bindLocal,Quaternion modelBind,Quaternion vmdLocal) {
    return vmd::Multiply(bindLocal,Delta(modelBind,vmdLocal));
}
inline Quaternion Input(const Settings& settings,std::wstring_view source,Quaternion sampled) {
    // The normal player path uses disabled settings as its Legacy mode. It
    // still targets the same MODEL_Y_180 avatar frame as the validated FK
    // path; leaving this branch raw mirrors hands/fingers on the live model.
    if (!settings.enabled) return ModelInput(sampled);
    if(FkTest(settings)) {
        const auto finger=fingers::Slot(source);
        const bool included=source==L"下半身"?settings.fullFK:MainFk(source)||finger>=0||(settings.fullFK&&LegsFk(source));
        if(!included)return {0,0,0,1};
        sampled=ModelInput(sampled);
        // 单指对照直接使用指定帧的原始轨道；其余指节和身体保持静态参考。
        if(settings.pose==Pose::FingerTest)return finger>=0&&(finger/3)%5==settings.axisBone?sampled:Quaternion{0,0,0,1};
        if(settings.pose==Pose::FkAxes) {
            // 独立轴测例直接指定目标模型的 +30°，不当作 VMD 源坐标输入。
            const wchar_t* targets[]={L"下半身",L"上半身",L"左足",L"右足"};
            if(settings.axisBone<0||source!=targets[settings.axisBone])return {0,0,0,1};
            constexpr float s=0.2588190451f,c=0.9659258263f;
            return {settings.axis==0?s:0,settings.axis==1?s:0,settings.axis==2?s:0,c};
        }
        return sampled;
    }
    if (!MainFk(source)||settings.pose==Pose::Identity) return {0,0,0,1};
    if (settings.pose==Pose::X||settings.pose==Pose::Y||settings.pose==Pose::Z) {
        if (source!=L"左腕") return {0,0,0,1};
        constexpr float s=0.2588190451f,c=0.9659258263f;
        return {settings.pose==Pose::X?s:0,settings.pose==Pose::Y?s:0,settings.pose==Pose::Z?s:0,c};
    }
    return sampled;
}
inline float Angle(Quaternion a,Quaternion b) {
    a=vmd::Normalize(a);b=vmd::Normalize(b);
    // atan2 比 acos(dot) 在几乎相同的四元数之间更稳定，并且忽略 q/-q。
    const auto d=vmd::Multiply(vmd::Inverse(a),b);
    return 114.591559f*std::atan2(std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z),std::fabs(d.w));
}
inline const char* RouteName(Route route) {return route==Route::Legacy?"Legacy":"Basis";}
inline const char* PoseName(Pose pose) {
    switch (pose) {case Pose::Identity:return "Identity";case Pose::X:return "X+30";case Pose::Y:return "Y+30";case Pose::Z:return "Z+30";case Pose::Frame:return "FixedFrame";case Pose::Sequence:return "Sequence";case Pose::FkSequence:return "FKFixedFrames";case Pose::FkAxes:return "FKSingleAxes";case Pose::FkDance:return "FKDance";case Pose::FingerTest:return "FingerTest";case Pose::FingerFrames:return "FingerFrames";default:return "Dance";}
}
constexpr unsigned SequenceStages=11;
inline unsigned Stage(uint64_t milliseconds) {
    // 前五段各 3 秒，六段同帧 A/B 各 4 秒；最后一段保持，仍由用户手动停止。
    return milliseconds<15000?unsigned(milliseconds/3000):unsigned(std::min(uint64_t(10),5+(milliseconds-15000)/4000));
}
inline Settings StageSettings(unsigned stage) {
    Settings out;out.enabled=true;
    if(stage<2) {out.route=stage?Route::PerBone:Route::Legacy;out.pose=Pose::Identity;}
    else if(stage<5) {out.route=Route::PerBone;out.pose=stage==2?Pose::X:stage==3?Pose::Y:Pose::Z;}
    else {out.route=(stage%2)?Route::Legacy:Route::PerBone;out.pose=Pose::Frame;out.frame=stage<7?0:stage<9?30:120;}
    return out;
}
constexpr unsigned FkStages=6,FkAxesStages=13;
inline unsigned FkStage(uint64_t milliseconds,Pose pose){
    if(pose==Pose::FingerTest)return unsigned(std::min<uint64_t>(5,milliseconds/4000));
    if(pose==Pose::FingerFrames)return unsigned(std::min<uint64_t>(2,milliseconds/6000));
    return pose==Pose::FkDance?(milliseconds<1000?0:milliseconds<4000?1:2):unsigned(std::min<uint64_t>(pose==Pose::FkAxes?12:5,milliseconds/(pose==Pose::FkAxes?4000:6000)));
}
inline Settings FkStageSettings(unsigned stage,Pose pose) {
    Settings out;out.enabled=true;out.route=Route::Legacy;out.pose=pose;
    if(pose==Pose::FkDance){out.fullFK=true;out.frame=stage==0?0:stage==1?30:120;}
    else if(pose==Pose::FkAxes){out.fullFK=true;if(stage){out.axisBone=int((stage-1)/3);out.axis=int((stage-1)%3);}}
    else{out.fullFK=(stage%2)!=0;out.frame=stage<2?0:stage<4?30:120;}
    return out;
}
inline Settings FkStageSettings(unsigned stage,const Settings& configured) {
    if(configured.pose!=Pose::FingerTest&&configured.pose!=Pose::FingerFrames)return FkStageSettings(stage,configured.pose);
    auto out=configured;out.fullFK=true;
    if(out.pose==Pose::FingerTest)out.axisBone=int(stage)-1;
    else out.frame=out.fingerFrames[std::min(stage,2u)];
    return out;
}
}
