"""本地、无额外依赖的 EFMI 网格/附着点编辑器。INI 仅作为数据读取。"""
from __future__ import annotations
import argparse, array, ast, codecs, ctypes, hashlib, json, math, mimetypes, os, re, secrets, struct, sys, threading, time, traceback
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from cleanup import cleanup_probe_data
from effect_settings import FIELDS, read_effect_settings, write_effect_settings
from file_transaction import commit_updates
from keybinding import build_binding, read_key_binding, write_key_binding
from profile_store import ProfileStore
from urllib.parse import parse_qs, unquote, urlsplit

HERE = Path(__file__).resolve().parent
UI_ROOT = HERE.parent / 'ui'
OUTPUT = HERE / 'output'
TOKEN = secrets.token_urlsafe(24)
PARENT_PID = 0
CACHE_LIMIT = 3
DECODED_LIMIT = 32
NOZZLE_INTERVAL_MIN = 0.5
NOZZLE_INTERVAL_MAX = 300.0
FRAME_CSP = (
    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
    "img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; "
    "form-action 'self'; frame-ancestors 'self' "
    "http://localhost:1422 http://127.0.0.1:1422 "
    "http://tauri.localhost https://tauri.localhost"
)
ALLOWED_FRAME_ORIGINS = {
    'http://localhost:1422',
    'http://127.0.0.1:1422',
    'http://tauri.localhost',
    'https://tauri.localhost',
}
LOCK = threading.RLock()
CATALOG = {}; MODELS = {}; CACHE = {}; DECODED = {}

def parent_process_alive(pid):
    if not pid or os.name!='nt':return True
    kernel32=ctypes.windll.kernel32
    handle=kernel32.OpenProcess(0x1000,False,int(pid))
    if not handle:return False
    try:
        code=ctypes.c_ulong()
        return bool(kernel32.GetExitCodeProcess(handle,ctypes.byref(code))) and code.value==259
    finally:kernel32.CloseHandle(handle)

def watch_parent_process(pid):
    while True:
        time.sleep(2)
        if not parent_process_alive(pid):
            print('SSMT 已退出，关闭插件编辑器。',flush=True)
            os._exit(0)

def find_mods_folder(start):
    """查找最近的上级 Mods 文件夹，不依赖安装深度。"""
    return next((p for p in start.resolve().parents if p.name.casefold() == 'mods'), start.parent.parent)


MODS = find_mods_folder(HERE)
PROBE = HERE.parent
STORE = None
EMBEDDED_SERVER = None
EMBEDDED_THREAD = None
EMBEDDED_STOP = threading.Event()
EMBEDDED_STATE_LOCK = threading.Lock()
EMBEDDED_LOG = None

def ensure_data_files(probe):
    probe=Path(probe);surface=probe/'surface'
    surface.mkdir(parents=True,exist_ok=True)
    probe_file=probe/'Probe.ini'
    if not probe_file.exists():
        lines=[]
        for spec in FIELDS:
            value=spec['default']
            text=str(int(value)) if spec['kind'] in ('toggle','select') or (spec['step']>=1 and float(spec['step']).is_integer()) else format(float(value),'.6g')
            lines.append(f"global ${spec['key']} = {text}")
        lines.extend(['','[KeyToggle]','key = NO_CTRL NO_ALT NO_SHIFT VK_NUMPAD1'])
        probe_file.write_text('\r\n'.join(lines)+'\r\n',encoding='utf-8',newline='')
    defaults={
        surface/'profiles.json':'{"profiles":[]}',
        surface/'attachment-models.json':'{"models":[]}',
        surface/'profiles.buf':'',
        surface/'attachment-offsets.buf':'',
    }
    for path,content in defaults.items():
        if not path.exists():path.write_text(content,encoding='utf-8')

RUNTIME_REQUIRED_SECTIONS=(
    '[CommandListFluidFrame]',
    '[CommandListCaptureSurface]',
    '[CustomShaderRenderFluidDepth]',
    '[CustomShaderRenderFluidNoDepth]',
    '[Present]',
)
RUNTIME_REQUIRED_ASSETS=(
    'ResolvePlayerAnchor.hlsl',
    'surface/CaptureTriangle.hlsl',
    'surface/LoadProfilesJSON.hlsl',
    'surface/ResolveSurface.hlsl',
    'fluid/Collision.hlsl',
    'fluid/FluidSolver.hlsl',
    'fluid/FluidRenderColor.hlsl',
    'fluid/SurfaceFrame.hlsl',
)

def runtime_validation(probe=None):
    probe=Path(probe or PROBE)
    errors=[]
    probe_file=probe/'Probe.ini'
    if not probe_file.is_file():
        return {'ready':False,'errors':['Probe.ini 不存在']}
    text=probe_file.read_text(encoding='utf-8-sig')
    if 'namespace = LiquidGravityProbe' not in text:
        errors.append('Probe.ini 缺少 LiquidGravityProbe 命名空间')
    for section in RUNTIME_REQUIRED_SECTIONS:
        if section not in text:
            errors.append(f'Probe.ini 缺少 {section}')
    for relative in RUNTIME_REQUIRED_ASSETS:
        if not (probe/relative).is_file():
            errors.append(f'缺少运行时文件 {relative}')
    for relative in sorted(set(re.findall(r'^[ \t]*(?:vs|ps|gs|hs|ds|cs)[ \t]*=[ \t]*([^\s;]+\.hlsl)',text,re.M|re.I))):
        if not (probe/relative.replace('\\','/')).is_file():
            errors.append(f'缺少着色器 {relative}')
    if '__' in text and re.search(r'__[A-Z_]+__',text):
        errors.append('Probe.ini 仍有未生成的池参数占位符')
    return {'ready':not errors,'errors':errors}

def particle_pool_layout(values):
    primary=max(512,min(2048,int(round(float(values.get('particle_pool',1024))/256)*256)))
    airborne=primary
    puddle=primary*2
    splash=airborne+puddle
    return {
        'primary':primary,
        'base_primary':primary//2,
        'airborne':airborne,
        'puddle':puddle,
        'splash':splash,
        'event':primary,
        'primary_dispatch':(primary+63)//64,
        'splash_dispatch':(splash+63)//64,
        'render_draw':6*(primary+splash),
    }

def render_runtime_template(text, layout):
    replacements={
        '__PRIMARY_COUNT__':layout['primary'],
        '__PRIMARY_DISPATCH__':layout['primary_dispatch'],
        '__SPLASH_COUNT__':layout['splash'],
        '__SPLASH_DISPATCH__':layout['splash_dispatch'],
        '__EVENT_CAPACITY__':layout['event'],
        '__RENDER_DRAW_COUNT__':layout['render_draw'],
    }
    for token,value in replacements.items():text=text.replace(token,str(value))
    return text

def render_particle_config(text, layout):
    replacements={
        'PRIMARY_COUNT':layout['primary'],
        'AIRBORNE_SPLASH_COUNT':layout['airborne'],
        'PUDDLE_COUNT':layout['puddle'],
        'EVENT_CAPACITY':layout['event'],
        'BASE_PRIMARY_COUNT':layout['base_primary'],
    }
    for key,value in replacements.items():
        text=re.sub(rf'^(static const uint {re.escape(key)}=)\d+',rf'\g<1>{value}',text,flags=re.M)
    return text

