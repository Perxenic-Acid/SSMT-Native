#include "SkinSurface.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace poser;
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool Below(Vector3 a,Vector3 b,float tolerance=.00001f){return ik::Length(ik::Sub(a,b))<tolerance;}
}
void SkinSurfaceOracle() {
    const physics::InputBone bones[]={
        {L"Actor",-1,{{},{0,0,0,1}},{},true,true},
        {L"BodyAnchor",0,{{0,1,0},{0,0,0,1}},{0,1,0},true,true},
        {L"UnlabelledA",1,{{.2f,1,0},{0,0,0,1}},{.2f,0,0},true,false},
        {L"UnlabelledB",2,{{.2f,.8f,0},{0,0,0,1}},{0,-.2f,0},true,false},
        {L"UnselectedGarment",1,{{.4f,.9f,0},{0,0,0,1}},{.4f,-.1f,0},true,false}
    };
    const unsigned selected[]={2,3};physics::Profile profile;std::string error;
    const auto profileReady=physics::Build(bones,selected,1,profile,error);Check(profileReady,error.c_str());
    const physics::Candidate candidates[]={{2},{3},{4}};
    const auto vertex=[&](Vector3 rest,unsigned a,unsigned b,float weight) {
        physics::skin::Vertex result;result.rest=rest;
        result.influences[0]={a,weight,ik::Sub(rest,profile.bones[a].rest.position)};
        result.influences[1]={b,1-weight,ik::Sub(rest,profile.bones[b].rest.position)};return result;
    };
    std::vector<physics::skin::Vertex> vertices={
        vertex({.2f,1,0},2,2,1),vertex({.25f,.8f,.03125f},3,3,1),vertex({.125f,.9f,.125f},1,3,.5f),
        vertex({0,1,0},1,1,1),{},vertex({.25f,.8f,.03125f},1,1,1),
        vertex({.4f,.9f,0},4,4,1),vertex({.125f,1,0},1,1,1)
    };
    vertices[4]=vertices[1];std::swap(vertices[4].influences[0],vertices[4].influences[3]);
    const std::vector<physics::skin::Face> faces={{0,1,2},{0,2,3},{1,4,0},{0,3,7},{3,5,7},{6,7,5}};
    physics::skin::Surface surface;
    const auto surfaceReady=physics::skin::Surface::Build(profile,vertices,faces,candidates,surface,error);Check(surfaceReady,error.c_str());
    Check(surface.Compatible(profile)&&surface.Points().size()==8&&surface.Body().size()==1&&surface.Cloth().size()==4,"surface did not separate selected, fixed body and unselected garment weights");
    Check(surface.Edges().size()==9,"surface edge graph lost source connectivity or merged coincident vertices");
    Check(surface.Coincidences().size()==1&&surface.Coincidences()[0].representative==1&&surface.Coincidences()[0].vertex==4,"coincidence candidates ignored canonical weights or joined different layers");
    Check(surface.Points()[2].movableWeight==.5f&&!surface.Points()[2].body&&surface.Points()[6].movableWeight==0&&!surface.Points()[6].body,"partial attachment weights or unselected cloth were discarded");
    Check(std::any_of(surface.Edges().begin(),surface.Edges().end(),[](auto edge){return edge.a==3&&edge.b==7;}),"fixed boundary edge was removed from a physical triangle");
    Check(std::any_of(surface.Edges().begin(),surface.Edges().end(),[](auto edge){return edge.a==1&&edge.b==4&&edge.length==0;}),"coincident source edge was silently deleted or given an artificial rest length");
    std::vector<physics::Pose> poses;for(const auto& bone:bones)poses.push_back(bone.rest);
    std::vector<Vector3> positions(vertices.size());Check(surface.Skin(poses,positions),"static surface evaluation failed");
    for(unsigned i=0;i<vertices.size();++i)Check(Below(positions[i],vertices[i].rest),"surface does not reproduce STATIC skin");
    const auto placement=ik::AxisRotation(ik::Unit({1,2,3}),.7f);const Vector3 translation{2,-3,1};
    auto placed=poses;
    for(auto& pose:placed){pose.position=ik::Add(translation,ik::Rotate(placement,pose.position));pose.rotation=vmd::Multiply(placement,pose.rotation);}
    Check(surface.Skin(placed,positions),"placed skin evaluation failed");
    for(unsigned i=0;i<vertices.size();++i)Check(Below(positions[i],ik::Add(translation,ik::Rotate(placement,vertices[i].rest))),"mixed-weight skin depends on world placement");

    // 用独立的整棵 FK 子树旋转检验解析梯度；包括后代、混合固定权重和零杠杆。
    const auto rotated=[&](unsigned joint,Vector3 axis,float angle) {
        auto result=placed;const auto bone=profile.joints[joint].bone;const auto pivot=placed[bone].position;const auto rotation=ik::AxisRotation(axis,angle);
        for(unsigned i=0;i<profile.boneCount;++i) {
            bool affected=false;for(auto ancestor=int(i);ancestor>=0;ancestor=profile.bones[ancestor].parent)affected|=ancestor==int(bone);
            if(affected){result[i].position=ik::Add(pivot,ik::Rotate(rotation,ik::Sub(placed[i].position,pivot)));result[i].rotation=vmd::Multiply(rotation,placed[i].rotation);}
        }
        return result;
    };
    const Vector3 normal=ik::Unit({.3f,.7f,-.5f});constexpr float epsilon=.001f;
    std::vector<Vector3> plus(vertices.size()),minus(vertices.size());
    for(unsigned joint=0;joint<profile.jointCount;++joint)for(const auto axis:{Vector3{1,0,0},Vector3{0,1,0},Vector3{0,0,1},ik::Unit({2,-1,3})}) {
        Check(surface.Skin(rotated(joint,axis,epsilon),plus)&&surface.Skin(rotated(joint,axis,-epsilon),minus),"perturbed FK rejected");
        for(unsigned i=0;i<vertices.size();++i) {
            const auto expected=ik::Dot(ik::Sub(plus[i],minus[i]),normal)/(2*epsilon);
            const auto actual=ik::Dot(surface.ContactGradient(i,joint,placed,normal),axis);
            Check(std::fabs(actual-expected)<.0004f,"four-weight FK contact Jacobian differs from finite differences");
        }
        for(const auto& edge:surface.Edges())if(edge.length>.0005f) {
            const auto a=physics::skin::Surface::Evaluate(surface.Points()[edge.a].vertex,placed),b=physics::skin::Surface::Evaluate(surface.Points()[edge.b].vertex,placed);
            const auto direction=ik::Unit(ik::Sub(b,a));
            const auto actual=ik::Dot(ik::Sub(surface.ContactGradient(edge.b,joint,placed,direction),surface.ContactGradient(edge.a,joint,placed,direction)),axis);
            const auto expected=(ik::Length(ik::Sub(plus[edge.b],plus[edge.a]))-ik::Length(ik::Sub(minus[edge.b],minus[edge.a])))/(2*epsilon);
            Check(std::fabs(actual-expected)<.0004f,"actual source edge Jacobian differs from finite differences");
        }
    }
    auto reordered=vertices;for(auto& point:reordered)std::reverse(point.influences.begin(),point.influences.end());
    physics::skin::Surface equivalent;Check(physics::skin::Surface::Build(profile,reordered,faces,candidates,equivalent,error)&&equivalent.Key()==surface.Key(),"palette influence order changed canonical surface identity");
    reordered=vertices;reordered[1].influences[0].weight=.25f;reordered[1].influences[1]=reordered[1].influences[0];reordered[1].influences[1].weight=.75f;
    Check(physics::skin::Surface::Build(profile,reordered,faces,candidates,equivalent,error)&&equivalent.Key()==surface.Key(),"splitting a repeated bone influence changed equivalent geometry");
    reordered=vertices;reordered[0].influences[3]={std::numeric_limits<unsigned>::max(),0,{std::numeric_limits<float>::quiet_NaN(),0,0}};
    Check(physics::skin::Surface::Build(profile,reordered,faces,candidates,equivalent,error)&&equivalent.Key()==surface.Key(),"zero-weight unused palette slot influenced the surface");
    auto changed=faces;changed[0]={0,2,1};
    Check(physics::skin::Surface::Build(profile,vertices,changed,candidates,equivalent,error)&&equivalent.Key()!=surface.Key(),"surface fingerprint ignores source topology");
    auto otherProfile=profile;++otherProfile.key;Check(!surface.Compatible(otherProfile),"surface accepted another static profile generation");
    const auto originalKey=surface.Key();
    const auto reject=[&](std::vector<physics::skin::Vertex> invalid,std::vector<physics::skin::Face> invalidFaces) {
        Check(!physics::skin::Surface::Build(profile,invalid,invalidFaces,candidates,surface,error)&&!error.empty()&&surface.Key()==originalKey,"invalid input was accepted or changed the previous surface");
    };
    auto invalid=vertices;invalid[0].influences[0].weight=std::numeric_limits<float>::quiet_NaN();reject(invalid,faces);
    invalid=vertices;invalid[0].influences[0].weight=.5f;reject(invalid,faces);
    invalid=vertices;invalid[0].influences[0].bone=profile.boneCount;reject(invalid,faces);
    invalid=vertices;invalid[0].rest.x+=.001f;reject(invalid,faces);
    auto extreme=profile;extreme.bones[2].rest.position.x=std::numeric_limits<float>::max();extreme.bones[3].rest.position.x=-std::numeric_limits<float>::max();
    physics::skin::Vertex overflow;overflow.influences[0]={2,.5f,{std::numeric_limits<float>::max(),0,0}};overflow.influences[1]={3,.5f,{-std::numeric_limits<float>::max(),0,0}};
    Check(!physics::skin::Surface::Build(extreme,std::span(&overflow,1),std::vector<physics::skin::Face>{{0,0,0}},candidates,equivalent,error)&&error=="SKIN_SURFACE_STATIC_MISMATCH","finite inputs produced an accepted non-finite STATIC skin");
    changed=faces;changed[0][0]=unsigned(vertices.size());reject(vertices,changed);
    otherProfile=profile;otherProfile.bones[1].parent=3;
    Check(!physics::skin::Surface::Build(otherProfile,vertices,faces,candidates,equivalent,error)&&error=="SKIN_SURFACE_HIERARCHY_INVALID","cyclic static rig was accepted");
    Check(!surface.Skin(std::span(poses.data(),poses.size()-1),positions)&&!surface.Skin(poses,std::span(positions.data(),positions.size()-1)),"surface accepted mismatched output capacity");
    auto badPose=poses;badPose[0].position.x=std::numeric_limits<float>::infinity();Check(!surface.Skin(badPose,positions),"non-finite skin pose was accepted");
    std::cout<<"STATIC skin graph: exact edges, fixed weights, conservative seams, four-weight FK contact / edge Jacobians and identity rejection PASS\n";
}
