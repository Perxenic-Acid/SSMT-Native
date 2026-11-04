"""Read and write the animation-synced fluid settings stored in Probe.ini."""
from __future__ import annotations

import codecs
import math
import os
import re
from pathlib import Path

FIELDS = [
    {'key':'anim_sync','label':'启用 40 帧同步','kind':'toggle','group':'时间轴','default':0,'min':0,'max':1,'step':1},
    {'key':'anim_sync_profile','label':'同步配置 ID','kind':'number','group':'时间轴','default':1,'min':0,'max':100000,'step':1},
    {'key':'anim_fps','label':'动画 FPS（自动扫描）','kind':'number','group':'时间轴','default':60,'min':1,'max':240,'step':1,'hidden':True},
    {'key':'anim_frames','label':'总帧数（自动扫描）','kind':'number','group':'时间轴','default':40,'min':2,'max':256,'step':1,'hidden':True},
    {'key':'anim_segment_ramp','label':'中段开始帧','kind':'number','group':'时间轴','default':10,'min':0,'max':255,'step':1},
    {'key':'anim_segment_burst','label':'高峰开始帧','kind':'number','group':'时间轴','default':20,'min':0,'max':255,'step':1},
    {'key':'anim_segment_after','label':'余韵开始帧','kind':'number','group':'时间轴','default':30,'min':0,'max':256,'step':1},
    {'key':'anim_drip_interval','label':'前段滴落间隔','kind':'number','group':'时间轴','default':4,'min':1,'max':64,'step':1},
    {'key':'anim_ramp_interval','label':'中段发射间隔','kind':'number','group':'时间轴','default':2,'min':1,'max':64,'step':1},

    {'key':'anim_burst1_start','label':'爆发 1 开始','kind':'number','group':'爆发窗口','default':20,'min':0,'max':255,'step':1},
    {'key':'anim_burst1_end','label':'爆发 1 结束','kind':'number','group':'爆发窗口','default':22,'min':0,'max':255,'step':1},
    {'key':'anim_pressure_burst1','label':'爆发 1 压力','kind':'number','group':'爆发窗口','default':2.15,'min':0.1,'max':5,'step':0.05},
    {'key':'anim_splash_burst1','label':'爆发 1 飞溅量','kind':'number','group':'爆发窗口','default':3,'min':0,'max':16,'step':1},
    {'key':'anim_burst2_start','label':'爆发 2 开始','kind':'number','group':'爆发窗口','default':24,'min':0,'max':255,'step':1},
    {'key':'anim_burst2_end','label':'爆发 2 结束','kind':'number','group':'爆发窗口','default':25,'min':0,'max':255,'step':1},
    {'key':'anim_pressure_burst2','label':'爆发 2 压力','kind':'number','group':'爆发窗口','default':1.85,'min':0.1,'max':5,'step':0.05},
    {'key':'anim_splash_burst2','label':'爆发 2 飞溅量','kind':'number','group':'爆发窗口','default':2,'min':0,'max':16,'step':1},
    {'key':'anim_burst3_start','label':'爆发 3 开始','kind':'number','group':'爆发窗口','default':27,'min':0,'max':255,'step':1},
    {'key':'anim_burst3_end','label':'爆发 3 结束','kind':'number','group':'爆发窗口','default':28,'min':0,'max':255,'step':1},
    {'key':'anim_pressure_burst3','label':'爆发 3 压力','kind':'number','group':'爆发窗口','default':1.55,'min':0.1,'max':5,'step':0.05},
    {'key':'anim_splash_burst3','label':'爆发 3 飞溅量','kind':'number','group':'爆发窗口','default':1,'min':0,'max':16,'step':1},
    {'key':'anim_pressure_drip','label':'前段压力','kind':'number','group':'爆发窗口','default':0.92,'min':0.1,'max':5,'step':0.05},
    {'key':'anim_pressure_ramp_start','label':'中段起始压力','kind':'number','group':'爆发窗口','default':1.05,'min':0.1,'max':5,'step':0.05},
    {'key':'anim_pressure_ramp_step','label':'中段压力增量','kind':'number','group':'爆发窗口','default':0.03,'min':0,'max':1,'step':0.01},
    {'key':'particle_red','label':'粒子红色','kind':'number','group':'外观','default':0.78,'min':0,'max':1,'step':0.01,'hidden':True},
    {'key':'particle_green','label':'粒子绿色','kind':'number','group':'外观','default':0.51,'min':0,'max':1,'step':0.01,'hidden':True},
    {'key':'particle_blue','label':'粒子蓝色','kind':'number','group':'外观','default':0.055,'min':0,'max':1,'step':0.01,'hidden':True},
    {'key':'particle_pool','label':'粒子池预设','kind':'select','group':'性能','default':1024,'min':512,'max':2048,'step':1,
        'options':[
            {'value':512,'label':'流畅 · 512 主粒子'},
            {'value':1024,'label':'标准 · 1024 主粒子（推荐）'},
            {'value':2048,'label':'高画质 · 2048 主粒子'},
        ]},

]
FIELD_BY_KEY = {item['key']:item for item in FIELDS}
GLOBAL_LINE = re.compile(r'^(?P<indent>\s*)global\s+\$(?P<key>[A-Za-z0-9_]+)\s*=\s*(?P<value>[^\r\n]*)(?P<eol>\r?\n)?$')


