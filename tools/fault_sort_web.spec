# -*- mode: python ; coding: utf-8 -*-
"""
PyInstaller spec 文件 - 缺陷压缩包分类工具 Web 版

打包命令:
    pyinstaller fault_sort_web.spec

输出:
    dist/缺陷分类工具/缺陷分类工具.exe  (目录模式, 推荐用于 Windows)
    dist/缺陷分类工具.exe            (单文件模式, 见下方 onefile 变量)

注意: Flask 的模板和静态文件需要通过 datas 打包进去。
"""

import sys
from pathlib import Path

block_cipher = None

# 打包模式: True=单文件 exe, False=目录模式 (启动更快)
onefile = True

# 源码目录
tools_dir = Path(SPECPATH)

# 需要打包的数据文件: Flask 模板和静态资源
datas = [
    (str(tools_dir / 'templates'), 'templates'),
    (str(tools_dir / 'static'), 'static'),
]

# 需要打包的 Python 模块
hiddenimports = [
    'flask',
    'jinja2',
    'markupsafe',
    'itsdangerous',
    'click',
    'werkzeug',
    'sort_fault_zips',
]

a = Analysis(
    [str(tools_dir / 'fault_sort_web.py')],
    pathex=[str(tools_dir)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'tkinter',      # Web 版不需要 GUI 库
        'matplotlib',
        'numpy',
        'pandas',
        'PIL',
        'cv2',
        'PyQt5', 'PyQt6', 'PySide2', 'PySide6',
        'pytest', 'unittest',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

if onefile:
    exe = EXE(
        pyz,
        a.scripts,
        a.binaries,
        a.datas,
        [],
        name='缺陷分类工具',
        debug=False,
        bootloader_ignore_signals=False,
        strip=False,
        upx=True,
        upx_exclude=[],
        runtime_tmpdir=None,
        console=True,           # 保留控制台窗口以显示启动日志
        disable_windowed_traceback=False,
        argv_emulation=False,
        target_arch=None,
        codesign_identity=None,
        entitlements_file=None,
        icon=None,              # 可指定 .ico 图标路径
    )
else:
    exe = EXE(
        pyz,
        a.scripts,
        [],
        exclude_binaries=True,
        name='缺陷分类工具',
        debug=False,
        bootloader_ignore_signals=False,
        strip=False,
        upx=True,
        console=True,
        disable_windowed_traceback=False,
        argv_emulation=False,
        target_arch=None,
        codesign_identity=None,
        entitlements_file=None,
    )
    coll = COLLECT(
        exe,
        a.binaries,
        a.datas,
        strip=False,
        upx=True,
        upx_exclude=[],
        name='缺陷分类工具',
    )
