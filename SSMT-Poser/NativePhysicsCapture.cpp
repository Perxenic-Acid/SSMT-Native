#include "NativePhysicsCapture.h"
#include "BoneWriteProbe.h"
#include "RuntimeProbe.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace poser::native_physics {
namespace {
struct Configuration {
    uintptr_t animator=0,arrayClass=0,particleClass=0,capsuleClass=0,sphereClass=0;
    uintptr_t meshClass=0;
    std::vector<uintptr_t> components;
    bool ready=false,nativeTransform=false;
    bool meshLayout=false,skinLayout=false,indexLayout=false;
};
Configuration configuration;
std::string frozen,failure;
template<class T> T Read(uintptr_t address) {
    T value{};SIZE_T size=0;
    if(address<0x10000||address>UINTPTR_MAX-sizeof(T)||
        !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&size)||size!=sizeof(value))
        throw std::runtime_error("OBJECT_UNREADABLE");
    return value;
}
std::vector<uint8_t> Bytes(uintptr_t address,size_t size) {
    if(address<0x10000||size==0||size>32*1024*1024||address>UINTPTR_MAX-size)throw std::runtime_error("BUFFER_BOUNDS_INVALID");
    std::vector<uint8_t> value(size);SIZE_T received=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),value.data(),size,&received)||received!=size)
        throw std::runtime_error("BUFFER_UNREADABLE");
    return value;
}
bool Pattern(uintptr_t address,size_t size,const char* signature) {
    if(!address||size>8192)return false;
    std::vector<uint8_t> bytes(size);SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.data(),size,&copied)&&copied==size&&
        native_detail::FindPattern(bytes,signature).size()==1;
}
uintptr_t Method(GenshinNativeRuntime& runtime,const NativeClass& type,const char* name,uint8_t parameters) {
    NativeMethod result;
    return runtime.FindMethod(type,name,parameters,result)?result.entry:0;
}
template<class T> std::string Hex(const T& bytes) {
    constexpr char digits[]="0123456789abcdef";
    std::string result(bytes.size()*2,'0');size_t index=0;
    for(const auto byte:bytes){result[index++]=digits[byte>>4];result[index++]=digits[byte&15];}
    return result;
}
std::string Text(const wchar_t* text) {
    const auto utf8=Utf8(text?std::wstring(text):std::wstring{});
    std::string result="\"";
    for(const auto c:utf8){if(c=='"'||c=='\\')result+='\\';if(static_cast<unsigned char>(c)<32)result+='?';else result+=c;}
    return result+'"';
}
template<class T,size_t N> T Field(const std::array<uint8_t,N>& bytes,size_t offset) {
    T value{};
    if(offset>N||sizeof(T)>N-offset)throw std::runtime_error("FIELD_OUT_OF_RANGE");
    std::memcpy(&value,bytes.data()+offset,sizeof(T));return value;
}
struct List {
    uintptr_t address=0,array=0;
    std::array<uint8_t,0x20> header{};
    std::vector<uintptr_t> pointers;
};
List ReadList(uintptr_t address,unsigned limit) {
    List result;result.address=address;result.header=Read<decltype(result.header)>(address);
    result.array=Field<uintptr_t>(result.header,0x10);
    const auto count=Field<int>(result.header,0x18);
    if(count<0||unsigned(count)>limit)throw std::runtime_error("LIST_COUNT_INVALID");
    if(!count)return result;
    const auto bounds=Read<uintptr_t>(result.array+0x10),capacity=Read<uint64_t>(result.array+0x18);
    if(bounds||capacity<uint64_t(count)||capacity>4096)throw std::runtime_error("ARRAY_BOUNDS_INVALID");
    result.pointers.reserve(count);
    for(int i=0;i<count;++i)result.pointers.push_back(Read<uintptr_t>(result.array+0x20+size_t(i)*8));
    return result;
}
bool Stable(const List& list) {
    if(Read<decltype(list.header)>(list.address)!=list.header)return false;
    for(size_t i=0;i<list.pointers.size();++i)if(Read<uintptr_t>(list.array+0x20+i*8)!=list.pointers[i])return false;
    return true;
}
const Bone* Find(std::span<const Bone> bones,uintptr_t value,bool native=false) {
    for(const auto& bone:bones)if((native?bone.native:bone.transform)==value)return &bone;
    return nullptr;
}
void Pointer(std::ostream& out,uintptr_t value){out<<"\"0x"<<std::hex<<value<<std::dec<<'"';}
void Name(std::ostream& out,const Bone* bone){if(bone)out<<Text(bone->name);else out<<"null";}
bool Finite(const Vector3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
void Vector(std::ostream& out,const Vector3& v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';}
std::string Collect(std::span<const Bone> bones,std::span<const Mesh> meshes) {
    std::ostringstream out;out<<std::setprecision(9);
    out<<"{\"complete\":true,\"pid\":"<<GetCurrentProcessId()<<",\"capture\":\"IN_PROCESS_READONLY_MAIN_THREAD\",\"animator_native\":";
    Pointer(out,Read<uintptr_t>(configuration.animator+0x10));
    out<<",\"layout_evidence\":\"HASH_GATED_METADATA_AND_NATIVE_CONSUMERS\",\"components\":[";
    bool firstComponent=true;
    for(const auto object:configuration.components) {
        if(Read<uintptr_t>(object)!=configuration.arrayClass)throw std::runtime_error("COMPONENT_IDENTITY_CHANGED");
        const auto raw=Read<std::array<uint8_t,0x268>>(object);
        const auto root=Field<uintptr_t>(raw,0x260),particleAddress=Field<uintptr_t>(raw,0x200),colliderAddress=Field<uintptr_t>(raw,0x130);
        const auto particles=ReadList(particleAddress,256);
        if(particles.pointers.empty())throw std::runtime_error("PARTICLE_LIST_EMPTY");
        if(!firstComponent)out<<',';firstComponent=false;
        out<<"{\"instance\":";Pointer(out,object);out<<",\"root_transform\":";Pointer(out,root);
        out<<",\"root_bone\":";Name(out,Find(bones,root));out<<",\"count\":"<<particles.pointers.size()
            <<",\"opaque_component_bytes_hex\":\""<<Hex(raw)<<"\",\"particles\":[";
        unsigned matched=0;
        for(size_t i=0;i<particles.pointers.size();++i) {
            const auto particle=particles.pointers[i];
            if(Read<uintptr_t>(particle)!=configuration.particleClass)throw std::runtime_error("PARTICLE_CLASS_CHANGED");
            const auto data=Read<std::array<uint8_t,0x1D0>>(particle);
            const auto transform=Field<uintptr_t>(data,0x18);const auto parent=Field<int>(data,0x1C0);
            if(parent<-1||parent>=int(particles.pointers.size())||parent==int(i))throw std::runtime_error("PARTICLE_PARENT_INVALID");
            const auto bone=Find(bones,transform);matched+=bone!=nullptr;
            if(i)out<<',';
            out<<"{\"index\":"<<i<<",\"parent\":"<<parent<<",\"transform\":";Pointer(out,transform);
            out<<",\"bone\":";Name(out,bone);out<<",\"opaque_particle_bytes_hex\":\""<<Hex(data)<<"\"}";
        }
        if(!matched)throw std::runtime_error("PARTICLES_OUTSIDE_STATIC_RIG");
        out<<"],\"matched_bones\":"<<matched<<",\"collider_references\":[";
        List colliders;
        if(colliderAddress)colliders=ReadList(colliderAddress,128);
        bool firstCollider=true;
        for(const auto collider:colliders.pointers) {
            if(!collider)continue;
            const auto klass=Read<uintptr_t>(collider),native=Read<uintptr_t>(collider+0x10);
            const bool capsule=klass&&klass==configuration.capsuleClass,sphere=klass&&klass==configuration.sphereClass;
            if(!firstCollider)out<<',';firstCollider=false;
            out<<"{\"instance\":";Pointer(out,collider);out<<",\"runtime_class\":";Pointer(out,klass);
            out<<",\"native\":";Pointer(out,native);out<<",\"shape\":";
            if(capsule||sphere) {
                std::array<uint8_t,0x38> data{};
                if(capsule)data=Read<decltype(data)>(collider);
                else {const auto sphereData=Read<std::array<uint8_t,0x34>>(collider);std::copy(sphereData.begin(),sphereData.end(),data.begin());}
                const auto center=Field<Vector3>(data,0x1C);const auto radius=Field<float>(data,0x30);
                if(!Finite(center)||!std::isfinite(radius)||radius<0)throw std::runtime_error("COLLIDER_SHAPE_INVALID");
                out<<(capsule?"\"capsule\"":"\"sphere\"")<<",\"local_center\":";Vector(out,center);
                out<<",\"radius\":"<<radius<<",\"bound_raw\":"<<Field<int>(data,0x28);
                if(capsule) {
                    const auto height=Field<float>(data,0x34);const auto direction=Field<int>(data,0x18);
                    if(!std::isfinite(height)||height<0||direction<0||direction>2)throw std::runtime_error("CAPSULE_SHAPE_INVALID");
                    out<<",\"height\":"<<height<<",\"direction\":"<<direction;
                }
                if(native&&configuration.nativeTransform) {
                    const auto transform=Read<uintptr_t>(native+0x40);
                    out<<",\"transform_native\":";Pointer(out,transform);out<<",\"attached_bone\":";Name(out,Find(bones,transform,true));
                }
            } else out<<"null";
            out<<'}';
        }
        if(!Stable(particles)||(colliderAddress&&!Stable(colliders))||Read<uintptr_t>(object)!=configuration.arrayClass||
            Read<uintptr_t>(object+0x200)!=particleAddress||Read<uintptr_t>(object+0x130)!=colliderAddress||Read<uintptr_t>(object+0x260)!=root)
            throw std::runtime_error("CAPTURE_CHANGED_DURING_READ");
        out<<"]}";
    }
    out<<"],\"bone_scales\":[";bool first=true;
    for(const auto& bone:bones) {
        Vector3 scale{};
        if(!ReadBoneScale(bone.transform,bone.native,scale)||!Finite(scale))throw std::runtime_error("BONE_SCALE_READ_FAILED");
        if(!first)out<<',';first=false;
        out<<"{\"index\":"<<bone.index<<",\"name\":"<<Text(bone.name)<<",\"live_local_scale\":";Vector(out,scale);
        out<<",\"static_local_scale\":[1,1,1],\"parent\":"<<bone.parent;
        // 与 STATIC 分开记录绑定时的原生姿态，供辅助骨语义核验；不能回填成静态参考。
        Quaternion rotation{};Vector3 position{};
        const auto poseReady=ReadBoneRotation(bone.transform,bone.native,rotation)&&bone_detail::Valid(rotation)&&
            ReadBonePosition(bone.transform,bone.native,position,false)&&Finite(position);
        out<<",\"local_pose_read_complete\":"<<(poseReady?"true":"false");
        if(poseReady) {
            out<<",\"live_local_rotation\":["<<rotation.x<<','<<rotation.y<<','<<rotation.z<<','<<rotation.w<<"],\"live_local_position\":";
            Vector(out,position);
        }
        if(bone.staticLocalReady) {
            const auto q=bone.staticLocalRotation;
            out<<",\"static_local_rotation\":["<<q.x<<','<<q.y<<','<<q.z<<','<<q.w<<"],\"static_local_position\":";
            Vector(out,bone.staticLocalPosition);
        }
        out<<'}';
    }
    out<<"],\"static_mesh_layout_verified\":"<<(configuration.meshLayout?"true":"false")
        <<",\"static_mesh_sources\":[";first=true;
    size_t meshBytes=0;
    if(configuration.meshLayout)for(const auto& mesh:meshes) {
        if(!first)out<<',';first=false;
        out<<"{\"name\":"<<Text(mesh.name)<<",\"mesh_native\":";Pointer(out,mesh.native);
        try {
            if(Read<uintptr_t>(mesh.object)!=configuration.meshClass||Read<uintptr_t>(mesh.object+0x10)!=mesh.native)
                throw std::runtime_error("MESH_IDENTITY_CHANGED");
            const auto shared=Read<uintptr_t>(mesh.native+0x48);
            // 经 BakeMesh 的共享 MeshData 消费者核验容量；这里只读源资产，不调用 BakeMesh。
            const auto header=Read<std::array<uint8_t,0x1D8>>(shared);
            const auto buffer=Field<uintptr_t>(header,0x68);
            const auto byteCount=Field<uint64_t>(header,0x78);
            const auto vertexCount=Field<uint32_t>(header,0x74);
            out<<",\"shared_data_header_hex\":\""<<Hex(header)<<"\",\"vertex_count\":"<<vertexCount<<",\"vertex_bytes\":"<<byteCount;
            if(!buffer||!byteCount)out<<",\"cpu_vertex_buffer\":\"UNAVAILABLE\"";
            else {
                if(!vertexCount||vertexCount>200000||byteCount>8*1024*1024)throw std::runtime_error("VERTEX_BUFFER_TOO_LARGE");
                // 单次绑定限制总采集量，避免多 Mesh 在游戏主线程产生无界分配。
                if(byteCount>16*1024*1024-meshBytes)throw std::runtime_error("TOTAL_MESH_CAPTURE_LIMIT");
                meshBytes+=byteCount;
                const auto data=Bytes(buffer,byteCount);
                // +0x70 是通道掩码；字节长度由 VertexData 复制消费者在 +0x78 按 64 位读取。
                out<<",\"cpu_vertex_bytes_hex\":\""<<Hex(data)<<'"';
            }
            if(configuration.skinLayout) {
                const auto weights=Field<uintptr_t>(header,0xF0),weightCount=Field<uint64_t>(header,0x100),capacity=Field<uint64_t>(header,0x108);
                const auto binds=Field<uint32_t>(header,0xE0);
                if(weights&&weightCount) {
                    if(weightCount!=vertexCount||capacity<weightCount||capacity>200000||!binds||binds>256)
                        throw std::runtime_error("SKIN_WEIGHT_BOUNDS_INVALID");
                    const auto size=weightCount*32;
                    if(size>16*1024*1024-meshBytes)throw std::runtime_error("TOTAL_MESH_CAPTURE_LIMIT");
                    const auto data=Bytes(weights,size);meshBytes+=size;
                    for(size_t vertex=0;vertex<weightCount;++vertex) {
                        float sum=0;
                        for(unsigned influence=0;influence<4;++influence) {
                            float weight=0;int32_t index=-1;
                            std::memcpy(&weight,data.data()+vertex*32+influence*4,4);
                            std::memcpy(&index,data.data()+vertex*32+16+influence*4,4);
                            if(!std::isfinite(weight)||weight<0||weight>1.0001f||index<0||unsigned(index)>=binds)
                                throw std::runtime_error("SKIN_WEIGHT_VALUE_INVALID");
                            sum+=weight;
                        }
                        if(std::fabs(sum-1)>.001f)throw std::runtime_error("SKIN_WEIGHT_SUM_INVALID");
                    }
                    out<<",\"bone_weight_layout\":\"4xfloat32_4xint32\",\"bone_weight_count\":"<<weightCount
                        <<",\"bone_weight_bytes_hex\":\""<<Hex(data)<<'"';
                }
            }
            // 原始索引缓冲保留；只有额外的 GetTrianglesImpl 消费链通过后才核验子网格语义。
            const auto indices=Field<uintptr_t>(header,0x90),indexBytes=Field<uint64_t>(header,0xA0);
            if(indices&&indexBytes) {
                if(indexBytes>8*1024*1024||indexBytes>16*1024*1024-meshBytes)throw std::runtime_error("INDEX_BUFFER_TOO_LARGE");
                const auto data=Bytes(indices,indexBytes);meshBytes+=indexBytes;
                out<<",\"opaque_index_bytes_hex\":\""<<Hex(data)<<'"';
            }
            if(configuration.indexLayout) {
                const auto submeshes=Field<uintptr_t>(header,0xB0),submeshCount=Field<uint64_t>(header,0xC0),capacity=Field<uint64_t>(header,0xC8);
                const auto format=Field<uint32_t>(header,0x1C8);
                if(!submeshes||!submeshCount||submeshCount>128||capacity<submeshCount||capacity>128)throw std::runtime_error("SUBMESH_BOUNDS_INVALID");
                if(format>1||!indices||!indexBytes)throw std::runtime_error("SUBMESH_INDEX_FORMAT_UNAVAILABLE");
                if(submeshCount*64>16*1024*1024-meshBytes)throw std::runtime_error("TOTAL_MESH_CAPTURE_LIMIT");
                const auto data=Bytes(submeshes,submeshCount*64);
                meshBytes+=data.size();
                const auto width=format?4ull:2ull;
                for(unsigned i=0;i<submeshCount;++i) {
                    uint32_t offset=0,count=0,topology=0,base=0;
                    std::memcpy(&offset,data.data()+i*64+0x28,4);std::memcpy(&count,data.data()+i*64+0x2C,4);
                    std::memcpy(&topology,data.data()+i*64+0x30,4);std::memcpy(&base,data.data()+i*64+0x34,4);
                    if(topology||count%3||offset%width||uint64_t(offset)+uint64_t(count)*width>indexBytes||base>=vertexCount)
                        throw std::runtime_error("SUBMESH_TRIANGLE_RANGE_INVALID");
                }
                out<<",\"index_format\":"<<format<<",\"submesh_count\":"<<submeshCount
                    <<",\"submesh_bytes_hex\":\""<<Hex(data)<<'"';
            }
            if(Read<uintptr_t>(mesh.object)!=configuration.meshClass||Read<uintptr_t>(mesh.object+0x10)!=mesh.native||
                Read<uintptr_t>(mesh.native+0x48)!=shared||Read<std::array<uint8_t,0x1D8>>(shared)!=header)
                throw std::runtime_error("MESH_BUFFER_CHANGED");
            out<<",\"complete\":true";
        }catch(const std::exception& error){out<<",\"complete\":false,\"failure\":\""<<error.what()<<'"';}
        out<<'}';
    }
    out<<"],\"opaque_fields\":\"Particle parameters require further consumer decoding; source mesh bytes are not live poses or a ready simulation profile.\"}";
    return out.str();
}
}
void Initialize(GenshinNativeRuntime& runtime,uintptr_t animator,std::span<const uintptr_t> components,std::ostream& report) {
    configuration={};frozen.clear();failure="NOT_CAPTURED";
    NativeClass array,particle,component;
    if(!animator||components.empty()||components.size()>8||
        !runtime.FindClass("VerletEngine","DynamicBoneArray",array)||array.metadataId!=64511||
        !runtime.FindClass("VerletEngine","VerletParticle",particle)||particle.metadataId!=27425) {
        failure="SELECTED_COMPONENTS_UNAVAILABLE";return;
    }
    const auto apply=Method(runtime,array,"ApplyParticlesToTransforms",0);
    const auto update=Method(runtime,array,"UpdateParticlesConstraints",0);
    uintptr_t constraints=0;
    if(Pattern(update,32,"48 83 EC 38 0F 57 C0 0F 29 44 24 20 44 0F B6 41 70 48 8D 54 24 20 E8 ?? ?? ?? ??"))
        constraints=native_detail::RelativeTarget(update+22,Read<int32_t>(update+23),5);
    const auto ctor=Method(runtime,particle,".ctor",0);
    const bool valid=Pattern(Method(runtime,array,"get_verletParticles",0),8,"48 8B 81 00 02 00 00 C3")&&
        Pattern(Method(runtime,array,"get_rootTransform",0),8,"48 8B 81 60 02 00 00 C3")&&
        Pattern(Method(runtime,particle,"get_m_BoneTransform",0),5,"48 8B 41 18 C3")&&
        Pattern(Method(runtime,particle,"isRoot",0),10,"8B 81 C0 01 00 00 C1 E8 1F C3")&&
        Pattern(ctor,144,"89 81 CC 01 00 00")&&
        Pattern(apply,1536,"48 8B 6C E8 20")&&Pattern(apply,1536,"8B BD C0 01 00 00")&&
        Pattern(constraints,3072,"48 8B 97 30 01 00 00");
    if(!valid){failure="LAYOUT_CONSUMERS_REJECTED";report<<"[NATIVE_PHYSICS_CAPTURE] ready=0 reason="<<failure<<'\n';return;}
    auto collider=[&](const char* name,uint32_t id,bool capsule) {
        NativeClass type;if(!runtime.FindClass("",name,type)||type.metadataId!=id)return uintptr_t(0);
        const auto method=Method(runtime,type,"CollideParticle",2);
        const bool validShape=capsule?
            Pattern(method,256,"F3 44 0F 10 59 30")&&Pattern(method,256,"F3 0F 10 77 34")&&Pattern(method,256,"8B 47 18"):
            Pattern(method,256,"F3 44 0F 10 61 30")&&Pattern(method,256,"8B 5F 28");
        return validShape?type.address:uintptr_t(0);
    };
    configuration.animator=animator;configuration.arrayClass=array.address;configuration.particleClass=particle.address;
    configuration.capsuleClass=collider("DynamicBoneCapsuleCollider",19673,true);
    configuration.sphereClass=collider("DynamicBoneSphereCollider",31304,false);
    NativeMethod transform;
    configuration.nativeTransform=runtime.FindClass("UnityEngine","Component",component)&&
        runtime.FindMethod(component,"get_transform",0,transform)&&Pattern(transform.target,64,"48 8B 50 40");
    configuration.components.assign(components.begin(),components.end());configuration.ready=true;
    NativeClass mesh,renderer;
    if(runtime.FindClass("UnityEngine","Mesh",mesh)&&runtime.FindClass("UnityEngine","SkinnedMeshRenderer",renderer)) {
        NativeMethod bake;
        if(runtime.FindMethod(renderer,"BakeMesh",1,bake)&&
            Pattern(bake.target,96,"48 8B D7 48 8B CB 48 8B 5C 24 50 48 83 C4 20 5F 5E 5D E9 ?? ?? ?? ??")) {
            // 目标必须是已核验的 wrapper / native 消费链，不能把任意 RVA 当作布局依据。
            if(Pattern(bake.target+0x4C,5,"E9 ?? ?? ?? ??")) {
                const auto consumer=native_detail::RelativeTarget(bake.target+0x4C,Read<int32_t>(bake.target+0x4D),5);
                if(Pattern(consumer+0x164,5,"E8 ?? ?? ?? ??")) {
                    const auto clone=native_detail::RelativeTarget(consumer+0x164,Read<int32_t>(consumer+0x165),5);
                    configuration.meshLayout=Pattern(clone,768,"48 8B 41 48")&&Pattern(clone,768,"B9 D8 01 00 00")&&
                        Pattern(clone,768,"4D 39 B7 D0 01 00 00")&&Pattern(clone,768,"45 8B 47 70");
                    if(configuration.meshLayout&&Pattern(clone+0xAE,5,"E8 ?? ?? ?? ??")) {
                        const auto vertices=native_detail::RelativeTarget(clone+0xAE,Read<int32_t>(clone+0xAF),5);
                        configuration.meshLayout=Pattern(vertices,1024,"4C 8B 43 68 48 89 7B 58")&&
                            Pattern(vertices,1024,"48 8B 46 58 4C 3B 46 68");
                    }else configuration.meshLayout=false;
                }
                if(configuration.meshLayout&&Pattern(consumer+0x150,5,"E8 ?? ?? ?? ??")) {
                    const auto setup=native_detail::RelativeTarget(consumer+0x150,Read<int32_t>(consumer+0x151),5);
                    if(Pattern(setup+0x2EF,5,"E8 ?? ?? ?? ??")) {
                        const auto weights=native_detail::RelativeTarget(setup+0x2EF,Read<int32_t>(setup+0x2F0),5);
                        if(Pattern(weights,16,"48 8B 49 48 48 81 C1 D0 00 00 00 E9 ?? ?? ?? ??")) {
                            const auto container=native_detail::RelativeTarget(weights+0xB,Read<int32_t>(weights+0xC),5);
                            configuration.skinLayout=Pattern(container,768,"48 8B 41 30")&&Pattern(container,768,"48 8B 71 20")&&
                                Pattern(container,768,"83 FA 04 75 13 48 8B C6")&&
                                Pattern(container,768,"48 8D 56 10 0F 1F 00 8B 02 48 8D 52 20");
                        }
                    }
                }
            }
        }
        configuration.meshClass=mesh.address;
        NativeMethod triangles;
        if(runtime.FindMethod(mesh,"GetTrianglesImpl",2,triangles)&&Pattern(triangles.target+0x39,5,"E8 ?? ?? ?? ??")) {
            const auto native=native_detail::RelativeTarget(triangles.target+0x39,Read<int32_t>(triangles.target+0x3A),5);
            if(Pattern(native+0x14,7,"48 8D 0D ?? ?? ?? ??")) {
                const auto bridge=native_detail::RelativeTarget(native+0x14,Read<int32_t>(native+0x17),7);
                if(Pattern(bridge,8,"49 8B D1 E9 ?? ?? ?? ??")) {
                    const auto all=native_detail::RelativeTarget(bridge+3,Read<int32_t>(bridge+4),5);
                    if(Pattern(all+0x54,5,"E8 ?? ?? ?? ??")) {
                        const auto decode=native_detail::RelativeTarget(all+0x54,Read<int32_t>(all+0x55),5);
                        configuration.indexLayout=Pattern(all,256,"48 8B 51 48")&&Pattern(all,256,"4C 8D 82 B0 00 00 00")&&
                            Pattern(decode,1024,"41 8B 89 C8 01 00 00")&&Pattern(decode,1024,"48 C1 E7 06")&&
                            Pattern(decode,1024,"44 8B 47 34");
                    }
                }
            }
        }
    }
    report<<"[NATIVE_PHYSICS_CAPTURE] ready=1 components="<<components.size()
        <<" mesh_layout_verified="<<configuration.meshLayout<<" skin_layout_verified="<<configuration.skinLayout
        <<" index_layout_verified="<<configuration.indexLayout<<" transform_read_getters=1 physics_invocation=NONE bakemesh_invocation=NONE writes=NONE\n";
}
void Capture(uintptr_t animator,std::span<const Bone> bones,std::span<const Mesh> meshes) {
    frozen.clear();
    if(!configuration.ready||animator!=configuration.animator||bones.empty()){if(configuration.ready)failure="STATIC_ACTOR_MISMATCH";return;}
    try {frozen=Collect(bones,meshes);failure.clear();}catch(const std::exception& error){failure=error.what();}
}
void Export(const std::filesystem::path& directory,std::ostream& report) {
    std::ofstream out(directory/L"native_physics_config.json",std::ios::trunc);
    if(frozen.empty())out<<"{\"complete\":false,\"failure\":\""<<failure<<"\"}";else out<<frozen;
    out.flush();report<<"[NATIVE_PHYSICS_CAPTURE] complete="<<(!frozen.empty()&&out.good())<<" reason="<<failure<<" file=native_physics_config.json\n";
}
}