def _probe_path(probe: Path) -> Path:
    return Path(probe) / 'Probe.ini'


def _read_probe(probe: Path) -> tuple[str, bool]:
    raw = _probe_path(probe).read_bytes()
    return raw.decode('utf-8-sig'), raw.startswith(codecs.BOM_UTF8)


def _parse_probe(probe: Path) -> dict[str,float]:
    text, _ = _read_probe(probe)
    found: dict[str,float] = {}
    for line in text.splitlines():
        match = GLOBAL_LINE.match(line)
        if not match or match['key'] not in FIELD_BY_KEY:
            continue
        try:
            found[match['key']] = float(match['value'].strip())
        except ValueError:
            continue
    return found


def _coerce(spec: dict, value) -> int | float:
    if spec['kind'] in ('toggle','select'):
        if isinstance(value, bool):
            result = int(value)
        else:
            result = int(float(value))
    elif spec['step'] >= 1 and float(spec['step']).is_integer():
        result = int(float(value))
    else:
        result = float(value)
    if not math.isfinite(float(result)):
        raise ValueError(f"{spec['label']} 不是有限数值")
    if not spec['min'] <= result <= spec['max']:
        raise ValueError(f"{spec['label']} 必须在 {spec['min']}–{spec['max']} 范围内")
    return result


def _validate(values: dict) -> dict[str,int | float]:
    result = {}
    for spec in FIELDS:
        result[spec['key']] = _coerce(spec, values.get(spec['key'], spec['default']))
    frames = int(result['anim_frames'])
    ramp = int(result['anim_segment_ramp'])
    burst = int(result['anim_segment_burst'])
    after = int(result['anim_segment_after'])
    if not 0 <= ramp < burst < after <= frames:
        raise ValueError('阶段必须满足：0 ≤ 中段开始 < 高峰开始 < 余韵开始 ≤ 总帧数')
    for index in range(1,4):
        start = int(result[f'anim_burst{index}_start'])
        end = int(result[f'anim_burst{index}_end'])
        if not burst <= start <= end < after:
            raise ValueError(f'爆发 {index} 必须位于高峰阶段内，且开始帧不大于结束帧')
    return result


def read_effect_settings(probe: Path) -> dict:
    values = {spec['key']:spec['default'] for spec in FIELDS}
    values.update(_parse_probe(Path(probe)))
    try:
        values = _validate(values)
    except (TypeError,ValueError):
        values = {spec['key']:spec['default'] for spec in FIELDS}
    return {'values':values,'fields':FIELDS}


def _format_value(spec: dict, value: int | float) -> str:
    if spec['kind'] in ('toggle','select') or (spec['step'] >= 1 and float(spec['step']).is_integer()):
        return str(int(value))
    return format(float(value), '.6g')


def write_effect_settings(probe: Path, incoming: dict) -> dict:
    values = _validate(incoming)
    path = _probe_path(Path(probe))
    text, bom = _read_probe(Path(probe))
    seen: set[str] = set()
    output = []
    for line in text.splitlines(keepends=True):
        match = GLOBAL_LINE.match(line)
        if not match or match['key'] not in FIELD_BY_KEY:
            output.append(line)
            continue
        key = match['key']
        if key in seen:
            raise ValueError(f'Probe.ini 中 $ {key} 重复')
        seen.add(key)
        eol = match['eol'] or ''
        output.append(f"{match['indent']}global ${key} = {_format_value(FIELD_BY_KEY[key],values[key])}{eol}")
    missing = sorted(set(FIELD_BY_KEY) - seen)
    if missing:
        raise ValueError('Probe.ini 缺少效果参数：' + ', '.join('$'+key for key in missing))
    data = (codecs.BOM_UTF8 if bom else b'') + ''.join(output).encode('utf-8')
    temp = path.with_name(path.name + '.effects-tmp')
    temp.write_bytes(data)
    os.replace(temp, path)
    return {'values':values,'fields':FIELDS,'path':str(path)}
