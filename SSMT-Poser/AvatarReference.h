#pragma once
#include "ReferencePose.h"
#include <span>
#include <string_view>

namespace poser::avatar_reference {
constexpr unsigned Limit=512;
struct Node { int parent=-1,axes=-1; };
struct Local { float position[4];Quaternion rotation;float scale[4]; };
static_assert(sizeof(Node)==8&&sizeof(Local)==48);
inline uint32_t Hash(std::string_view name) {
    uint32_t crc=UINT32_MAX;
    for(const unsigned char value:name){crc^=value;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xEDB88320u:0);}
    return ~crc;
}
// 当前 Avatar serializer 存储局部 TRS；仅接受无缩放的拓扑序骨架。
// 与 Body 全量 bindpose 交叉核验通过前，这个结果只是候选，不能用于 setter。
inline bool Compose(std::span<const Node> nodes,std::span<const Local> locals,std::span<reference::Pose> model,std::span<const uint8_t> required={}) {
    if(nodes.empty()||nodes.size()>Limit||locals.size()!=nodes.size()||model.size()<nodes.size()||(!required.empty()&&required.size()!=nodes.size()))return false;
    for(unsigned i=0;i<nodes.size();++i) {
        const auto parent=nodes[i].parent;const auto& local=locals[i];
        if(parent<-1||parent>=int(i))return false;
        if(!required.empty()&&!required[i])continue;
        if(parent>=0&&!required.empty()&&!required[parent])return false;
        if(!bone_detail::Valid(local.rotation))return false;
        const Vector3 position{local.position[0],local.position[1],local.position[2]};if(!ik::Finite(position))return false;
        for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(local.scale[axis])||std::fabs(local.scale[axis]-1)>0.0001f)return false;
        model[i]={vmd::Normalize(local.rotation),position};
        if(parent>=0) {
            model[i].rotation=vmd::Multiply(model[parent].rotation,model[i].rotation);
            model[i].position=ik::Add(model[parent].position,ik::Rotate(model[parent].rotation,position));
        }
    }
    return true;
}
}
