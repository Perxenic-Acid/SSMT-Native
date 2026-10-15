#pragma once
#include "SecondaryPhysics.h"
#include <algorithm>
#include <numeric>
#include <tuple>

namespace poser::physics::skin {
struct Influence {unsigned bone=0;float weight=0;Vector3 local{};};
struct Vertex {Vector3 rest{};std::array<Influence,4> influences{};};
using Face=std::array<uint32_t,3>;
struct Edge {unsigned a=0,b=0;float length=0;};
struct Point {
    Vertex vertex;
    float movableWeight=0;
    bool body=false;
};
// 同位置、同蒙皮只是接缝候选证据；没有部件 / 邻接证据时不自动缝合重叠衣层。
struct Coincident {unsigned representative=0,vertex=0;};

// 四权重和源拓扑保持不变。此模型供最终 FK 接触约束使用，不是另一个刚体代理。
class Surface {
    std::vector<Point> points;
    std::vector<Face> body,cloth;
    std::vector<Edge> edges;
    std::vector<Coincident> coincident;
    std::array<std::array<bool,MaxBones>,MaxJoints> affected{};
    std::array<unsigned,MaxJoints> jointBones{};
    unsigned boneCount=0,jointCount=0;
    uint64_t profileKey=0,key=0;
    static float Canonical(float value){return value==0?0:value;}
    static auto PointKey(const Point& p) {
        const auto& v=p.vertex;
        return std::tuple{v.rest.x,v.rest.y,v.rest.z,
            v.influences[0].bone,v.influences[0].weight,v.influences[0].local.x,v.influences[0].local.y,v.influences[0].local.z,
            v.influences[1].bone,v.influences[1].weight,v.influences[1].local.x,v.influences[1].local.y,v.influences[1].local.z,
            v.influences[2].bone,v.influences[2].weight,v.influences[2].local.x,v.influences[2].local.y,v.influences[2].local.z,
            v.influences[3].bone,v.influences[3].weight,v.influences[3].local.x,v.influences[3].local.y,v.influences[3].local.z};
    }
public:
    static Vector3 Evaluate(const Vertex& vertex,std::span<const Pose> poses) {
        Vector3 result{};
        for(const auto& influence:vertex.influences)if(influence.weight>0) {
            const auto& pose=poses[influence.bone];
            result=ik::Add(result,ik::Scale(ik::Add(pose.position,ik::Rotate(pose.rotation,influence.local)),influence.weight));
        }
        return result;
    }
    static bool Build(const Profile& profile,std::span<const Vertex> source,std::span<const Face> faces,
        std::span<const Candidate> candidates,Surface& out,std::string& error) {
        const auto fail=[&](const char* reason){error=reason;return false;};
        if(!profile.boneCount||profile.boneCount>MaxBones||profile.jointCount>MaxJoints||source.empty()||source.size()>200000||faces.empty()||faces.size()>400000)
            return fail("SKIN_SURFACE_SIZE_INVALID");
        Surface next;next.boneCount=profile.boneCount;next.jointCount=profile.jointCount;next.profileKey=profile.key;
        std::array<bool,MaxBones> candidate{};std::array<Pose,MaxBones> rest{};
        for(const auto& item:candidates) {
            if(item.bone>=profile.boneCount||!profile.bones[item.bone].valid)return fail("SKIN_SURFACE_CANDIDATE_INVALID");
            candidate[item.bone]=true;
        }
        for(unsigned j=0;j<profile.jointCount;++j) {
            const auto bone=profile.joints[j].bone;
            if(bone>=profile.boneCount||!profile.bones[bone].valid||profile.bones[bone].joint!=int(j))return fail("SKIN_SURFACE_JOINT_INVALID");
            candidate[bone]=true;
            next.jointBones[j]=bone;
        }
        std::array<bool,MaxBones> movable{},garment{};
        for(unsigned i=0;i<profile.boneCount;++i)if(profile.bones[i].valid) {
            const auto& node=profile.bones[i];rest[i]=node.rest;
            if(!ik::Finite(rest[i].position)||!ValidRotation(rest[i].rotation))return fail("SKIN_SURFACE_REST_INVALID");
            if(node.parent< -1)return fail("SKIN_SURFACE_HIERARCHY_INVALID");
            unsigned depth=0;
            for(auto ancestor=int(i);ancestor>=0;ancestor=profile.bones[ancestor].parent) {
                if(unsigned(ancestor)>=profile.boneCount||!profile.bones[ancestor].valid||++depth>profile.boneCount)return fail("SKIN_SURFACE_HIERARCHY_INVALID");
                const auto joint=profile.bones[ancestor].joint;
                if(joint>=0) {
                    if(unsigned(joint)>=profile.jointCount||profile.joints[joint].bone!=unsigned(ancestor))return fail("SKIN_SURFACE_JOINT_INVALID");
                    next.affected[unsigned(joint)][i]=true;movable[i]=true;
                }
                garment[i]|=candidate[ancestor];
            }
        }
        next.points.reserve(source.size());
        for(const auto& vertex:source) {
            if(!ik::Finite(vertex.rest))return fail("SKIN_SURFACE_VERTEX_INVALID");
            Point point{};point.vertex.rest=vertex.rest;point.body=true;float sum=0;unsigned count=0;
            auto influences=vertex.influences;
            for(auto& influence:influences) {
                if(!std::isfinite(influence.weight)||influence.weight<0||influence.weight>1.0001f)return fail("SKIN_SURFACE_WEIGHT_INVALID");
                if(influence.weight==0){influence={};continue;}
                if(influence.bone>=profile.boneCount||!profile.bones[influence.bone].valid||!ik::Finite(influence.local))return fail("SKIN_SURFACE_INFLUENCE_INVALID");
                sum+=influence.weight;
            }
            if(std::fabs(sum-1)>.001f)return fail("SKIN_SURFACE_WEIGHT_SUM_INVALID");
            const auto influenceKey=[](const auto& i){return std::tuple{i.weight==0,i.bone,i.weight,i.local.x,i.local.y,i.local.z};};
            std::sort(influences.begin(),influences.end(),[&](const auto& a,const auto& b){return influenceKey(a)<influenceKey(b);});
            for(const auto& influence:influences)if(influence.weight>0) {
                if(count&&point.vertex.influences[count-1].bone==influence.bone) {
                    auto& previous=point.vertex.influences[count-1];
                    if(previous.local.x!=influence.local.x||previous.local.y!=influence.local.y||previous.local.z!=influence.local.z)
                        previous.local=ik::Scale(ik::Add(ik::Scale(previous.local,previous.weight),ik::Scale(influence.local,influence.weight)),1/(previous.weight+influence.weight));
                    previous.weight+=influence.weight;
                }else point.vertex.influences[count++]=influence;
                if(movable[influence.bone])point.movableWeight+=influence.weight;
                if(garment[influence.bone])point.body=false;
            }
            const auto evaluated=Evaluate(point.vertex,std::span(rest.data(),profile.boneCount));
            if(!ik::Finite(evaluated)||ik::Length(ik::Sub(evaluated,vertex.rest))>.00002f)return fail("SKIN_SURFACE_STATIC_MISMATCH");
            next.points.push_back(point);
        }
        std::vector<uint64_t> edgeKeys;edgeKeys.reserve(faces.size()*3);
        for(const auto& face:faces) {
            bool isBody=true,isMovable=false;
            for(auto v:face) {
                if(v>=source.size())return fail("SKIN_SURFACE_INDEX_INVALID");
                isBody&=next.points[v].body;isMovable|=next.points[v].movableWeight>0;
            }
            if(isBody)next.body.push_back(face);
            if(!isMovable)continue;
            next.cloth.push_back(face);
            // 固定端之间的边也属于同一片参与模拟的三角形，不能删掉其附着边界。
            for(unsigned k=0;k<3;++k) {
                const auto a=std::min(face[k],face[(k+1)%3]),b=std::max(face[k],face[(k+1)%3]);
                if(a!=b)edgeKeys.push_back((uint64_t(a)<<32)|b);
            }
        }
        std::sort(edgeKeys.begin(),edgeKeys.end());edgeKeys.erase(std::unique(edgeKeys.begin(),edgeKeys.end()),edgeKeys.end());
        next.edges.reserve(edgeKeys.size());
        for(auto edge:edgeKeys) {
            const auto a=unsigned(edge>>32),b=unsigned(edge&0xffffffff);
            const auto length=ik::Length(ik::Sub(source[a].rest,source[b].rest));
            if(!std::isfinite(length))return fail("SKIN_SURFACE_EDGE_INVALID");
            next.edges.push_back({a,b,length});
        }
        std::vector<unsigned> order(source.size());std::iota(order.begin(),order.end(),0u);
        std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return PointKey(next.points[a])<PointKey(next.points[b]);});
        for(size_t first=0;first<order.size();) {
            auto last=first+1;while(last<order.size()&&PointKey(next.points[order[first]])==PointKey(next.points[order[last]]))++last;
            const auto representative=*std::min_element(order.begin()+first,order.begin()+last);
            for(auto i=first;i<last;++i)if(order[i]!=representative)next.coincident.push_back({representative,order[i]});
            first=last;
        }
        std::sort(next.coincident.begin(),next.coincident.end(),[](const auto& a,const auto& b){return std::tie(a.representative,a.vertex)<std::tie(b.representative,b.vertex);});
        // 不散列结构体填充字节；指纹包含固定权重、局部参考、参与范围和原始拓扑。
        auto key=profile.key;
        const auto hash=[&](const auto& value){key=HashBytes(key,&value,sizeof(value));};
        hash(uint32_t(1));hash(uint32_t(source.size()));hash(uint32_t(faces.size()));
        for(const auto& point:next.points) {
            const auto& v=point.vertex;hash(Canonical(v.rest.x));hash(Canonical(v.rest.y));hash(Canonical(v.rest.z));
            for(const auto& i:v.influences){hash(i.bone);hash(Canonical(i.weight));hash(Canonical(i.local.x));hash(Canonical(i.local.y));hash(Canonical(i.local.z));}
            hash(point.body);hash(point.movableWeight);
        }
        for(const auto& face:faces)for(auto v:face)hash(v);
        next.key=key;out=std::move(next);error.clear();return true;
    }
    bool Compatible(const Profile& profile)const{return !points.empty()&&profile.key==profileKey&&profile.boneCount==boneCount&&profile.jointCount==jointCount;}
    bool Skin(std::span<const Pose> poses,std::span<Vector3> output)const {
        if(poses.size()!=boneCount||output.size()!=points.size())return false;
        for(const auto& pose:poses)if(!ik::Finite(pose.position)||!ValidRotation(pose.rotation))return false;
        for(unsigned i=0;i<points.size();++i){output[i]=Evaluate(points[i].vertex,poses);if(!ik::Finite(output[i]))return false;}
        return true;
    }
    // 世界空间小角度旋转的接触梯度。包含受影响的所有后代和每一项蒙皮权重，
    // 不是只旋转最大权重骨，也不是对刚体质心求导。调用前须验证姿态与 Profile。
    Vector3 ContactGradient(unsigned vertex,unsigned joint,std::span<const Pose> poses,Vector3 normal)const {
        if(vertex>=points.size()||joint>=jointCount||poses.size()!=boneCount)return {};
        Vector3 lever{};const auto pivot=poses[jointBones[joint]].position;
        for(const auto& influence:points[vertex].vertex.influences)if(influence.weight>0&&affected[joint][influence.bone]) {
            const auto& bone=poses[influence.bone];
            const auto point=ik::Add(bone.position,ik::Rotate(bone.rotation,influence.local));
            lever=ik::Add(lever,ik::Scale(ik::Sub(point,pivot),influence.weight));
        }
        return ik::Cross(lever,normal);
    }
    uint64_t Key()const{return key;}
    std::span<const Point> Points()const{return points;}
    std::span<const Face> Body()const{return body;}
    std::span<const Face> Cloth()const{return cloth;}
    std::span<const Edge> Edges()const{return edges;}
    std::span<const Coincident> Coincidences()const{return coincident;}
};
}
