from pathlib import Path
import os


def atomic_write(path: Path, data: bytes, suffix: str = '.transaction-tmp') -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + suffix)
    try:
        temp.write_bytes(data)
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def commit_updates(updates: dict[Path, bytes | None]) -> bool:
    changed = {}
    originals = {}
    for path, content in updates.items():
        path = Path(path)
        original = path.read_bytes() if path.exists() else None
        if content == original:
            continue
        changed[path] = content
        originals[path] = original
    if not changed:
        return False

    written: list[Path] = []
    try:
        for path, content in changed.items():
            if content is None:
                path.unlink(missing_ok=True)
            else:
                atomic_write(path, content)
            written.append(path)
    except Exception:
        for path in reversed(written):
            original = originals[path]
            if original is None:
                path.unlink(missing_ok=True)
            else:
                atomic_write(path, original)
        raise
    return True
