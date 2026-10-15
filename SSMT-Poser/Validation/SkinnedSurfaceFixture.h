#pragma once
#include "PhysicsMotion.h"
#include "ReferencePose.h"
#include "TriangleSurfaceFixture.h"
#include "SkinStrainFixture.h"
#include "SkinSurface.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

// 独立实网格检查：验证最终 FK 蒙皮，不能以骨段代理的通过代替可见表面的通过。
namespace poser::validation {
struct SkinVertex { Vector3 position;std::array<float,4> weights;std::array<uint32_t,4> indices; };
static_assert(sizeof(SkinVertex)==44);
class SkinnedSurfaceFixture {
    struct Influence {unsigned bone;float weight;Vector3 local;Vector3 normal;};
    struct Point {std::array<Influence,4> influences;std::array<float,physics::MaxColliders> baseline{};};
    std::vector<Point> points;
    std::vector<Point> allPoints;
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    TriangleSurfaceFixture triangles;
    SkinStrainFixture strain;
    physics::skin::Surface skinSurface;
    std::vector<Vector3> surfacePositions;
    std::array<physics::Pose,physics::MaxBones> worstPose{};
    std::filesystem::path sourcePath;
    uint64_t poseKey=0;
    unsigned poseBoneCount=0;
    static void Require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
    static Vector3 Transform(const reference::Matrix& matrix,Vector3 point) {
        const auto* m=matrix.value;
        return {m[0]*point.x+m[4]*point.y+m[8]*point.z+m[12],
            m[1]*point.x+m[5]*point.y+m[9]*point.z+m[13],
            m[2]*point.x+m[6]*point.y+m[10]*point.z+m[14]};
    }
    static Vector3 Skin(const Point& point,std::span<const physics::Pose> poses) {
        Vector3 result{};
        for(const auto& influence:point.influences)if(influence.weight>0) {
            const auto& bone=poses[influence.bone];
            result=ik::Add(result,ik::Scale(ik::Add(bone.position,ik::Rotate(bone.rotation,influence.local)),influence.weight));
        }
        return result;
    }
    static Vector3 SkinNormal(const Point& point,std::span<const physics::Pose> poses) {
        Vector3 result{};
        for(const auto& influence:point.influences)if(influence.weight>0)
            result=ik::Add(result,ik::Scale(ik::Rotate(poses[influence.bone].rotation,influence.normal),influence.weight));
        return result;
    }
public:
    std::array<unsigned,physics::MaxBones> InvertedBoneCounts()const {
        std::array<unsigned,physics::MaxBones> result{};
        for(const auto& face:triangles.InvertedFaces())for(auto v:face)for(const auto& influence:allPoints[v].influences)if(influence.weight>.01f)++result[influence.bone];
        return result;
    }
    float maximumStaticError=0,maximumAbsolute=0,maximumAdded=0,maximumRestPenetration=0;
    unsigned worstVertex=0,worstCollider=0,worstTick=0;
    uint64_t absoluteSamples=0,addedSamples=0;
    template<class Bind,class Locate>
    void Load(const wchar_t* path,std::span<const Bind> binds,Locate locate,const physics::Profile& profile,std::span<const physics::Candidate> candidates) {
        if(!path)return;
        sourcePath=path;
        std::ifstream input(std::filesystem::path(path),std::ios::binary);
        std::array<char,8> magic{};uint32_t count=0,bindCount=0;
        input.read(magic.data(),magic.size());input.read(reinterpret_cast<char*>(&count),4);input.read(reinterpret_cast<char*>(&bindCount),4);
        const auto format=std::string_view(magic.data(),8);
        Require(input.good()&&(format=="SSMTSKN1"||format=="SSMTSKN2"||format=="SSMTSKN3")&&count>0&&count<=200000&&bindCount==binds.size(),"skin fixture header invalid");
        std::vector<SkinVertex> vertices(count);input.read(reinterpret_cast<char*>(vertices.data()),std::streamsize(count*sizeof(SkinVertex)));
        Require(input.good(),"skin fixture truncated");
        std::vector<Triangle> faces;
        if(format!="SSMTSKN1") {
            uint32_t triangleCount=0;input.read(reinterpret_cast<char*>(&triangleCount),4);
            Require(input.good()&&triangleCount>0&&triangleCount<=400000,"triangle fixture count invalid");
            faces.resize(triangleCount);input.read(reinterpret_cast<char*>(faces.data()),std::streamsize(triangleCount*sizeof(Triangle)));
            for(const auto& face:faces)for(const auto index:face)Require(index<count,"triangle fixture index invalid");
        }
        if(format=="SSMTSKN3") {
            normals.resize(count);input.read(reinterpret_cast<char*>(normals.data()),std::streamsize(count*sizeof(Vector3)));
            for(const auto& normal:normals)Require(ik::Finite(normal),"source normal non-finite");
        }
        Require(input.good()&&input.peek()==EOF,"skin fixture has missing or trailing bytes");
        poseKey=physics::HashBytes(profile.key,vertices.data(),vertices.size()*sizeof(SkinVertex));
        poseKey=physics::HashBytes(poseKey,faces.data(),faces.size()*sizeof(Triangle));poseBoneCount=profile.boneCount;
        std::array<uint8_t,physics::MaxBones> eligible{};
        for(const auto& candidate:candidates)eligible[candidate.bone]=1;
        std::vector<uint8_t> bodyVertices(count),physicalVertices(count);
        std::vector<physics::skin::Vertex> sourceSurface;sourceSurface.reserve(count);
        positions.resize(count);allPoints.reserve(count);
        std::array<physics::Pose,physics::MaxBones> rest{};
        for(unsigned i=0;i<profile.boneCount;++i)rest[i]=profile.bones[i].rest;
        for(unsigned ordinal=0;ordinal<count;++ordinal) {
            const auto& vertex=vertices[ordinal];
            Require(ik::Finite(vertex.position),"skin fixture vertex invalid");
            Point point{};float sum=0,participation=0,clothWeight=0;
            for(unsigned k=0;k<4;++k) {
                const auto weight=vertex.weights[k];const auto index=vertex.indices[k];
                Require(std::isfinite(weight)&&weight>=0&&weight<=1.0001f&&index<binds.size(),"skin fixture influence invalid");
                const auto bone=locate(binds[index].name);Require(bone>=0&&profile.bones[bone].valid,"skin fixture bone invalid");
                point.influences[k]={unsigned(bone),weight,Transform(binds[index].inverse,vertex.position),{}};
                if(!normals.empty())point.influences[k].normal=ik::Sub(Transform(binds[index].inverse,normals[ordinal]),Transform(binds[index].inverse,{}));
                sum+=weight;
                for(auto ancestor=bone;ancestor>=0;ancestor=profile.bones[ancestor].parent)if(profile.bones[ancestor].joint>=0){participation+=weight;break;}
                for(auto ancestor=bone;ancestor>=0;ancestor=profile.bones[ancestor].parent)if(eligible[ancestor]){clothWeight+=weight;break;}
            }
            Require(std::fabs(sum-1)<.001f,"skin fixture weights do not sum to one");
            maximumStaticError=std::max(maximumStaticError,ik::Length(ik::Sub(Skin(point,rest),vertex.position)));
            bodyVertices[ordinal]=clothWeight<=.0001f;physicalVertices[ordinal]=participation>.0001f;
            allPoints.push_back(point);positions[ordinal]=Skin(point,rest);
            physics::skin::Vertex sourcePoint;sourcePoint.rest=vertex.position;
            for(unsigned k=0;k<4;++k){const auto& i=point.influences[k];sourcePoint.influences[k]={i.bone,i.weight,i.local};}
            sourceSurface.push_back(sourcePoint);
            if(participation<=.0001f)continue;
            const auto position=Skin(point,rest);
            for(unsigned c=0;c<profile.colliderCount;++c) {
                const auto& collider=profile.colliders[c];
                point.baseline[c]=std::max(0.0f,collider.radius-physics::SegmentClearance(position,position,rest[collider.a].position,rest[collider.b].position));
                maximumRestPenetration=std::max(maximumRestPenetration,point.baseline[c]);
            }
            points.push_back(point);
        }
        Require(maximumStaticError<.00002f&&!points.empty(),"source skin does not reproduce the STATIC reference");
        std::cout<<"Actual skin vertices="<<count<<" physical_surface_vertices="<<points.size()<<" STATIC_error="<<maximumStaticError<<" rest_body_proxy_penetration="<<maximumRestPenetration<<'\n';
        if(!faces.empty())triangles.Initialize(faces,bodyVertices,physicalVertices,positions);
        if(!faces.empty()) {
            std::string error;const auto ready=physics::skin::Surface::Build(profile,sourceSurface,faces,candidates,skinSurface,error);Require(ready,error.c_str());
            surfacePositions.resize(count);
            unsigned mixed=0;for(const auto& point:skinSurface.Points())mixed+=point.movableWeight>0&&point.movableWeight<.9999f;
            std::cout<<"Actual skin topology edges="<<skinSurface.Edges().size()<<" coincidence_candidates="<<skinSurface.Coincidences().size()
                <<" mixed_fixed_weight_vertices="<<mixed<<" key="<<skinSurface.Key()<<" solver=NOT_CONNECTED\n";
        }
        Require(strain.Initialize(faces,physicalVertices,positions),"source cloth edge topology invalid");
        if(!normals.empty())triangles.Normals(positions,normals,0);
    }
    void Observe(const physics::Profile& profile,std::span<const physics::Pose> poses,unsigned tick) {
        for(unsigned i=0;i<allPoints.size();++i) {
            positions[i]=Skin(allPoints[i],poses);if(!normals.empty())normals[i]=SkinNormal(allPoints[i],poses);
        }
        if(!skinSurface.Points().empty()) {
            Require(skinSurface.Compatible(profile)&&skinSurface.Skin(poses,surfacePositions),"static skin surface pose rejected");
            for(unsigned i=0;i<positions.size();++i)Require(ik::Length(ik::Sub(surfacePositions[i],positions[i]))<.00002f,"canonical skin differs from source inverse-bind evaluation");
        }
        triangles.Observe(positions,tick);
        Require(strain.Observe(positions,tick),"final skin edge geometry invalid");
        triangles.Normals(positions,normals,tick);
        if(triangles.WorstTick()==tick)std::copy(poses.begin(),poses.end(),worstPose.begin());
        for(unsigned vertex=0;vertex<points.size();++vertex) {
            const auto& point=points[vertex];const auto position=Skin(point,poses);
            for(unsigned c=0;c<profile.colliderCount;++c) {
                const auto& collider=profile.colliders[c];
                const auto depth=std::max(0.0f,collider.radius-physics::SegmentClearance(position,position,poses[collider.a].position,poses[collider.b].position));
                const auto added=depth-point.baseline[c];
                maximumAbsolute=std::max(maximumAbsolute,depth);absoluteSamples+=depth>.002f;addedSamples+=added>.002f;
                if(added>maximumAdded){maximumAdded=added;worstVertex=vertex;worstCollider=c;worstTick=tick;}
            }
        }
    }
    void Report(bool savePose=true)const {
        if(points.empty())return;
        std::cout<<"Actual skin body proxy absolute_penetration="<<maximumAbsolute<<" added_to_STATIC="<<maximumAdded
            <<" samples_above_2mm="<<absoluteSamples<<" added_samples_above_2mm="<<addedSamples
            <<" vertex="<<worstVertex<<" collider="<<worstCollider<<" tick="<<worstTick<<'\n';
        triangles.Report();
        strain.Report();
        if(savePose&&triangles.Ready()) {
            auto path=sourcePath;path+=L".driver-"+std::to_wstring(physics::MotionDriverVersion)+L".worst-pose-"+std::to_wstring(poseKey);
            std::ofstream output(path,std::ios::binary);output.write("SSMTPSE1",8);
            output.write(reinterpret_cast<const char*>(&poseKey),8);output.write(reinterpret_cast<const char*>(&poseBoneCount),4);
            output.write(reinterpret_cast<const char*>(worstPose.data()),worstPose.size()*sizeof(physics::Pose));
            std::cout<<"Worst skin pose: "<<path.string()<<'\n';
        }
        std::cout.flush();
    }
    void ReplayPose(const wchar_t* path,const physics::Profile& profile) {
        std::ifstream input(std::filesystem::path(path),std::ios::binary);
        std::array<char,8> magic{};uint64_t key=0;unsigned count=0;
        input.read(magic.data(),8);input.read(reinterpret_cast<char*>(&key),8);input.read(reinterpret_cast<char*>(&count),4);
        Require(input.good()&&std::string_view(magic.data(),8)=="SSMTPSE1"&&key==poseKey&&count==poseBoneCount,"surface pose fixture belongs to another geometry/profile");
        input.read(reinterpret_cast<char*>(worstPose.data()),worstPose.size()*sizeof(physics::Pose));
        Require(input.good()&&input.peek()==EOF,"surface pose fixture truncated");
        for(unsigned i=0;i<profile.boneCount;++i)if(profile.bones[i].valid)
            Require(ik::Finite(worstPose[i].position)&&physics::ValidRotation(worstPose[i].rotation),"surface pose fixture non-finite");
        Observe(profile,std::span(worstPose.data(),profile.boneCount),0);
        Report(false);
    }
    bool HasNewCrossings()const{return triangles.maximumNewPairs!=0;}
};
}
