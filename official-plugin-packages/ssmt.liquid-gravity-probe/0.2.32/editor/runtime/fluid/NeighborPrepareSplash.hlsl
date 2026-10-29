#include "ParticleConfig.hlsl"
#include "Puddle.hlsl"
#include "NeighborRow.hlsl"

struct FluidParticle{float4 positionAge;float4 velocityAlive;float4 predictedLambda;float4 densityBirth;float4 birthMeta;};
Texture1D<float4> IniParams:register(t120);
StructuredBuffer<FluidParticle> splashes:register(t101);
StructuredBuffer<float4> surface:register(t103);
RWStructuredBuffer<NeighborRow> neighbors:register(u0);
static const float RADIUS=FLUID_RADIUS;

[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint i=id.x;
 if(i>=SPLASH_COUNT)return;
 NeighborRow row;
 row.first=uint4(0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu);
 row.second=row.first;
 FluidParticle p=splashes[i];
 if(p.velocityAlive.w<.5){neighbors[i]=row;return;}

 bool puddle=floorPuddle(p.densityBirth.w);
 uint pSlot=min((uint)max(p.birthMeta.x,0),3u);
 float effectStrength=clamp(surface[pSlot*12u+9u].w,.2,2);
 float effectivePressure=IniParams[237].z*effectStrength;
 float visualScale=lerp(.75,1,smoothstep(.28,1.69,effectivePressure));
 float radius=RADIUS*visualScale*max(p.densityBirth.z,.1);
 if(puddle)radius=RADIUS*max(p.densityBirth.z,.1)*puddleWidth(p.densityBirth.x);
 uint begin=puddle?AIRBORNE_SPLASH_COUNT:0;
 uint count=puddle?SPLASH_COUNT:AIRBORNE_SPLASH_COUNT;
 float distances[8];float candidates[8];
 [unroll]for(uint k=0;k<8;k++){distances[k]=1e20;candidates[k]=0xffffffffu;}
 [loop]for(uint j=begin;j<count;j++){
  if(j==i)continue;
  FluidParticle q=splashes[j];
  if(q.velocityAlive.w<.5)continue;
  bool otherPuddle=floorPuddle(q.densityBirth.w);
  if(puddle!=otherPuddle)continue;
  if(puddle && (dot(p.predictedLambda.xyz,q.predictedLambda.xyz)<.985
     || abs(dot(q.positionAge.xyz-p.positionAge.xyz,p.predictedLambda.xyz))>radius*.2))continue;
  uint qSlot=min((uint)max(q.birthMeta.x,0),3u);
  float qStrength=clamp(surface[qSlot*12u+9u].w,.2,2);
  float qVisualScale=lerp(.75,1,smoothstep(.28,1.69,IniParams[237].z*qStrength));
  float otherRadius=RADIUS*qVisualScale*max(q.densityBirth.z,.1);
  if(otherPuddle)otherRadius=RADIUS*max(q.densityBirth.z,.1)*puddleWidth(q.densityBirth.x);
  if(abs(otherRadius-radius)>radius*.05)continue;
  float3 delta=(q.positionAge.xyz-p.positionAge.xyz)/radius;
  float distance=dot(delta,delta);
  if(distance>=25 || !all(isfinite(delta)))continue;
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
  if(puddle)continue;
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
