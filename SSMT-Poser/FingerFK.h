#pragma once
#include "ReferencePose.h"
#include <string_view>
#include <span>

namespace poser::fingers {
inline constexpr std::wstring_view Names[]={L"親指",L"人指",L"中指",L"薬指",L"小指"};
// 使用静态几何核对整个模型的左右和前后方向，不从现场手势推断朝向。
inline bool ModelFrame(Vector3 leftHand,Vector3 rightHand,Vector3 leftThumb,Vector3 leftLittle,Vector3 rightThumb,Vector3 rightLittle) {
    return ik::Finite(leftHand)&&ik::Finite(rightHand)&&ik::Finite(leftThumb)&&ik::Finite(leftLittle)&&ik::Finite(rightThumb)&&ik::Finite(rightLittle)&&
        leftHand.x<-0.00001f&&rightHand.x>0.00001f&&leftThumb.z>leftLittle.z+0.00001f&&rightThumb.z>rightLittle.z+0.00001f;
}
// 这里只接受规范化后的精确源名，不能把“３先”或捩骨误当作指节。
inline int Slot(std::wstring_view name) {
    if(name.empty()||(name[0]!=L'左'&&name[0]!=L'右'))return -1;
    const auto side=name[0]==L'右'?1:0;
    for(int finger=0;finger<5;++finger) {
        const auto stem=Names[finger];
        if(name.size()!=stem.size()+2||name.substr(1,stem.size())!=stem)continue;
        const auto joint=int(name.back()-L'０')-(finger?1:0);
        if(joint>=0&&joint<3)return side*15+finger*3+joint;
    }
    return -1;
}
struct Node {int index=-1,parent=-1;bool reference=false;Vector3 position;};
// Hand + 三节，实际父链和静态几何必须同时成立；名称只是候选入口。
inline bool Chain(std::span<const Node> nodes) {
    if(nodes.size()!=4)return false;
    for(unsigned i=0;i<nodes.size();++i) {
        const auto& node=nodes[i];
        if(node.index<0||!node.reference||!std::isfinite(node.position.x)||!std::isfinite(node.position.y)||!std::isfinite(node.position.z))return false;
        for(unsigned j=0;j<i;++j)if(nodes[j].index==node.index)return false;
        if(i&&(node.parent!=nodes[i-1].index||ik::Length(ik::Sub(node.position,nodes[i-1].position))<0.00001f))return false;
    }
    return true;
}
}
