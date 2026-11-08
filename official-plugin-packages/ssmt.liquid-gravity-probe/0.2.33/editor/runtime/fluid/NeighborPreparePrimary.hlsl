#include "ParticleConfig.hlsl"
#include "JetGuide.hlsl"
#include "NeighborRow.hlsl"

struct FluidParticle{float4 positionAge;float4 velocityAlive;float4 predictedLambda;float4 densityBirth;float4 birthMeta;};
Texture1D<float4> IniParams:register(t120);
StructuredBuffer<FluidParticle> particles:register(t100);
StructuredBuffer<float4> surface:register(t103);
StructuredBuffer<JetGuide> renderGuides:register(t112);
RWStructuredBuffer<NeighborRow> neighbors:register(u0);
static const float RADIUS=FLUID_RADIUS;

float birthRadiusScale(float value){return value>=48?.70:(value>=32?.85:1);}

[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint i=id.x;
 if(i>=PRIMARY_COUNT)return;
 NeighborRow row;
 row.first=uint4(0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu);
 row.second=row.first;
 FluidParticle p=particles[i];
 if(p.velocityAlive.w<.5){neighbors[i]=row;return;}

 uint pSlot=min((uint)max(p.birthMeta.x,0),3u);
 float effectStrength=clamp(surface[pSlot*12u+9u].w,.2,2);
 float effectivePressure=IniParams[237].z*effectStrength;
 float visualScale=lerp(.75,1,smoothstep(.28,1.69,effectivePressure));
 float radius=RADIUS*(p.densityBirth.w>=16?birthRadiusScale(p.densityBirth.w):visualScale);
 float breakup=saturate(-renderGuides[i].carrierAmount.w);
 float branchTag=renderGuides[i].longLane.w;
 bool branch=breakup>.95 && branchTag>=2;
 if(branchTag< -2){neighbors[i]=row;return;}

 float distances[8];float candidates[8];
 [unroll]for(uint k=0;k<8;k++){distances[k]=1e20;candidates[k]=0xffffffffu;}
 [loop]for(uint j=0;j<PRIMARY_COUNT;j++){
  if(j==i)continue;
  FluidParticle q=particles[j];
  if(q.velocityAlive.w<.5)continue;
  if(renderGuides[j].longLane.w< -2)continue;
  uint qSlot=min((uint)max(q.birthMeta.x,0),3u);
  float qStrength=clamp(surface[qSlot*12u+9u].w,.2,2);
  float qVisualScale=lerp(.75,1,smoothstep(.28,1.69,IniParams[237].z*qStrength));
  float otherRadius=RADIUS*(q.densityBirth.w>=16?birthRadiusScale(q.densityBirth.w):qVisualScale);
  if(abs(otherRadius-radius)>radius*.05)continue;
  float3 delta=(q.positionAge.xyz-p.positionAge.xyz)/radius;
  float distance=dot(delta,delta);
  if(distance>=(branch?16:lerp(25,4.0,breakup)) || !all(isfinite(delta)))continue;
  if(distance<=4.41){
   uint candidate=j;
   [unroll]for(uint closeSlot=2;closeSlot<8;closeSlot++){
    if(distance<distances[closeSlot]){
     float oldDistance=distances[closeSlot];float oldCandidate=candidates[closeSlot];
     distances[closeSlot]=distance;candidates[closeSlot]=candidate;
     distance=oldDistance;candidate=oldCandidate;
    }
   }
   continue;
  }
  if(breakup>=.999 && !branch)continue;
  if(branch){
   JetGuide ownGuide=renderGuides[i],otherGuide=renderGuides[j];
   if(abs(otherGuide.longLane.w-branchTag)>.1 || otherGuide.carrierAmount.w>-.95
      || floor(ownGuide.originBirth.w/4.8)!=floor(otherGuide.originBirth.w/4.8))continue;
  }
  float speedP=length(p.velocityAlive.xyz),speedQ=length(q.velocityAlive.xyz);
  if(min(speedP,speedQ)<.02)continue;
  float3 directionP=p.velocityAlive.xyz/speedP,directionQ=q.velocityAlive.xyz/speedQ;
  if(dot(directionP,directionQ)<.90)continue;
  float3 tangent=normalize(directionP+directionQ);
  float along=dot(delta,tangent);
  float separation=sqrt(distance);
  float lateral=length(delta-tangent*along);
  if(abs(along)<.85*separation || lateral>.8+.1*separation)continue;
  uint slot=along>=0?0:1;
  if(distance<distances[slot]){distances[slot]=distance;candidates[slot]=j;}
 }
 row.first=uint4(candidates[0],candidates[1],candidates[2],candidates[3]);
 row.second=uint4(candidates[4],candidates[5],candidates[6],candidates[7]);
 neighbors[i]=row;
}
