#pragma once
#include "ReferencePose.h"

namespace poser::basis {
enum class Route { Legacy, PerBone };
enum class Pose { Dance, Identity, X, Y, Z, Frame, Sequence, FkSequence, FkAxes, FkDance };
struct Settings { bool enabled=false; Route route=Route::Legacy; Pose pose=Pose::Frame; double frame=0; bool fullFK=false; int axisBone=-1,axis=0; };
inline bool FkTest(const Settings& settings){return settings.enabled&&(settings.pose==Pose::FkSequence||settings.pose==Pose::FkAxes||settings.pose==Pose::FkDance);}
inline double SampleFrame(const Settings& settings,uint64_t milliseconds,bool first=false){return settings.enabled&&(settings.pose!=Pose::Dance&&settings.pose!=Pose::FkDance||(settings.pose==Pose::FkDance&&first))?settings.frame:double(milliseconds)*0.03;}
inline bool LegsFk(std::wstring_view name){return name==L"左足"||name==L"右足"||name==L"左ひざ"||name==L"右ひざ"||name==L"左足首"||name==L"右足首";}
inline bool MainFk(std::wstring_view name) {
    return name==L"下半身"||name==L"上半身"||name==L"上半身2"||name==L"首"||name==L"頭"||
        name==L"左肩"||name==L"右肩"||name==L"左腕"||name==L"右腕"||
        name==L"左ひじ"||name==L"右ひじ"||name==L"左手首"||name==L"右手首";
}
// MMD 与 Unity 均采用 Y-up 左手坐标。本实验固定模型轴对齐 S=I，A/B 不额外改变轴映射。
// 若模型朝向校准发现 S!=I，应另做受控实验，不能把 Blender 的 Y/Z 反射直接当作旋转。
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
    if (!settings.enabled) return sampled;
    if(FkTest(settings)) {
        const bool included=source==L"下半身"?settings.fullFK:MainFk(source)||(settings.fullFK&&LegsFk(source));
        if(!included)return {0,0,0,1};
        if(settings.pose==Pose::FkAxes) {
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
    switch (pose) {case Pose::Identity:return "Identity";case Pose::X:return "X+30";case Pose::Y:return "Y+30";case Pose::Z:return "Z+30";case Pose::Frame:return "FixedFrame";case Pose::Sequence:return "Sequence";case Pose::FkSequence:return "FKFixedFrames";case Pose::FkAxes:return "FKSingleAxes";case Pose::FkDance:return "FKDance";default:return "Dance";}
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
inline unsigned FkStage(uint64_t milliseconds,Pose pose){return pose==Pose::FkDance?(milliseconds<1000?0:milliseconds<4000?1:2):unsigned(std::min<uint64_t>(pose==Pose::FkAxes?12:5,milliseconds/(pose==Pose::FkAxes?4000:6000)));}
inline Settings FkStageSettings(unsigned stage,Pose pose) {
    Settings out;out.enabled=true;out.route=Route::Legacy;out.pose=pose;
    if(pose==Pose::FkDance){out.fullFK=true;out.frame=stage==0?0:stage==1?30:120;}
    else if(pose==Pose::FkAxes){out.fullFK=true;if(stage){out.axisBone=int((stage-1)/3);out.axis=int((stage-1)%3);}}
    else{out.fullFK=(stage%2)!=0;out.frame=stage<2?0:stage<4?30:120;}
    return out;
}
}
