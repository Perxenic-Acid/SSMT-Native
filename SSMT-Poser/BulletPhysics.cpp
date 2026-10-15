#include "SecondaryPhysics.h"
#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>
#include <BulletCollision/CollisionDispatch/btCollisionObjectWrapper.h>
#include <algorithm>

namespace poser::physics {
namespace {
constexpr float Dt=1.0f/Hz;
btVector3 Bt(Vector3 v){return {v.x,v.y,v.z};}
btQuaternion Bt(Quaternion q){return {q.x,q.y,q.z,q.w};}
Quaternion Quat(const btQuaternion& q){return vmd::Normalize({q.x(),q.y(),q.z(),q.w()});}
btTransform Transform(const Pose& p){return btTransform(Bt(p.rotation),Bt(p.position));}
Pose Mix(const Pose& a,const Pose& b,float t){return {ik::Add(a.position,ik::Scale(ik::Sub(b.position,a.position),t)),vmd::Slerp(a.rotation,b.rotation,t)};}
bool Valid(std::span<const Pose> poses,const Profile& p){
    if(poses.size()!=p.boneCount)return false;
    for(unsigned i=0;i<p.boneCount;++i)if(p.bones[i].valid&&(!ik::Finite(poses[i].position)||!ValidRotation(poses[i].rotation)))return false;
    return true;
}
class World final:public btDiscreteDynamicsWorld {
public:
    using btDiscreteDynamicsWorld::btDiscreteDynamicsWorld;
    void performDiscreteCollisionDetection() override {
        btDiscreteDynamicsWorld::performDiscreteCollisionDetection();
        for(int i=0;i<getDispatcher()->getNumManifolds();++i) {
            auto* manifold=getDispatcher()->getManifoldByIndexInternal(i);
            for(int c=0;c<manifold->getNumContacts();++c) {
                auto& contact=manifold->getContactPoint(c);const auto depth=-contact.getDistance();
                // 动画可把固定衣物骨根带入身体：限制去穿透速度，不能把不可行冲突变成爆炸冲量。
                contact.m_contactPointFlags|=BT_CONTACT_FLAG_HAS_CONTACT_ERP;
                contact.m_contactERP=depth>0?std::min(.2f,.5f*(Dt/Substeps)/depth):.2f;
            }
        }
    }
};
class PairDispatcher final:public btCollisionDispatcher {
public:
    PairDispatcher(btCollisionConfiguration* config,const Profile& profile):btCollisionDispatcher(config),jointCount(profile.jointCount),colliderCount(profile.colliderCount) {
        for(unsigned j=0;j<profile.jointCount;++j)for(unsigned c=0;c<profile.colliderCount;++c) {
            masks[j]=profile.joints[j].colliders;
            const auto& joint=profile.joints[j];const auto start=joint.collisionStart[c];
            if(start<=0)continue;
            shifts[j*MaxColliders+c]=ik::Length(joint.tailLocal)*start*.5f;
            auto& shape=shapes[j*MaxColliders+c];shape=std::make_unique<btCapsuleShape>(joint.radius,std::max(.001f,ik::Length(joint.tailLocal)*(1-start)));shape->setMargin(.0005f);
        }
        setNearCallback(Near);
    }
private:
    unsigned jointCount,colliderCount;
    std::array<uint32_t,MaxJoints> masks{};
    std::array<float,MaxJoints*MaxColliders> shifts{};
    std::array<std::unique_ptr<btCapsuleShape>,MaxJoints*MaxColliders> shapes;
    static void Near(btBroadphasePair& pair,btCollisionDispatcher& base,const btDispatcherInfo& info) {
        auto& self=static_cast<PairDispatcher&>(base);
        auto* first=static_cast<btCollisionObject*>(pair.m_pProxy0->m_clientObject);
        auto* last=static_cast<btCollisionObject*>(pair.m_pProxy1->m_clientObject);
        const auto swapped=first->getUserIndex2()==1&&last->getUserIndex2()==2;
        auto* cloth=swapped?last:first;auto* body=swapped?first:last;
        if(cloth->getUserIndex2()!=2||body->getUserIndex2()!=1||info.m_dispatchFunc!=btDispatcherInfo::DISPATCH_DISCRETE){defaultNearCallback(pair,base,info);return;}
        const auto j=unsigned(cloth->getUserIndex()),c=unsigned(body->getUserIndex());
        if(j>=self.jointCount||c>=self.colliderCount||!base.needsCollision(first,last))return;
        if(!(self.masks[j]&(1u<<c)))return;
        const auto* shape=self.shapes[j*MaxColliders+c].get();
        if(!shape){defaultNearCallback(pair,base,info);return;}
        auto transform=cloth->getWorldTransform();
        // 只替换本碰撞对的窄相 wrapper；刚体质心、惯量及其他碰撞对仍使用完整骨段。
        transform.setOrigin(transform*btVector3(0,self.shifts[j*MaxColliders+c],0));
        btCollisionObjectWrapper a(nullptr,swapped?first->getCollisionShape():shape,first,swapped?first->getWorldTransform():transform,-1,-1);
        btCollisionObjectWrapper b(nullptr,swapped?shape:last->getCollisionShape(),last,swapped?transform:last->getWorldTransform(),-1,-1);
        if(!pair.m_algorithm)pair.m_algorithm=base.findAlgorithm(&a,&b,nullptr,BT_CONTACT_POINT_ALGORITHMS);
        if(pair.m_algorithm){btManifoldResult result(&a,&b);pair.m_algorithm->processCollision(&a,&b,info,&result);}
    }
};
}
class Simulation {
public:
    explicit Simulation(const Profile& profile):key(profile.key) {
        btDefaultCollisionConstructionInfo configuration;
        configuration.m_defaultMaxPersistentManifoldPoolSize=2048;configuration.m_defaultMaxCollisionAlgorithmPoolSize=2048;
        config=std::make_unique<btDefaultCollisionConfiguration>(configuration);
        dispatcher=std::make_unique<PairDispatcher>(config.get(),profile);
        broadphase=std::make_unique<btDbvtBroadphase>();solver=std::make_unique<btSequentialImpulseConstraintSolver>();
        world=std::make_unique<World>(dispatcher.get(),broadphase.get(),solver.get(),config.get());
        world->setGravity({0,-9.81f,0});auto& settings=world->getSolverInfo();
        // 分支根允许有限动态位移，链内保持骨长；接触和关节使用同一冲量求解。
        settings.m_numIterations=40;settings.m_splitImpulse=false;settings.m_erp=.3f;settings.m_erp2=.2f;
        settings.m_solverMode&=~SOLVER_RANDMIZE_ORDER;settings.m_solverMode|=SOLVER_USE_2_FRICTION_DIRECTIONS;
        // 接近米制的小型角色：默认 2cm 接触阈值过大，边距在各形状上单独设置。
        world->getDispatchInfo().m_allowedCcdPenetration=.0001f;
        for(unsigned c=0;c<profile.colliderCount;++c) {
            const auto& collider=profile.colliders[c];const auto length=ik::Length(ik::Sub(profile.bones[collider.b].rest.position,profile.bones[collider.a].rest.position));
            std::unique_ptr<btCollisionShape> shape;
            if(length<.000001f)shape=std::make_unique<btSphereShape>(collider.radius);
            else shape=std::make_unique<btCapsuleShape>(collider.radius,length);
            colliders[c]=Add(std::move(shape),0,BodyColliderPose(profile,c),1,2);
            colliders[c]->setUserIndex(int(c));colliders[c]->setUserIndex2(1);
        }
        for(unsigned j=0;j<profile.jointCount;++j) {
            const auto& joint=profile.joints[j];const auto length=ik::Length(joint.tailLocal);
            offsets[j]={ik::Add(ik::Scale(joint.tailLocal,.5f),joint.collisionOffset),ik::FromTo({0,1,0},joint.tailLocal)};
            const auto bodyRest=BodyPose(profile.bones[joint.bone].rest,j);
            dynamic[j]=Add(std::make_unique<btCapsuleShape>(joint.radius,std::max(.001f,length)),joint.mass,bodyRest,2,3);
            dynamic[j]->setDamping(.15f,joint.damping);dynamic[j]->setFriction(.35f);dynamic[j]->setRestitution(0);
            dynamic[j]->setCcdMotionThreshold(joint.radius*.5f);dynamic[j]->setCcdSweptSphereRadius(joint.radius*.8f);
            dynamic[j]->setGravity({0,-joint.gravity,0});dynamic[j]->setUserIndex(int(j));
            dynamic[j]->setUserIndex2(2);
            parent[j]=-1;
            for(auto ancestor=profile.bones[joint.bone].parent;ancestor>=0;ancestor=profile.bones[ancestor].parent)
                if(profile.bones[ancestor].joint>=0){parent[j]=profile.bones[ancestor].joint;break;}
            if(parent[j]<0)anchors[j]=Add(std::make_unique<btEmptyShape>(),0,profile.bones[joint.bone].rest,4,0);
        }
        for(unsigned j=0;j<profile.jointCount;++j) {
            root[j]=j;while(parent[root[j]]>=0)root[j]=unsigned(parent[root[j]]);
            const auto& joint=profile.joints[j];const auto& rest=profile.bones[joint.bone].rest;
            const auto frame=btTransform(Bt(vmd::Multiply(rest.rotation,offsets[j].rotation)),Bt(rest.position));
            auto* first=parent[j]>=0?dynamic[parent[j]]:anchors[j];
            auto constraint=std::make_unique<btGeneric6DofSpring2Constraint>(*first,*dynamic[j],
                first->getWorldTransform().inverse()*frame,dynamic[j]->getWorldTransform().inverse()*frame);
            const auto slide=joint.translationLimit;
            constraint->setLinearLowerLimit({-slide,-slide,-slide});constraint->setLinearUpperLimit({slide,slide,slide});
            if(slide>0) {
                float mass=0;
                for(unsigned child=0;child<profile.jointCount;++child) {
                    int ancestor=int(child);while(ancestor>=0&&ancestor!=int(j))ancestor=parent[ancestor];
                    if(ancestor>=0)mass+=profile.joints[child].mass;
                }
                // 接合位置的回形力与临界阻尼；接触所需位移继续写回，FK 不能将其丢掉。
                constexpr float frequency=50;
                linearDamping[j]=2*mass*frequency;
                for(int axis=0;axis<3;++axis) {
                    constraint->enableSpring(axis,true);constraint->setStiffness(axis,mass*frequency*frequency,true);constraint->setDamping(axis,0);
                    constraint->setEquilibriumPoint(axis,0);constraint->enableMotor(axis,true);constraint->setTargetVelocity(axis,0);
                }
            }
            const auto bend=joint.maxAngle*.65f,twist=joint.maxAngle*.1f;
            constraint->setAngularLowerLimit({-bend,-twist,-bend});constraint->setAngularUpperLimit({bend,twist,bend});
            // 静态子树按各旋转轴计算惯量；轴向扭转不能套用弯曲轴的长杆惯量。
            for(int axis=0;axis<3;++axis) {
                const auto direction=ik::Rotate(vmd::Multiply(rest.rotation,offsets[j].rotation),axis==0?Vector3{1,0,0}:axis==1?Vector3{0,1,0}:Vector3{0,0,1});
                float inertia=0,gravityStiffness=0;
                for(unsigned child=0;child<profile.jointCount;++child) {
                    int ancestor=int(child);while(ancestor>=0&&ancestor!=int(j))ancestor=parent[ancestor];
                    if(ancestor<0)continue;
                    const auto& descendant=profile.joints[child];const auto& childRest=profile.bones[descendant.bone].rest;
                    const auto length=ik::Length(descendant.tailLocal);
                    const auto delta=ik::Sub(BodyPose(childRest,child).position,rest.position);
                    const auto along=ik::Dot(delta,direction);
                    const auto alignment=ik::Dot(ik::Rotate(childRest.rotation,ik::Unit(descendant.tailLocal)),direction);
                    const auto radius2=descendant.radius*descendant.radius;
                    inertia+=descendant.mass*(std::max(0.0f,ik::Dot(delta,delta)-along*along)+
                        (length*length/12+radius2*.25f)*(1-alignment*alignment)+radius2*.5f*alignment*alignment);
                    gravityStiffness+=descendant.mass*descendant.gravity*ik::Length(delta)*(1-direction.y*direction.y);
                }
                inertia=std::max(.00000001f,inertia);
                const auto stiffness=inertia*joint.frequency*joint.frequency;
                const auto ratio=joint.kind==Kind::Clothing?1.0f:.8f;
                const auto damping=2*ratio*std::sqrt((stiffness+gravityStiffness)*inertia);
                constraint->enableSpring(axis+3,true);constraint->setStiffness(axis+3,stiffness,true);constraint->setDamping(axis+3,0);
                constraint->setEquilibriumPoint(axis+3,0);
                // 阻尼由零目标速度的限扭矩 motor 隐式求解，避免显式阻尼在短段 / 长父链之间反转速度。
                constraint->enableMotor(axis+3,true);constraint->setTargetVelocity(axis+3,0);angularDamping[j][axis]=damping;
            }
            world->addConstraint(constraint.get(),true);constraints[j]=std::move(constraint);
        }
        // 邻接骨段共用接头，不对共用端点作自碰撞；其它动态分支仍参与接触。
        for(unsigned j=0;j<profile.jointCount;++j)for(unsigned other=0;other<j;++other) {
            if(parent[j]==int(other)||parent[other]==int(j)||profile.bones[profile.joints[j].bone].parent==profile.bones[profile.joints[other].bone].parent) {
                dynamic[j]->setIgnoreCollisionCheck(dynamic[other],true);dynamic[other]->setIgnoreCollisionCheck(dynamic[j],true);
            }
            selfPairs[j][other]=selfPairs[other][j]=dynamic[j]->checkCollideWith(dynamic[other]);
        }
    }
    ~Simulation(){
        for(auto& constraint:constraints)if(constraint)world->removeConstraint(constraint.get());
        for(unsigned i=0;i<count;++i)world->removeRigidBody(bodies[i].get());
    }
    uint64_t key;
    bool Initialize(const Profile& p,std::span<const Pose> drivers,State& state) {
        for(unsigned c=0;c<p.colliderCount;++c)Set(colliders[c],BodyColliderPose(p,c,drivers),true);
        for(unsigned j=0;j<p.jointCount;++j) {
            Set(dynamic[j],BodyPose(drivers[p.joints[j].bone],j),true);
            if(anchors[j])Set(anchors[j],AnchorPose(p,j,drivers.data()),true);
        }
        solver->reset();std::copy(drivers.begin(),drivers.end(),previous.begin());
        Reflect(p,drivers,state);
        ProjectContacts(p,drivers,state);ProjectSelfContacts(p,drivers,state);ProjectRoots(p,drivers,state);
        for(unsigned j=0;j<p.jointCount;++j)Set(dynamic[j],BodyPose(state.poses[p.joints[j].bone],j),false);
        ClearMovedContacts();
        state.initialized=true;return true;
    }
    bool Step(const Profile& p,std::span<const Pose> drivers,State& state) {
        // 120 Hz 采样内使用固定子步，动画碰撞体按同一时间段插值，避免一次跳到终点。
        std::array<Pose,MaxBones> intermediate{};
        std::array<Vector3,MaxJoints> previousTips{};
        const auto previousLocal=state.local;const auto previousPosition=state.positionDelta;
        for(unsigned j=0;j<p.jointCount;++j)previousTips[j]=state.particles[j].current;
        for(unsigned step=1;step<=Substeps;++step) {
#ifdef SSMT_POSER_TRACE_PHYSICS
            auto traceLocal=state.local;auto tracePosition=state.positionDelta;
            const auto trace=[&](unsigned phase) {
                if(!state.trace)return;
                auto& peak=state.trace->phases[phase];
                for(unsigned j=0;j<p.jointCount;++j) {
                    const auto angle=AngularDistance(traceLocal[j],state.local[j]);
                    const auto position=ik::Length(ik::Sub(tracePosition[j],state.positionDelta[j]));
                    if(angle>peak.angle){peak.angle=angle;peak.angleJoint=j;peak.angleStep=state.steps+1;}
                    if(position>peak.position){peak.position=position;peak.positionJoint=j;peak.positionStep=state.steps+1;}
                }
                traceLocal=state.local;tracePosition=state.positionDelta;
            };
#endif
            for(unsigned i=0;i<p.boneCount;++i)if(p.bones[i].valid)intermediate[i]=Mix(previous[i],drivers[i],float(step)/Substeps);
            for(unsigned c=0;c<p.colliderCount;++c)Set(colliders[c],BodyColliderPose(p,c,std::span(intermediate.data(),p.boneCount)),false);
            for(unsigned j=0;j<p.jointCount;++j)if(anchors[j])Set(anchors[j],AnchorPose(p,j,intermediate.data()),false);
            for(unsigned j=0;j<p.jointCount;++j) {
                auto& constraint=*constraints[j];constraint.calculateTransforms();
                const auto relative=constraint.getRigidBodyA().getAngularVelocity()-constraint.getRigidBodyB().getAngularVelocity();
                for(int axis=0;axis<3;++axis)constraint.setMaxMotorForce(axis+3,angularDamping[j][axis]*btFabs(relative.dot(constraint.getAxis(axis))));
                if(linearDamping[j]>0) {
                    const auto& a=constraint.getRigidBodyA();const auto& b=constraint.getRigidBodyB();
                    const auto velocity=a.getVelocityInLocalPoint(constraint.getCalculatedTransformA().getOrigin()-a.getCenterOfMassPosition())-
                        b.getVelocityInLocalPoint(constraint.getCalculatedTransformB().getOrigin()-b.getCenterOfMassPosition());
                    for(int axis=0;axis<3;++axis)constraint.setMaxMotorForce(axis,linearDamping[j]*btFabs(velocity.dot(constraint.getCalculatedTransformA().getBasis().getColumn(axis))));
                }
            }
            world->stepSimulation(Dt/Substeps,0,Dt/Substeps);
            for(int m=0;m<dispatcher->getNumManifolds();++m) {
                const auto* manifold=dispatcher->getManifoldByIndexInternal(m);bool contact=false;
                for(int n=0;n<manifold->getNumContacts();++n)contact|=manifold->getContactPoint(n).getDistance()<0;
                state.contacts+=contact;
            }
            for(unsigned j=0;j<p.jointCount;++j) {
                const auto& body=*dynamic[j];const auto position=body.getWorldTransform().getOrigin();const auto rotation=body.getWorldTransform().getRotation();
                const auto linear=body.getLinearVelocity(),angular=body.getAngularVelocity();
                const auto finite=[](const btVector3& value){return std::isfinite(value.x())&&std::isfinite(value.y())&&std::isfinite(value.z());};
                // 必须在四元数归一化 / 固定骨长合回前校验，不能让它们掩盖引擎内部的非有限状态。
                if(!finite(position)||!finite(linear)||!finite(angular)||!ValidRotation({rotation.x(),rotation.y(),rotation.z(),rotation.w()}))return false;
            }
            Reflect(p,std::span(intermediate.data(),p.boneCount),state);
#ifdef SSMT_POSER_TRACE_PHYSICS
            trace(0);
#endif
            ProjectContacts(p,std::span(intermediate.data(),p.boneCount),state);
#ifdef SSMT_POSER_TRACE_PHYSICS
            trace(1);
#endif
            ProjectSelfContacts(p,std::span(intermediate.data(),p.boneCount),state);
#ifdef SSMT_POSER_TRACE_PHYSICS
            trace(2);
#endif
            ProjectRoots(p,std::span(intermediate.data(),p.boneCount),state);
#ifdef SSMT_POSER_TRACE_PHYSICS
            trace(3);
#endif
            for(unsigned j=0;j<p.jointCount;++j) {
                auto transform=dynamic[j]->getWorldTransform();const auto expected=Bt(BodyPose(state.poses[p.joints[j].bone],j).position);
                state.maxMergeOffset=std::max(state.maxMergeOffset,(transform.getOrigin()-expected).length());
                contactMoved[j]|=(transform.getOrigin()-expected).length2()>1e-12f;
                // 动态根保留位置；链内位置按静态 FK 合回，避免求解器容差积成骨链拉伸。
                transform.setOrigin(expected);dynamic[j]->setCenterOfMassTransform(transform);world->updateSingleAabb(dynamic[j]);
            }
            ClearMovedContacts();
        }
        std::copy(drivers.begin(),drivers.end(),previous.begin());
        for(unsigned j=0;j<p.jointCount;++j)state.particles[j].previous=previousTips[j];
        for(unsigned j=0;j<p.jointCount;++j) {
            state.maxAngularStep=std::max(state.maxAngularStep,AngularDistance(previousLocal[j],state.local[j]));
            state.maxRootStep=std::max(state.maxRootStep,ik::Length(ik::Sub(previousPosition[j],state.positionDelta[j])));
        }
        for(unsigned j=0;j<p.jointCount;++j)if(!ValidRotation(state.local[j])||!ik::Finite(state.particles[j].current))return false;
        // 在写回后的胶囊位置重新检测，统计不能只描述未合回骨骼的引擎内部几何。
        world->performDiscreteCollisionDetection();
        const auto manifolds=dispatcher->getNumManifolds();
        for(int m=0;m<manifolds;++m) {
            const auto* manifold=dispatcher->getManifoldByIndexInternal(m);float deepest=0;
            for(int n=0;n<manifold->getNumContacts();++n) {
                const auto distance=manifold->getContactPoint(n).getDistance();deepest=std::max(deepest,-distance);
            }
            state.unresolved+=deepest>.002f;state.maxPenetration=std::max(state.maxPenetration,deepest);
            const auto self=manifold->getBody0()->getUserIndex2()==2&&manifold->getBody1()->getUserIndex2()==2;
            if(self) {
                state.selfUnresolved+=deepest>.002f;
                if(deepest>state.selfMaxPenetration) {
                    state.selfMaxPenetration=deepest;state.selfPairA=unsigned(manifold->getBody0()->getUserIndex());state.selfPairB=unsigned(manifold->getBody1()->getUserIndex());
                }
            }else{state.bodyUnresolved+=deepest>.002f;state.bodyMaxPenetration=std::max(state.bodyMaxPenetration,deepest);}
        }
        ++state.steps;return true;
    }
private:
    static constexpr unsigned MaxBodies=MaxJoints*2+MaxColliders;
    unsigned count=0;
    std::unique_ptr<btDefaultCollisionConfiguration> config;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btDbvtBroadphase> broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::unique_ptr<btDiscreteDynamicsWorld> world;
    std::array<std::unique_ptr<btCollisionShape>,MaxBodies> shapes;
    std::array<std::unique_ptr<btRigidBody>,MaxBodies> bodies;
    std::array<std::unique_ptr<btGeneric6DofSpring2Constraint>,MaxJoints> constraints;
    std::array<btRigidBody*,MaxJoints> dynamic{},anchors{};
    std::array<btRigidBody*,MaxColliders> colliders{};
    std::array<int,MaxJoints> parent{};
    std::array<unsigned,MaxJoints> root{};
    std::array<std::array<bool,MaxJoints>,MaxJoints> selfPairs{};
    std::array<bool,MaxJoints> contactMoved{};
    std::array<Pose,MaxJoints> offsets{};
    std::array<std::array<float,3>,MaxJoints> angularDamping{};
    std::array<float,MaxJoints> linearDamping{};
    std::array<Pose,MaxBones> previous{};
    static float AngularDistance(Quaternion a,Quaternion b) {
        const auto delta=vmd::Multiply(vmd::Inverse(a),b);
        return 2*std::atan2(ik::Length({delta.x,delta.y,delta.z}),std::fabs(delta.w));
    }
    btRigidBody* Add(std::unique_ptr<btCollisionShape> shape,float mass,const Pose& pose,int group,int mask) {
        shape->setMargin(.0005f);btVector3 inertia{0,0,0};if(mass>0)shape->calculateLocalInertia(mass,inertia);
        btRigidBody::btRigidBodyConstructionInfo info(mass,nullptr,shape.get(),inertia);
        auto body=std::make_unique<btRigidBody>(info);body->setCenterOfMassTransform(Transform(pose));
        body->setInterpolationWorldTransform(body->getWorldTransform());body->setActivationState(DISABLE_DEACTIVATION);
        body->setContactProcessingThreshold(.002f);
        if(!mass)body->setCollisionFlags((body->getCollisionFlags()&~btCollisionObject::CF_STATIC_OBJECT)|btCollisionObject::CF_KINEMATIC_OBJECT);
        auto* result=body.get();world->addRigidBody(result,group,mask);shapes[count]=std::move(shape);bodies[count++]=std::move(body);return result;
    }
    Pose BodyPose(const Pose& bone,unsigned j)const {
        return {ik::Add(bone.position,ik::Rotate(bone.rotation,offsets[j].position)),vmd::Multiply(bone.rotation,offsets[j].rotation)};
    }
    static Pose AnchorPose(const Profile& p,unsigned j,const Pose* drivers) {
        const auto bone=p.joints[j].bone;const auto& node=p.bones[bone];
        // 固定端只继承动画父骨与静态局部旋转；不能把物理骨当前偏转当成关节参考。
        return {drivers[bone].position,node.parent>=0?vmd::Multiply(drivers[node.parent].rotation,node.localRotation):node.localRotation};
    }
    void Set(btRigidBody* body,const Pose& pose,bool reset) {
        const auto transform=Transform(pose);
        if(reset) {
            world->getPairCache()->cleanProxyFromPairs(body->getBroadphaseHandle(),dispatcher.get());
            body->setLinearVelocity({0,0,0});body->setAngularVelocity({0,0,0});body->clearForces();body->setInterpolationWorldTransform(transform);
        }
        if(body->isKinematicObject())body->setWorldTransform(transform);else body->setCenterOfMassTransform(transform);
        world->updateSingleAabb(body);
    }
    Quaternion Limit(unsigned j,Quaternion boneRotation) {
        auto& constraint=*constraints[j];constraint.calculateTransforms();
        const auto a=constraint.getCalculatedTransformA().getRotation();
        const auto b=vmd::Multiply(vmd::Multiply(boneRotation,offsets[j].rotation),Quat(constraint.getFrameOffsetB().getRotation()));
        const auto relative=vmd::Multiply(vmd::Inverse(Quat(a)),b);
        btVector3 angles,current;
        btGeneric6DofSpring2Constraint::matrixToEulerXYZ(btMatrix3x3(Bt(relative)),angles);
        btGeneric6DofSpring2Constraint::matrixToEulerXYZ(constraint.getCalculatedTransformA().getBasis().transpose()*constraint.getCalculatedTransformB().getBasis(),current);
        btVector3 lower,upper;constraint.getAngularLowerLimit(lower);constraint.getAngularUpperLimit(upper);
        for(int axis=0;axis<3;++axis) {
            angles[axis]=current[axis]+btNormalizeAngle(angles[axis]-current[axis]);
            // 冲量积分可能尚有角限位误差。接触修正只禁止继续加深误差，不能顺带
            // 将其它旋转轴瞬间吸回限位；这会把 0.2 rad 的接触候选变成约 1 rad 跳变。
            angles[axis]=btClamped(angles[axis],btMin(lower[axis],current[axis]),btMax(upper[axis],current[axis]));
        }
        // Bullet 的 XYZ 分解对应 Rz(-z) Ry(-y) Rx(-x) 的转置约定。
        const auto x=ik::AxisRotation({1,0,0},-angles.x()),y=ik::AxisRotation({0,1,0},-angles.y()),z=ik::AxisRotation({0,0,1},-angles.z());
        const auto limited=vmd::Multiply(z,vmd::Multiply(y,x));
        return vmd::Multiply(vmd::Multiply(Quat(a),limited),vmd::Inverse(vmd::Multiply(offsets[j].rotation,Quat(constraint.getFrameOffsetB().getRotation()))));
    }
    Vector3 LimitPosition(const Profile& p,unsigned j,Vector3 position) {
        auto& constraint=*constraints[j];constraint.calculateTransforms();const auto& frame=constraint.getCalculatedTransformA();
        auto relative=frame.inverse()*Bt(position);const auto limit=p.joints[j].translationLimit;
        const auto length=relative.length();if(length>limit)relative*=limit/length;
        const auto out=frame*relative;return {out.x(),out.y(),out.z()};
    }
    void MoveBranch(unsigned branch,Vector3 delta,Vector3 normal) {
        for(unsigned child=0;child<dynamic.size();++child) {
            if(!dynamic[child]||root[child]!=branch)continue;
            contactMoved[child]=true;
            auto transform=dynamic[child]->getWorldTransform();transform.getOrigin()+=Bt(delta);dynamic[child]->setCenterOfMassTransform(transform);world->updateSingleAabb(dynamic[child]);
            const auto velocity=dynamic[child]->getLinearVelocity(),axis=Bt(normal);const auto inward=velocity.dot(axis);
            if(inward<0)dynamic[child]->setLinearVelocity(velocity-axis*inward);
        }
    }
    void ClearMovedContacts() {
        for(int m=0;m<dispatcher->getNumManifolds();++m) {
            auto* manifold=dispatcher->getManifoldByIndexInternal(m);
            const auto moved=[&](const btCollisionObject* body) {
                return body->getUserIndex2()==2&&unsigned(body->getUserIndex())<MaxJoints&&contactMoved[unsigned(body->getUserIndex())];
            };
            // FK / 接触投影已改变形状位置；旧法线和局部接触点不能继续热启动下一子步。
            // 只清受影响的 manifold，保留窄相 algorithm 和未移动物体的接触缓存。
            if(moved(manifold->getBody0())||moved(manifold->getBody1()))manifold->clearManifold();
        }
        contactMoved.fill(false);
    }
    bool Descendant(unsigned child,unsigned ancestor)const {
        auto current=int(child);while(current>=0&&current!=int(ancestor))current=parent[unsigned(current)];return current>=0;
    }
    bool ImprovesRotation(const Profile& p,std::span<const Pose> drivers,const State& state,unsigned pivot,Quaternion rotation)const {
        const auto& pose=state.poses[p.joints[pivot].bone];
        const auto delta=vmd::Multiply(rotation,vmd::Inverse(pose.rotation));
        std::array<bool,MaxJoints> moving{};
        std::array<std::array<Vector3,2>,MaxJoints> before{},after{};
        for(unsigned j=0;j<p.jointCount;++j) {
            before[j]=after[j]=CollisionSegment(p.joints[j],state.poses[p.joints[j].bone]);
            moving[j]=Descendant(j,pivot);
            if(moving[j])for(auto& point:after[j])point=ik::Add(pose.position,ik::Rotate(delta,ik::Sub(point,pose.position)));
        }
        double change=0;
        const auto energy=[](float distance,float radius) {
            const auto depth=std::max(0.0f,radius-.0005f-distance);return double(depth)*depth;
        };
        for(unsigned j=0;j<p.jointCount;++j)if(moving[j]) {
            const auto& joint=p.joints[j];
            for(unsigned c=0;c<p.colliderCount;++c)if(joint.colliders&(1u<<c)) {
                const auto& collider=p.colliders[c];const auto a=drivers[collider.a].position,b=drivers[collider.b].position;
                change+=energy(SegmentClearance(after[j][0],after[j][1],a,b,joint.collisionStart[c]),joint.collisionRadius[c])-
                    energy(SegmentClearance(before[j][0],before[j][1],a,b,joint.collisionStart[c]),joint.collisionRadius[c]);
            }
            for(unsigned other=0;other<p.jointCount;++other)if(!moving[other]&&selfPairs[j][other]) {
                const auto radius=joint.radius+p.joints[other].radius;
                change+=energy(SegmentClearance(after[j][0],after[j][1],before[other][0],before[other][1]),radius)-
                    energy(SegmentClearance(before[j][0],before[j][1],before[other][0],before[other][1]),radius);
            }
        }
        // 接触旋转会移动整个子树；不能只修好当前碰撞对，再由下一阶段把新增穿入转回去。
        return change<-1e-11;
    }
    void RotateBranch(const Profile& p,std::span<const Pose> drivers,State& state,unsigned j,const Pose& pose) {
        const auto before=dynamic[j]->getWorldTransform().getRotation();const auto transform=Transform(BodyPose(pose,j));
        const auto branchDelta=vmd::Multiply(pose.rotation,vmd::Inverse(state.poses[p.joints[j].bone].rotation));
#ifdef SSMT_POSER_TRACE_PHYSICS
        if(state.trace) {
            auto& peak=state.trace->correction;const auto angle=AngularDistance(pose.rotation,state.poses[p.joints[j].bone].rotation);
            if(angle>peak.angle){peak.angle=angle;peak.angleJoint=j;peak.angleStep=state.steps+1;}
        }
#endif
        // 接触修正连同整条子树绕接头旋转，否则 FK 合回会重新推进子骨。
        for(unsigned child=0;child<p.jointCount;++child)if(child!=j&&Descendant(child,j)) {
            contactMoved[child]=true;
            auto childPose=state.poses[p.joints[child].bone];
            childPose.position=ik::Add(pose.position,ik::Rotate(branchDelta,ik::Sub(childPose.position,pose.position)));
            childPose.rotation=vmd::Multiply(branchDelta,childPose.rotation);
            dynamic[child]->setCenterOfMassTransform(Transform(BodyPose(childPose,child)));world->updateSingleAabb(dynamic[child]);
        }
        contactMoved[j]=true;dynamic[j]->setCenterOfMassTransform(transform);world->updateSingleAabb(dynamic[j]);
        auto delta=transform.getRotation()*before.inverse();if(delta.w()<0)delta=-delta;
        const auto axis=delta.getAxis(),velocity=dynamic[j]->getAngularVelocity();
        // 去掉继续朝接触内部转动的速度分量，避免下一子步重新穿入并弹回。
        const auto inward=velocity.dot(axis);if(inward<0)dynamic[j]->setAngularVelocity(velocity-axis*inward);
        Reflect(p,drivers,state);
    }
    void ProjectContacts(const Profile& p,std::span<const Pose> drivers,State& state) {
        for(unsigned pass=0;pass<6;++pass) {
            bool changed=false;
            for(unsigned ordinal=0;ordinal<p.boneCount;++ordinal) {
                const auto bone=p.order[ordinal];const auto index=p.bones[bone].joint;if(index<0)continue;
                const auto j=unsigned(index);const auto& joint=p.joints[j];auto pose=state.poses[bone];
                for(unsigned c=0;c<p.colliderCount;++c)if(joint.colliders&(1u<<c)) {
                    const auto& collider=p.colliders[c];
                    const auto a=drivers[collider.a].position,b=drivers[collider.b].position;
                    const auto segment=CollisionSegment(joint,pose);
                    const auto contact=ClosestSegments(segment[0],segment[1],a,b,joint.collisionStart[c]);
                    const auto depth=joint.collisionRadius[c]-contact.distance;if(depth<=.0005f)continue;
                    auto normal=ik::Unit(ik::Sub(contact.point,contact.center));
                    if(contact.distance<.00001f) {
                        const auto& rest=p.bones[bone].rest;
                        const auto refSegment=CollisionSegment(joint,rest);
                        const auto ref=ClosestSegments(refSegment[0],refSegment[1],p.bones[collider.a].rest.position,p.bones[collider.b].rest.position,joint.collisionStart[c]);
                        normal=ik::Rotate(vmd::Multiply(drivers[collider.a].rotation,vmd::Inverse(p.bones[collider.a].rest.rotation)),ik::Unit(ik::Sub(ref.point,ref.center)));
                    }
                    const auto gradient=ik::Cross(ik::Sub(contact.point,pose.position),normal);const auto leverage=ik::Length(gradient);
                    auto angle=leverage>.00001f?std::min(.25f,depth/leverage):0;Quaternion candidate{};bool improved=false;
                    for(unsigned attempt=0;attempt<5&&angle>0;++attempt,angle*=.5f) {
                        candidate=Limit(j,vmd::Multiply(ik::AxisRotation(ik::Unit(gradient),angle),pose.rotation));
                        const auto projected=CollisionSegment(joint,{pose.position,candidate});
                        if(SegmentClearance(projected[0],projected[1],a,b,joint.collisionStart[c])>contact.distance+.000001f&&
                            ImprovesRotation(p,drivers,state,j,candidate)){improved=true;break;}
                    }
                    if(improved){pose.rotation=candidate;RotateBranch(p,drivers,state,j,pose);pose=state.poses[bone];changed=true;}
                    // 子骨接头进入身体时，先用上游关节改变链形，减少整条衣物根的位移。
                    const auto updatedSegment=CollisionSegment(joint,pose);
                    auto remainingContact=ClosestSegments(updatedSegment[0],updatedSegment[1],a,b,joint.collisionStart[c]);
                    if(joint.collisionRadius[c]-remainingContact.distance>.001f&&parent[j]>=0) {
                        auto bestDistance=remainingContact.distance;int bestJoint=-1;Pose bestPose{};
                        const auto outward=remainingContact.distance>.00001f?ik::Unit(ik::Sub(remainingContact.point,remainingContact.center)):normal;
                        for(auto ancestor=parent[j];ancestor>=0;ancestor=parent[unsigned(ancestor)]) {
                            const auto selected=unsigned(ancestor);auto pivot=state.poses[p.joints[selected].bone];
                            const auto parentGradient=ik::Cross(ik::Sub(remainingContact.point,pivot.position),outward);const auto parentLeverage=ik::Length(parentGradient);
                            auto parentAngle=parentLeverage>.00001f?std::min(.25f,(joint.collisionRadius[c]+.0005f-remainingContact.distance)/parentLeverage):0;
                            for(unsigned attempt=0;attempt<5&&parentAngle>0;++attempt,parentAngle*=.5f) {
                                const auto rotation=Limit(selected,vmd::Multiply(ik::AxisRotation(ik::Unit(parentGradient),parentAngle),pivot.rotation));
                                const auto delta=vmd::Multiply(rotation,vmd::Inverse(pivot.rotation));
                                const Pose projected{ik::Add(pivot.position,ik::Rotate(delta,ik::Sub(pose.position,pivot.position))),vmd::Multiply(delta,pose.rotation)};
                                const auto candidateSegment=CollisionSegment(joint,projected);const auto distance=SegmentClearance(candidateSegment[0],candidateSegment[1],a,b,joint.collisionStart[c]);
                                if(distance<=bestDistance+.000001f)continue;
                                if(!ImprovesRotation(p,drivers,state,selected,rotation))continue;
                                pivot.rotation=rotation;bestDistance=distance;bestJoint=int(selected);bestPose=pivot;break;
                            }
                            if(bestDistance>=joint.collisionRadius[c]+.0002f)break;
                        }
                        if(bestJoint>=0){RotateBranch(p,drivers,state,unsigned(bestJoint),bestPose);pose=state.poses[bone];changed=true;}
                    }
                    // 固定骨根进入动画身体时，旋转没有足够杠杆。保留分支根的有限动态
                    // 位移，将整条分支移开，随后通过同一局部 TRS 写回 / 烘焙。
                    const auto branch=root[j];
                    if(p.joints[branch].translationLimit<=0)continue;
                    const auto projected=CollisionSegment(joint,pose);const auto after=ClosestSegments(projected[0],projected[1],a,b,joint.collisionStart[c]);
                    const auto remaining=joint.collisionRadius[c]-after.distance;if(remaining<=.0005f)continue;
                    auto outward=after.distance>.00001f?ik::Unit(ik::Sub(after.point,after.center)):normal;
                    const auto rootBone=p.joints[branch].bone;const auto origin=state.poses[rootBone].position;
                    const auto target=LimitPosition(p,branch,ik::Add(origin,ik::Scale(outward,remaining+.0005f)));
                    const auto delta=ik::Sub(target,origin);
                    if(SegmentClearance(ik::Add(projected[0],delta),ik::Add(projected[1],delta),a,b,joint.collisionStart[c])<=after.distance+.000001f)continue;
                    MoveBranch(branch,delta,outward);Reflect(p,drivers,state);pose.position=state.poses[bone].position;changed=true;
                }
                if(!bone_detail::Same(pose.rotation,state.poses[bone].rotation)) {
                    RotateBranch(p,drivers,state,j,pose);
                }
            }
            if(!changed)break;
            Reflect(p,drivers,state);
        }
    }
    void ProjectSelfContacts(const Profile& p,std::span<const Pose> drivers,State& state) {
        for(unsigned pass=0;pass<5;++pass) {
            bool changed=false;
            for(unsigned j=0;j<p.jointCount;++j)for(unsigned other=0;other<j;++other)if(selfPairs[j][other]) {
                const auto first=CollisionSegment(p.joints[j],state.poses[p.joints[j].bone]);
                const auto last=CollisionSegment(p.joints[other],state.poses[p.joints[other].bone]);
                const auto contact=ClosestSegments(first[0],first[1],last[0],last[1]);
                const auto depth=p.joints[j].radius+p.joints[other].radius-contact.distance;if(depth<=.0005f)continue;
                auto separation=ik::Sub(contact.point,contact.center);
                if(contact.distance<.00001f) {
                    const auto firstRest=CollisionSegment(p.joints[j],p.bones[p.joints[j].bone].rest);
                    const auto lastRest=CollisionSegment(p.joints[other],p.bones[p.joints[other].bone].rest);
                    const auto restContact=ClosestSegments(firstRest[0],firstRest[1],lastRest[0],lastRest[1]);
                    separation=ik::Sub(restContact.point,restContact.center);
                    if(ik::Length(separation)<.00001f) {
                        const auto direction=ik::Unit(ik::Sub(firstRest[1],firstRest[0]));
                        separation=ik::Cross(direction,ik::Sub(lastRest[1],lastRest[0]));
                        if(ik::Length(separation)<.00001f)separation=ik::Cross(direction,std::fabs(direction.x)<.9f?Vector3{1,0,0}:Vector3{0,1,0});
                    }
                    const auto opposing=p.joints[other].bone;
                    separation=ik::Rotate(vmd::Multiply(state.poses[opposing].rotation,vmd::Inverse(p.bones[opposing].rest.rotation)),ik::Unit(separation));
                }
                // 旋转子树不能连带旋转另一接触对象；祖孙接触只修正较深的骨。
                for(const auto contacted:{j,other}) {
                    const auto opposing=contacted==j?other:j;
                    const auto& joint=p.joints[contacted];const auto bonePose=state.poses[joint.bone];
                    const auto normal=ik::Unit(contacted==j?separation:ik::Scale(separation,-1));
                    const auto pivot=contacted==j?contact.point:contact.center;
                    const auto opposite=contacted==j?last:first;
                    auto bestDistance=contact.distance;int bestJoint=-1;Pose bestPose{};
                    // 接触骨的固定接头本身进入祖先代理时，须由更上游的关节移动它。
                    // 只沿可改变相对位置的父链回溯；共同祖先不能同时旋转接触双方。
                    for(auto ancestor=int(contacted);ancestor>=0;ancestor=parent[unsigned(ancestor)]) {
                        const auto selected=unsigned(ancestor);if(Descendant(opposing,selected))break;
                        const auto pose=state.poses[p.joints[selected].bone];
                        const auto evaluate=[&](Quaternion candidate) {
                            const auto delta=vmd::Multiply(candidate,vmd::Inverse(pose.rotation));
                            const Pose projected{ik::Add(pose.position,ik::Rotate(delta,ik::Sub(bonePose.position,pose.position))),vmd::Multiply(delta,bonePose.rotation)};
                            const auto segment=CollisionSegment(joint,projected);
                            const auto distance=SegmentClearance(segment[0],segment[1],opposite[0],opposite[1]);
                            if(distance<=bestDistance+.000001f)return false;
                            if(!ImprovesRotation(p,drivers,state,selected,candidate))return false;
                            bestDistance=distance;bestJoint=int(selected);bestPose={pose.position,candidate};return true;
                        };
                        const auto gradient=ik::Cross(ik::Sub(pivot,pose.position),normal);const auto leverage=ik::Length(gradient);
                        auto angle=leverage>.00001f?std::min(.25f,(depth+.0005f)/leverage):0;
                        for(unsigned attempt=0;attempt<5&&angle>0;++attempt,angle*=.5f) {
                            const auto candidate=Limit(selected,vmd::Multiply(ik::AxisRotation(ik::Unit(gradient),angle),pose.rotation));
                            if(evaluate(candidate))break;
                        }
                        // 最近法线要求的轴若已到限位，仍可沿另一个轴离开接触平面。
                        // 有限角候选都须通过相同角限位和真实胶囊间距验证。
                        if(bestDistance<p.joints[j].radius+p.joints[other].radius+.0002f) {
                            constraints[selected]->calculateTransforms();const auto basis=constraints[selected]->getCalculatedTransformA().getBasis();
                            for(unsigned direction=0;direction<6;++direction) {
                                const auto axis=basis.getColumn(direction/2);const Vector3 worldAxis{axis.x(),axis.y(),axis.z()};
                                const auto candidate=Limit(selected,vmd::Multiply(ik::AxisRotation(worldAxis,(direction&1)?-.2f:.2f),pose.rotation));
                                evaluate(candidate);
                                if(bestDistance>=p.joints[j].radius+p.joints[other].radius+.0002f)break;
                            }
                        }
                        if(bestDistance>=p.joints[j].radius+p.joints[other].radius+.0002f)break;
                    }
                    if(bestJoint>=0){RotateBranch(p,drivers,state,unsigned(bestJoint),bestPose);changed=true;break;}
                }
            }
            if(!changed)break;
        }
    }
    void ProjectRoots(const Profile& p,std::span<const Pose> drivers,State& state) {
        std::array<std::array<Vector3,2>,MaxJoints> segments{};
        for(unsigned j=0;j<p.jointCount;++j)segments[j]=CollisionSegment(p.joints[j],state.poses[p.joints[j].bone]);
        for(unsigned iteration=0;iteration<3;++iteration) {
            bool corrected=false;
            for(unsigned branch=0;branch<p.jointCount;++branch) {
                if(root[branch]!=branch||p.joints[branch].translationLimit<=0)continue;
                const auto origin=state.poses[p.joints[branch].bone].position;
                const auto score=[&](Vector3 delta) {
                    float deepest=0;
                    for(unsigned j=0;j<p.jointCount;++j)if(root[j]==branch) {
                        for(unsigned c=0;c<p.colliderCount;++c)if(p.joints[j].colliders&(1u<<c)) {
                            const auto& collider=p.colliders[c];
                            deepest=std::max(deepest,p.joints[j].collisionRadius[c]-SegmentClearance(ik::Add(segments[j][0],delta),
                                ik::Add(segments[j][1],delta),drivers[collider.a].position,drivers[collider.b].position,p.joints[j].collisionStart[c]));
                        }
                        for(unsigned other=0;other<p.jointCount;++other)if(root[other]!=branch&&selfPairs[j][other])
                            deepest=std::max(deepest,p.joints[j].radius+p.joints[other].radius-SegmentClearance(ik::Add(segments[j][0],delta),
                                ik::Add(segments[j][1],delta),segments[other][0],segments[other][1]));
                    }
                    return deepest;
                };
                auto bestDepth=score({});if(bestDepth<=.001f)continue;
                Vector3 best{};float bestLength=0;
                constraints[branch]->calculateTransforms();const auto& frame=constraints[branch]->getCalculatedTransformA();const auto basis=frame.getBasis();
                const auto anchor=frame.getOrigin();const Vector3 reference{anchor.x(),anchor.y(),anchor.z()};
                // 手臂 / 躯干等同时夹住分支时，逐碰撞对的最短推出会相互抵消。
                // 对整条分支拟合一个共同位移，保留完整代理、角限位和静态链长。
                for(unsigned seed=0;seed<8;++seed) {
                    const auto axis=seed>1?basis.getColumn((seed-2)/2)*((seed&1)?-1.0f:1.0f):btVector3(0,0,0);
                    const auto span=p.joints[branch].translationLimit*.9f;
                    // 搜索整个静态 FK 接合球，不能只在已经顶到边界的旧位移附近搜索。
                    const auto seeded=seed?ik::Add(reference,{axis.x()*span,axis.y()*span,axis.z()*span}):origin;
                    auto delta=ik::Sub(LimitPosition(p,branch,seeded),origin);
                    for(unsigned pass=0;pass<24;++pass) {
                        bool changed=false;
                        for(unsigned j=0;j<p.jointCount;++j)if(root[j]==branch) {
                            for(unsigned c=0;c<p.colliderCount;++c)if(p.joints[j].colliders&(1u<<c)) {
                                const auto& collider=p.colliders[c];
                                const auto a=drivers[collider.a].position,b=drivers[collider.b].position;
                                const auto contact=ClosestSegments(ik::Add(segments[j][0],delta),ik::Add(segments[j][1],delta),
                                    a,b,p.joints[j].collisionStart[c]);
                                const auto depth=p.joints[j].collisionRadius[c]+.0005f-contact.distance;if(depth<=.00001f)continue;
                                auto normal=ik::Unit(ik::Sub(contact.point,contact.center));
                                if(contact.distance<.00001f)normal={axis.x(),axis.y(),axis.z()};
                                const auto next=LimitPosition(p,branch,ik::Add(origin,ik::Add(delta,ik::Scale(normal,depth))));
                                delta=ik::Sub(next,origin);changed=true;
                            }
                            for(unsigned other=0;other<p.jointCount;++other)if(root[other]!=branch&&selfPairs[j][other]) {
                                const auto contact=ClosestSegments(ik::Add(segments[j][0],delta),ik::Add(segments[j][1],delta),segments[other][0],segments[other][1]);
                                const auto depth=p.joints[j].radius+p.joints[other].radius+.0005f-contact.distance;if(depth<=.00001f)continue;
                                auto normal=ik::Unit(ik::Sub(contact.point,contact.center));
                                if(contact.distance<.00001f)normal={axis.x(),axis.y(),axis.z()};
                                const auto next=LimitPosition(p,branch,ik::Add(origin,ik::Add(delta,ik::Scale(normal,depth))));
                                delta=ik::Sub(next,origin);changed=true;
                            }
                        }
                        if(!changed)break;
                    }
                    if(score(delta)<=.001f) {
                        float lower=0,upper=1;
                        for(unsigned attempt=0;attempt<10;++attempt) {
                            const auto middle=(lower+upper)*.5f;
                            if(score(ik::Scale(delta,middle))<=.001f)upper=middle;else lower=middle;
                        }
                        delta=ik::Scale(delta,upper);
                    }
                    const auto depth=score(delta),length=ik::Length(delta);
                    const bool clear=depth<=.001f,bestClear=bestDepth<=.001f;
                    if((clear&&(!bestClear||length<bestLength))||(!clear&&!bestClear&&depth<bestDepth-.00001f)) {
                        best=delta;bestDepth=depth;bestLength=length;
                    }
                }
                if(bestLength>.000001f) {
                    MoveBranch(branch,best,ik::Unit(best));Reflect(p,drivers,state);corrected=true;
                    for(unsigned j=0;j<p.jointCount;++j)if(root[j]==branch)segments[j]=CollisionSegment(p.joints[j],state.poses[p.joints[j].bone]);
                }
            }
            if(!corrected)break;
        }
    }
    void Reflect(const Profile& p,std::span<const Pose> drivers,State& state) {
        for(unsigned ordinal=0;ordinal<p.boneCount;++ordinal) {
            const auto i=p.order[ordinal];const auto& node=p.bones[i];if(!node.valid)continue;
            const auto parentRotation=node.parent>=0?state.poses[node.parent].rotation:Quaternion{0,0,0,1};
            const auto rotation=node.parent>=0?vmd::Multiply(vmd::Inverse(drivers[node.parent].rotation),drivers[i].rotation):drivers[i].rotation;
            const auto localPosition=node.parent>=0?ik::Rotate(vmd::Inverse(drivers[node.parent].rotation),ik::Sub(drivers[i].position,drivers[node.parent].position)):drivers[i].position;
            auto& pose=state.poses[i];pose.position=node.parent>=0?ik::Add(state.poses[node.parent].position,ik::Rotate(parentRotation,localPosition)):localPosition;
            pose.rotation=vmd::Multiply(parentRotation,rotation);
            if(node.joint<0)continue;
            const auto j=unsigned(node.joint);pose.rotation=vmd::Multiply(Quat(dynamic[j]->getWorldTransform().getRotation()),vmd::Inverse(offsets[j].rotation));
            if(p.joints[j].translationLimit>0) {
                const auto origin=dynamic[j]->getWorldTransform().getOrigin();
                const auto bonePosition=ik::Sub({origin.x(),origin.y(),origin.z()},ik::Rotate(pose.rotation,offsets[j].position));
                pose.position=LimitPosition(p,j,bonePosition);
            }
            state.local[j]=vmd::Multiply(vmd::Inverse(parentRotation),pose.rotation);
            const auto local=node.parent>=0?ik::Rotate(vmd::Inverse(parentRotation),ik::Sub(pose.position,state.poses[node.parent].position)):pose.position;
            state.positionDelta[j]=ik::Sub(local,node.localPosition);
            auto& particle=state.particles[j];particle.previous=particle.current;particle.anchor=pose.position;
            particle.current=ik::Add(pose.position,ik::Rotate(pose.rotation,p.joints[j].tailLocal));
        }
    }
};
State::State()=default;State::~State()=default;State::State(State&&) noexcept=default;State& State::operator=(State&&) noexcept=default;
bool Prepare(const Profile& p,std::span<const Pose> drivers,State& state) {
    if(!Valid(drivers,p)||!p.jointCount)return false;
    try {
        if(!state.simulation||state.simulation->key!=p.key)state.simulation=std::make_unique<Simulation>(p);
        return state.simulation->Initialize(p,drivers,state);
    }catch(...){state.simulation.reset();state.initialized=false;return false;}
}
void Reset(State& state){state.initialized=false;state.tick=state.steps=state.contacts=state.resets=state.unresolved=state.bodyUnresolved=state.selfUnresolved=0;state.maxPenetration=state.maxMergeOffset=state.bodyMaxPenetration=state.selfMaxPenetration=state.maxAngularStep=state.maxRootStep=0;}
bool Initialize(const Profile& p,std::span<const Pose> drivers,State& state){return Prepare(p,drivers,state);}
bool Step(const Profile& p,std::span<const Pose> drivers,State& state){
    return state.initialized&&state.simulation&&state.simulation->key==p.key&&Valid(drivers,p)&&state.simulation->Step(p,drivers,state);
}
}
