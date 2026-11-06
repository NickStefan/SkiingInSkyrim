"""Workspace-local Wine settings shared by the toolchain fetch and build scripts."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
PREFIX = Path(os.environ.get('SIS_WINEPREFIX', BUILD / 'wine')).resolve()
PROTON = Path(os.environ.get(
    'SIS_PROTON', Path.home() / '.local/share/Steam/steamapps/common/Proton 10.0/files'))
WINE = Path(os.environ.get('SIS_WINE', PROTON / 'bin/wine64'))
DRIVE = 's:'


def env():
    result = dict(os.environ)
    result.update(
        WINEPREFIX=str(PREFIX),
        WINEDEBUG='-all',
        # winemenubuilder writes .desktop files into ~/.local; keep everything in the prefix.
        WINEDLLOVERRIDES='winemenubuilder.exe=d;mscoree=d;mshtml=d',
        DISPLAY='',
        WAYLAND_DISPLAY='',
    )
    return result


def ensure_prefix():
    """Create the prefix once and map S: to the workspace root (its path contains spaces)."""
    if not WINE.exists():
        raise SystemExit(f'Wine not found at {WINE}; set SIS_WINE or SIS_PROTON')
    if not (PREFIX / 'system.reg').exists():
        PREFIX.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(WINE), 'wineboot', '-i'], env=env(), check=True, timeout=600)
    link = PREFIX / 'dosdevices' / DRIVE
    if not link.is_symlink() or Path(os.readlink(link)) != ROOT:
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(ROOT, target_is_directory=True)


def win(path):
    """Windows spelling of a Linux path, preferring the space-free S: drive."""
    path = Path(path).resolve()
    try:
        relative = path.relative_to(ROOT)
        return DRIVE.upper() + '\\' + str(relative).replace('/', '\\')
    except ValueError:
        return 'Z:' + str(path).replace('/', '\\')


def run(program, args, cwd=ROOT, timeout=1800, extra_env=None, **kwargs):
    command = [str(WINE), str(program), *map(str, args)]
    environment = env()
    if extra_env:
        environment.update(extra_env)
    return subprocess.run(command, cwd=cwd, env=environment, timeout=timeout, **kwargs)