def install_runtime_assets(runtime_root=None):
    runtime_root=Path(runtime_root or HERE/'runtime')
    template=runtime_root/'Probe.runtime.tpl'
    if not template.is_file():return False
    template_text=template.read_bytes().decode('utf-8-sig')
    missing_sections=[section for section in RUNTIME_REQUIRED_SECTIONS if section not in template_text]
    if missing_sections:
        raise ValueError('插件运行时模板不完整：'+', '.join(missing_sections))
    missing_assets=[relative for relative in RUNTIME_REQUIRED_ASSETS if not (runtime_root/relative).is_file()]
    if missing_assets:
        raise ValueError('插件运行时缺少文件：'+', '.join(missing_assets))
    shader_references=sorted(set(re.findall(r'^[ \t]*(?:vs|ps|gs|hs|ds|cs)[ \t]*=[ \t]*([^\s;]+\.hlsl)',template_text,re.M|re.I)))
    missing_shaders=[relative for relative in shader_references if not (runtime_root/relative.replace('\\','/')).is_file()]
    if missing_shaders:
        raise ValueError('插件运行时缺少着色器：'+', '.join(missing_shaders))
    values={spec['key']:spec['default'] for spec in FIELDS}
    nozzle_count=1
    nozzle_intervals=[0.0]*4
    binding=None
    probe_file=PROBE/'Probe.ini'
    if probe_file.exists():
        try:values.update(read_effect_settings(PROBE)['values'])
        except (OSError,ValueError):pass
        legacy=probe_file.read_text(encoding='utf-8-sig')
        nozzle=re.search(r'^[ \t]*global[ \t]+\$nozzle_count[ \t]*=[ \t]*(\d+)',legacy,re.M|re.I)
        if nozzle:nozzle_count=max(1,min(4,int(nozzle[1])))
        for slot in range(4):
            interval=re.search(rf'^[ \t]*global[ \t]+\$nozzle_interval_{slot}[ \t]*=[ \t]*([0-9.]+)',legacy,re.M|re.I)
            if interval:
                try:nozzle_intervals[slot]=max(0.,min(NOZZLE_INTERVAL_MAX,float(interval[1])))
                except ValueError:pass
        try:binding=read_key_binding(PROBE)
        except (OSError,ValueError):pass

    layout=particle_pool_layout(values)
    text=render_runtime_template(template_text,layout)
    asset_bytes={}
    for source in runtime_root.rglob('*'):
        if not source.is_file() or source.suffix.lower()=='.tpl':continue
        if source.suffix.lower() not in ('.hlsl','.bin'):continue
        relative=source.relative_to(runtime_root)
        target=PROBE/relative
        source_hlsl=source.parent/(source.name.split('.',1)[0]+'.hlsl')
        if source.suffix.lower()=='.bin' and source_hlsl.is_file():
            if target.exists():target.unlink()
            continue
        data=source.read_bytes()
        if relative.as_posix()=='fluid/ParticleConfig.hlsl':
            data=render_particle_config(data.decode('utf-8'),layout).encode('utf-8')
        asset_bytes[relative]=data
        if not target.exists() or target.read_bytes()!=data:atomic(target,data)

    for key,value in values.items():text=set_probe_global(text,key,value)
    text=set_probe_global(text,'nozzle_count',nozzle_count)
    for slot,value in enumerate(nozzle_intervals):text=set_probe_global(text,f'nozzle_interval_{slot}',format(value,'.6g'))
    if binding:
        section=re.search(r'^\[KeyToggle\][ \t]*\r?\n(.*?)(?=^\[|\Z)',text,re.M|re.S|re.I)
        if section:
            key_line=build_binding(binding['key'],binding['ctrl'],binding['alt'],binding['shift'])
            body,count=re.subn(r'^[ \t]*key[ \t]*=[^\r\n]*',f'key = {key_line}',section[1],count=1,flags=re.M|re.I)
            if count==1:text=text[:section.start(1)]+body+text[section.end(1):]
    ordered=sorted(asset_bytes,key=lambda item:item.as_posix())
    marker=hashlib.sha256(text.encode('utf-8')+b''.join(asset_bytes[key] for key in ordered)).hexdigest()
    marker_path=PROBE/'surface/runtime-assets.sha256'
    marker_value=marker+'\n'
    data=text.encode('utf-8')
    if not probe_file.exists() or probe_file.read_bytes()!=data:atomic(probe_file,data)
    if not marker_path.exists() or marker_path.read_text(encoding='ascii')!=marker_value:atomic(marker_path,marker_value.encode('ascii'))
    status=runtime_validation(PROBE)
    if not status['ready']:
        raise ValueError('游戏运行时安装不完整：'+'；'.join(status['errors']))
    print('Liquid Gravity Probe 运行时校验通过。',flush=True)
    return True

def profile_store():
    global STORE
    if STORE is None or STORE.probe != PROBE:
        STORE = ProfileStore(PROBE, MODS, OUTPUT)
    return STORE


def profile_name(model):
    return model.get('profile_key', model['name'])

def sync_profiles():
    if profile_store().sync():
        CACHE.clear()
        print('配置文件夹已同步。请回到游戏按 F10 重新加载。', flush=True)


def read_json(path, fallback):
    return json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else fallback

def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.editor-tmp')
    temp.write_bytes(data); os.replace(temp, path)

def cache_get(cache, key):
    if key not in cache:return None
    value=cache.pop(key);cache[key]=value;return value

def remember_limited(cache, key, value, limit):
    cache.pop(key,None);cache[key]=value
    while len(cache)>limit:cache.pop(next(iter(cache)))

def reusable_profile_slot(profiles):
    return next((index for index,profile in enumerate(profiles)
        if not profile.get('enabled',True) and profile.get('status')=='removed_nozzle'),len(profiles))

def clean(line):
    return line.split(';', 1)[0].strip()

def sections(text):
    return [(m[1], m[2], m.start(2)) for m in re.finditer(r'^\[([^\]]+)\][ \t]*\r?\n(.*?)(?=^\[|\Z)', text, re.M | re.S)]

def value(body, key, default=''):
    m = re.search(r'^\s*' + re.escape(key) + r'\s*=\s*([^\r\n]+)', body, re.M | re.I)
    return clean(m[1]) if m else default

def eval_condition(expression, variables, numeric=False):
    """受限数值表达式解释器，不会对 INI 使用 eval/exec。"""
    expression = re.sub(r'\$[\w\\.]+', lambda m: str(variables.get(m[0].lower(), 0)), expression)
    expression = expression.replace('&&', ' and ').replace('||', ' or ')
    expression = re.sub(r'!(?!=)', ' not ', expression).strip()
    ops = {ast.Add: lambda a,b:a+b, ast.Sub: lambda a,b:a-b, ast.Mult:lambda a,b:a*b,
           ast.Div:lambda a,b:a/b, ast.Mod:lambda a,b:a%b, ast.Eq:lambda a,b:a==b,
           ast.NotEq:lambda a,b:a!=b, ast.Lt:lambda a,b:a<b, ast.LtE:lambda a,b:a<=b,
           ast.Gt:lambda a,b:a>b, ast.GtE:lambda a,b:a>=b}
    def walk(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, (int,float,bool)): return n.value
        if isinstance(n, ast.UnaryOp):
            x=walk(n.operand)
            if isinstance(n.op,ast.Not):return not x
            if isinstance(n.op,ast.USub):return -x
            if isinstance(n.op,ast.UAdd):return x
        if isinstance(n, ast.BinOp) and type(n.op) in ops:return ops[type(n.op)](walk(n.left),walk(n.right))
        if isinstance(n, ast.BoolOp):return (all if isinstance(n.op,ast.And) else any)(walk(v) for v in n.values)
        if isinstance(n, ast.Compare):return all(ops[type(op)](walk(a),walk(b)) for a,op,b in zip([n.left]+n.comparators,n.ops,n.comparators))
        raise ValueError('不支持的表达式')
    try:
        result=walk(ast.parse(expression,mode='eval').body)
        return result if numeric else bool(result)
    except (ValueError,SyntaxError,KeyError,ZeroDivisionError):return None

def apply_toggle_defaults(section_list, variables):
    for name,body,_ in section_list:
        if not re.search(r'^CommandList.*(?:ProcessToggles|UpdateToggles)$',name,re.I):continue
        stack=[]
        for raw in body.splitlines():
            line=clean(raw);low=line.lower()
            if low.startswith('if '):
                flag=eval_condition(line[3:],variables);stack.append([flag,flag]);continue
            if (low.startswith('elif ') or low.startswith('else if ')) and stack:
                before,_=stack[-1];flag=eval_condition(line[8:] if low.startswith('else if ') else line[5:],variables)
                stack[-1]=[before or flag,False if before else flag];continue
            if low=='else' and stack:
                before,_=stack[-1];stack[-1]=[True,not before if before is not None else None];continue
            if low=='endif':
                if stack:stack.pop()
                continue
            assignment=re.match(r'(\$[\w]+)\s*=\s*(.+)',line)
            if assignment and all(x[1] is not False for x in stack):
                result=eval_condition(assignment[2],variables,numeric=True)
                if result is not None:variables[assignment[1].lower()]=result

def resource_path(ini, filename):
    path=(ini.parent / filename.replace('\\','/').strip('"')).resolve()
    if not path.is_relative_to(MODS.resolve()):raise ValueError('资源路径位于 Mods 目录之外')
    return path

def detect_time_axis(ini_files):
    for ini in ini_files:
        try:text=ini.read_text(encoding='utf-8-sig',errors='replace')
        except OSError:continue
        def number(name):
            match=re.search(rf'^[ \t]*global[ \t]+\${re.escape(name)}[ \t]*=[ \t]*(-?\d+(?:\.\d+)?)',text,re.M|re.I)
            return float(match[1]) if match else None
        fps=number('fps');start=number('frameStart');end=number('frameEnd')
        if fps is None or start is None or end is None:continue
        expression=re.search(r'^[ \t]*\$swapvar[ \t]*=[ \t]*([^\r\n]+)',text,re.M|re.I)
        if not expression or 'time' not in expression[1].lower():continue
        fps=max(1,min(240,int(round(fps))));start=int(round(start));end=int(round(end))
        frames=end-start+1
        if 2<=frames<=256:
            return {'fps':fps,'start':start,'end':end,'frames':frames,
                'source':str(ini.relative_to(MODS)),'expression':clean(expression[1])}
    return None

