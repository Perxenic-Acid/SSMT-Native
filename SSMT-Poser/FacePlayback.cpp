#include "FacePlayback.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <ostream>
#include <sstream>

namespace poser::face {
namespace {
constexpr unsigned MaxTargets=2048,MaxRenderers=64;
struct Target {
    unsigned renderer=0,index=0;int track=-1;
    std::wstring name,label;
    float handoff=0,last=0;bool touched=false;
};
struct Renderer {
    uintptr_t object=0,native=0,mesh=0,meshNative=0;
    uint32_t handles[2]{};unsigned count=0;
};
struct Inventory { std::wstring label;int shapes=-1; };
NativeMethod countGetter,nameGetter,weightGetter,weightSetter,meshGetter;
uintptr_t rendererClass=0,meshClass=0;
RigCalls calls;
std::array<Renderer,MaxRenderers> renderers;
std::array<Target,MaxTargets> targets;
std::array<Inventory,MaxRenderers> inventory;
unsigned inventoryCount=0;
unsigned rendererCount=0,targetCount=0,writes=0,restores=0,failed=0;
unsigned overrideMode=0;
std::array<bool,MaxRenderers> mappedRenderers{};
uint64_t generation=0;
bool apiReady=false,overrideNative=false,external=false,playing=false;
vmd::Clip expression;
std::filesystem::path source;
template<class T> bool Read(uintptr_t address,T& result) {
    SIZE_T copied=0;return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&result,sizeof(result),&copied)&&copied==sizeof(result);
}
uint32_t Root(uintptr_t object) {
    const auto handle=reinterpret_cast<uint32_t (*)(uintptr_t,bool)>(calls.context.handleNew)(object,false);
    if(!handle)return 0;
    if((handle>>26)!=2||reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(handle)!=object) {
        reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(handle);return 0;
    }return handle;
}
bool Alive(const Renderer& r) {
    uintptr_t klass=0,native=0;
    for(unsigned i=0;i<2;++i)if(!r.handles[i]||reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(r.handles[i])!=(i?r.mesh:r.object))return false;
    if(!Read(r.object,klass)||klass!=rendererClass||!Read(r.object+0x10,native)||native!=r.native)return false;
    if(!Read(r.mesh,klass)||klass!=meshClass||!Read(r.mesh+0x10,native)||native!=r.meshNative)return false;
    return reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(meshGetter.entry)(r.object,meshGetter.descriptor)==r.mesh&&
        reinterpret_cast<int (*)(uintptr_t,uintptr_t)>(countGetter.entry)(r.mesh,countGetter.descriptor)==int(r.count);
}
float Weight(const Target& t) {
    return reinterpret_cast<float (*)(uintptr_t,int,uintptr_t)>(weightGetter.entry)(renderers[t.renderer].object,int(t.index),weightGetter.descriptor);
}
bool Write(Target& t,float value) {
    reinterpret_cast<void (*)(uintptr_t,int,float,uintptr_t)>(weightSetter.entry)(renderers[t.renderer].object,int(t.index),value,weightSetter.descriptor);
    const auto after=Weight(t);return std::isfinite(after)&&std::abs(after-value)<.01f;
}
std::wstring Normalize(std::wstring_view name) {
    // 仅忽略格式差异；不同模型的形态键语义仍由用户显式映射。
    const auto dot=name.find_last_of(L'.');if(dot!=name.npos)name.remove_prefix(dot+1);
    std::wstring result;for(const auto c:name)if(c!=L'_'&&c!=L' '&&c!=L'-')result+=wchar_t(std::towlower(c));return result;
}
void Match() {
    for(unsigned t=0;t<targetCount;++t) {
        auto& target=targets[t];target.track=-1;const auto normalized=Normalize(target.name);
        for(size_t s=0;s<expression.morphTracks.size();++s)if(Normalize(expression.morphTracks[s].name)==normalized) {
            if(target.track>=0){target.track=-1;break;}target.track=int(s);
        }
        // 通用导出名称的明确语义；不按角色名称或形态键编号猜测。
        if(target.track<0&&(normalized==L"blink"||normalized==L"eyeblinkleft"||normalized==L"eyeblinkright"||normalized==L"eyeblinkl"||normalized==L"eyeblinkr"))
            for(size_t s=0;s<expression.morphTracks.size();++s)if(expression.morphTracks[s].name==L"まばたき")target.track=int(s);
    }
    ++generation;
}
bool Controlled(const Target& target) {return target.track>=0||(overrideMode==2&&mappedRenderers[target.renderer]);}
void View(const std::wstring& message={}) {
    ui::Snapshot::FaceView view;view.path=source.wstring();view.generation=generation;view.overrideNative=overrideNative;view.mode=overrideMode;view.available=apiReady&&targetCount>0;
    for(const auto& track:expression.morphTracks)view.tracks.push_back(track.name);
    for(unsigned t=0;t<targetCount;++t) {
        const auto& target=targets[t];view.targets.push_back(target.label+(target.track>=0?L" ← "+expression.morphTracks[size_t(target.track)].name:L" · 未映射"));
        view.mapping.push_back(target.track);view.mapped+=target.track>=0;
    }
    view.message=!message.empty()?message:!apiReady?L"当前游戏版本的形态键接口未通过核验。":!targetCount?L"当前角色没有可访问的 Mesh 形态键；材质表情需另行适配。":
        L"形态键 "+std::to_wstring(targetCount)+L" · 已映射 "+std::to_wstring(view.mapped)+(overrideMode==2?L"；替换仅作用于已映射目标所在网格，其余键归零。":L"；未映射项保留游戏控制。");
    ui::Face(std::move(view));
}
}
bool Initialize(GenshinNativeRuntime& runtime,std::ostream& report) {
    apiReady=false;NativeClass mesh,renderer;
    if(!runtime.FindClass("UnityEngine","Mesh",mesh)||!runtime.FindClass("UnityEngine","SkinnedMeshRenderer",renderer))return false;
    const auto method=[&](const NativeClass& owner,const char* name,uint8_t count,NativeMethod& out){return runtime.FindMethod(owner,name,count,out)&&out.entry&&out.descriptor&&out.target;};
    if(!method(mesh,"get_blendShapeCount",0,countGetter)||!method(mesh,"GetBlendShapeName",1,nameGetter)||
        !method(renderer,"GetBlendShapeWeight",1,weightGetter)||!method(renderer,"SetBlendShapeWeight",2,weightSetter)||!method(renderer,"get_sharedMesh",0,meshGetter))return false;
    const auto pattern=[](uintptr_t address,size_t size,const char* signature){std::vector<uint8_t> bytes(size);SIZE_T read=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.data(),bytes.size(),&read)&&read==size&&native_detail::FindPattern(bytes,signature).size()==1;};
    // 离线同哈希样本确认：index 位于 EDX，setter 的 float 位于 XMM2；getter 为 XMM0 返回。
    apiReady=pattern(countGetter.target,100,"48 8B 40 48 48 8B 88 98 01 00 00 48 2B 88 90 01 00 00")&&
        pattern(nameGetter.target,70,"8B D7 48 8B C8 E8 ?? ?? ?? ?? 48 85 C0")&&
        pattern(weightGetter.target,52,"8B D7 48 8B C8 48 8B 5C 24 38 48 83 C4 20 5F E9")&&
        pattern(weightSetter.target,70,"0F 28 F2 8B FA 48 8B D9")&&pattern(weightSetter.target,70,"0F 28 D6 8B D7 48 8B C8");
    meshClass=mesh.address;rendererClass=renderer.address;
    report<<"[FACE_API] ready="<<apiReady<<" count_id="<<countGetter.metadataId<<" name_id="<<nameGetter.metadataId<<" get_id="<<weightGetter.metadataId<<" set_id="<<weightSetter.metadataId<<" ABI=METADATA_AND_NATIVE_FINGERPRINT\n";
    return apiReady;
}
void BeginInventory(const RigCalls& value) {Release();calls=value;overrideNative=false;overrideMode=0;++generation;}
void Capture(uintptr_t object,uintptr_t native,uintptr_t mesh,uintptr_t meshNative,const wchar_t* label) {
    auto* item=inventoryCount<MaxRenderers?&inventory[inventoryCount++]:nullptr;if(item){item->label=label;item->shapes=-1;}
    if(!apiReady||rendererCount==MaxRenderers)return;
    uintptr_t klass=0,observed=0;
    if(!Read(object,klass)||klass!=rendererClass||!Read(object+0x10,observed)||observed!=native||!Read(mesh,klass)||klass!=meshClass||!Read(mesh+0x10,observed)||observed!=meshNative){++failed;return;}
    const auto count=reinterpret_cast<int (*)(uintptr_t,uintptr_t)>(countGetter.entry)(mesh,countGetter.descriptor);
    if(item)item->shapes=count;
    if(count<0||count>int(MaxTargets-targetCount)){++failed;return;}
    if(!count)return;
    auto& r=renderers[rendererCount];r={object,native,mesh,meshNative,{Root(object),Root(mesh)},unsigned(count)};
    if(!r.handles[0]||!r.handles[1]){for(const auto h:r.handles)if(h)reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(h);r={};++failed;return;}
    const auto start=targetCount;
    for(int i=0;i<count;++i) {
        const auto text=reinterpret_cast<uintptr_t (*)(uintptr_t,int,uintptr_t)>(nameGetter.entry)(mesh,i,nameGetter.descriptor);
        const auto root=text?Root(text):0;int length=0;wchar_t name[256]{};SIZE_T copied=0;
        uintptr_t stringClass=0,className=0;char typeName[7]{};
        const auto ok=root&&Read(text,stringClass)&&Read(stringClass+0x28,className)&&Read(className,typeName)&&std::string_view(typeName,6)=="String"&&Read(text+0x10,length)&&length>0&&length<int(std::size(name))&&
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(text+0x14),name,size_t(length)*sizeof(wchar_t),&copied)&&copied==size_t(length)*sizeof(wchar_t);
        if(root)reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(root);
        if(!ok){++failed;break;}
        auto& target=targets[targetCount++];target={rendererCount,unsigned(i),-1,name,std::wstring(label)+L" / "+name};
    }
    if(targetCount!=start+unsigned(count)){for(unsigned i=start;i<targetCount;++i)targets[i]={};targetCount=start;}
    ++rendererCount;
}
void Release() {
    playing=false;
    for(auto& r:renderers){for(const auto h:r.handles)if(h&&calls.context.handleFree)reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(h);r={};}
    for(unsigned i=0;i<targetCount;++i)targets[i]={};targetCount=rendererCount=failed=0;overrideNative=false;overrideMode=0;++generation;
    for(auto& item:inventory)item={};inventoryCount=0;
}
void Publish(std::ostream& report) {
    const auto utf8=[](const std::wstring& s){const auto size=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),text.data(),size,nullptr,nullptr);return text;};
    Match();View();Report(report);
    for(unsigned i=0;i<inventoryCount;++i)report<<"  face_mesh="<<utf8(inventory[i].label)<<" blend_shape_count="<<inventory[i].shapes<<'\n';
    for(unsigned i=0;i<targetCount;++i)report<<"  face_target="<<i<<" name="<<utf8(targets[i].label)<<'\n';
}
bool Load(const std::filesystem::path& path,std::ostream& report,HANDLE stop) {
    if(playing)return false;
    vmd::Clip loaded;std::string error;
    if(!vmd::Load(path,loaded,error,stop)||loaded.morphTracks.empty()){report<<"face_load=REJECTED reason="<<(error.empty()?"NO_MORPH_TRACKS":error)<<'\n';View(L"表情文件未载入：文件无形态键或格式无效。");return false;}
    expression={};expression.morphTracks=std::move(loaded.morphTracks);expression.morphKeys=loaded.morphKeys;expression.lastFrame=loaded.lastFrame;source=path;external=true;
    Match();View();report<<"face_load=OK keys="<<expression.morphKeys<<" tracks="<<expression.morphTracks.size()<<" last_frame="<<expression.lastFrame<<'\n';return true;
}
void UseMotion(const vmd::Clip& motion,const std::filesystem::path& path,std::ostream& report,HANDLE stop) {
    if(external)return;
    expression={};source.clear();
    if(!motion.morphTracks.empty()){expression.morphTracks=motion.morphTracks;expression.morphKeys=motion.morphKeys;expression.lastFrame=motion.lastFrame;source=path;Match();View();return;}
    // 只尝试同目录、同动作名前缀的唯一表情文件；不猜下载目录或跨包拼接。
    const auto stem=path.stem().wstring();const auto dash=stem.find_first_of(L"-－");
    if(dash!=stem.npos) {
        const auto companion=path.parent_path()/(stem.substr(0,dash)+L"-表情.vmd");std::error_code error;
        if(companion!=path&&std::filesystem::is_regular_file(companion,error)&&Load(companion,report,stop)){external=false;return;}
    }Match();View();
}
bool Configure(const ui::Command& command,std::ostream& report) {
    if(playing||command.generation!=generation)return false;
    if(command.kind==ui::CommandKind::ConfigureFace){if(command.text!=L"0"&&command.text!=L"1"&&command.text!=L"2")return false;overrideMode=unsigned(command.text[0]-L'0');overrideNative=overrideMode!=0;}
    else if(command.kind==ui::CommandKind::MapFace) {
        std::wistringstream input(command.text);int track=-2,target=-1;std::wstring extra;
        if(!(input>>track>>target)||(input>>extra)||track< -1||track>=int(expression.morphTracks.size())||target<0||target>=int(targetCount))return false;
        targets[target].track=track;
    }else return false;
    ++generation;View();report<<"face_configure=OK override="<<overrideNative<<'\n';return true;
}
bool Start() {
    writes=restores=0;playing=false;
    for(unsigned i=0;i<targetCount;++i)targets[i].touched=false;
    if(!overrideNative||expression.morphTracks.empty())return true;
    mappedRenderers.fill(false);for(unsigned i=0;i<targetCount;++i)if(targets[i].track>=0)mappedRenderers[targets[i].renderer]=true;
    for(unsigned r=0;r<rendererCount;++r)if(!Alive(renderers[r]))return false;
    for(unsigned i=0;i<targetCount;++i)if(Controlled(targets[i])) {
        const auto weight=Weight(targets[i]);if(!std::isfinite(weight))return false;targets[i].handoff=targets[i].last=weight;
    }playing=true;return true;
}
bool Tick(double frame) {
    if(!playing)return true;
    for(unsigned r=0;r<rendererCount;++r)if(!Alive(renderers[r]))return false;
    for(unsigned i=0;i<targetCount;++i) {
        auto& t=targets[i];if(!Controlled(t))continue;
        const auto current=Weight(t);if(!std::isfinite(current))return false;
        if(!t.touched||std::abs(current-t.last)>.01f)t.handoff=current;
        const auto desired=t.track>=0?100.0f*std::clamp(vmd::SampleMorph(expression.morphTracks[size_t(t.track)],frame),0.0f,1.0f):0.0f;
        t.touched=true;if(!Write(t,desired))return false;t.last=desired;++writes;
    }return true;
}
bool Stop() {
    playing=false;bool ok=true;
    for(unsigned r=0;r<rendererCount;++r) {
        bool touched=false;for(unsigned i=0;i<targetCount;++i)touched|=targets[i].renderer==r&&targets[i].touched;
        if(!touched)continue;if(!Alive(renderers[r])){ok=false;continue;}
        for(unsigned i=0;i<targetCount;++i)if(targets[i].renderer==r&&targets[i].touched) {
            auto& t=targets[i];const auto current=Weight(t);if(!std::isfinite(current)){ok=false;continue;}
            if(std::abs(current-t.last)>.01f)t.handoff=current;
            if(Write(t,t.handoff)){++restores;t.touched=false;}else ok=false;
        }
    }return ok;
}
void Report(std::ostream& report) {unsigned mapped=0;for(unsigned i=0;i<targetCount;++i)mapped+=targets[i].track>=0;report<<"[FACE] api="<<apiReady<<" renderers="<<rendererCount<<" targets="<<targetCount<<" tracks="<<expression.morphTracks.size()<<" mapped="<<mapped<<" override="<<overrideNative<<" mode="<<overrideMode<<" writes="<<writes<<" restored="<<restores<<" capture_failures="<<failed<<" time=BODY_VMD_FRAME weight_scale=100 unmapped="<<(overrideMode==2?"ZERO_ON_MAPPED_RENDERERS":"NATIVE")<<'\n';}
}
