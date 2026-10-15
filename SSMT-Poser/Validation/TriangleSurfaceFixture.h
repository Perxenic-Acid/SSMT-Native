#pragma once
#include "SecondaryPhysics.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <cfloat>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace poser::validation {
using Triangle=std::array<uint32_t,3>;
// 真实三角形相交检查，不使用胶囊尺寸、最近法线符号或闭合身体网格假设。
class TriangleSurfaceFixture {
    struct Bounds {
        Vector3 minimum{FLT_MAX,FLT_MAX,FLT_MAX},maximum{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        void Add(Vector3 p) {
            minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
            maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};
        }
        void Add(const Bounds& box){Add(box.minimum);Add(box.maximum);}
        bool Overlaps(const Bounds& box)const {
            return minimum.x<=box.maximum.x&&maximum.x>=box.minimum.x&&minimum.y<=box.maximum.y&&maximum.y>=box.minimum.y&&minimum.z<=box.maximum.z&&maximum.z>=box.minimum.z;
        }
        float DistanceSquared(Vector3 p)const {
            const auto clamp=[](float x,float low,float high){return std::max(low-x,std::max(0.0f,x-high));};
            const auto x=clamp(p.x,minimum.x,maximum.x),y=clamp(p.y,minimum.y,maximum.y),z=clamp(p.z,minimum.z,maximum.z);return x*x+y*y+z*z;
        }
    };
    struct Node {Bounds bounds;unsigned first=0,count=0,left=0,right=0;};
    std::vector<Triangle> body,cloth;
    std::vector<unsigned> order;
    std::vector<Node> tree;
    std::vector<uint8_t> staticIntersection;
    std::unordered_set<uint64_t> staticPairs;
    std::vector<float> orientation;
    std::vector<Triangle> worstInverted;
    Bounds Box(unsigned triangle,std::span<const Vector3> points)const {
        Bounds box;for(const auto vertex:body[triangle])box.Add(points[vertex]);return box;
    }
    unsigned Build(unsigned first,unsigned count,std::span<const Vector3> points) {
        const auto index=unsigned(tree.size());tree.push_back({});Bounds bounds;
        for(unsigned i=first;i<first+count;++i)bounds.Add(Box(order[i],points));
        tree[index].bounds=bounds;tree[index].first=first;tree[index].count=count;
        if(count<=8)return index;
        const auto size=ik::Sub(bounds.maximum,bounds.minimum);
        const auto axis=size.x>size.y?(size.x>size.z?0:2):(size.y>size.z?1:2);
        const auto component=[&](Vector3 point){return axis==0?point.x:axis==1?point.y:point.z;};
        const auto middle=first+count/2;
        std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](unsigned a,unsigned b) {
            float x=0,y=0;for(unsigned k=0;k<3;++k){x+=component(points[body[a][k]]);y+=component(points[body[b][k]]);}return x<y;
        });
        const auto left=Build(first,count/2,points),right=Build(middle,count-count/2,points);
        tree[index].left=left;tree[index].right=right;tree[index].count=0;return index;
    }
    void Refit(std::span<const Vector3> points) {
        for(size_t i=tree.size();i-->0;) {
            auto& node=tree[i];node.bounds={};
            if(node.count)for(unsigned j=node.first;j<node.first+node.count;++j)node.bounds.Add(Box(order[j],points));
            else {node.bounds.Add(tree[node.left].bounds);node.bounds.Add(tree[node.right].bounds);}
        }
    }
    static bool SegmentTriangle(Vector3 a,Vector3 b,Vector3 p,Vector3 q,Vector3 r) {
        const auto direction=ik::Sub(b,a),edge1=ik::Sub(q,p),edge2=ik::Sub(r,p);
        const auto h=ik::Cross(direction,edge2);const auto determinant=ik::Dot(edge1,h);
        if(std::fabs(determinant)<1e-12f)return false;
        const auto inverse=1/determinant;const auto s=ik::Sub(a,p);const auto u=inverse*ik::Dot(s,h);
        if(u<.00001f||u>1-.00001f)return false;
        const auto t=ik::Cross(s,edge1);const auto v=inverse*ik::Dot(direction,t);
        if(v<.00001f||u+v>1-.00001f)return false;
        const auto distance=inverse*ik::Dot(edge2,t);
        // 共同顶点和切触不算穿过面；仅统计面内、线段内部的横穿。
        return distance>.00001f&&distance<1-.00001f;
    }
    static bool Intersects(const Triangle& first,const Triangle& last,std::span<const Vector3> points) {
        for(unsigned k=0;k<3;++k) {
            if(SegmentTriangle(points[first[k]],points[first[(k+1)%3]],points[last[0]],points[last[1]],points[last[2]]))return true;
            if(SegmentTriangle(points[last[k]],points[last[(k+1)%3]],points[first[0]],points[first[1]],points[first[2]]))return true;
        }
        return false;
    }
    bool Intersects(const Triangle& triangle,std::span<const Vector3> points)const {
        Bounds box;for(const auto vertex:triangle)box.Add(points[vertex]);
        std::array<unsigned,64> stack{};unsigned size=1;stack[0]=0;
        while(size) {
            const auto& node=tree[stack[--size]];if(!node.bounds.Overlaps(box))continue;
            if(node.count) {
                for(unsigned j=node.first;j<node.first+node.count;++j)if(Intersects(triangle,body[order[j]],points))return true;
            }else {stack[size++]=node.left;stack[size++]=node.right;}
        }
        return false;
    }
    template<class Consumer> void Pairs(std::span<const Vector3> points,Consumer consume)const {
        for(unsigned c=0;c<cloth.size();++c) {
            const auto& triangle=cloth[c];Bounds box;for(const auto vertex:triangle)box.Add(points[vertex]);
            std::array<unsigned,64> stack{};unsigned size=1;stack[0]=0;
            while(size) {
                const auto& node=tree[stack[--size]];if(!node.bounds.Overlaps(box))continue;
                if(node.count) {
                    for(unsigned j=node.first;j<node.first+node.count;++j) {
                        const auto b=order[j];if(Intersects(triangle,body[b],points))consume(c,b,triangle,body[b]);
                    }
                }else {stack[size++]=node.left;stack[size++]=node.right;}
            }
        }
    }
