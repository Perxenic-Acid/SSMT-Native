"""Remove LiquidGravityProbe attachment hooks and generated profiles."""
from __future__ import annotations

import codecs
import json
import os
import re
import shutil
from pathlib import Path

CONFIRMATION = 'DELETE_ALL_PROBE_DATA'
PROFILE_FOLDER_NAME = 'LiquidGravityProbe_Profiles'

CAPTURE_HOOK = re.compile(
    r'^[ \t]*x243[ \t]*=[ \t]*\d+[ \t]*\r?\n'
    r'[ \t]*y243[ \t]*=[ \t]*\d+[ \t]*\r?\n'
    r'[ \t]*z243[ \t]*=[ \t]*-?\d+[ \t]*\r?\n'
    r'(?:[ \t]*w243[ \t]*=[ \t]*\d+[ \t]*\r?\n)?'
    r'[ \t]*run[ \t]*=[ \t]*CommandList\\LiquidGravityProbe\\CaptureSurface[ \t]*'
    r'(?:\r?\n|\Z)',
    re.I | re.M,
)


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.cleanup-tmp')
    temp.write_bytes(data)
    os.replace(temp, path)


def _empty_loader() -> bytes:
    text = (
        '; LiquidGravityProbe 配置已清空。\r\n'
        'namespace = LiquidGravityProbe\\JSONProfiles\r\n'
        '[Constants]\r\n'
        'global $loaded = 0\r\n'
        '[CommandListInitialize]\r\n'
        'if !$loaded\r\n'
        '    clear = Resource\\LiquidGravityProbe\\SurfaceProfiles\r\n'
        '    clear = Resource\\LiquidGravityProbe\\SurfaceModelOffsets\r\n'
        '    clear = Resource\\LiquidGravityProbe\\SurfaceEffects\r\n'
        '    $loaded = 1\r\n'
        'endif\r\n'
    )
    return codecs.BOM_UTF8 + text.encode('utf-8')


def _empty_index() -> bytes:
    return json.dumps(
        {
            'schema_version': 1,
            'slot_count': 1,
            'registered_mods': [],
            'note': 'LiquidGravityProbe attachment profiles were cleared.',
        },
        ensure_ascii=False,
        indent=2,
    ).encode('utf-8')


def _empty_models() -> bytes:
    return json.dumps(
        {
            'coordinate_order': ['right', 'up', 'forward'],
            'units': 'metres',
            'models': [],
        },
        ensure_ascii=False,
        indent=2,
    ).encode('utf-8')


def _is_probe_folder(relative: Path) -> bool:
    return any('liquidgravityprobe-zh-cn' in part.casefold() for part in relative.parts[:-1])


def _hook_changes(mods: Path, probe: Path) -> tuple[list[dict], list[str]]:
    changes: list[dict] = []
    errors: list[str] = []
    needle = b'commandlist\\liquidgravityprobe\\capturesurface'
    for path in sorted(mods.rglob('*.ini')):
        try:
            resolved = path.resolve()
            if resolved.is_relative_to(probe):
                continue
            relative = path.relative_to(mods)
            if _is_probe_folder(relative):
                continue
            raw = path.read_bytes()
            if needle not in raw.lower():
                continue
            text = raw.decode('utf-8-sig')
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f'{path}: {exc}')
            continue
        updated, count = CAPTURE_HOOK.subn('', text)
        if count:
            changes.append(
                {
                    'path': path,
                    'relative': relative,
                    'after': (codecs.BOM_UTF8 if raw.startswith(codecs.BOM_UTF8) else b'')
                    + updated.encode('utf-8'),
                    'hooks': count,
                }
            )
    return changes, errors


def _plan(mods: Path, probe: Path) -> dict:
    changes, errors = _hook_changes(mods, probe)
    profile_folder = mods / PROFILE_FOLDER_NAME
    legacy_folder = probe / 'Profiles'
    marker = probe / 'surface' / 'profile-folder-migrated.json'
    anchors = probe / 'AttachmentEditor' / 'output' / 'anchors.json'
    backup_histories = [
        probe / 'output' / 'backups',
        probe / 'AttachmentEditor' / 'output' / 'backups',
    ]
    split_backups = list(probe.glob('Probe.ini.pre-native-split-*.bak'))
    generated = {
        probe / 'surface' / 'profiles.json': json.dumps(
            {'profiles': []}, ensure_ascii=False, indent=2
        ).encode('utf-8'),
        probe / 'surface' / 'profiles.buf': b'',
        probe / 'surface' / 'attachment-offsets.buf': b'',
        probe / 'surface' / 'attachment-models.json': _empty_models(),
        probe / 'surface' / 'ProfileLoader.ini': _empty_loader(),
    }
    profile_files = (
        [path for path in profile_folder.rglob('*') if path.is_file()]
        if profile_folder.is_dir()
        else []
    )
    return {
        'hook_changes': changes,
        'errors': errors,
        'profile_folder': profile_folder,
        'profile_files': profile_files,
        'legacy_folder': legacy_folder,
        'delete_directories': [legacy_folder, *backup_histories],
        'delete_files': [marker, anchors, *split_backups],
        'generated': generated,
    }


def cleanup_probe_data(
    mods: Path,
    probe: Path,
    *,
    preview: bool = False,
    confirmation: str | None = None,
) -> dict:
    """Preview or remove all attachment hooks and generated profile data."""
    mods = Path(mods).resolve()
    probe = Path(probe).resolve()
    if not mods.is_dir() or not probe.is_dir():
        raise ValueError('Mods 或 Probe 目录不存在')
    if not probe.is_relative_to(mods):
        raise ValueError('Probe 目录必须位于 Mods 目录内')

    plan = _plan(mods, probe)
    hooks = sum(item['hooks'] for item in plan['hook_changes'])
    profile_files = len(plan['profile_files'])
    summary = {
        'preview': preview,
        'hook_files': len(plan['hook_changes']),
        'hooks': hooks,
        'profile_files': profile_files,
        'legacy_profile_folder': plan['legacy_folder'].is_dir(),
        'backup_history': any(path.is_dir() for path in plan['delete_directories'][1:]),
        'generated_files': sum(path.exists() for path in plan['generated']),
        'delete_files': sum(path.exists() for path in plan['delete_files']),
        'delete_directories': sum(path.is_dir() for path in plan['delete_directories']),
        'errors': plan['errors'],
    }
    if preview:
        return summary
    if confirmation != CONFIRMATION:
        raise ValueError('清理确认无效')
    for item in plan['hook_changes']:
        _atomic_write(item['path'], item['after'])
    for path, data in plan['generated'].items():
        _atomic_write(path, data)
    for path in plan['delete_files']:
        path.unlink(missing_ok=True)
    for path in plan['delete_directories']:
        if path.is_dir():
            shutil.rmtree(path)
    profile_folder = plan['profile_folder']
    if profile_folder.is_dir():
        shutil.rmtree(profile_folder)
    profile_folder.mkdir(parents=True, exist_ok=True)
    _atomic_write(profile_folder / '_index.json', _empty_index())
    summary.update(
        {
            'permanent': True,
            'message': '已永久删除 Probe 插入的钩子、配置与旧备份。',
        }
    )
    return summary
