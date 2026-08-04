#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
缺陷压缩包分类工具

解析指定目录下所有 *.zip 压缩包，读取其中的 CSV 元数据
(FAULTINF_CLASS 字段即缺陷类型代码 type_id)，按缺陷类型把图片归档到
<输出目录>/<中文缺陷名>/ 子文件夹中。

类型代码到中文名的映射来源于 config_code.xml，与 manual_defect_tool.py
中的 BUILTIN_XLBH_CODES 保持一致。

用法:
    python sort_fault_zips.py <zip目录> [-o 输出目录] [--dry-run]

示例:
    python tools/sort_fault_zips.py data/20260518/fault/20260518183516
    python tools/sort_fault_zips.py data/20260518/fault/20260518183516 -o D:/sorted

依赖: 仅使用 Python 标准库 (zipfile / csv / argparse)
"""

import argparse
import csv
import io
import sys
import zipfile
from pathlib import Path


# ============================================================
# 缺陷类型编码 (XLBH_type, XLBH_name), 来源于 config_code.xml
# 与 manual_defect_tool.py 中的 BUILTIN_XLBH_CODES 保持一致
# ============================================================
DEFECT_CLASS_MAP = {
    2:    "钢轨表面擦伤",
    3:    "钢轨剥离掉块",
    16:   "弹条缺失",
    24:   "弹条断裂",
    32:   "弹条位移",
    48:   "螺栓缺失",
    49:   "螺栓松脱",
    50:   "垫片位移",
    51:   "翻浆冒泥",
    128:  "轨枕裂开",
    192:  "轨枕剥离掉块",
    1024: "道床裂开",
    1536: "道床异物",
    8192: "螺母缺失",
}


def class_code_to_name(code):
    """type_id -> 中文名，空代码或未知代码兜底"""
    if not code:
        return "未知_元数据缺失"
    try:
        key = int(code)
    except (TypeError, ValueError):
        return "未知_无代码"
    return DEFECT_CLASS_MAP.get(key, f"未知_{key}")


def display_width(s):
    """计算字符串在终端中的显示宽度 (中文/全角占 2, 英文/半角占 1)"""
    return sum(2 if ord(c) > 0x2E80 else 1 for c in s)


def pad_to_width(s, width):
    """按显示宽度右填充空格, 使终端中对齐"""
    pad = width - display_width(s)
    return s + " " * max(0, pad)


def parse_zip_fault_class(zip_path):
    """
    从 zip 内的 CSV 解析缺陷类型代码。
    返回 (type_id_str, object_name, img_name) 或 None。
    CSV 格式: 第1行表头, 第2行数据, UTF-8 编码。

    若 CSV 损坏但 jpg 仍可提取，返回 ("", "", img_name) 以便归到兜底文件夹，
    而不是整体丢弃图片。
    """
    try:
        with zipfile.ZipFile(zip_path, "r") as zf:
            csv_name = next(
                (n for n in zf.namelist() if n.lower().endswith(".csv")), None
            )
            jpg_name = next(
                (n for n in zf.namelist() if n.lower().endswith(".jpg")), None
            )
            if not jpg_name:
                return None
            img = Path(jpg_name).name

            if not csv_name:
                return "", "", img

            try:
                with zf.open(csv_name) as f:
                    raw = f.read()
                text = raw.decode("utf-8", errors="replace")
            except (zipfile.BadZipFile, OSError, KeyError):
                # CSV 损坏，但 jpg 可能仍可提取
                return "", "", img

            # 用 csv 模块按行解析，避免字段内含逗号的问题
            reader = csv.reader(io.StringIO(text))
            rows = [row for row in reader if row]
            if len(rows) < 2:
                return "", "", img
            headers = [h.strip() for h in rows[0]]
            data = rows[1]

            def get(field):
                idx = headers.index(field) if field in headers else -1
                return data[idx].strip() if 0 <= idx < len(data) else ""

            type_id = get("FAULTINF_CLASS")
            obj = get("FAULTINF_OBJECT")
            return type_id, obj, img
    except (zipfile.BadZipFile, OSError) as e:
        print(f"  [警告] 跳过 {Path(zip_path).name}: {e}", file=sys.stderr)
        return None


def unique_dest_path(dest_dir, img_name):
    """若目标路径已存在，则追加 _1/_2... 避免覆盖"""
    dest = dest_dir / img_name
    if not dest.exists():
        return dest
    stem, suffix = dest.stem, dest.suffix
    i = 1
    while True:
        cand = dest_dir / f"{stem}_{i}{suffix}"
        if not cand.exists():
            return cand
        i += 1


def sort_zips(src_dir, out_dir, dry_run=False, progress_callback=None):
    """
    解析并分类压缩包。

    Args:
        src_dir: 包含 .zip 的输入目录
        out_dir: 分类输出目录
        dry_run: 仅预览不写文件
        progress_callback: 可选回调 fn(index, total, zip_name, folder_name, status)
            status 为 'ok' / 'failed' / 'skipped'，用于上报进度。
            在每个 zip 处理完毕后调用。
    """
    src_dir = Path(src_dir).resolve()
    out_dir = Path(out_dir).resolve()
    if not src_dir.is_dir():
        print(f"错误: 输入目录不存在: {src_dir}", file=sys.stderr)
        return 1

    zips = sorted(src_dir.glob("*.zip"), key=lambda p: p.name)
    if not zips:
        print(f"错误: 目录下未找到 zip 文件: {src_dir}", file=sys.stderr)
        return 1

    total = len(zips)
    print(f"输入目录: {src_dir}")
    print(f"输出目录: {out_dir}")
    print(f"压缩包数量: {total}")
    if dry_run:
        print("[dry-run 模式] 仅预览，不写文件\n")
    else:
        out_dir.mkdir(parents=True, exist_ok=True)

    stats = {}      # 文件夹名 -> 数量
    failed = 0
    for i, zip_path in enumerate(zips):
        result = parse_zip_fault_class(zip_path)
        if not result:
            failed += 1
            if progress_callback:
                progress_callback(i + 1, total, zip_path.name, "", "skipped")
            continue
        type_id, _, img_name = result
        folder_name = class_code_to_name(type_id)
        stats[folder_name] = stats.get(folder_name, 0) + 1

        if dry_run:
            if progress_callback:
                progress_callback(i + 1, total, zip_path.name, folder_name, "ok")
            continue
        dest_dir = out_dir / folder_name
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest_img = unique_dest_path(dest_dir, img_name)
        try:
            with zipfile.ZipFile(zip_path, "r") as zf:
                with zf.open(img_name) as src, open(dest_img, "wb") as dst:
                    dst.write(src.read())
        except (zipfile.BadZipFile, OSError, KeyError) as e:
            # jpg 提取失败: 删除可能已创建的空文件, 回退统计, 清理空目录
            try:
                if dest_img.exists() and dest_img.stat().st_size == 0:
                    dest_img.unlink()
            except OSError:
                pass
            stats[folder_name] -= 1
            if stats[folder_name] <= 0:
                del stats[folder_name]
                try:
                    if not any(dest_dir.iterdir()):
                        dest_dir.rmdir()
                except OSError:
                    pass
            failed += 1
            print(f"  [警告] 提取图片失败 {Path(zip_path).name}: {e}", file=sys.stderr)
            if progress_callback:
                progress_callback(i + 1, total, zip_path.name, folder_name, "failed")
        else:
            if progress_callback:
                progress_callback(i + 1, total, zip_path.name, folder_name, "ok")

    # 输出统计
    print("\n=== 分类结果 ===")
    total = 0
    for name in sorted(stats.keys()):
        cnt = stats[name]
        total += cnt
        print(f"  {pad_to_width(name, 20)} {cnt:>4} 张")
    print(f"  {pad_to_width('合计', 20)} {total:>4} 张")
    if failed:
        print(f"  失败/跳过: {failed} 个")

    return 0 if total else 2


def main():
    parser = argparse.ArgumentParser(
        description="解析缺陷压缩包并按中文缺陷类型分类图片",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="示例:\n  python tools/sort_fault_zips.py data/20260518/fault/20260518183516",
    )
    parser.add_argument("src_dir", help="包含 .zip 压缩包的目录")
    parser.add_argument(
        "-o", "--output",
        default=None,
        help="输出目录，默认为 <src_dir>_sorted",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="只预览分类结果，不写出文件",
    )
    args = parser.parse_args()

    out_dir = args.output or (str(Path(args.src_dir).resolve()) + "_sorted")
    sys.exit(sort_zips(args.src_dir, out_dir, args.dry_run))


if __name__ == "__main__":
    main()
