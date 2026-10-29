"""每个 Mod 的配置文件是权威来源；GPU 表格是由它生成的缓存。"""
from pathlib import Path
import hashlib, json, math, os, shutil, struct

from file_transaction import commit_updates

DEFAULT_EFFECT_COLOR = [0.78, 0.51, 0.055]
DEFAULT_EFFECT_STRENGTH = 1.0

def read(path, default):
    return json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else default

def encode(value):
    return json.dumps(value, ensure_ascii=False, indent=2).encode('utf-8')

def encode_profile(doc):
    rows=[]
    effect_color=doc.get('effect_color',DEFAULT_EFFECT_COLOR)
    effect_strength=doc.get('effect_strength',DEFAULT_EFFECT_STRENGTH)
    for p in doc['profiles']:
        row=list(p['gpu_row'])
        if not p.get('enabled',True):row[0]=1000000.
        rows.append([p['id'],*row,*p.get('offset_local_m',[0,0,0]),*effect_color,effect_strength])
    # GPU 读取器要求运行时字段放在较长的元数据之前。
    return encode({'schema_version':1,'enabled':doc.get('enabled',True),'gpu_data':rows,
        'mod_folder':doc['mod_folder'],'editor_anchor':doc.get('editor_anchor'),
        'editor_nozzles':doc.get('editor_nozzles'),'effect_color':doc.get('effect_color',DEFAULT_EFFECT_COLOR),
        'effect_strength':doc.get('effect_strength',DEFAULT_EFFECT_STRENGTH),
        'profiles':doc['profiles']})

def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name+'.profile-tmp')
    temp.write_bytes(data)
    os.replace(temp, path)

