#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
打包构建脚本

用法:
    python build_exe.py          # 打包为单文件 exe
    python build_exe.py --dir    # 打包为目录模式 (启动更快)
    python build_exe.py --clean  # 打包前清理旧文件

输出目录: dist/
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOLS_DIR.parent


def main():
    parser = argparse.ArgumentParser(description="打包缺陷分类工具为 exe")
    parser.add_argument("--dir", action="store_true",
                        help="目录模式 (启动快, 体积稍大)")
    parser.add_argument("--clean", action="store_true",
                        help="打包前清理 build/ 和 dist/")
    args = parser.parse_args()

    spec = TOOLS_DIR / "fault_sort_web.spec"

    # 如果要目录模式, 临时修改 spec 中的 onefile
    if args.dir:
        text = spec.read_text(encoding="utf-8")
        text = text.replace("onefile = True", "onefile = False")
        tmp_spec = TOOLS_DIR / "fault_sort_web_dir.spec"
        tmp_spec.write_text(text, encoding="utf-8")
        spec = tmp_spec

    # 清理
    if args.clean:
        for d in [PROJECT_DIR / "build", PROJECT_DIR / "dist"]:
            if d.exists():
                print(f"清理 {d}")
                shutil.rmtree(d, ignore_errors=True)

    # 运行 PyInstaller
    cmd = [sys.executable, "-m", "PyInstaller", str(spec),
           "--noconfirm", "--distpath", str(PROJECT_DIR / "dist"),
           "--workpath", str(PROJECT_DIR / "build")]
    print(f"运行: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=str(TOOLS_DIR))

    # 清理临时 spec
    if args.dir:
        tmp_spec = TOOLS_DIR / "fault_sort_web_dir.spec"
        if tmp_spec.exists():
            tmp_spec.unlink()

    if result.returncode == 0:
        print("\n打包成功!")
        if args.dir:
            print(f"输出: {PROJECT_DIR / 'dist' / '缺陷分类工具' / '缺陷分类工具.exe'}")
        else:
            print(f"输出: {PROJECT_DIR / 'dist' / '缺陷分类工具.exe'}")
    else:
        print(f"\n打包失败 (exit {result.returncode})", file=sys.stderr)
        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