def scan():
    global CATALOG, MODELS, CACHE, DECODED
    catalog=[]; models={}; CACHE={};DECODED={}
    user_config=MODS.parent/'d3dx_user.ini'
    persisted={m[1].strip().lower():float(m[2]) for m in re.finditer(r'^(\$[^=\r\n]+?)\s*=\s*(-?\d+(?:\.\d+)?)\s*$',user_config.read_text(encoding='utf-8-sig',errors='replace') if user_config.exists() else '',re.M)}
    folder_inis={}
    for ini in sorted(MODS.rglob('*.ini')):
        if ini.resolve().is_relative_to(PROBE.resolve()):continue
        parts=ini.parent.relative_to(MODS).parts
        if any('backup' in p.lower() or p.lower().startswith(('disabled','_')) for p in parts):continue
        if ini.name.lower().startswith('disabled'):continue
        folder_inis.setdefault(ini.parent,[]).append(ini)
    for folder,ini_files in sorted(folder_inis.items()):
        records=[]; errors=[]; resources={}; parsed=[]
        for ini in ini_files:
            try:text=ini.read_text(encoding='utf-8-sig',errors='replace')
            except OSError as e:errors.append(str(e));continue
            sec=sections(text); local={}
            variables=dict(persisted)
            namespace=re.search(r'^namespace\s*=\s*(.+)',text,re.M|re.I)
            namespace=clean(namespace[1]).lower() if namespace else str(ini.relative_to(MODS.parent)).lower()
            for m in re.finditer(r'global\s+(?:persist\s+)?(\$[\w]+)\s*=\s*(-?\d+(?:\.\d+)?)',text,re.I):
                variables[m[1].lower()]=persisted.get('$\\'+namespace+'\\'+m[1][1:].lower(),float(m[2]))
            apply_toggle_defaults(sec,variables)
            for name,body,_ in sec:
                if not name.lower().startswith('resource'):continue
                fn=value(body,'filename')
                if not fn:continue
                try:
                    path=resource_path(ini,fn)
                    if path.suffix.lower() not in ('.buf','.ib','.vb') or not path.is_file():continue
                    resource={'path':str(path),'name':name,'stride':int(value(body,'stride','16')),'format':value(body,'format')}
                    local[name.lower()]=resource;resources.setdefault(name.lower(),[]).append(resource)
                except (OSError,ValueError) as e:errors.append(f'{ini.name}: {e}')
            parsed.append((ini,sec,local,variables,namespace))
        command_sections={}
        for pini,psec,plocal,pvariables,pnamespace in parsed:
            for pname,pbody,_ in psec:
                command_sections[(pnamespace,pname.lower())]=(pini,pname,pbody,plocal,pvariables,pnamespace)
        seen_records=set();seen_geometry={};fingerprint_cache={}

        def fingerprint(path):
            path=Path(path);stat=path.stat();key=(path.resolve(),stat.st_mtime_ns,stat.st_size)
            if key not in fingerprint_cache:fingerprint_cache[key]=hashlib.sha256(path.read_bytes()).hexdigest()
            return fingerprint_cache[key]

        def command_target(command,namespace):
            if not command.casefold().startswith('commandlist'):return None
            if command.casefold().startswith('commandlist\\'):
                suffix=command.split('\\',1)[1]
                parts=suffix.split('\\')
                target_namespace='\\'.join(parts[:-1]) or namespace
                target_name=parts[-1]
            else:
                target_namespace=namespace
                target_name=command[len('commandlist'):]
            return target_namespace.casefold(),('commandlist'+target_name).casefold()

        def trace_section(ini,secname,body,local,variables,namespace,vb=None,ib=None,label='',
                          inherited_visible=True,inherited_conditional=False,chain=(),call_origin=None):
            identity=(namespace.casefold(),secname.casefold())
            if identity in chain:return
            chain=chain+(identity,)
            record_ini,record_section,origin_run_index,origin_run_line=call_origin or (ini,secname,None,None)
            def resolve(name):
                name=re.sub(r'^(?:ref|copy)\s+','',name,flags=re.I).strip().lower()
                if name in local:return local[name]
                options=resources.get(name,[])
                if len(options)==1:return options[0]
                if 'virtual' in name:return resolve(name.replace('virtual',''))
                for suffix in ('_0','0'):
                    options=resources.get(name+suffix,[])
                    if len(options)==1:return options[0]
                return None
            stack=[];ordinal=0;run_ordinal=0
            for raw in body.splitlines():
                line=clean(raw);low=line.lower()
                if 'draw object' in raw.lower() or re.search(r';\s*Draw Component',raw,re.I):label=raw.strip(' ;\t')
                if low.startswith('if '):
                    condition=eval_condition(line[3:],variables);stack.append([condition is True,condition is not False]);continue
                if (low.startswith('elif ') or low.startswith('else if ')) and stack:
                    previous,_=stack[-1];result=eval_condition(line[8:] if low.startswith('else if ') else line[5:],variables)
                    stack[-1]=[previous or result is True,not previous and result is not False];continue
                if low=='else' and stack:
                    previous,_=stack[-1];stack[-1]=[True,not previous];continue
                if low=='endif':
                    if stack:stack.pop()
                    continue
                # Unknown GPU-state conditions are preview possibilities, not a
                # definite "off" branch. Keep tracing them so body meshes hidden
                # behind shader/CB checks are still available in the editor.
                active=all(x[1] is not False for x in stack)
                assignment=re.match(r'(?:local\s+)?(\$[\w\\.]+)\s*=\s*(.+)',line,re.I)
                if assignment and active:
                    result=eval_condition(assignment[2],variables,numeric=True)
                    if result is not None:variables[assignment[1].lower()]=result
                    continue
                bind=re.match(r'(vb0|ib)\s*=\s*(.+)',line,re.I)
                if bind and active:
                    if bind[1].lower()=='vb0':
                        source=re.sub(r'^(?:ref|copy)\s+','',bind[2],flags=re.I).strip()
                        vb=resolve(source)
                        if not vb and source.lower().startswith('pool_shapekeyedpositionsrw[') and ib:
                            component=re.match(r'(resource_component\d+)_ib$',ib['name'],re.I)
                            if component:vb=resolve(component[1]+'_VB0')
                        if not vb:
                            animated=re.match(r'(resource_component\d+)_vb0_anim$',source,re.I)
                            if animated:vb=resolve(animated[1]+'_VB0')
                    else:ib=resolve(bind[2])
                    continue
                draw=re.match(r'drawindexed(instanced)?\s*=\s*(.+)',line,re.I)
                if draw:
                    ordinal+=1
                    if not vb or not ib:continue
                    args=[x.strip() for x in draw[2].split(',')]
                    def resolve_int(token):
                        try:return int(token,0)
                        except (TypeError,ValueError):pass
                        result=eval_condition(token,variables,numeric=True)
                        if result is None or isinstance(result,bool):return None
                        rounded=round(result)
                        return int(rounded) if abs(result-rounded)<1e-6 else None
                    try:count=int(args[0]);start=int(args[2] if draw[1] else args[1]);base=int(args[3] if draw[1] else args[2])
                    except (ValueError,IndexError):
                        try:count=resolve_int(args[0]);start=resolve_int(args[2] if draw[1] else args[1]);base=resolve_int(args[3] if draw[1] else args[2])
                        except IndexError:continue
                        if count is None or start is None or base is None:continue
                    width=2 if 'R16_UINT' in ib.get('format','').upper() else 4
                    available=(Path(ib['path']).stat().st_size//width)-start
                    if available<=0:continue
                    if count>available:
                        count=(available//3)*3
                        errors.append(f'{ini.name}: {secname} draw {ordinal} 索引缓冲截断，预览 {count}/{args[0]}')
                        if count<3:continue
                    if count<3 or count%3 or vb['stride']<12 or vb['stride']>128:continue
                    key=hashlib.sha256(
                        f'{record_ini}|{record_section}|{origin_run_index}|{secname}|{ordinal}|{vb["path"]}|{ib["path"]}|'
                        f'{count}|{start}|{base}|{bool(draw[1])}'.encode()
                    ).hexdigest()[:18]
                    if key in seen_records:continue
                    seen_records.add(key)
                    visible=inherited_visible and active
                    conditional=inherited_conditional or bool(stack)
                    geometry_key=(fingerprint(vb['path']),fingerprint(ib['path']),
                        count,start,base,bool(draw[1]))
                    record={'id':key,'ini':str(record_ini),'section':record_section,'drawIni':str(ini),
                        'drawSection':secname,'drawOrdinal':ordinal,'runIndex':origin_run_index,
                        'vb':vb,'ib':ib,'count':count,'start':start,'base':base,'instanced':bool(draw[1]),
                        'label':label or record_section,'defaultVisible':visible,'conditional':conditional,
                        'draw':line,'run':origin_run_line}
                    if geometry_key in seen_geometry:
                        existing=seen_geometry[geometry_key]
                        if visible and not existing['defaultVisible']:
                            records[records.index(existing)]=record
                            seen_geometry[geometry_key]=record
                        continue
                    seen_geometry[geometry_key]=record
                    records.append(record)
                    continue
                command=re.match(r'run\s*=\s*(commandlist.+)$',line,re.I)
                if command:
                    run_ordinal+=1
                    target=command_target(command[1].strip(),namespace)
                    target_info=command_sections.get(target)
                    if target_info:
                        trace_section(*target_info,vb=vb,ib=ib,label=label,
                            inherited_visible=inherited_visible and active,
                            inherited_conditional=inherited_conditional or bool(stack),chain=chain,
                            call_origin=(ini,secname,run_ordinal,line))

        for ini,sec,local,variables,namespace in parsed:
            for secname,body,_ in sec:
                trace_section(ini,secname,body,local,variables,namespace)
        model_id=hashlib.sha256(str(folder).encode()).hexdigest()[:16]
        key=folder.relative_to(MODS).as_posix()
        display=key if key!='.' else 'Mods（根目录）'
        model={'id':model_id,'name':display,'profile_key':key,'folder':str(folder),'groups':records,'errors':errors,
            'timeAxis':detect_time_axis(ini_files)}
        if records:
            models[model_id]=model
            catalog.append({'id':model_id,'name':display,'groups':len(records),'status':'ready','errors':errors[:3]})
    MODELS=models;CATALOG={'root':str(MODS),'mods':catalog}
    return CATALOG

def positions(resource):
    path=Path(resource['path']);stride=resource['stride'];key=('positions',str(path),path.stat().st_mtime_ns,stride)
    cached=cache_get(DECODED,key)
    if cached is not None:return cached
    raw=path.read_bytes()
    if len(raw)%stride:raise ValueError('顶点缓冲大小不是 stride 的整数倍')
    out=array.array('f')
    for offset in range(0,len(raw),stride):
        p=struct.unpack_from('<3f',raw,offset)
        if not all(math.isfinite(x) and abs(x)<1000 for x in p):raise ValueError('位置数据格式不受支持或包含非有限值')
        out.extend(p)
    remember_limited(DECODED,key,out,DECODED_LIMIT);return out

def indices(group, vertex_count):
    path=Path(group['ib']['path']);fmt=group['ib']['format'].upper()
    width=2 if 'R16_UINT' in fmt else 4
    if not fmt and Path(group['ib']['path']).suffix=='.ib':width=4
    start=group['start'];count=group['count']
    key=('indices',str(path),path.stat().st_mtime_ns,width,start,count,group['base'],vertex_count)
    cached=cache_get(DECODED,key)
    if cached is not None:return cached
    raw=path.read_bytes()
    if (start+count)*width>len(raw):raise ValueError('Draw 范围超出索引缓冲')
    out=array.array('I',(x[0]+group['base'] for x in struct.iter_unpack('<H' if width==2 else '<I',raw[start*width:(start+count)*width])))
    if not out or min(out)<0 or max(out)>=vertex_count:raise ValueError('顶点索引超出位置缓冲范围')
    remember_limited(DECODED,key,out,DECODED_LIMIT);return out

def anchor_from_profile(model_id, group, profile, offset_bytes):
    index=profile['triangle_first_index'];bary=profile['barycentric']
    triangle=(index-group['start'])//3
    _,_,candidate=validate_anchor({'model':model_id,'group':group['id'],'triangle':triangle,'barycentric':bary})
    a,b,c=candidate['triangle_vertices'];t=unit(sub(b,a));n=unit(cross(sub(b,a),sub(c,a)));bt=cross(n,t)
    f=profile.get('rest_forward_coefficients',[0,0,1]);u=profile.get('rest_up_coefficients',[0,1,0])
    forward=unit([t[k]*f[0]+bt[k]*f[1]+n[k]*f[2] for k in range(3)])
    up_hint=unit([t[k]*u[0]+bt[k]*u[1]+n[k]*u[2] for k in range(3)])
    right=unit(cross(up_hint,forward));up=unit(cross(forward,right))
    pid=profile['id'];off=struct.unpack_from('<4f',offset_bytes,pid*16)[:3] if len(offset_bytes)>=(pid+1)*16 else [0,0,0]
    candidate['offset']=[right[k]*off[0]+up[k]*off[1]+forward[k]*off[2] for k in range(3)]
    candidate['direction']=[-x for x in up];candidate['sourceProfile']=pid
    return candidate

def geometry(model_id):
    sync_profiles()
    cached=cache_get(CACHE,model_id)
    if cached is not None:return cached
    model=MODELS[model_id]; blobs=[]; meshes=[]; asset_cache={}; offset=0; valid=[]; errors=[]
    for group in model['groups']:
        try:
            key=(group['vb']['path'],group['vb']['stride'])
            if key not in asset_cache:
                p=positions(group['vb']); b=p.tobytes();asset_cache[key]=(p,len(meshes))
                meshes.append({'offset':offset,'length':len(b),'vertices':len(p)//3});blobs.append(b);offset+=len(b)
            p,mesh=asset_cache[key];idx=indices(group,len(p)//3);b=idx.tobytes()
            valid.append({**{k:group[k] for k in ('id','label','section','count','start','base','defaultVisible','conditional')},
                          'mesh':mesh,'offset':offset,'length':len(b),'ini':str(Path(group['ini']).relative_to(MODS)),
                          'vertexFile':str(Path(group['vb']['path']).relative_to(MODS))})
            blobs.append(b);offset+=len(b)
        except (OSError,ValueError,struct.error,OverflowError) as e:errors.append({'group':group['label'],'error':str(e)})
    saved_anchor=profile_store().saved_anchor(profile_name(model))
    if saved_anchor and saved_anchor.get('group') not in {g['id'] for g in valid}:saved_anchor=None
    if saved_anchor:saved_anchor={**saved_anchor,'model':model_id}
    nozzles={}
    saved_nozzles=profile_store().saved_nozzles(profile_name(model))
    if isinstance(saved_nozzles,list):
        valid_groups={group['id'] for group in valid}
        for index,item in enumerate(saved_nozzles[:4]):
            anchor=item.get('anchor')
            if not isinstance(anchor,dict) or anchor.get('group') not in valid_groups:continue
            slot=max(0,min(3,int(item.get('slot',index))))
            try:interval=normalize_nozzle_interval(item.get('intervalSeconds'))
            except ValueError:interval=None
            nozzles[slot]=(0,{'slot':slot,'anchor':{**anchor,'model':model_id},
                'sourceProfile':item.get('sourceProfile',anchor.get('sourceProfile')),'intervalSeconds':interval})
    if not nozzles and profile_store().path(profile_name(model)).exists():
        profiles=read_json(PROBE/'surface/profiles.json',{'profiles':[]})['profiles']
        offset_path=PROBE/'surface/attachment-offsets.buf'
        offset_bytes=offset_path.read_bytes() if offset_path.exists() else b''
        valid_ids={g['id'] for g in valid}
        for group in model['groups']:
            if group['id'] not in valid_ids:continue
            for profile in profiles:
                if not profile.get('enabled',True) or profile.get('selection_error',0)>100:continue
                if any(str(profile.get(key,'')).lower()!=str(expected).lower() for key,expected in [('ini',group['ini']),('vb',group['vb']['path']),('ib',group['ib']['path'])]):continue
                index=profile['triangle_first_index']
                if profile.get('base_vertex',0)!=group['base'] or not group['start']<=index<group['start']+group['count']:continue
                try:
                    candidate=anchor_from_profile(model_id,group,profile,offset_bytes)
                    slot=max(0,min(3,int(profile.get('nozzle_slot',0))))
                    rank=(not group['defaultVisible'],profile.get('selection_error',0))
                    if slot not in nozzles or rank<nozzles[slot][0]:
                        try:interval=normalize_nozzle_interval(profile.get('nozzle_interval_seconds'))
                        except ValueError:interval=None
                        nozzles[slot]=(rank,{'slot':slot,'anchor':candidate,'sourceProfile':profile['id'],
                            'intervalSeconds':interval})
                except (ValueError,IndexError,KeyError):continue
    nozzle_list=[value[1] for _,value in sorted(nozzles.items())]
    if saved_anchor and not any(item['slot']==0 for item in nozzle_list):
        nozzle_list.insert(0,{'slot':0,'anchor':saved_anchor,'sourceProfile':saved_anchor.get('sourceProfile'),
            'intervalSeconds':None})
    if not saved_anchor and nozzle_list:saved_anchor=nozzle_list[0]['anchor']
    info={'id':model_id,'name':model['name'],'meshes':meshes,'groups':valid,'errors':errors,'timeAxis':model.get('timeAxis'),
          'anchor':saved_anchor,'nozzles':nozzle_list,'coordinateSystem':'原始网格，Z 向上，-Y 向前',
          'effectColor':profile_store().saved_effect_color(profile_name(model)),
          'effectStrength':profile_store().saved_effect_strength(profile_name(model)),
          'integration':model_integration_status(model)}
    header=json.dumps(info,ensure_ascii=False).encode('utf-8');data=struct.pack('<I',len(header))+header+b''.join(blobs)
    remember_limited(CACHE,model_id,data,CACHE_LIMIT);return data

def sub(a,b):return [a[i]-b[i] for i in range(3)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def unit(v):
    length=math.sqrt(dot(v,v))
    if length<1e-10:raise ValueError('退化三角形')
    return [x/length for x in v]

def normalize_nozzle_interval(value):
    if value is None or value=='':return None
    try:interval=float(value)
    except (TypeError,ValueError):raise ValueError('喷口间隔必须是秒数')
    if not math.isfinite(interval) or interval<0:raise ValueError('喷口间隔必须是有效的秒数')
    if interval==0:return None
    if not NOZZLE_INTERVAL_MIN<=interval<=NOZZLE_INTERVAL_MAX:
        raise ValueError(f'喷口间隔必须在 {NOZZLE_INTERVAL_MIN:g}–{NOZZLE_INTERVAL_MAX:g} 秒之间')
    return round(interval,3)

def validate_anchor(data):
    model=MODELS[data['model']]; group=next(g for g in model['groups'] if g['id']==data['group'])
    triangle=int(data['triangle']);bary=[float(x) for x in data['barycentric']]
    if len(bary)!=3 or min(bary)<-1e-5 or max(bary)>1.00001 or abs(sum(bary)-1)>.0001:raise ValueError('重心坐标无效')
    p=positions(group['vb']);idx=indices(group,len(p)//3)
    if triangle<0 or triangle*3+2>=len(idx):raise ValueError('三角形索引无效')
    ids=list(idx[triangle*3:triangle*3+3]);tri=[list(p[i*3:i*3+3]) for i in ids]
    point=[sum(tri[k][a]*bary[k] for k in range(3)) for a in range(3)]
    offset=[float(x) for x in data.get('offset',[0,0,0])];direction=unit([float(x) for x in data.get('direction',[0,0,-1])])
    if len(offset)!=3 or not all(math.isfinite(x) and abs(x)<=.25 for x in offset):raise ValueError('偏移上限为 25 cm')
    return model,group,{'model':data['model'],'group':group['id'],'triangle':triangle,'barycentric':bary,
        'point':point,'offset':offset,'direction':direction,'vertex_ids':ids,'triangle_vertices':tri,
        'updated':datetime.now().isoformat(timespec='seconds')}

def save_anchor(data):
    model,group,anchor=validate_anchor(data)
    path=profile_store().save_anchor(profile_name(model),anchor)
    CACHE.pop(model['id'],None)
    return {'saved':True,'anchor':anchor,'path':str(path)}

def save_nozzles(data):
    model=MODELS[data['model']]
    requested=data.get('nozzles')
    if not isinstance(requested,list) or not 1<=len(requested)<=4:raise ValueError('喷口数量必须为 1–4 个')
    drafts=[];seen=set()
    for index,item in enumerate(requested):
        slot=int(item.get('slot',index))
        if slot<0 or slot>=4 or slot in seen:raise ValueError('喷口槽位无效')
        seen.add(slot)
        _,group,anchor=validate_anchor({**item,'model':model['id']})
        drafts.append({'slot':slot,'anchor':{**anchor,'model':model['id']},
            'sourceProfile':item.get('sourceProfile'),'intervalSeconds':normalize_nozzle_interval(item.get('intervalSeconds'))})
    drafts.sort(key=lambda item:item['slot'])
    if [item['slot'] for item in drafts]!=list(range(len(drafts))):raise ValueError('喷口槽位必须从 0 连续排列')
    path=profile_store().save_nozzles(profile_name(model),drafts,drafts[0]['anchor'])
    CACHE.pop(model['id'],None)
    return {'saved':True,'nozzles':len(drafts),'path':str(path)}

def model_integration_status(model):
    saved=profile_store().saved_nozzles(profile_name(model))
    if not isinstance(saved,list) or not saved:return {'configured':False,'installed':False,'hooks':0,'expected':0}
    profiles=read_json(PROBE/'surface/profiles.json',{'profiles':[]})['profiles']
    hooks=0;errors=[]
    for item in saved[:4]:
        pid=item.get('sourceProfile',item.get('anchor',{}).get('sourceProfile'))
        if type(pid)!=int or not 0<=pid<len(profiles):continue
        ini=profiles[pid].get('ini')
        if not ini:continue
        try:
            text=Path(ini).read_text(encoding='utf-8-sig',errors='replace')
        except OSError as error:
            errors.append(str(error));continue
        pattern=rf'^[ \t]*x243\s*=\s*{pid}\s*$'
        match=re.search(pattern,text,re.M|re.I)
        if match and re.search(r'CommandList\\LiquidGravityProbe\\CaptureSurface',text[match.start():match.start()+600],re.I):
            hooks+=1
    expected=len(saved)
    return {'configured':True,'installed':hooks==expected,'hooks':hooks,'expected':expected,'errors':errors}

def save_model_effect(data):
    model=MODELS[data['model']]
    color=data.get('color')
    strength=data.get('strength',1)
    path=profile_store().save_effect_settings(profile_name(model),color,strength)
    sync_profiles()
    CACHE.pop(model['id'],None)
    return {'saved':True,'color':profile_store().saved_effect_color(profile_name(model)),
        'strength':profile_store().saved_effect_strength(profile_name(model)),'path':str(path)}

def reinject_model(data):
    model=MODELS[data['model']]
    saved=profile_store().saved_nozzles(profile_name(model))
    if not isinstance(saved,list) or not saved:
        raise ValueError('当前 Mod 没有保存过喷口，无法重新注入')
    nozzles=[]
    for index,item in enumerate(saved[:4]):
        anchor=item.get('anchor')
        if not isinstance(anchor,dict):continue
        nozzles.append({**anchor,'model':model['id'],'slot':int(item.get('slot',index)),
            'intervalSeconds':item.get('intervalSeconds')})
    if not nozzles:raise ValueError('当前 Mod 的喷口配置无效')
    return apply_nozzles({'model':model['id'],'nozzles':nozzles})

def remove_model_effect(data):
    return apply_nozzles({'model':data['model'],'nozzles':[]})

def draw_ini_path(group):
    return Path(group.get('drawIni') or group['ini']).resolve()

def validate_group_call(text, group):
    if not group.get('run') or not group.get('runIndex'):
        return
    matches=[x for x in sections(text) if x[0].casefold()==group['section'].casefold()]
    if len(matches)!=1:
        raise ValueError('Call 段已变化，需要重新扫描')
    _,body,_=matches[0]
    calls=list(re.finditer(r'^[ \t]*run\s*=\s*CommandList\\.*$',body,re.M|re.I))
    run_index=int(group['runIndex'])
    target=calls[run_index-1] if 0<run_index<=len(calls) else None
    if target is None or clean(target[0]).casefold()!=clean(group['run']).casefold():
        raise ValueError('Call 已变化，需要重新扫描')

def locate_group_draw(text, group):
    section_name=group.get('drawSection') or group['section']
    matches=[x for x in sections(text) if x[0].casefold()==section_name.casefold()]
    if len(matches)!=1:
        raise ValueError(f'Draw 段已变化，需要重新扫描（{section_name}: {len(matches)} 个同名段）')
    secname,body,body_offset=matches[0]
    draws=list(re.finditer(r'^[ \t]*drawindexed(?:instanced)?\s*=.*$',body,re.M|re.I))
    draw_index=int(group.get('drawOrdinal') or group.get('ordinal') or 0)
    target=draws[draw_index-1] if 0<draw_index<=len(draws) else None
    if target is None or clean(target[0]).casefold()!=clean(group['draw']).casefold():
        actual=clean(target[0]) if target is not None else '未找到'
        raise ValueError(f'Draw 已变化，需要重新扫描（{section_name} #{draw_index}/{len(draws)}，当前：{actual}）')
    return secname,body,body_offset,target

def apply_anchor(data):
    sync_profiles()
    model,group,anchor=validate_anchor(data)
    profile_path=PROBE/'surface/profiles.json';buf_path=PROBE/'surface/profiles.buf'
    profiles_doc=read_json(profile_path,{'profiles':[]});profiles=profiles_doc['profiles']
    raw=bytearray(buf_path.read_bytes());offset_path=PROBE/'surface/attachment-offsets.buf';offsets=bytearray(offset_path.read_bytes())
    registry_path=PROBE/'surface/attachment-models.json';registry=read_json(registry_path,{'models':[]})
    if len(raw)!=len(profiles)*48 or len(offsets)!=len(profiles)*16:raise ValueError('已安装的配置缓冲与 JSON 不匹配，未修改任何文件')
    draw_ini=draw_ini_path(group)
    if group.get('run'):
        validate_group_call(Path(group['ini']).read_text(encoding='utf-8-sig'),group)
    text=draw_ini.read_text(encoding='utf-8-sig')
    secname,body,body_offset,draw=locate_group_draw(text,group)
    prefix=body[:draw.start()]
    hook=re.search(r'x243\s*=\s*(\d+)\s*\n\s*y243\s*=\s*\d+\s*\n\s*z243\s*=\s*-?\d+\s*\n(?:\s*w243\s*=\s*\d+\s*\n)?\s*run\s*=\s*CommandList\\LiquidGravityProbe\\CaptureSurface\s*$',prefix,re.I)
    first=group['start']+anchor['triangle']*3
    pid=int(hook[1]) if hook else reusable_profile_slot(profiles)
    if not hook:
        same=next((i for i,p in enumerate(profiles)
            if Path(p.get('ini','')).resolve()==draw_ini
            and p.get('vb')==group['vb']['path'] and p.get('ib')==group['ib']['path']
            and p.get('triangle_first_index')==first),None)
        if same is not None:pid=same

    if pid>=len(profiles) or not profiles[pid].get('ini') or profiles[pid].get('status')=='removed_nozzle':
        pid=reusable_profile_slot(profiles)
    elif Path(profiles[pid]['ini']).resolve()!=draw_ini:
        owned=[i for i,p in enumerate(profiles) if p.get('ini') and Path(p['ini']).resolve()==draw_ini]
        exact=next((i for i in owned if profiles[i].get('component')==group['section'] and profiles[i].get('triangle_first_index')==first),None)
        pid=exact if exact is not None else reusable_profile_slot(profiles)
    a,b,c=anchor['triangle_vertices'];tangent=unit(sub(b,a));normal=unit(cross(sub(b,a),sub(c,a)));bitangent=cross(normal,tangent)
    # Make the selected emission axis the local down axis. This edits this model only.
    up=[-x for x in anchor['direction']];reference=[0,-1,0]
    if abs(dot(up,reference))>.95:reference=[1,0,0]
    right=unit(cross(up,reference));forward=unit(cross(right,up))
    f=[dot(forward,x) for x in (tangent,bitangent,normal)];u=[dot(up,x) for x in (tangent,bitangent,normal)]
    priority=min([0.]+[float(p.get('selection_error',0)) for p in profiles if str(p.get('ini','')).lower()==str(draw_ini).lower()])-1
    row=struct.pack('<12f',priority,*anchor['barycentric'],*f,0.,*u,0.)
    # Offset sliders use source XYZ; runtime offset order is right/up/forward.
    local_offset=[dot(anchor['offset'],x) for x in (right,up,forward)]
    entry={**(profiles[pid] if pid<len(profiles) else {}),'id':pid,'ini':str(draw_ini),
        'component':group['section'],'label':group['label'],'triangle_first_index':first,'base_vertex':group['base'],
        'vertex_ids':anchor['vertex_ids'],'barycentric':anchor['barycentric'],'rest_position':anchor['point'],
        'rest_forward_coefficients':f,'rest_up_coefficients':u,'selection_error':priority,'enabled':True,
        'status':'manually_selected_surface_needs_in_game_validation','vb':group['vb']['path'],'ib':group['ib']['path'],'instanced':group['instanced']}
    if pid==len(profiles):profiles.append(entry);raw.extend(row);offsets.extend(struct.pack('<4f',*local_offset,0))
    else:profiles[pid]=entry;raw[pid*48:pid*48+48]=row;offsets[pid*16:pid*16+16]=struct.pack('<4f',*local_offset,0)
    indent=re.match(r'[ \t]*',draw[0])[0]
    insert=f'{indent}x243 = {pid}\n{indent}y243 = {first}\n{indent}z243 = {group["base"]}\n{indent}w243 = 0\n{indent}run = CommandList\\LiquidGravityProbe\\CaptureSurface\n'
    start=body_offset+(hook.start() if hook else draw.start());end=body_offset+(hook.end() if hook else draw.start())
    placeholder=f'__LGP_HOOK_{TOKEN}__'
    patched=text[:start]+placeholder+text[end:]
    if pid<len(profiles):
        owned_hook=re.compile(
            rf'^[ \t]*x243\s*=\s*{pid}\s*\r?\n[ \t]*y243\s*=\s*\d+\s*\r?\n'
            rf'[ \t]*z243\s*=\s*-?\d+\s*\r?\n(?:[ \t]*w243\s*=\s*\d+\s*\r?\n)?[ \t]*run\s*=\s*CommandList\\LiquidGravityProbe\\CaptureSurface[ \t]*\r?\n',
            re.M|re.I)
        patched=owned_hook.sub('',patched)
    patched=patched.replace(placeholder,insert)
    # Keep an individually selected draw separate from the model-wide fallback offset.
    old_model=next((m for m in registry.get('models',[]) if pid in m['profile_ids']),None)
    if old_model:old_model['profile_ids'].remove(pid)
    registry['models']=[m for m in registry.get('models',[]) if m['profile_ids']]
    registry['models'].append({'model_key':model['name']+' / '+group['id'],'kind':'replacement','source_ini':str(draw_ini),
        'profile_ids':[pid],'offset_local_m':local_offset,'attachment':'manually_selected_surface_triangle'})
    updates={profile_path:json.dumps(profiles_doc,ensure_ascii=False,indent=2).encode('utf-8'),
             buf_path:bytes(raw),offset_path:bytes(offsets),
             registry_path:json.dumps(registry,ensure_ascii=False,indent=2).encode('utf-8')}
    updates.update(profile_store().model_updates(profile_name(model),profiles_doc,raw,offsets,anchor,pid))
    updates[draw_ini]=patched.encode('utf-8-sig')
    commit_updates(updates)
    save_anchor(data)
    return {'applied':True,'profile':pid,'message':'请按 F10 应用。本次只修改了所选的绘制项。'}

def strip_capture_hooks(text, profile_ids):
    for pid in profile_ids:
        pattern=re.compile(
            rf'^[ \t]*x243\s*=\s*{pid}\s*\r?\n'
            rf'[ \t]*y243\s*=\s*\d+\s*\r?\n'
            rf'[ \t]*z243\s*=\s*-?\d+\s*\r?\n'
            rf'(?:[ \t]*w243\s*=\s*\d+\s*\r?\n)?'
            rf'[ \t]*run\s*=\s*CommandList\\LiquidGravityProbe\\CaptureSurface[ \t]*\r?\n',
            re.M|re.I,
        )
        text=pattern.sub('',text)
    return text

def active_nozzle_count(profiles):
    slots=[
        int(profile.get('nozzle_slot',0))+1
        for profile in profiles
        if profile.get('enabled',True) and profile.get('ini')
    ]
    return max(1,min(4,max(slots,default=0)))

def set_probe_global(text, key, value):
    eol='\r\n' if '\r\n' in text else '\n'
    pattern=re.compile(r'^(?P<indent>[ \t]*)global[ \t]+\$'+re.escape(key)+r'[ \t]*=[ \t]*[^\r\n]*(?P<eol>\r?\n)?$',re.M)
    lines=text.splitlines(keepends=True)
    in_constants=False
    constants_index=None
    matches=[]
    for index,line in enumerate(lines):
        section=re.match(r'^[ \t]*\[([^\]]+)\]',line)
        if section:
            in_constants=section.group(1).strip().lower()=='constants'
            if in_constants:constants_index=index+1
            continue
        if pattern.match(line.rstrip('\r\n')):
            matches.append((index,in_constants))
    if len(matches)>1:raise ValueError(f'Probe.ini 中 ${key} 定义数量异常：{len(matches)}')
    if constants_index is None:
        prefix=f'[Constants]{eol}'
        if text and not text.endswith(('\n','\r')):prefix=eol+prefix
        return prefix+f'global ${key} = {value}{eol}'+text
    replacement=f'global ${key} = {value}{eol}'
    if len(matches)==1:
        index,inside=matches[0]
        if inside:
            indent=re.match(r'^[ \t]*',lines[index]).group(0)
            lines[index]=f'{indent}global ${key} = {value}{eol}'
            return ''.join(lines)
        del lines[index]
        if index<constants_index:constants_index-=1
    lines.insert(constants_index,replacement)
    return ''.join(lines)

def apply_nozzles(data):
    sync_profiles()
    model=MODELS[data['model']]
    requested=data.get('nozzles')
    if not isinstance(requested,list) or len(requested)>4:raise ValueError('喷口数量必须为 0–4 个')
    anchors=[];seen=set()
    for item in requested:
        try:slot=int(item.get('slot'))
        except (TypeError,ValueError):raise ValueError('喷口槽位无效')
        if slot<0 or slot>=4 or slot in seen:raise ValueError('喷口槽位必须唯一且在 0–3 之间')
        seen.add(slot)
        _,group,anchor=validate_anchor({**item,'model':data['model']})
        anchors.append((slot,group,anchor,normalize_nozzle_interval(item.get('intervalSeconds'))))
    anchors.sort(key=lambda item:item[0])
    if [item[0] for item in anchors]!=list(range(len(anchors))):raise ValueError('喷口槽位必须从 0 连续排列')

    profile_path=PROBE/'surface/profiles.json';buf_path=PROBE/'surface/profiles.buf'
    profiles_doc=read_json(profile_path,{'profiles':[]});profiles=profiles_doc['profiles']
    raw=bytearray(buf_path.read_bytes());offset_path=PROBE/'surface/attachment-offsets.buf';offsets=bytearray(offset_path.read_bytes())
    registry_path=PROBE/'surface/attachment-models.json';registry=read_json(registry_path,{'models':[]})
    if len(raw)!=len(profiles)*48 or len(offsets)!=len(profiles)*16:raise ValueError('已安装的配置缓冲与 JSON 不匹配，未修改任何文件')

    model_folder=Path(model['folder']).resolve()
    owned={};old_ini_by_pid={}
    for pid,profile in enumerate(profiles):
        ini=profile.get('ini')
        if not ini:continue
        try:inside=Path(ini).resolve().is_relative_to(model_folder)
        except ValueError:inside=False
        if inside:
            slot=int(profile.get('nozzle_slot',0))
            if slot not in owned:owned[slot]=pid
            old_ini_by_pid[pid]=Path(ini).resolve()
    old_pids=set(owned.values());assigned={};new_pids=[]
    for slot,group,anchor,interval_seconds in anchors:
        pid=owned.get(slot)
        if pid is None or pid in assigned:
            pid=reusable_profile_slot(profiles)
            if pid==len(profiles):
                profiles.append({'id':pid});raw.extend(b'\0'*48);offsets.extend(b'\0'*16)
        assigned[slot]=pid;new_pids.append(pid)
        a,b,c=anchor['triangle_vertices'];tangent=unit(sub(b,a));normal=unit(cross(sub(b,a),sub(c,a)));bitangent=cross(normal,tangent)
        up=[-x for x in anchor['direction']];reference=[0,-1,0]
        if abs(dot(up,reference))>.95:reference=[1,0,0]
        right=unit(cross(up,reference));forward=unit(cross(right,up))
        f=[dot(forward,x) for x in (tangent,bitangent,normal)];u=[dot(up,x) for x in (tangent,bitangent,normal)]
        draw_ini=draw_ini_path(group)
        priority=min([0.]+[float(p.get('selection_error',0)) for p in profiles if str(p.get('ini','')).lower()==str(draw_ini).lower()])-1
        row=struct.pack('<12f',priority,*anchor['barycentric'],*f,0.,*u,0.)
        local_offset=[dot(anchor['offset'],x) for x in (right,up,forward)]
        entry={**(profiles[pid] if pid<len(profiles) else {}),'id':pid,'ini':str(draw_ini),
            'component':group['section'],'label':group['label'],'triangle_first_index':group['start']+anchor['triangle']*3,
            'base_vertex':group['base'],'vertex_ids':anchor['vertex_ids'],'barycentric':anchor['barycentric'],
            'rest_position':anchor['point'],'rest_forward_coefficients':f,'rest_up_coefficients':u,
            'selection_error':priority,'enabled':True,'status':'manually_selected_surface_needs_in_game_validation',
            'vb':group['vb']['path'],'ib':group['ib']['path'],'instanced':group['instanced'],'nozzle_slot':slot,
            'nozzle_interval_seconds':interval_seconds}
        profiles[pid]=entry
        raw[pid*48:(pid+1)*48]=row
        offsets[pid*16:(pid+1)*16]=struct.pack('<4f',*local_offset,0)

    obsolete=old_pids-set(new_pids)
    for pid in obsolete:
        profiles[pid]={**profiles[pid],'enabled':False,'selection_error':1000000.,'status':'removed_nozzle'}
        raw[pid*48:(pid+1)*48]=struct.pack('<12f',1000000.,1/3,1/3,1/3,0.,0.,0.,0.,0.,0.,0.,0.)
        offsets[pid*16:(pid+1)*16]=struct.pack('<4f',0.,0.,0.,0.)

    affected_pids=old_pids|set(new_pids)
    ini_paths={draw_ini_path(group) for _,group,_,_ in anchors}
    for _,group,_,_ in anchors:
        outer_ini=Path(group['ini']).resolve()
        if group.get('run'):
            validate_group_call(outer_ini.read_text(encoding='utf-8-sig'),group)
    ini_paths.update(old_ini_by_pid.values())
    ini_updates={}
    for ini_path in ini_paths:
        raw_ini=ini_path.read_bytes();bom=raw_ini.startswith(codecs.BOM_UTF8)
        text=raw_ini.decode('utf-8-sig')
        text=strip_capture_hooks(text,affected_pids)
        insertions=[]
        for slot,group,anchor,_ in anchors:
            if draw_ini_path(group)!=ini_path:continue
            secname,body,body_offset,target=locate_group_draw(text,group)
            eol='\r\n' if '\r\n' in text else '\n';indent=re.match(r'[ \t]*',target[0])[0];pid=assigned[slot]
            hook=(f'{indent}x243 = {pid}{eol}{indent}y243 = {group["start"]+anchor["triangle"]*3}{eol}'
                  f'{indent}z243 = {group["base"]}{eol}{indent}w243 = {slot}{eol}'
                  f'{indent}run = CommandList\\LiquidGravityProbe\\CaptureSurface{eol}')
            insertions.append((body_offset+target.start(),hook))
        for offset,hook in sorted(insertions,reverse=True):text=text[:offset]+hook+text[offset:]
        ini_updates[ini_path]=(codecs.BOM_UTF8 if bom else b'')+text.encode('utf-8')

    registry['models']=[item for item in registry.get('models',[]) if not affected_pids.intersection(item.get('profile_ids',[]))]
    for slot,group,anchor,_ in anchors:
        pid=assigned[slot];off=struct.unpack_from('<4f',offsets,pid*16)[:3]
        registry['models'].append({'model_key':model['name']+' / '+str(pid),'kind':'replacement','source_ini':str(draw_ini_path(group)),
            'profile_ids':[pid],'offset_local_m':list(off),'attachment':'manually_selected_surface_triangle','nozzle_slot':slot})
    metadata_updates={profile_path:json.dumps(profiles_doc,ensure_ascii=False,indent=2).encode('utf-8'),
        buf_path:bytes(raw),offset_path:bytes(offsets),registry_path:json.dumps(registry,ensure_ascii=False,indent=2).encode('utf-8')}
    stored_anchor=anchors[0][2] if anchors else None
    metadata_updates.update(profile_store().model_updates(profile_name(model),profiles_doc,raw,offsets,stored_anchor,new_pids,exclusive=True))
    probe_path=PROBE/'Probe.ini';probe_raw=probe_path.read_bytes();probe_text=probe_raw.decode('utf-8-sig')
    axis=model.get('timeAxis')
    if axis:
        probe_text=set_probe_global(probe_text,'anim_fps',axis['fps'])
        probe_text=set_probe_global(probe_text,'anim_frames',axis['frames'])
    probe_text=set_probe_global(probe_text,'nozzle_count',active_nozzle_count(profiles))
    intervals=[0.]*4
    for slot,_,_,interval_seconds in anchors:
        intervals[slot]=interval_seconds or 0.
    for slot,value in enumerate(intervals):probe_text=set_probe_global(probe_text,f'nozzle_interval_{slot}',format(value,'.6g'))
    metadata_updates[probe_path]=(codecs.BOM_UTF8 if probe_raw.startswith(codecs.BOM_UTF8) else b'')+probe_text.encode('utf-8')
    updates=dict(metadata_updates)
    if anchors:updates.update(ini_updates)
    else:updates={**ini_updates,**metadata_updates}

    commit_updates(updates)
    CACHE.clear();DECODED.clear()
    drafts=[{'slot':slot,'anchor':{**anchor,'model':model['id']},'sourceProfile':assigned[slot],
        'intervalSeconds':interval_seconds} for slot,_,anchor,interval_seconds in anchors]
    profile_store().save_nozzles(profile_name(model),drafts,stored_anchor)
    sync_profiles()
    message='粒子效果已从当前 Mod 移除。请按 F10 恢复默认。' if not anchors else '多喷口配置已应用。请按 F10。'
    return {'applied':True,'profiles':new_pids,'nozzle_count':len(anchors),'message':message}

def cleanup_all(data):
    global STORE
    result=cleanup_probe_data(
        MODS,
        PROBE,
        preview=bool(data.get('preview')),
        confirmation=data.get('confirm'),
    )
    if not result['preview']:
        STORE=None
        result['catalog']=scan()
    return result

def save_effects(data):
    values=data.get('values')
    if not isinstance(values,dict):raise ValueError('效果参数格式无效')
    previous=read_effect_settings(PROBE)['values']
    result=write_effect_settings(PROBE,values)
    if int(previous.get('particle_pool',1024))!=int(result['values']['particle_pool']):
        install_runtime_assets()
    return result

def save_key_binding(data):
    return write_key_binding(PROBE,data)

class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print(f"[{self.log_date_time_string()}] {format % args}", flush=True)
    def reply(self,status,body,content_type='application/json; charset=utf-8'):
        if not isinstance(body,bytes):body=json.dumps(body,ensure_ascii=False).encode('utf-8')
        origin=self.headers.get('Origin','')
        self.send_response(status);self.send_header('Content-Type',content_type);self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Content-Security-Policy',FRAME_CSP)
        self.send_header('Referrer-Policy','no-referrer')
        self.send_header('Permissions-Policy','camera=(), microphone=(), geolocation=(), clipboard-read=(), clipboard-write=()')
        if origin in ALLOWED_FRAME_ORIGINS:
            self.send_header('Access-Control-Allow-Origin',origin)
            self.send_header('Vary','Origin')
        self.end_headers();self.wfile.write(body)
    def do_OPTIONS(self):
        origin=self.headers.get('Origin','')
        self.send_response(204)
        if origin in ALLOWED_FRAME_ORIGINS:self.send_header('Access-Control-Allow-Origin',origin)
        self.send_header('Access-Control-Allow-Methods','GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers','Content-Type, X-Editor-Token')
        self.send_header('Access-Control-Max-Age','600')
        self.send_header('Vary','Origin')
        self.send_header('Content-Length','0')
        self.end_headers()
    def do_GET(self):
        route=urlsplit(self.path)
        try:
            if route.path.startswith('/ui/'):
                relative=unquote(route.path[len('/ui/'):]) or 'index.html'
                target=(UI_ROOT/relative).resolve()
                if not target.is_relative_to(UI_ROOT.resolve()) or not target.is_file():
                    return self.reply(404,{'error':'未找到'})
                content_type=mimetypes.guess_type(target.name)[0] or 'application/octet-stream'
                return self.reply(200,target.read_bytes(),content_type)
            if self.headers.get('X-Editor-Token')!=TOKEN:
                return self.reply(403,{'error':'本地编辑器令牌无效'})
            if route.path=='/api/health':
                return self.reply(200,{'service':'liquid-gravity-probe-editor','protocolVersion':1,'pid':os.getpid(),'instanceToken':TOKEN})
            if route.path=='/api/catalog':return self.reply(200,CATALOG)
            if route.path=='/api/effects':return self.reply(200,read_effect_settings(PROBE))
            if route.path=='/api/keybind':return self.reply(200,read_key_binding(PROBE))
            if route.path=='/api/runtime':return self.reply(200,runtime_validation(PROBE))
            if route.path=='/api/geometry':return self.reply(200,geometry(parse_qs(route.query)['id'][0]),'application/octet-stream')
            self.reply(404,{'error':'未找到'})
        except Exception as e:
            traceback.print_exc()
            self.reply(400,{'error':str(e)})
    def do_POST(self):
        try:
            if self.headers.get('X-Editor-Token')!=TOKEN:return self.reply(403,{'error':'本地编辑器令牌无效'})
            if int(self.headers.get('Content-Length','0'))>65536:raise ValueError('请求过大')
            data=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))) or b'{}')
            with LOCK:
                action={'/api/save':save_anchor,'/api/save-nozzles':save_nozzles,'/api/apply':apply_anchor,
                    '/api/apply-nozzles':apply_nozzles,'/api/effects':save_effects,'/api/keybind':save_key_binding,
                    '/api/model-effect':save_model_effect,'/api/reinject-model':reinject_model,
                    '/api/remove-model':remove_model_effect,'/api/cleanup':cleanup_all,'/api/rescan':lambda _:scan()}.get(self.path)
                if not action:return self.reply(404,{'error':'未找到'})
                self.reply(200,action(data))
        except Exception as e:
            traceback.print_exc()
            self.reply(400,{'error':str(e)})

def configure(mods, probe, output, instance_token):
    global MODS,PROBE,OUTPUT,STORE,TOKEN
    MODS=Path(mods).resolve();PROBE=Path(probe).resolve();OUTPUT=Path(output).resolve()
    if instance_token:TOKEN=instance_token
    if not MODS.is_dir():raise ValueError('Mods 文件夹不存在：'+str(MODS))
    OUTPUT.mkdir(parents=True,exist_ok=True);STORE=None
    ensure_data_files(PROBE)
    install_runtime_assets()

def embedded_worker(port):
    global EMBEDDED_SERVER
    server=None
    try:
        print('开始加载插件内置编辑器…',flush=True)
        sync_profiles()
        scan()
        if EMBEDDED_STOP.is_set():return
        print(f'扫描完成：{len(MODELS)} 个模型，{sum(len(m["groups"]) for m in MODELS.values())} 个 draw 分组',flush=True)
        server=ThreadingHTTPServer(('127.0.0.1',port),Handler)
        if EMBEDDED_STOP.is_set():
            server.server_close();return
        with EMBEDDED_STATE_LOCK:EMBEDDED_SERVER=server
        print(f'附着点编辑器：http://127.0.0.1:{port}',flush=True)
        server.serve_forever()
    except BaseException:
        traceback.print_exc()
    finally:
        with EMBEDDED_STATE_LOCK:
            if EMBEDDED_SERVER is server:EMBEDDED_SERVER=None
        if server is not None:server.server_close()
        print('插件内置编辑器已停止。',flush=True)

def start_embedded(port,mods,probe,output,instance_token,log_path=''):
    global EMBEDDED_THREAD,EMBEDDED_LOG
    stop_embedded()
    if log_path:
        log=open(log_path,'a',encoding='utf-8',buffering=1)
        EMBEDDED_LOG=log
        sys.stdout=log;sys.stderr=log
    configure(mods,probe,output,instance_token)
    EMBEDDED_STOP.clear()
    EMBEDDED_THREAD=threading.Thread(target=embedded_worker,args=(int(port),),daemon=True,name='LiquidGravityProbeEditor')
    EMBEDDED_THREAD.start()
    return {'ok':True}

def stop_embedded():
    global EMBEDDED_SERVER,EMBEDDED_THREAD,EMBEDDED_LOG
    EMBEDDED_STOP.set()
    with EMBEDDED_STATE_LOCK:server=EMBEDDED_SERVER
    if server is not None:
        server.shutdown()
        server.server_close()
    thread=EMBEDDED_THREAD
    if thread is not None and thread.is_alive():thread.join(timeout=3)
    with EMBEDDED_STATE_LOCK:
        EMBEDDED_SERVER=None;EMBEDDED_THREAD=None
    if EMBEDDED_LOG is not None:
        sys.stdout=sys.__stdout__;sys.stderr=sys.__stderr__
        EMBEDDED_LOG.flush();EMBEDDED_LOG.close();EMBEDDED_LOG=None
    return {'ok':True}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mods',default=str(MODS));parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--probe',default=str(PROBE));parser.add_argument('--output',default='')
    parser.add_argument('--open-browser', action='store_true')
    parser.add_argument('--instance-token',default=os.environ.get('SSMT_LGP_TOKEN',''))
    parser.add_argument('--parent-pid',type=int,default=0)
    args=parser.parse_args()
    output=args.output or str(Path(args.probe)/'AttachmentEditor'/'output')
    configure(args.mods,args.probe,output,args.instance_token)
    if args.parent_pid:
        threading.Thread(target=watch_parent_process,args=(args.parent_pid,),daemon=True,name='SSMTParentWatchdog').start()
    print('正在扫描 Mods：'+str(MODS), flush=True)
    sync_profiles()
    scan()
    print(f'扫描完成：{len(MODELS)} 个模型，{sum(len(m["groups"]) for m in MODELS.values())} 个 draw 分组', flush=True)
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    print(f'附着点编辑器：http://127.0.0.1:{args.port}',flush=True)
    print('请保持此控制台开启。按 Ctrl+C 或关闭窗口即可停止。', flush=True)
    if args.open_browser:
        import webbrowser
        editor_url=f'http://127.0.0.1:{args.port}/ui/#token={TOKEN}&game=EFMI'
        threading.Timer(0.5, lambda: webbrowser.open(editor_url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\n正在停止编辑器…', flush=True)
    finally:
        server.server_close()
        print('编辑器已停止。', flush=True)

if __name__=='__main__':main()
