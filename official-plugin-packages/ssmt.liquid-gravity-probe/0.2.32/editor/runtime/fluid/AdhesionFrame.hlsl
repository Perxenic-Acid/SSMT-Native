// Per-droplet frame history: material transport is separate from relative slip.
#include "SurfaceFrame.hlsl"

struct AdhesionFrame{float4 right;float4 up;float4 forward;float4 origin;};
RWStructuredBuffer<AdhesionFrame> adhesionFrames:register(u5);
bool liveAdhesionFrame(uint nozzleSlot){
 return liquidSurfaceLive(nozzleSlot);
}
float3 carryAdhesionVector(float3 v,AdhesionFrame previous,uint nozzleSlot){
 return liquidSurfaceRow(nozzleSlot,0).xyz*dot(v,previous.right.xyz)
       +liquidSurfaceRow(nozzleSlot,1).xyz*dot(v,previous.up.xyz)
       +liquidSurfaceRow(nozzleSlot,2).xyz*dot(v,previous.forward.xyz);
}
// Nearby upright surfaces can follow the live body frame. Horizontal floors
// remain world-fixed. Screen depth still validates every transported contact.
float adhesionContactMode(float3 contact,float3 normal,uint nozzleSlot){
 float3 offset=contact+collisionRoot()-liquidSurfaceRow(nozzleSlot,3).xyz;
 return liveAdhesionFrame(nozzleSlot) && dot(offset,offset)<.45*.45 && abs(normal.y)<.85 ? 2:1;
}
void saveAdhesionFrame(uint i,uint nozzleSlot){
 AdhesionFrame frame;
 frame.right=float4(liquidSurfaceRow(nozzleSlot,0).xyz,nozzleSlot);
 frame.up=liquidSurfaceRow(nozzleSlot,1);
 frame.forward=liquidSurfaceRow(nozzleSlot,2);
 frame.origin=float4(liquidSurfaceRow(nozzleSlot,3).xyz,liveAdhesionFrame(nozzleSlot)?1:0);
 adhesionFrames[i]=frame;
}
