#pragma once
#include "LegIK.h"
#include <array>
#include <span>
#include <string>
#include <vector>
#include <memory>

namespace poser::physics {
constexpr unsigned MaxBones=256,MaxJoints=128,MaxColliders=12,Hz=120,Version=7;
constexpr unsigned Substeps=2;
constexpr uint64_t MaxBakeBytes=64ull*1024*1024;
enum class Mode : unsigned { Native, Realtime, Baked };
enum class Kind : unsigned { None, Hair, Clothing, Accessory };
struct Pose { Vector3 position{};Quaternion rotation{0,0,0,1}; };
struct InputBone {
    std::wstring_view name;
    int parent=-1;
    Pose rest;
    Vector3 localPosition{};
    bool valid=false,controlled=false;
    bool skinned=true;
};
struct Candidate {
    unsigned bone=0;Kind kind=Kind::None;Vector3 tailLocal{};
    bool virtualTail=false,defaultSelected=false,inferred=false;
};
struct Node { int parent=-1,joint=-1;Pose rest;Vector3 localPosition{};Quaternion localRotation{0,0,0,1};bool valid=false; };
struct Joint {
    unsigned bone=0;
    Kind kind=Kind::None;
    Vector3 tailLocal{};
    // 刚体与骨骼独立：从静态几何拟合的平移随骨旋转，不改骨根、骨长或蒙皮参考。
    Vector3 collisionOffset{};
    float translationLimit=0; // 仅分支根的总位移半径；不会作用于身体或腿骨。
    float frequency=0,damping=0,gravity=0,maxAngle=0,radius=0,mass=0;
    float collisionTrim=0;
    uint32_t colliders=0;
    // 生产配置使用完整胶囊与名义半径；按对起点只保留给显式几何配置。
    std::array<float,MaxColliders> collisionStart{},collisionRadius{};
};
struct Collider { unsigned a=0,b=0;float radius=0; };
struct CapsuleContact { Vector3 point{},center{};float fraction=0,distance=0; };
struct Profile {
    unsigned boneCount=0,jointCount=0,colliderCount=0,preset=1;
    std::array<Node,MaxBones> bones{};
    std::array<unsigned,MaxBones> order{};
    std::array<Joint,MaxJoints> joints{};
    std::array<Collider,MaxColliders> colliders{};
    uint64_t key=0;
};
struct Particle { Vector3 current{},previous{},anchor{}; };
#ifdef SSMT_POSER_TRACE_PHYSICS
// 仅离线验证目标编译：分离积分 / 身体接触 / 自碰撞 / 根投影的实际局部变化。
struct PhasePeak {float angle=0,position=0;unsigned angleJoint=0,positionJoint=0;uint64_t angleStep=0,positionStep=0;};
struct PhaseTrace {std::array<PhasePeak,4> phases{};PhasePeak correction{};};
#endif
class Simulation;
struct State {
    State();~State();State(State&&) noexcept;State& operator=(State&&) noexcept;
    State(const State&)=delete;State& operator=(const State&)=delete;
    std::unique_ptr<Simulation> simulation;
    std::array<Particle,MaxJoints> particles{};
    std::array<Pose,MaxBones> poses{};
    std::array<Quaternion,MaxJoints> local{};
    std::array<Vector3,MaxJoints> positionDelta{};
    uint64_t tick=0,steps=0,contacts=0,resets=0,unresolved=0;
    float maxPenetration=0,maxMergeOffset=0;
    float maxAngularStep=0,maxRootStep=0;
    uint64_t bodyUnresolved=0,selfUnresolved=0;
    float bodyMaxPenetration=0,selfMaxPenetration=0;
    unsigned selfPairA=0,selfPairB=0;
    bool initialized=false;
#ifdef SSMT_POSER_TRACE_PHYSICS
    PhaseTrace* trace=nullptr;
#endif
};
struct Bake {
    uint64_t profileKey=0,motionKey=0;
    unsigned frames=0;
    std::vector<unsigned> bones;
    std::vector<Quaternion> rotations;
    std::vector<Vector3> positions; // 相对静态局部位置的物理位移，不含身体 / 足 IK 平移。
    uint64_t contacts=0,unresolved=0;
    float maxPenetration=0,maxMergeOffset=0;
};
// Sampler 只提供值类型的静态 FK/IK 姿态，不持有或调用 Unity 对象。
using Sampler=bool (*)(void*,double,std::span<Pose>);
using Cancel=bool (*)(void*);
using Progress=void (*)(void*,unsigned);
Kind Classify(std::wstring_view name);
const wchar_t* KindName(Kind kind);
bool Candidates(std::span<const InputBone> bones,std::vector<Candidate>& out,std::string& error);
bool Build(std::span<const InputBone> bones,std::span<const unsigned> selected,unsigned preset,Profile& out,std::string& error);
bool ValidRotation(Quaternion q);
uint64_t HashBytes(uint64_t hash,const void* bytes,size_t count);
uint64_t FrameCount(double seconds);
bool ProjectCapsule(Vector3& point,Vector3 a,Vector3 b,float radius,Vector3 fallback);
float SegmentClearance(Vector3 anchor,Vector3 tip,Vector3 a,Vector3 b,float start=0);
CapsuleContact ClosestSegments(Vector3 anchor,Vector3 tip,Vector3 a,Vector3 b,float start=0);
std::array<Vector3,2> CollisionSegment(const Joint& joint,const Pose& bone);
Pose BodyColliderPose(const Profile& profile,unsigned collider,std::span<const Pose> drivers={});
// 建图 / 分配只在 worker 或离线初始化进行；重播与实时步进复用同一物理世界。
bool Prepare(const Profile& profile,std::span<const Pose> drivers,State& state);
void Reset(State& state);
bool Initialize(const Profile& profile,std::span<const Pose> drivers,State& state);
bool Step(const Profile& profile,std::span<const Pose> drivers,State& state);
bool Advance(const Profile& profile,Sampler sampler,void* context,uint64_t tick,State& state);
bool Generate(const Profile& profile,uint64_t motionKey,double seconds,Sampler sampler,void* context,
    Cancel cancel,Progress progress,void* observer,Bake& out,std::string& error);
bool Sample(const Bake& bake,const Profile& profile,double seconds,std::span<Quaternion> out,std::span<Vector3> positions={});
bool Save(const std::filesystem::path& path,const Bake& bake,std::string& error);
bool Load(const std::filesystem::path& path,const Profile& profile,uint64_t motionKey,double seconds,Bake& out,std::string& error);
}
