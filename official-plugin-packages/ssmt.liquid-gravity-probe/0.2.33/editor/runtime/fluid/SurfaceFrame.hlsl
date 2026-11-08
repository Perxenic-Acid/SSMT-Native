#ifndef LIQUID_SURFACE_FRAME
#define LIQUID_SURFACE_FRAME

static const uint LIQUID_MAX_NOZZLES = 4;
static const uint LIQUID_SURFACE_ROWS = 12;

uint liquidNozzleCount(){
 return clamp((uint)IniParams[250].x,1u,LIQUID_MAX_NOZZLES);
}

uint liquidSurfaceBase(uint slot){
 return min(slot,LIQUID_MAX_NOZZLES-1u)*LIQUID_SURFACE_ROWS;
}

bool liquidSurfaceLive(uint slot){
 uint base=liquidSurfaceBase(slot);
 return IniParams[241].x>.5
     && surface[base+4].w>.5
     && all(isfinite(surface[base+3]));
}

float liquidNozzleIntervalSeconds(uint slot){
 float4 intervals=IniParams[252];
 if(slot==0)return intervals.x;
 if(slot==1)return intervals.y;
 if(slot==2)return intervals.z;
 return intervals.w;
}

bool liquidSurfaceEmitting(uint slot){
 if(!liquidSurfaceLive(slot))return false;
 float interval=liquidNozzleIntervalSeconds(slot);
 if(interval<.5)return true;
 float burst=clamp(interval*.28,.18,.65);
 return fmod(max(IniParams[239].y,0),interval)<burst;
}

float4 liquidSurfaceRow(uint slot,uint row){
 return surface[liquidSurfaceBase(slot)+row];
}

uint liquidActiveNozzleCount(){
 uint count=liquidNozzleCount();
 uint active=0;
 [loop]for(uint slot=0;slot<count;slot++){
  if(liquidSurfaceLive(slot))active++;
 }
 return active;
}

uint liquidLiveNozzle(uint seed){
 uint count=liquidNozzleCount();
 uint active=liquidActiveNozzleCount();
 if(active==0)return 0xffffffffu;
 uint target=seed%active,ordinal=0;
 [loop]for(uint slot=0;slot<count;slot++){
  if(!liquidSurfaceLive(slot))continue;
  if(ordinal==target)return slot;
  ordinal++;
 }
 return 0xffffffffu;
}

uint liquidEmittingNozzleCount(){
 uint count=liquidNozzleCount();
 uint active=0;
 [loop]for(uint slot=0;slot<count;slot++){
  if(liquidSurfaceEmitting(slot))active++;
 }
 return active;
}

uint liquidEmittingNozzle(uint seed){
 uint count=liquidNozzleCount();
 uint active=liquidEmittingNozzleCount();
 if(active==0)return 0xffffffffu;
 uint target=seed%active,ordinal=0;
 [loop]for(uint slot=0;slot<count;slot++){
  if(!liquidSurfaceEmitting(slot))continue;
  if(ordinal==target)return slot;
  ordinal++;
 }
 return 0xffffffffu;
}

bool anyLiquidSurface(){
 return liquidActiveNozzleCount()>0;
}

bool anyLiquidEmittingSurface(){
 return liquidEmittingNozzleCount()>0;
}

#endif
