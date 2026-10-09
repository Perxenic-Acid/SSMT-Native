"""独立核验实机 FK 数据；只读日志，不操纵游戏。"""
import json, math, sys
from pathlib import Path

def norm(q):
    assert len(q) == 4 and all(math.isfinite(v) for v in q)
    n = math.sqrt(sum(v*v for v in q)); assert abs(n-1) < 0.0001
    return [v/n for v in q]
def mul(a,b):
    x,y,z,w=a; X,Y,Z,W=b
    return norm([w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z])
def inv(q): return [-q[0],-q[1],-q[2],q[3]]
def angle(a,b):
    d=mul(inv(norm(a)),norm(b))
    return math.degrees(2*math.atan2(math.sqrt(sum(v*v for v in d[:3])),abs(d[3])))

parents={'下半身':None,'上半身':None,'上半身2':'上半身','首':'上半身2','頭':'首',
         '左肩':'上半身2','右肩':'上半身2','左腕':'左肩','右腕':'右肩',
         '左ひじ':'左腕','右ひじ':'右腕','左手首':'左ひじ','右手首':'右ひじ',
         '左足':'下半身','右足':'下半身','左ひざ':'左足','右ひざ':'右足',
         '左足首':'左ひざ','右足首':'右ひざ'}
directory=Path(sys.argv[1]); destination=Path(sys.argv[2]); stages=[]
for path in sorted(directory.glob('pelvis_fk.*.json')):
    data=json.loads(path.read_text(encoding='utf-8'))
    assert data['route']=='Legacy' and data['avatar_reference_ready']
    assert data['fault']==0 and data['restore']
    assert not data['IK'] and not data['root_motion'] and data['translation']=='NOT_WRITTEN'
    bones={b['index']:b for b in data['bones']}; assert len(bones) in [21,22]
    auxiliary=[b for b in bones.values() if b.get('pelvis_auxiliary')]
    assert len(auxiliary)==len(bones)-21
    if auxiliary:assert auxiliary[0]['name']=='+PelvisTwist CF A01' and auxiliary[0]['source']=='下半身'
    sources={}
    for b in bones.values():
        if b['source']:
            if b['source'] in sources:
                previous=sources[b['source']]
                assert bool(b.get('pelvis_auxiliary'))!=bool(previous.get('pelvis_auxiliary')) and angle(b['vmd_raw'],previous['vmd_raw'])<.001 and angle(b['vmd_input'],previous['vmd_input'])<.001
                if previous.get('pelvis_auxiliary'):sources[b['source']]=b
            else:sources[b['source']]=b
    assert set(sources)==set(parents)
    if data['pose']=='FKDance':assert 'last_frame' in data and data['last_frame']>=data['frame'], 'Old continuous diagnostic mislabeled its first rotation with its last frame'
    if data['pose']=='FKSingleAxes':
        stage=data['stage'];targets_by_axis=['下半身','上半身','左足','右足']
        assert data['scope']=='FullFK' and data['axis_bone']==((stage-1)//3 if stage else -1)
        assert data['axis']==((stage-1)%3 if stage else 0)
        for name,b in sources.items():
            expected_input=[0,0,0,1]
            if stage and name==targets_by_axis[data['axis_bone']]:
                expected_input[data['axis']]=math.sin(math.radians(15));expected_input[3]=math.cos(math.radians(15))
            assert angle(expected_input,b['vmd_input'])<0.001, (path.name,name,'axis input mismatch')
    source_model={}; targets={}
    def source_world(name):
        if name not in source_model:
            parent=parents[name]; q=norm(sources[name]['vmd_input'])
            source_model[name]=mul(source_world(parent),q) if parent else q
        return source_model[name]
    def target_model(index):
        if index not in targets:
            b=bones[index]; parent=b['parent']
            if b['source']: q=mul(source_world(b['source']),b['bind_model_rotation'])
            elif parent in bones and bones[parent]['written']:
                q=mul(target_model(parent),b['bind_local_rotation'])
            else: q=norm(b['bind_model_rotation'])
            targets[index]=q
        return targets[index]
    root_bone=next(b for b in bones.values() if b['name']==('Bip001' if data['scope']=='FullFK' else 'Bip001 Spine'))
    root_world=mul(root_bone['unity_world'],inv(target_model(root_bone['index'])))
    max_local=0; max_world=0; records=[]
    for b in bones.values():
        for field in ['unity_local','unity_world','restore_rotation','bind_model_rotation','vmd_raw','vmd_input']:
            norm(b[field])
        assert b['bind_local_rotation'] is not None or b['name']=='Bip001'
        assert all(math.isfinite(v) for v in b['unity_position']+b['bind_model_position'])
        if b['written']:
            expected_world=mul(root_world,target_model(b['index']))
            parent_world=bones[b['parent']]['unity_world'] if b['parent'] in bones else root_world
            expected_local=mul(inv(parent_world),expected_world)
            world_error=angle(expected_world,b['unity_world']); local_error=angle(expected_local,b['unity_local'])
            assert world_error<0.001 and local_error<0.001, (path.name,b['name'],local_error,world_error)
            assert b['local_error_deg']<0.001 and b['world_error_deg']<0.001
            max_local=max(max_local,local_error);max_world=max(max_world,world_error)
            records.append({'name':b['name'],'source':b['source'],'parent':b['parent'],
                            'bind_local_rotation':b['bind_local_rotation'],'unity_local':b['unity_local'],
                            'independent_local_error_deg':local_error,'independent_world_error_deg':world_error})
    expected_count=len(bones) if data['scope']=='FullFK' else 13
    assert len(records)==expected_count and data['writes']==data['frames']*expected_count
    stages.append({'file':path.name,'pose':data['pose'],'stage':data['stage'],'frame':data['frame'],
                   'scope':data['scope'],'frames':data['frames'],'writes':data['writes'],
                   'written_bones':len(records),'restored_bones':data['restored_bones'],
                   'last_frame':data.get('last_frame',data['frame']),
                   'max_local_error_deg':max_local,'max_world_error_deg':max_world,'bones':records})
    if auxiliary and auxiliary[0]['written']:
        b=auxiliary[0];pelvis=sources['下半身'];bip=bones[b['parent']]
        assert b['before_local'] is not None and b['parent']==pelvis['parent'] and bip['name']=='Bip001'
        delta_helper=mul(b['unity_world'],inv(b['bind_model_rotation']))
        delta_pelvis=mul(pelvis['unity_world'],inv(pelvis['bind_model_rotation']))
        gap=angle(delta_helper,delta_pelvis);assert gap<.001
        predicted_old_model=mul(bip['bind_model_rotation'],b['before_local'])
        stages[-1]['auxiliary']={'name':b['name'],'pelvis_model_increment_gap_deg':gap,
                                 'uncontrolled_gap_if_parent_static_deg':angle(predicted_old_model,target_model(b['index'])),
                                 'before_local':b['before_local'],'after_local':b['unity_local'],
                                 'baseline_kind':'counterfactual with the captured pre-write local; not a previous-render-frame observation'}
assert stages, 'No stopped-session diagnostics found'
runs={}
for stage in stages:runs.setdefault(stage['file'].split('.')[-2],[]).append(stage)
for values in runs.values():
    touched={b['name'] for s in values for b in s['bones']}
    assert all(s['restored_bones']==len(touched) for s in values), 'Restore missed a previously written bone'
complete=[key for key,values in runs.items() if values[0]['pose']=='FKFixedFrames' and sorted(s['stage'] for s in values)==list(range(6))]
axis_complete=[key for key,values in runs.items() if values[0]['pose']=='FKSingleAxes' and sorted(s['stage'] for s in values)==list(range(13))]
continuous_complete=[key for key,values in runs.items() if values[0]['pose']=='FKDance' and sorted(s['frame'] for s in values)==[0,30,120]]
waist=[]
for key in complete:
    for s in runs[key]:
        d=json.loads((directory/s['file']).read_text(encoding='utf-8'))
        spine=next(b for b in d['bones'] if b['name']=='Bip001 Spine')
        pelvis=next(b for b in d['bones'] if b['name']=='Bip001 Pelvis')
        waist.append({'run':key,'stage':d['stage'],'frame':d['frame'],'scope':d['scope'],
                      'spine_relative_bind_angle_deg':angle(spine['bind_local_rotation'],spine['unity_local']),
                      'source_upper_angle_deg':angle([0,0,0,1],spine['vmd_raw']),
                      'source_lower_angle_deg':angle([0,0,0,1],pelvis['vmd_raw']),
                      'source_upper_vs_lower_angle_deg':angle(spine['vmd_raw'],pelvis['vmd_raw'])})
result={'numeric_validation':'PASS','complete_fixed_frame_runs':complete,'complete_axis_runs':axis_complete,'complete_continuous_reference_runs':continuous_complete,'waist_comparison':waist,'runs':runs,
        'visual_validation':'USER_FEEDBACK_REQUIRED','root_world_oracle':'ONE_REFERENCE_BONE_PER_STAGE',
        'limitations':'Quaternion composition verified; model axis calibration and visual improvement are separate checks.'}
destination.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'numeric_validation':'PASS','stages':len(stages),'complete_fixed_frame_runs':complete,'complete_axis_runs':axis_complete,'complete_continuous_reference_runs':continuous_complete,
                  'max_local_error_deg':max(s['max_local_error_deg'] for s in stages),
                  'max_world_error_deg':max(s['max_world_error_deg'] for s in stages)},ensure_ascii=False))
