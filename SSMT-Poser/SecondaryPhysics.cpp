#include "SecondaryPhysics.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <cstring>

namespace poser::physics {
namespace {
constexpr float Pi=3.14159265358979323846f;
constexpr uint64_t Seed=14695981039346656037ull;
bool FinitePose(const Pose& pose) {return ik::Finite(pose.position)&&ValidRotation(pose.rotation);}
bool Contains(std::wstring_view name,std::wstring_view hint) {
    return std::search(name.begin(),name.end(),hint.begin(),hint.end(),[](wchar_t a,wchar_t b){
        return (a>=L'A'&&a<=L'Z'?a+32:a)==b;
    })!=name.end();
}
Vector3 Closest(Vector3 point,Vector3 a,Vector3 b) {
    const auto delta=ik::Sub(b,a);const auto denominator=ik::Dot(delta,delta);
    return ik::Add(a,ik::Scale(delta,denominator>1e-12f?std::clamp(ik::Dot(ik::Sub(point,a),delta)/denominator,0.0f,1.0f):0));
}
struct SegmentContact {Vector3 point,center;float t=0,distance=0;};
SegmentContact Contact(Vector3 anchor,Vector3 tip,Vector3 a,Vector3 b,float start) {
    const auto first=ik::Add(anchor,ik::Scale(ik::Sub(tip,anchor),start));
    const auto u=ik::Sub(tip,first),v=ik::Sub(b,a),w=ik::Sub(first,a);
    const auto uu=ik::Dot(u,u),vv=ik::Dot(v,v),uv=ik::Dot(u,v),uw=ik::Dot(u,w),vw=ik::Dot(v,w);
    float s=0,t=0;
    if(uu<1e-12f)t=vv>1e-12f?std::clamp(vw/vv,0.0f,1.0f):0;
    else if(vv<1e-12f)s=std::clamp(-uw/uu,0.0f,1.0f);
    else {
        const auto denominator=uu*vv-uv*uv;
        s=denominator>1e-12f?std::clamp((uv*vw-vv*uw)/denominator,0.0f,1.0f):0;
        t=(uv*s+vw)/vv;
        if(t<0){t=0;s=std::clamp(-uw/uu,0.0f,1.0f);}
        else if(t>1){t=1;s=std::clamp((uv-uw)/uu,0.0f,1.0f);}
    }
    SegmentContact result{ik::Add(first,ik::Scale(u,s)),ik::Add(a,ik::Scale(v,t)),start+(1-start)*s};
    result.distance=ik::Length(ik::Sub(result.point,result.center));return result;
}
bool RoleToken(std::wstring_view name,std::wstring_view token) {
    const auto upper=[](wchar_t c){return c>=L'A'&&c<=L'Z';};
    const auto lower=[](wchar_t c){return c>=L'a'&&c<=L'z';};
    const auto letter=[&](wchar_t c){return upper(c)||lower(c);};
    for(size_t begin=0;begin<name.size();) {
        if(!letter(name[begin])){++begin;continue;}
        auto end=begin+1;
        while(end<name.size()&&letter(name[end])&&!(lower(name[end-1])&&upper(name[end])))++end;
        const auto word=name.substr(begin,end-begin);
        if(word.size()==token.size()&&Contains(word,token))return true;
        begin=end;
    }return false;
}
bool Protected(const InputBone& bone) {
    if(bone.controlled||bone.name.starts_with(L"Bip001")||bone.name.starts_with(L"+"))return true;
    // 导出骨架的通用角色标记，不按角色或衣物名称维护排除例外。
    for(const auto token:{L"twist",L"shape",L"pos",L"root",L"rot"})if(RoleToken(bone.name,token))return true;
    return false;
}
bool Validate(std::span<const InputBone> bones,std::array<unsigned,MaxBones>& order,std::string& error) {
    if(bones.empty()||bones.size()>MaxBones){error="PHYSICS_RIG_SIZE";return false;}
    std::array<unsigned,MaxBones> depth{};
    for(unsigned i=0;i<bones.size();++i) {
        order[i]=i;const auto& bone=bones[i];if(!bone.valid)continue;
        if(!FinitePose(bone.rest)||!ik::Finite(bone.localPosition)){error="PHYSICS_STATIC_REFERENCE_INVALID";return false;}
        auto parent=bone.parent;
        while(parent>=0) {
            if(unsigned(parent)>=bones.size()||parent==int(i)||!bones[parent].valid||++depth[i]>32){error="PHYSICS_PARENT_CHAIN_INVALID";return false;}
            parent=bones[parent].parent;
        }
        if(bone.parent>=0) {
            const auto& reference=bones[bone.parent].rest;
            if(ik::Length(ik::Sub(ik::Add(reference.position,ik::Rotate(reference.rotation,bone.localPosition)),bone.rest.position))>.0002f) {
                error="PHYSICS_STATIC_LOCAL_MODEL_MISMATCH";return false;
            }
        }
    }
    std::stable_sort(order.begin(),order.begin()+bones.size(),[&](unsigned a,unsigned b){return depth[a]<depth[b];});
    return true;
}
uint64_t Fingerprint(const Profile& profile) {
    auto hash=HashBytes(Seed,&Version,sizeof(Version));hash=HashBytes(hash,&Hz,sizeof(Hz));
    hash=HashBytes(hash,&profile.preset,sizeof(profile.preset));
    for(unsigned i=0;i<profile.boneCount;++i) {
        const auto& node=profile.bones[i];hash=HashBytes(hash,&node.valid,sizeof(node.valid));if(!node.valid)continue;
        hash=HashBytes(hash,&i,sizeof(i));hash=HashBytes(hash,&node.parent,sizeof(node.parent));
        hash=HashBytes(hash,&node.rest.position,sizeof(Vector3));hash=HashBytes(hash,&node.rest.rotation,sizeof(Quaternion));
        hash=HashBytes(hash,&node.localPosition,sizeof(Vector3));
    }
    for(unsigned i=0;i<profile.jointCount;++i) {
        const auto& joint=profile.joints[i];
        hash=HashBytes(hash,&joint.bone,sizeof(joint.bone));hash=HashBytes(hash,&joint.kind,sizeof(joint.kind));
        hash=HashBytes(hash,&joint.tailLocal,sizeof(Vector3));hash=HashBytes(hash,&joint.colliders,sizeof(joint.colliders));
        hash=HashBytes(hash,&joint.collisionOffset,sizeof(Vector3));
        hash=HashBytes(hash,joint.collisionStart.data(),sizeof(joint.collisionStart));
        hash=HashBytes(hash,joint.collisionRadius.data(),sizeof(joint.collisionRadius));
        for(const auto value:{joint.frequency,joint.damping,joint.gravity,joint.maxAngle,joint.radius,joint.mass,joint.collisionTrim,joint.translationLimit})hash=HashBytes(hash,&value,sizeof(value));
    }
    for(unsigned i=0;i<profile.colliderCount;++i) {
        const auto& collider=profile.colliders[i];hash=HashBytes(hash,&collider.a,sizeof(collider.a));
        hash=HashBytes(hash,&collider.b,sizeof(collider.b));hash=HashBytes(hash,&collider.radius,sizeof(collider.radius));
    }
    return hash;
}
struct Header {
    char magic[8];uint32_t version,hz;uint64_t profile,motion;uint32_t frames,joints;uint64_t checksum;
    uint64_t contacts,unresolved;float maxPenetration,maxMergeOffset;
};
static_assert(sizeof(Header)==72&&sizeof(Quaternion)==16&&sizeof(Vector3)==12&&sizeof(unsigned)==4);
uint64_t Checksum(const Bake& bake) {
    auto hash=HashBytes(Seed,bake.bones.data(),bake.bones.size()*sizeof(unsigned));
    hash=HashBytes(hash,&bake.contacts,sizeof(bake.contacts));hash=HashBytes(hash,&bake.unresolved,sizeof(bake.unresolved));
    hash=HashBytes(hash,&bake.maxPenetration,sizeof(bake.maxPenetration));
    hash=HashBytes(hash,&bake.maxMergeOffset,sizeof(bake.maxMergeOffset));
    hash=HashBytes(hash,bake.rotations.data(),bake.rotations.size()*sizeof(Quaternion));
    return HashBytes(hash,bake.positions.data(),bake.positions.size()*sizeof(Vector3));
}
}
bool ValidRotation(Quaternion q) {
    const auto norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(norm)&&std::fabs(norm-1)<.001f;
}
uint64_t HashBytes(uint64_t hash,const void* bytes,size_t count) {
    const auto* data=static_cast<const unsigned char*>(bytes);
    for(size_t i=0;i<count;++i){hash^=data[i];hash*=1099511628211ull;}return hash;
}
Kind Classify(std::wstring_view name) {
    if(name.starts_with(L"Bip001")||name.starts_with(L"+"))return Kind::None;
    for(const auto hint:{L"hair",L"pony",L"bang",L"髪"})if(Contains(name,hint))return Kind::Hair;
    for(const auto hint:{L"dress",L"skirt",L"cloth",L"coat",L"cape",L"sleeve",L"scarf",L"amice",L"ribbon",L"裙"})if(Contains(name,hint))return Kind::Clothing;
    for(const auto hint:{L"bowknot",L"tail",L"tassel"})if(Contains(name,hint))return Kind::Accessory;
    return Kind::None;
}
const wchar_t* KindName(Kind kind) {
    switch(kind){case Kind::Hair:return L"头发";case Kind::Clothing:return L"衣物";case Kind::Accessory:return L"饰物";default:return L"其它";}
}
bool Candidates(std::span<const InputBone> bones,std::vector<Candidate>& out,std::string& error) {
    out.clear();std::array<unsigned,MaxBones> order{};if(!Validate(bones,order,error))return false;
    std::array<bool,MaxBones> eligible{};std::array<Kind,MaxBones> kinds{};
    for(unsigned i=0;i<bones.size();++i)eligible[i]=bones[i].valid&&bones[i].skinned&&bones[i].parent>=0&&!Protected(bones[i]);
    for(unsigned ordinal=0;ordinal<bones.size();++ordinal) {
        const auto i=order[ordinal];const auto& bone=bones[i];auto kind=Classify(bone.name);
        if(!eligible[i])continue;
        const bool inferred=kind==Kind::None;
        if(inferred) {
            // 未知词汇继承静态父分支的材质；无提示时用保守衣物参数，名称不决定是否可选。
            for(auto parent=bone.parent;parent>=0;parent=bones[parent].parent) {
                if(kinds[parent]!=Kind::None){kind=kinds[parent];break;}
                if(bones[parent].controlled||bones[parent].name.starts_with(L"Bip001"))break;
            }
            if(kind==Kind::None)kind=Kind::Clothing;
        }
        kinds[i]=kind;
        Vector3 tail{};float length=0;
        bool chain=eligible[bone.parent];
        for(unsigned childIndex=0;childIndex<bones.size();++childIndex)if(eligible[childIndex]&&bones[childIndex].parent==int(i)) {
            const auto& child=bones[childIndex];chain=true;
            const auto delta=ik::Sub(child.rest.position,bone.rest.position);const auto distance=ik::Length(delta);
            if(distance>length){length=distance;tail=delta;}
        }
        const bool virtualTail=length<.003f;
        if(virtualTail) {
            // 末端没有 child 时仅沿静态入骨方向延长，明确标为估计末端；零长骨不猜轴。
            tail=ik::Sub(bone.rest.position,bones[bone.parent].rest.position);length=ik::Length(tail);
            if(length<.003f)continue;
            tail=ik::Scale(ik::Unit(tail),std::clamp(length,.02f,.12f));length=ik::Length(tail);
        }
        if(!std::isfinite(length)||length>.75f)continue;
        out.push_back({i,kind,ik::Rotate(vmd::Inverse(bone.rest.rotation),tail),virtualTail,chain||!inferred,inferred});
    }
    // 候选可达 MaxBones；只有实际选入模拟的关节受 MaxJoints 限制。
    return true;
}
bool Build(std::span<const InputBone> bones,std::span<const unsigned> selected,unsigned preset,Profile& out,std::string& error) {
    if(preset>2||selected.size()>MaxJoints){error="PHYSICS_SETTINGS_INVALID";return false;}
    std::vector<Candidate> candidates;if(!Candidates(bones,candidates,error))return false;
    Profile profile{};profile.boneCount=unsigned(bones.size());profile.preset=preset;
    if(!Validate(bones,profile.order,error))return false;
    std::array<bool,MaxBones> wanted{};
    for(const auto bone:selected) {
        if(bone>=bones.size()||wanted[bone]||std::none_of(candidates.begin(),candidates.end(),[&](const Candidate& c){return c.bone==bone;})) {
            error="PHYSICS_SELECTION_INVALID";return false;
        }wanted[bone]=true;
    }
    for(unsigned i=0;i<bones.size();++i) {
        auto& node=profile.bones[i];node.valid=bones[i].valid;node.parent=bones[i].parent;
        node.rest=bones[i].rest;node.localPosition=bones[i].localPosition;
        if(node.valid)node.localRotation=node.parent>=0?vmd::Multiply(vmd::Inverse(bones[node.parent].rest.rotation),node.rest.rotation):node.rest.rotation;
    }
    for(const auto& candidate:candidates)if(wanted[candidate.bone]) {
        const auto j=profile.jointCount++;auto& joint=profile.joints[j];profile.bones[candidate.bone].joint=int(j);
        joint.bone=candidate.bone;joint.kind=candidate.kind;joint.tailLocal=candidate.tailLocal;
        const bool hair=joint.kind==Kind::Hair,cloth=joint.kind==Kind::Clothing;
        // 角回形由 Bullet 关节按静态子树惯量转换为扭矩；damping 是每秒空气阻力。
        joint.frequency=hair?18.0f:cloth?14.0f:20.0f;joint.damping=hair?.35f:cloth?.45f:.4f;
        joint.gravity=hair?6.0f:9.81f;joint.maxAngle=(cloth?85.0f:75.0f)*Pi/180;
        const auto length=ik::Length(joint.tailLocal);joint.radius=std::clamp(length*.06f,.002f,.015f);
        joint.mass=std::clamp(length*.2f,.004f,.1f);
        // 重力弯矩相对惯量随骨长缩短而增大；统一角频率会让短衣物直接塌到限位。
        joint.frequency=std::max(joint.frequency,std::sqrt(6*joint.gravity/length));
        if(preset==0){joint.frequency*=.75f;joint.maxAngle*=1.1f;}
        if(preset==2){joint.frequency*=1.4f;joint.maxAngle*=.85f;}
    }
    const auto named=[&](std::wstring_view name) {
        int match=-1;for(unsigned i=0;i<bones.size();++i)if(bones[i].valid&&bones[i].name==name){if(match>=0)return -1;match=int(i);}return match;
    };
    const auto add=[&](const wchar_t* a,const wchar_t* b,float ratio,float minimum,float maximum) {
        const auto first=named(a),last=named(b);if(first<0||last<0||profile.colliderCount>=MaxColliders)return;
        const auto length=ik::Length(ik::Sub(bones[first].rest.position,bones[last].rest.position));
        profile.colliders[profile.colliderCount++]={unsigned(first),unsigned(last),std::clamp(length*ratio,minimum,maximum)};
    };
    add(L"Bip001 Pelvis",L"Bip001 Spine2",.4f,.07f,.14f);
    const auto head=named(L"Bip001 Head"),neck=named(L"Bip001 Neck");
    if(head>=0&&neck>=0)profile.colliders[profile.colliderCount++]={unsigned(head),unsigned(head),
        std::clamp(ik::Length(ik::Sub(bones[head].rest.position,bones[neck].rest.position)),.055f,.12f)};
    for(const auto side:{L"L",L"R"}) {
        const std::wstring prefix=L"Bip001 "+std::wstring(side)+L" ";
        add((prefix+L"Thigh").c_str(),(prefix+L"Calf").c_str(),.18f,.025f,.09f);
        add((prefix+L"Calf").c_str(),(prefix+L"Foot").c_str(),.13f,.02f,.07f);
        add((prefix+L"UpperArm").c_str(),(prefix+L"Forearm").c_str(),.13f,.015f,.05f);
        add((prefix+L"Forearm").c_str(),(prefix+L"Hand").c_str(),.12f,.012f,.04f);
    }
    for(unsigned j=0;j<profile.jointCount;++j) {
        auto& joint=profile.joints[j];const auto& rest=profile.bones[joint.bone].rest;
        const auto tip=ik::Add(rest.position,ik::Rotate(rest.rotation,joint.tailLocal));
        // MMD 的刚体有独立于骨骼的静态偏移。没有 PMX 形状时，先为整段代理拟合
        // 一个共同偏移，不能只删接合区，或让不同碰撞对各自使用不相容的形状。
        Vector3 best{};float bestLength=std::numeric_limits<float>::infinity();
        const auto axes=std::array{Vector3{1,0,0},Vector3{-1,0,0},Vector3{0,1,0},Vector3{0,-1,0},Vector3{0,0,1},Vector3{0,0,-1}};
        unsigned root=joint.bone;while(profile.bones[root].parent>=0)root=unsigned(profile.bones[root].parent);
        float largest=0;for(unsigned c=0;c<profile.colliderCount;++c)largest=std::max(largest,profile.colliders[c].radius+joint.radius);
        bool physicalParent=false;for(auto ancestor=profile.bones[joint.bone].parent;ancestor>=0;ancestor=profile.bones[ancestor].parent)physicalParent|=profile.bones[ancestor].joint>=0;
        if(!physicalParent)joint.translationLimit=largest*1.25f;
        const auto limit=2*largest+.002f;
        for(unsigned seed=0;seed<=axes.size();++seed) {
            Vector3 offset=seed?ik::Scale(ik::Rotate(profile.bones[root].rest.rotation,axes[seed-1]),largest):Vector3{};
            for(unsigned pass=0;pass<48;++pass) {
                bool clear=true;
                for(unsigned c=0;c<profile.colliderCount;++c) {
                    const auto& collider=profile.colliders[c];const auto a=profile.bones[collider.a].rest.position,b=profile.bones[collider.b].rest.position;
                    const auto anchor=ik::Add(rest.position,offset),end=ik::Add(tip,offset);
                    const auto contact=Contact(anchor,end,a,b,0);const auto radius=collider.radius+joint.radius+.001f;
                    if(contact.distance>=radius-.00001f)continue;
                    clear=false;auto direction=ik::Sub(contact.point,contact.center);
                    if(contact.distance<.000001f) {
                        const auto middle=ik::Scale(ik::Add(anchor,end),.5f);direction=ik::Sub(middle,Closest(middle,a,b));
                        if(ik::Length(direction)<.000001f)direction=ik::Sub(anchor,Closest(anchor,a,b));
                        if(ik::Length(direction)<.000001f) {
                            const auto axis=ik::Unit(ik::Sub(b,a));
                            float transverse=0;
                            for(const auto local:axes) {
                                const auto world=ik::Rotate(profile.bones[root].rest.rotation,local);
                                const auto perpendicular=ik::Sub(world,ik::Scale(axis,ik::Dot(world,axis)));
                                const auto length=ik::Length(perpendicular);if(length>transverse){transverse=length;direction=perpendicular;}
                            }
                        }
                    }
                    offset=ik::Add(offset,ik::Scale(ik::Unit(direction),radius-contact.distance));
                }
                if(clear)break;
            }
            bool clear=true;
            for(unsigned c=0;c<profile.colliderCount;++c) {
                const auto& collider=profile.colliders[c];
                clear&=SegmentClearance(ik::Add(rest.position,offset),ik::Add(tip,offset),profile.bones[collider.a].rest.position,profile.bones[collider.b].rest.position)>=collider.radius+joint.radius+.0009f;
            }
            const auto distance=ik::Length(offset);
            if(clear&&distance<bestLength&&distance<=limit){best=offset;bestLength=distance;}
        }
        if(!std::isfinite(bestLength)){error="PHYSICS_STATIC_COLLISION_FIT_INFEASIBLE";return false;}
        joint.collisionOffset=ik::Rotate(vmd::Inverse(rest.rotation),best);
        for(unsigned c=0;c<profile.colliderCount;++c) {
            const auto& collider=profile.colliders[c];
            const auto nominal=collider.radius+joint.radius;
            joint.collisionStart[c]=0;
            joint.collisionRadius[c]=nominal;
            // 保留全部身体碰撞对和整段长度；刚体偏移已经消除了静态接合矛盾。
            joint.colliders|=1u<<c;
        }
    }
    profile.key=Fingerprint(profile);out=std::move(profile);return true;
}
bool ProjectCapsule(Vector3& point,Vector3 a,Vector3 b,float radius,Vector3 fallback) {
    const auto center=Closest(point,a,b),delta=ik::Sub(point,center);const auto distance=ik::Length(delta);
    if(distance>=radius)return false;
    auto direction=distance>1e-6f?ik::Scale(delta,1/distance):ik::Unit(fallback);
    if(ik::Length(direction)<.5f)direction={1,0,0};
    point=ik::Add(center,ik::Scale(direction,radius));return true;
}
float SegmentClearance(Vector3 anchor,Vector3 tip,Vector3 a,Vector3 b,float start) {
    return Contact(anchor,tip,a,b,std::clamp(start,0.0f,1.0f)).distance;
}
CapsuleContact ClosestSegments(Vector3 anchor,Vector3 tip,Vector3 a,Vector3 b,float start) {
    const auto contact=Contact(anchor,tip,a,b,std::clamp(start,0.0f,1.0f));
    return {contact.point,contact.center,contact.t,contact.distance};
}
std::array<Vector3,2> CollisionSegment(const Joint& joint,const Pose& bone) {
    const auto first=ik::Add(bone.position,ik::Rotate(bone.rotation,joint.collisionOffset));
    return {first,ik::Add(first,ik::Rotate(bone.rotation,joint.tailLocal))};
}
Pose BodyColliderPose(const Profile& profile,unsigned index,std::span<const Pose> drivers) {
    const auto& collider=profile.colliders[index];
    const auto& first=drivers.empty()?profile.bones[collider.a].rest:drivers[collider.a];
    const auto& last=drivers.empty()?profile.bones[collider.b].rest:drivers[collider.b];
    const auto restDirection=ik::Sub(profile.bones[collider.b].rest.position,profile.bones[collider.a].rest.position);
    const auto localAxis=ik::Rotate(vmd::Inverse(profile.bones[collider.a].rest.rotation),restDirection);
    auto rotation=vmd::Multiply(first.rotation,ik::FromTo({0,1,0},localAxis));
    // 胶囊绕长轴虽然几何相同，运动学速度与摩擦并不相同。继承 STATIC 骨架
    // 的参考标架，避免每步从世界 Y 轴重建时在朝下方向产生虚假的轴向自转。
    rotation=vmd::Multiply(ik::FromTo(ik::Rotate(rotation,{0,1,0}),ik::Sub(last.position,first.position)),rotation);
    return {ik::Scale(ik::Add(first.position,last.position),.5f),rotation};
}
bool Advance(const Profile& profile,Sampler sampler,void* context,uint64_t tick,State& state) {
    std::array<Pose,MaxBones> drivers{};auto poses=std::span(drivers.data(),profile.boneCount);
    if(!state.initialized||tick<state.tick||tick-state.tick>32) {
        const bool reset=state.initialized;
        const auto start=reset||tick>32?tick:0;
        if(!sampler(context,double(start)/Hz,poses)||!Initialize(profile,poses,state))return false;
        state.tick=start;state.resets+=reset;
    }
    while(state.tick<tick) {
        if(!sampler(context,double(state.tick+1)/Hz,poses)||!Step(profile,poses,state))return false;
        ++state.tick;
    }return true;
}
uint64_t FrameCount(double seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds>1e6)return 0;
    return uint64_t(std::ceil(seconds*Hz))+1;
}
bool Generate(const Profile& profile,uint64_t motionKey,double seconds,Sampler sampler,void* context,
    Cancel cancel,Progress progress,void* observer,Bake& out,std::string& error) {
    const auto count=FrameCount(seconds);
    if(!count||!profile.jointCount||count>UINT32_MAX||count*profile.jointCount*(sizeof(Quaternion)+sizeof(Vector3))>MaxBakeBytes) {
        error="PHYSICS_BAKE_SIZE_LIMIT";return false;
    }
    Bake bake;bake.profileKey=profile.key;bake.motionKey=motionKey;bake.frames=unsigned(count);
    for(unsigned j=0;j<profile.jointCount;++j)bake.bones.push_back(profile.joints[j].bone);
    bake.rotations.resize(size_t(count)*profile.jointCount);
    bake.positions.resize(size_t(count)*profile.jointCount);
    State state{};std::array<Pose,MaxBones> drivers{};auto poses=std::span(drivers.data(),profile.boneCount);
    for(unsigned frame=0;frame<count;++frame) {
        if(cancel&&cancel(observer)){error="PHYSICS_BAKE_CANCELLED";return false;}
        if(!sampler(context,std::min(seconds,double(frame)/Hz),poses)||(frame? !Step(profile,poses,state):!Initialize(profile,poses,state))) {
            error="PHYSICS_BAKE_POSE_REJECTED";return false;
        }
        std::copy_n(state.local.begin(),profile.jointCount,bake.rotations.begin()+size_t(frame)*profile.jointCount);
        std::copy_n(state.positionDelta.begin(),profile.jointCount,bake.positions.begin()+size_t(frame)*profile.jointCount);
        if(progress&&(frame%Hz==0||frame+1==count))progress(observer,unsigned((frame+1)*100/count));
    }
    bake.contacts=state.contacts;bake.unresolved=state.unresolved;bake.maxPenetration=state.maxPenetration;bake.maxMergeOffset=state.maxMergeOffset;
    out=std::move(bake);return true;
}
bool Sample(const Bake& bake,const Profile& profile,double seconds,std::span<Quaternion> out,std::span<Vector3> positions) {
    if(bake.profileKey!=profile.key||bake.bones.size()!=profile.jointCount||out.size()<profile.jointCount||
        !bake.frames||bake.rotations.size()!=size_t(bake.frames)*profile.jointCount||bake.positions.size()!=bake.rotations.size()||
        (!positions.empty()&&positions.size()<profile.jointCount)||!std::isfinite(seconds)||seconds<0)return false;
    const auto time=std::min(seconds*Hz,double(bake.frames-1));const auto a=unsigned(time),b=std::min(a+1,bake.frames-1);
    for(unsigned j=0;j<profile.jointCount;++j) {
        out[j]=vmd::Slerp(bake.rotations[size_t(a)*profile.jointCount+j],bake.rotations[size_t(b)*profile.jointCount+j],float(time-a));
        if(!positions.empty())positions[j]=ik::Add(bake.positions[size_t(a)*profile.jointCount+j],ik::Scale(ik::Sub(bake.positions[size_t(b)*profile.jointCount+j],bake.positions[size_t(a)*profile.jointCount+j]),float(time-a)));
    }
    return true;
}
bool Save(const std::filesystem::path& path,const Bake& bake,std::string& error) {
    const auto joints=bake.bones.size();const auto pairs=joints*(joints?joints-1:0)/2+joints*MaxColliders;
    if(bake.bones.empty()||bake.bones.size()>MaxJoints||!bake.frames||bake.rotations.size()!=size_t(bake.frames)*bake.bones.size()||bake.positions.size()!=bake.rotations.size()||
        bake.rotations.size()*(sizeof(Quaternion)+sizeof(Vector3))>MaxBakeBytes||!std::isfinite(bake.maxPenetration)||bake.maxPenetration<0||
        !std::isfinite(bake.maxMergeOffset)||bake.maxMergeOffset<0||
        bake.contacts>uint64_t(bake.frames)*Substeps*pairs||bake.unresolved>uint64_t(bake.frames)*pairs) {
        error="PHYSICS_CACHE_SIZE_INVALID";return false;
    }
    for(const auto position:bake.positions)if(!ik::Finite(position)){error="PHYSICS_CACHE_POSITION_INVALID";return false;}
    Header header{};std::memcpy(header.magic,"SSMTPHY1",8);header.version=Version;header.hz=Hz;
    header.profile=bake.profileKey;header.motion=bake.motionKey;header.frames=bake.frames;header.joints=unsigned(bake.bones.size());header.checksum=Checksum(bake);
    header.contacts=bake.contacts;header.unresolved=bake.unresolved;header.maxPenetration=bake.maxPenetration;
    header.maxMergeOffset=bake.maxMergeOffset;
    auto temporary=path;temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header),sizeof(header));
    file.write(reinterpret_cast<const char*>(bake.bones.data()),std::streamsize(bake.bones.size()*sizeof(unsigned)));
    file.write(reinterpret_cast<const char*>(bake.rotations.data()),std::streamsize(bake.rotations.size()*sizeof(Quaternion)));
    file.write(reinterpret_cast<const char*>(bake.positions.data()),std::streamsize(bake.positions.size()*sizeof(Vector3)));
    file.flush();file.close();const auto complete=bool(file);
    if(!complete||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;std::filesystem::remove(temporary,ignored);error="PHYSICS_CACHE_WRITE_FAILED";return false;
    }return true;
}
bool Load(const std::filesystem::path& path,const Profile& profile,uint64_t motionKey,double seconds,Bake& out,std::string& error) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file){error="PHYSICS_CACHE_ABSENT";return false;}
    const auto bytes=file.tellg();file.seekg(0);Header header{};file.read(reinterpret_cast<char*>(&header),sizeof(header));
    if(!file||std::memcmp(header.magic,"SSMTPHY1",8)||header.version!=Version||header.hz!=Hz||header.profile!=profile.key||
        header.motion!=motionKey||!header.frames||header.frames!=FrameCount(seconds)||header.joints!=profile.jointCount||!header.joints||
        !std::isfinite(header.maxPenetration)||header.maxPenetration<0||!std::isfinite(header.maxMergeOffset)||header.maxMergeOffset<0||
        header.contacts>uint64_t(header.frames)*Substeps*(uint64_t(header.joints)*(header.joints-1)/2+uint64_t(header.joints)*MaxColliders)||
        header.unresolved>uint64_t(header.frames)*(uint64_t(header.joints)*(header.joints-1)/2+uint64_t(header.joints)*MaxColliders)||
        uint64_t(header.frames)*header.joints*(sizeof(Quaternion)+sizeof(Vector3))>MaxBakeBytes||
        uint64_t(bytes)!=sizeof(header)+uint64_t(header.joints)*sizeof(unsigned)+uint64_t(header.frames)*header.joints*(sizeof(Quaternion)+sizeof(Vector3))) {
        error="PHYSICS_CACHE_HEADER_REJECTED";return false;
    }
    Bake bake;bake.profileKey=header.profile;bake.motionKey=header.motion;bake.frames=header.frames;
    bake.contacts=header.contacts;bake.unresolved=header.unresolved;bake.maxPenetration=header.maxPenetration;
    bake.maxMergeOffset=header.maxMergeOffset;
    bake.bones.resize(header.joints);bake.rotations.resize(size_t(header.frames)*header.joints);
    bake.positions.resize(bake.rotations.size());
    file.read(reinterpret_cast<char*>(bake.bones.data()),std::streamsize(bake.bones.size()*sizeof(unsigned)));
    file.read(reinterpret_cast<char*>(bake.rotations.data()),std::streamsize(bake.rotations.size()*sizeof(Quaternion)));
    file.read(reinterpret_cast<char*>(bake.positions.data()),std::streamsize(bake.positions.size()*sizeof(Vector3)));
    if(!file||Checksum(bake)!=header.checksum){error="PHYSICS_CACHE_CHECKSUM_REJECTED";return false;}
    for(unsigned j=0;j<header.joints;++j)if(bake.bones[j]!=profile.joints[j].bone){error="PHYSICS_CACHE_BONES_REJECTED";return false;}
    for(const auto rotation:bake.rotations)if(!ValidRotation(rotation)){error="PHYSICS_CACHE_ROTATION_REJECTED";return false;}
    for(unsigned frame=0;frame<header.frames;++frame)for(unsigned j=0;j<header.joints;++j) {
        const auto position=bake.positions[size_t(frame)*header.joints+j];
        if(!ik::Finite(position)||ik::Length(position)>profile.joints[j].translationLimit+.0001f){error="PHYSICS_CACHE_POSITION_REJECTED";return false;}
    }
    out=std::move(bake);return true;
}
}