public:
    template<class Consumer> void NewCrossings(std::span<const Vector3> points,Consumer consume) {
        Refit(points);
        Pairs(points,[&](unsigned c,unsigned b,const Triangle& garment,const Triangle& skin) {
            if(!staticPairs.contains((uint64_t(c)<<32)|b))consume(garment,skin);
        });
    }
    struct Contact {unsigned vertex;Vector3 normal;float depth;};
    struct Nearest {Vector3 point{},normal{};unsigned triangle=0;float distanceSquared=FLT_MAX;Vector3 barycentric{};};
    Nearest Closest(Vector3 p,std::span<const Vector3> points,std::span<const Vector3> normals,std::span<const uint8_t> eligible={})const {
        Nearest nearest;if(tree.empty())return nearest;
        std::array<unsigned,64> stack{};unsigned size=1;stack[0]=0;
        while(size) {
            const auto& node=tree[stack[--size]];if(node.bounds.DistanceSquared(p)>nearest.distanceSquared)continue;
            if(!node.count) {
                const auto a=tree[node.left].bounds.DistanceSquared(p),b=tree[node.right].bounds.DistanceSquared(p);
                stack[size++]=a<b?node.right:node.left;stack[size++]=a<b?node.left:node.right;continue;
            }
            for(unsigned j=node.first;j<node.first+node.count;++j) {
                const auto index=order[j];const auto& t=body[index];const auto a=points[t[0]],b=points[t[1]],c=points[t[2]];
                if(!eligible.empty()&&!eligible[index])continue;
                const auto ab=ik::Sub(b,a),ac=ik::Sub(c,a),ap=ik::Sub(p,a);
                if(ik::Dot(ik::Cross(ab,ac),ik::Cross(ab,ac))<1e-20f)continue;
                float u=0,v=0;const auto d1=ik::Dot(ab,ap),d2=ik::Dot(ac,ap);
                if(d1<=0&&d2<=0) {}
                else {
                    const auto bp=ik::Sub(p,b);const auto d3=ik::Dot(ab,bp),d4=ik::Dot(ac,bp);
                    if(d3>=0&&d4<=d3)u=1;
                    else {
                        const auto vc=d1*d4-d3*d2;
                        if(vc<=0&&d1>=0&&d3<=0)u=d1/(d1-d3);
                        else {
                            const auto cp=ik::Sub(p,c);const auto d5=ik::Dot(ab,cp),d6=ik::Dot(ac,cp);
                            if(d6>=0&&d5<=d6)v=1;
                            else {
                                const auto vb=d5*d2-d1*d6;
                                if(vb<=0&&d2>=0&&d6<=0)v=d2/(d2-d6);
                                else {
                                    const auto va=d3*d6-d5*d4;
                                    if(va<=0&&d4-d3>=0&&d5-d6>=0){v=(d4-d3)/((d4-d3)+(d5-d6));u=1-v;}
                                    else {const auto denominator=va+vb+vc;if(std::fabs(denominator)<1e-20f)continue;u=vb/denominator;v=vc/denominator;}
                                }
                            }
                        }
                    }
                }
                const auto point=ik::Add(a,ik::Add(ik::Scale(ab,u),ik::Scale(ac,v)));const auto delta=ik::Sub(p,point);const auto squared=ik::Dot(delta,delta);
                if(squared>=nearest.distanceSquared)continue;
                auto normal=ik::Unit(ik::Cross(ab,ac));
                if(!normals.empty())normal=ik::Unit(ik::Add(ik::Scale(normals[t[0]],1-u-v),ik::Add(ik::Scale(normals[t[1]],u),ik::Scale(normals[t[2]],v))));
                if(ik::Length(normal)<.5f)continue;
                nearest={point,normal,index,squared,{1-u-v,u,v}};
            }
        }
        return nearest;
    }
    const Triangle& BodyAt(unsigned index)const{return body[index];}
    unsigned BodyCount()const{return unsigned(body.size());}
    const Triangle& ClothAt(unsigned index)const{return cloth[index];}
    unsigned ClothCount()const{return unsigned(cloth.size());}
    std::vector<uint64_t> StaticPairs()const {
        std::vector<uint64_t> result(staticPairs.begin(),staticPairs.end());std::sort(result.begin(),result.end());return result;
    }
    void Update(std::span<const Vector3> points){Refit(points);}
    unsigned staticCrossings=0,maximumCrossings=0,maximumAddedCrossings=0,worstTick=0;
    uint64_t crossingSamples=0,addedCrossingSamples=0;
    unsigned maximumNewPairs=0,newPairTick=0;
    uint64_t newPairSamples=0;
    unsigned maximumInverted=0,invertedTick=0;
    void Initialize(std::span<const Triangle> triangles,std::span<const uint8_t> bodyVertices,std::span<const uint8_t> physicalVertices,std::span<const Vector3> rest) {
        for(const auto& triangle:triangles) {
            bool isBody=true,isPhysical=false;
            for(const auto vertex:triangle){isBody&=bodyVertices[vertex]!=0;isPhysical|=physicalVertices[vertex]!=0;}
            if(isBody)body.push_back(triangle);else if(isPhysical)cloth.push_back(triangle);
        }
        if(body.empty()||cloth.empty())throw std::runtime_error("source mesh has no separable body/physical triangles");
        order.resize(body.size());std::iota(order.begin(),order.end(),0u);tree.reserve(body.size()*2);Build(0,unsigned(body.size()),rest);
        staticIntersection.resize(cloth.size());
        for(unsigned i=0;i<cloth.size();++i)staticCrossings+=staticIntersection[i]=Intersects(cloth[i],rest);
        Pairs(rest,[&](unsigned c,unsigned b,const Triangle&,const Triangle&){staticPairs.insert((uint64_t(c)<<32)|b);});
        std::cout<<"Actual triangles body="<<body.size()<<" physical_cloth="<<cloth.size()<<" STATIC_crossing_triangles="<<staticCrossings<<'\n';
    }
    void Contacts(std::span<const Vector3> points,std::vector<Contact>& contacts) {
        Refit(points);contacts.clear();
        Pairs(points,[&](unsigned c,unsigned b,const Triangle& garment,const Triangle& skin) {
            if(staticPairs.contains((uint64_t(c)<<32)|b))return;
            const auto a=points[skin[0]];const auto normal=ik::Unit(ik::Cross(ik::Sub(points[skin[1]],a),ik::Sub(points[skin[2]],a)));
            unsigned worst=garment[0];float depth=0;
            for(const auto vertex:garment) {
                const auto distance=-ik::Dot(ik::Sub(points[vertex],a),normal);
                if(distance>depth){depth=distance;worst=vertex;}
            }
            if(depth>.00001f)contacts.push_back({worst,normal,depth});
        });
    }
    void Normals(std::span<const Vector3> points,std::span<const Vector3> normals,unsigned tick) {
        if(body.empty()||normals.empty())return;
        const bool initialize=orientation.empty();if(initialize)orientation.resize(body.size());unsigned inverted=0;
        for(unsigned i=0;i<body.size();++i) {
            const auto& t=body[i];const auto face=ik::Cross(ik::Sub(points[t[1]],points[t[0]]),ik::Sub(points[t[2]],points[t[0]]));
            const auto normal=ik::Add(normals[t[0]],ik::Add(normals[t[1]],normals[t[2]]));
            const auto dot=ik::Dot(face,normal);
            if(initialize)orientation[i]=dot<0?-1.0f:1.0f;
            inverted+=dot*orientation[i]<-.05f*ik::Length(face)*ik::Length(normal);
        }
        if(inverted>maximumInverted){
            maximumInverted=inverted;invertedTick=tick;worstInverted.clear();
            for(unsigned i=0;i<body.size();++i){const auto& t=body[i];const auto face=ik::Cross(ik::Sub(points[t[1]],points[t[0]]),ik::Sub(points[t[2]],points[t[0]]));const auto normal=ik::Add(normals[t[0]],ik::Add(normals[t[1]],normals[t[2]]));if(ik::Dot(face,normal)*orientation[i]<-.05f*ik::Length(face)*ik::Length(normal))worstInverted.push_back(t);}
        }
    }
    std::span<const Triangle> InvertedFaces()const{return worstInverted;}
    unsigned NewPairs(std::span<const Vector3> points) {
        Refit(points);unsigned result=0;
        Pairs(points,[&](unsigned c,unsigned b,const Triangle&,const Triangle&){result+=!staticPairs.contains((uint64_t(c)<<32)|b);});return result;
    }
    unsigned WorstTick()const{return worstTick;}
    bool Ready()const{return !tree.empty();}
    unsigned AddedCrossings()const{return maximumAddedCrossings;}
    void Observe(std::span<const Vector3> points,unsigned tick) {
        if(tree.empty())return;
        Refit(points);unsigned crossings=0,added=0,newPairs=0;
        std::vector<uint8_t> crossed(cloth.size());
        Pairs(points,[&](unsigned c,unsigned b,const Triangle&,const Triangle&) {
            crossed[c]=1;newPairs+=!staticPairs.contains((uint64_t(c)<<32)|b);
        });
        for(unsigned i=0;i<cloth.size();++i)if(crossed[i]){++crossings;added+=!staticIntersection[i];}
        crossingSamples+=crossings;addedCrossingSamples+=added;
        newPairSamples+=newPairs;
        if(newPairs>maximumNewPairs){maximumNewPairs=newPairs;newPairTick=tick;}
        maximumCrossings=std::max(maximumCrossings,crossings);
        if(added>maximumAddedCrossings){maximumAddedCrossings=added;worstTick=tick;}
    }
    void Report()const {
        if(tree.empty())return;
        std::cout<<"Actual triangle intersections max_cloth_triangles="<<maximumCrossings<<" max_added_to_STATIC="<<maximumAddedCrossings
            <<" crossing_samples="<<crossingSamples<<" added_crossing_samples="<<addedCrossingSamples<<" tick="<<worstTick<<'\n';
        std::cout<<"Actual new triangle pairs maximum="<<maximumNewPairs<<" samples="<<newPairSamples<<" tick="<<newPairTick<<'\n';
        std::cout<<"Body skin face inversions maximum="<<maximumInverted<<" tick="<<invertedTick<<'\n';
    }
};
}
