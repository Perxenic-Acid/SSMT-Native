"""Read and update the particle toggle key in Probe.ini."""
from __future__ import annotations

import codecs
import os
import re
from pathlib import Path

KEY_TOKEN = re.compile(r'^(?:VK_[A-Z0-9_]+|[A-Z0-9]|F(?:[1-9]|1[0-9]|2[0-4]))$',re.I)
MODIFIERS={'ctrl','alt','shift','no_ctrl','no_alt','no_shift'}
RESERVED_KEYS={'F10','VK_NUMPAD0','VK_NUMPAD2','VK_NUMPAD3','VK_NUMPAD4'}


def _normalize_key(value: str) -> str:
    key=str(value).strip().upper()
    if not KEY_TOKEN.fullmatch(key):
        raise ValueError('按键格式无效，请使用 VK_xxx、A–Z、0–9 或 F1–F24')
    if key in RESERVED_KEYS:
        raise ValueError(f'{key} 已被重载、自动脉冲或强度快捷键占用')
    return key


def _display(key: str, ctrl: bool, alt: bool, shift: bool) -> str:
    names=[name for enabled,name in ((ctrl,'Ctrl'),(alt,'Alt'),(shift,'Shift')) if enabled]
    labels={
        'VK_NUMPAD0':'Num 0','VK_NUMPAD1':'Num 1','VK_NUMPAD2':'Num 2','VK_NUMPAD3':'Num 3',
        'VK_NUMPAD4':'Num 4','VK_NUMPAD5':'Num 5','VK_NUMPAD6':'Num 6','VK_NUMPAD7':'Num 7',
        'VK_NUMPAD8':'Num 8','VK_NUMPAD9':'Num 9','VK_LEFT':'←','VK_RIGHT':'→','VK_UP':'↑','VK_DOWN':'↓',
        'VK_SPACE':'Space','VK_RETURN':'Enter','VK_TAB':'Tab','VK_ESCAPE':'Esc',
    }
    names.append(labels.get(key,key.removeprefix('VK_').replace('_',' ').title() if key.startswith('VK_') else key))
    return ' + '.join(names)


def parse_binding(binding: str) -> dict:
    tokens=[item.upper() for item in re.split(r'\s+',binding.strip()) if item]
    keys=[item for item in tokens if item not in {m.upper() for m in MODIFIERS}]
    if len(keys)!=1:raise ValueError('按键配置必须包含且只包含一个主键')
    key=_normalize_key(keys[0])
    ctrl='CTRL' in tokens;alt='ALT' in tokens;shift='SHIFT' in tokens
    return {'key':key,'ctrl':ctrl,'alt':alt,'shift':shift,'binding':build_binding(key,ctrl,alt,shift),
        'display':_display(key,ctrl,alt,shift)}


def build_binding(key: str, ctrl: bool, alt: bool, shift: bool) -> str:
    key=_normalize_key(key)
    modifiers=('CTRL' if ctrl else 'NO_CTRL','ALT' if alt else 'NO_ALT','SHIFT' if shift else 'NO_SHIFT')
    return ' '.join((*modifiers,key))


def _read(probe: Path) -> tuple[str,bool]:
    raw=(Path(probe)/'Probe.ini').read_bytes()
    return raw.decode('utf-8-sig'),raw.startswith(codecs.BOM_UTF8)


def read_key_binding(probe: Path) -> dict:
    text,_=_read(Path(probe))
    section=re.search(r'^\[KeyToggle\][ \t]*\r?\n(.*?)(?=^\[|\Z)',text,re.M|re.S|re.I)
    if not section:raise ValueError('Probe.ini 缺少 [KeyToggle]')
    line=re.search(r'^[ \t]*key[ \t]*=[ \t]*([^\r\n]+)',section[1],re.M|re.I)
    if not line:raise ValueError('[KeyToggle] 缺少 key')
    return parse_binding(line[1])


def write_key_binding(probe: Path, data: dict) -> dict:
    key=_normalize_key(data.get('key',''))
    ctrl=bool(data.get('ctrl'));alt=bool(data.get('alt'));shift=bool(data.get('shift'))
    binding=build_binding(key,ctrl,alt,shift)
    path=Path(probe)/'Probe.ini';text,bom=_read(Path(probe))
    section=re.search(r'^\[KeyToggle\][ \t]*\r?\n(.*?)(?=^\[|\Z)',text,re.M|re.S|re.I)
    if not section:raise ValueError('Probe.ini 缺少 [KeyToggle]')
    body=section[1]
    updated,count=re.subn(r'^[ \t]*key[ \t]*=[^\r\n]*',f'key = {binding}',body,count=1,flags=re.M|re.I)
    if count!=1:raise ValueError('[KeyToggle] 缺少 key')
    text=text[:section.start(1)]+updated+text[section.end(1):]
    temp=path.with_name(path.name+'.keybind-tmp')
    temp.write_bytes((codecs.BOM_UTF8 if bom else b'')+text.encode('utf-8'))
    os.replace(temp,path)
    return parse_binding(binding)