class ProfileStore:
    def __init__(self, probe, mods, output):
        self.probe, self.mods, self.output = Path(probe), Path(mods), Path(output)
        self.folder = self.mods/'LiquidGravityProbe_Profiles'
        self.legacy_folder = self.probe/'Profiles'
        self.index = self.folder/'_index.json'
        self.marker = self.probe/'surface/profile-folder-migrated.json'
        self.stamp = None

    def path(self, name):
        if not isinstance(name,str) or not name or any(c in name for c in '<>:"\\|?*'):
            raise ValueError('Mod 文件夹名称无效')
        if name!='.' and any(p in ('','.','..') or p.endswith((' ','.')) for p in name.split('/')):
            raise ValueError('Mod 文件夹路径无效')
        # 保留现有顶层文件名。嵌套文件夹会附加稳定后缀，
        # 防止同名同级目录共享配置或逃出 Profiles 目录。
        filename=name if '/' not in name and name!='.' else (name.split('/')[-1] if name!='.' else 'Mods-root')[:80]+'--'+hashlib.sha256(name.encode('utf-8')).hexdigest()[:16]
        return self.folder/(filename+'.json')

    def owner(self, profile):
        ini=profile.get('ini')
        if not ini:
            if not profile.get('enabled',True):return None
            raise ValueError('已启用配置缺少 ini 路径')
        path=Path(ini).resolve().parent
        try:
            return path.relative_to(self.mods.resolve()).as_posix()
        except ValueError:
            parts=path.parts
            for i in range(len(parts)-1,-1,-1):
                if parts[i].casefold()=='mods':
                    return Path(*parts[i+1:]).as_posix()
            raise ValueError(f'配置中的旧路径无法映射到当前 Mods：{path}')

    def transaction(self, updates):
        changed = {p:b for p,b in updates.items() if (b is None and p.exists()) or (b is not None and (not p.exists() or p.read_bytes()!=b))}
        if not changed:return False
        for p,b in changed.items():
            if b is None and p.parent.resolve()!=self.folder.resolve():
                raise ValueError('尝试删除 Profiles 目录之外的配置')
        return commit_updates(changed)

    def initialize(self):
        if self.index.exists():return
        self.folder.mkdir(parents=True,exist_ok=True)
        if self.legacy_folder.is_dir():
            for source in self.legacy_folder.iterdir():
                target=self.folder/source.name
                if source.is_file() and not target.exists():shutil.copy2(source,target)
        if self.index.exists():return
        if self.marker.exists():
            count=max(int(read(self.marker,{})['slot_count']),len(read(self.probe/'surface/profiles.json',{'profiles':[]})['profiles']))
            atomic(self.index,encode({'schema_version':1,'slot_count':count,'registered_mods':read(self.marker,{}).get('registered_mods',[])}))
            return
        profile_doc=read(self.probe/'surface/profiles.json',{'profiles':[]})
        profiles=profile_doc['profiles']
        raw=(self.probe/'surface/profiles.buf').read_bytes()
        offsets=(self.probe/'surface/attachment-offsets.buf').read_bytes()
        if len(raw)!=len(profiles)*48 or len(offsets)!=len(profiles)*16:
            raise ValueError('无法迁移大小不匹配的配置缓冲')
        grouped={}
        for i,p in enumerate(profiles):
            if p['id']!=i:raise ValueError('配置 ID 不稳定')
            name=self.owner(p)
            if name is None:continue
            grouped.setdefault(name,[]).append({**p,'gpu_row':list(struct.unpack_from('<12f',raw,i*48)),
                'offset_local_m':list(struct.unpack_from('<4f',offsets,i*16)[:3])})
        old_saved=read(self.output/'anchors.json',{})
        updates={}
        for name,entries in grouped.items():
            target=self.path(name)
            if target.exists():raise ValueError('未初始化的 Profiles 文件夹中已存在 '+target.name)
            model_id=hashlib.sha256(str(self.mods/name).encode()).hexdigest()[:16]
            updates[target]=encode_profile({'schema_version':1,'mod_folder':name,'enabled':True,
                'editor_anchor':old_saved.get(model_id),'profiles':entries})
        updates[self.index]=encode({'schema_version':1,'slot_count':len(profiles),
            'registered_mods':sorted(grouped), 'note':'配置 ID 保持稳定；EFMI 直接读取 Mod JSON。请勿删除此索引。'})
        updates[self.marker]=encode({'schema_version':1,'slot_count':len(profiles),'registered_mods':sorted(grouped)})
        index=json.loads(updates[self.index]);updates[self.probe/'surface/ProfileLoader.ini']=self.loader(index['registered_mods'])
        self.transaction(updates)

    def signature(self):
        return tuple((p.name,p.stat().st_mtime_ns,p.stat().st_size) for p in sorted(self.folder.glob('*.json')))

    def documents(self):
        result=[]
        for path in sorted(self.folder.glob('*.json')):
            if path.name.startswith('_'):continue
            doc=read(path,{})
            if doc.get('schema_version')!=1 or self.path(doc.get('mod_folder',''))!=path:
                raise ValueError('配置文件无效：'+path.name)
            if not isinstance(doc.get('profiles'),list):raise ValueError('配置文件缺少 profiles：'+path.name)
            color=doc.get('effect_color',DEFAULT_EFFECT_COLOR)
            if (not isinstance(color,list) or len(color)!=3
                    or not all(isinstance(value,(int,float)) and math.isfinite(value) and 0<=value<=1 for value in color)):
                raise ValueError('每个 Mod 的粒子颜色无效：'+path.name)
            strength=doc.get('effect_strength',DEFAULT_EFFECT_STRENGTH)
            if not isinstance(strength,(int,float)) or not math.isfinite(strength) or not .2<=strength<=2:
                raise ValueError('每个 Mod 的粒子强度无效：'+path.name)
            result.append(doc)
        return result

    def migrate_folder_owners(self):
        """拆分旧版顶层文件夹配置，但不重新编号 GPU 槽位。"""
        docs=self.documents()
        if not any(any(self.owner(p)!=d['mod_folder'] for p in d['profiles']) for d in docs):return False
        grouped={};seen=set()
        for doc in docs:
            entries={}
            for p in doc['profiles']:
                if p['id'] in seen:raise ValueError('文件夹迁移时发现重复的配置 ID')
                owner=self.owner(p)
                if owner is None:continue
                seen.add(p['id']);entries.setdefault(owner,[]).append(p)
            if not entries:entries[doc['mod_folder']]=[]
            for name,profiles in entries.items():
                target=grouped.setdefault(name,{'schema_version':1,'mod_folder':name,'enabled':True,'editor_anchor':None,'profiles':[]})
                # 合并旧文件时保留每条配置实际生效的启用状态。
                target['profiles'].extend({**p,'enabled':doc.get('enabled',True) and p.get('enabled',True)} for p in profiles)
                if target['editor_anchor'] is None:target['editor_anchor']=doc.get('editor_anchor')
        index=read(self.index,{})
        index['registered_mods']=sorted(grouped)
        updates={self.path(name):encode_profile(doc) for name,doc in grouped.items()}
        updates[self.index]=encode(index)
        updates[self.probe/'surface/ProfileLoader.ini']=self.loader(index['registered_mods'])
        updates[self.marker]=encode({'schema_version':1,'slot_count':index['slot_count'],'registered_mods':index['registered_mods']})
        targets={self.path(name) for name in grouped}
        for doc in docs:
            old=self.path(doc['mod_folder'])
            if old not in targets:updates[old]=None
        return self.transaction(updates)

    def sync(self):
        self.initialize()
        self.migrate_folder_owners()
        stamp=self.signature()
        if self.stamp==stamp:return False
        index=read(self.index,{})
        count=int(index['slot_count'])
        if not 0<=count<=100000:raise ValueError('配置槽数量无效')
        docs=self.documents();seen=set();rows=[(1000000.,1/3,1/3,1/3,0,0,0,0,0,0,0,0)]*count
        offsets=[(0.,0.,0.,0.)]*count
        profiles=[{'id':i,'enabled':False,'selection_error':1000000.,'status':'profile_file_missing'} for i in range(count)]
        registry=read(self.probe/'surface/attachment-models.json',{})
        assignments=[]
        for doc in docs:
            grouped={}
            for p in doc['profiles']:
                pid=p['id']
                if type(pid)!=int or not 0<=pid<count or pid in seen:raise ValueError('配置 ID 无效或重复')
                if self.owner(p)!=doc['mod_folder']:raise ValueError('配置所属 Mod 不匹配')
                row=list(p['gpu_row']);offset=list(p.get('offset_local_m',[0,0,0]))
                if len(row)!=12 or len(offset)!=3 or not all(isinstance(v,(int,float)) and math.isfinite(v) for v in row+offset):
                    raise ValueError('配置数值无效')
                if any(abs(v)>.25 for v in offset):raise ValueError('偏移超过 25 cm')
                enabled=doc.get('enabled',True) and p.get('enabled',True)
                if not enabled:row[0]=1000000.
                metadata={k:v for k,v in p.items() if k not in ('gpu_row','offset_local_m')}
                profiles[pid]={**metadata,'enabled':bool(enabled)}
                rows[pid]=row;offsets[pid]=(*offset,0.);seen.add(pid)
                grouped.setdefault((p['ini'],tuple(offset)),[]).append(pid)
            for (ini,offset),ids in grouped.items():
                assignments.append({'model_key':doc['mod_folder']+' / '+str(ids[0]),'kind':'replacement',
                    'source_ini':ini,'profile_ids':ids,'offset_local_m':list(offset),'attachment':'profile_folder'})
        used_count=max(seen)+1 if seen else 0
        profiles=profiles[:used_count];rows=rows[:used_count];offsets=offsets[:used_count]
        index['slot_count']=used_count
        marker=read(self.marker,{})
        marker['slot_count']=used_count
        marker['registered_mods']=index.get('registered_mods',[])
        aggregate=read(self.probe/'surface/profiles.json',{})
        aggregate['profiles']=profiles
        registry['models']=assignments
        changed=self.transaction({self.probe/'surface/profiles.json':encode(aggregate),
            self.probe/'surface/profiles.buf':b''.join(struct.pack('<12f',*r) for r in rows),
            self.probe/'surface/attachment-offsets.buf':b''.join(struct.pack('<4f',*r) for r in offsets),
            self.probe/'surface/attachment-models.json':encode(registry),
            self.index:encode(index),self.marker:encode(marker)})
        self.stamp=stamp
        return changed

    def loader(self, names):
        lines=['; 自动生成的绑定列表。用户配置保存在 Mods/LiquidGravityProbe_Profiles 中。',
            'namespace = LiquidGravityProbe\\JSONProfiles','[Constants]','global $loaded = 0',
            '[CommandListInitialize]','if !$loaded',
            '    clear = Resource\\LiquidGravityProbe\\SurfaceProfiles',
            '    clear = Resource\\LiquidGravityProbe\\SurfaceModelOffsets',
            '    clear = Resource\\LiquidGravityProbe\\SurfaceEffects']
        for i,name in enumerate(names):lines.append(f'    run = CustomShaderRead{i}')
        lines.extend(['    $loaded = 1','endif',''])
        for i,name in enumerate(names):
            profile_path=self.path(name)
            filename=Path(os.path.relpath(profile_path,self.probe/'surface')).as_posix()
            lines.extend([f'[ResourceJSON{i}]','type = Buffer','format = R8_UINT',f'filename = {filename}','',
                f'[CustomShaderRead{i}]','cs = LoadProfilesJSON.hlsl','vs = null','ps = null','gs = null','hs = null','ds = null',
                f'cs-t0 = ResourceJSON{i}','cs-u0 = Resource\\LiquidGravityProbe\\SurfaceProfiles',
                'cs-u1 = Resource\\LiquidGravityProbe\\SurfaceModelOffsets',
                'cs-u2 = Resource\\LiquidGravityProbe\\SurfaceEffects',
                'dispatch = 1,1,1','cs-t0 = null','cs-u0 = null','cs-u1 = null','cs-u2 = null',''])
        return ('\n'.join(lines)+'\n').encode('utf-8-sig')

    def saved_anchor(self, name):
        return read(self.path(name),{}).get('editor_anchor')

    def saved_nozzles(self, name):
        return read(self.path(name),{}).get('editor_nozzles')

    def save_nozzles(self, name, nozzles, anchor):
        path=self.path(name)
        doc=read(path,{'schema_version':1,'mod_folder':name,'enabled':True,'profiles':[]})
        doc['editor_anchor']=anchor
        doc['editor_nozzles']=nozzles
        atomic(path,encode_profile(doc))
        return path

    def save_anchor(self, name, anchor):
        path=self.path(name)
        doc=read(path,{'schema_version':1,'mod_folder':name,'enabled':True,'profiles':[]})
        doc['editor_anchor']=anchor
        atomic(path,encode_profile(doc))
        return path

    def saved_effect_color(self, name):
        color=read(self.path(name),{}).get('effect_color',DEFAULT_EFFECT_COLOR)
        return [max(0.,min(1.,float(value))) for value in color]

    def save_effect_color(self, name, color):
        if not isinstance(color,list) or len(color)!=3:
            raise ValueError('粒子颜色必须包含 RGB 三个值')
        path=self.path(name)
        doc=read(path,{'schema_version':1,'mod_folder':name,'enabled':True,'profiles':[]})
        doc['effect_color']=[max(0.,min(1.,float(value))) for value in color]
        atomic(path,encode_profile(doc))
        return path

    def saved_effect_strength(self, name):
        value=read(self.path(name),{}).get('effect_strength',DEFAULT_EFFECT_STRENGTH)
        return max(.2,min(2.,float(value)))

    def save_effect_settings(self, name, color, strength):
        if not isinstance(color,list) or len(color)!=3:
            raise ValueError('粒子颜色必须包含 RGB 三个值')
        strength=float(strength)
        if not math.isfinite(strength) or not .2<=strength<=2:
            raise ValueError('粒子强度必须在 0.2–2.0 范围内')
        path=self.path(name)
        doc=read(path,{'schema_version':1,'mod_folder':name,'enabled':True,'profiles':[]})
        doc['effect_color']=[max(0.,min(1.,float(value))) for value in color]
        doc['effect_strength']=strength
        atomic(path,encode_profile(doc))
        return path

    def model_updates(self, name, aggregate, raw, offsets, anchor, selected_ids, exclusive=False):
        # 与 INI 和 GPU 表格一起加入编辑器现有的回滚事务。
        current=read(self.path(name),{'schema_version':1,'mod_folder':name,'enabled':True,'profiles':[]})
        entries=[]
        selected_ids=[selected_ids] if isinstance(selected_ids,int) else list(selected_ids)
        allowed=set(selected_ids) if exclusive else {p['id'] for p in current.get('profiles',[])}|set(selected_ids)
        for p in aggregate['profiles']:
            if p['id'] not in allowed:continue
            if 'ini' not in p or self.owner(p)!=name:continue
            pid=p['id']
            entries.append({**p,'gpu_row':list(struct.unpack_from('<12f',raw,pid*48)),
                'offset_local_m':list(struct.unpack_from('<4f',offsets,pid*16)[:3])})
        doc={'schema_version':1,'mod_folder':name,'enabled':True,'editor_anchor':anchor,
            'effect_color':current.get('effect_color',DEFAULT_EFFECT_COLOR),
            'effect_strength':current.get('effect_strength',DEFAULT_EFFECT_STRENGTH),'profiles':entries}
        index=read(self.index,{})
        index['slot_count']=max(index['slot_count'],len(aggregate['profiles']))
        index['registered_mods']=sorted(set(index.get('registered_mods',[]))|{name})
        return {self.path(name):encode_profile(doc),self.index:encode(index),self.probe/'surface/ProfileLoader.ini':self.loader(index['registered_mods'])}

if __name__=='__main__':
    probe=Path(__file__).resolve().parent.parent
    store=ProfileStore(probe,probe.parent,Path(__file__).resolve().parent/'output')
    try:
        changed=store.sync()
        print('Profiles 已同步。' if changed else 'Profiles 已经是最新状态。')
        print('文件夹：',store.folder)
        print('修改后请回到游戏按 F10 重新加载。')
    except Exception as error:
        print('配置同步失败：',error)
        raise SystemExit(1)
