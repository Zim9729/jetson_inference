#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sort_fault_zips.py 的单元测试

构造各种边界场景的 zip 文件，验证分类脚本的正确性:
  1. 正常 zip (各种 type_id)
  2. CSV 损坏但 jpg 可提取 -> 未知_元数据缺失
  3. jpg 损坏 -> 跳过并清理空目录
  4. 未知 type_id -> 未知_<code>
  5. 空 type_id -> 未知_元数据缺失
  6. 同名图片自动追加 _1/_2
  7. --dry-run 不写文件
  8. 自定义输出目录 -o
  9. 目录不存在 -> exit 1
 10. 无 zip 的目录 -> exit 1
 11. class_code_to_name 单元测试

运行: python tests/test_sort_fault_zips.py
依赖: 仅标准库 (unittest / zipfile / tempfile / subprocess)
"""

import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

# 把 tools 目录加入 path 以便直接 import
TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import sort_fault_zips as mod

SCRIPT = TOOLS_DIR / "sort_fault_zips.py"

# CSV 表头 (与真实数据一致)
HEADERS = (
    "ID,FAULTINF_BASLIB_INDEX,FAULTINF_BASLIB_IMGNAME,FAULTINF_IMGNAME,"
    "FAULTINF_PART_IMGNAME,FAULTINF_IMGPATH,FAULTINF_OVER_NUM,FAULTINF_LEVEL,"
    "FAULTINF_START_STATION,FAULTINF_STOP_STATION,FAULTINF_ROUTENO,"
    "FAULTINF_TRAIN_NUM,FAULTINF_CAM_POSITION,FAULTINF_TRAINCATETYPE,"
    "FAULTINF_RECOGNITION_NUM,FAULTINF_OBJECT,FAULTINF_CLASS,FAULTINF_POS_X,"
    "FAULTINF_POS_Y,FAULTINF_POS_W,FAULTINF_POS_H,FAULTINF_PROC_STATUS,"
    "FAULTINF_PROC_RESULT,FAULTINF_DOWNLOAD_TIME,FAULTINF_FEEDBACK_TIME,"
    "FAULTINF_MAINTENANCE,FAULTINF_OPERATOR_NAME,FAULTINF_CONFIRM_TIME,"
    "FAULTINF_REPAIR_FAULT_IDENTIFICATION_COUNT,FAULTINF_TEMP_IMAGE_CHECK_COUNT,"
    "FAULTINF_DETE_KM_MARK,FAULTINF_BASIS_KM_MARK,FAULTINF_GENERATE_TIME,"
    "FAULTINF_LOCATION_MM,FAULTINF_OBJECT_ID,FAULTINF_TYPE_ID,FAULTINF_CAM_NUM,"
    "FAULTINF_DETE_IMAGE_NAME"
)


def make_csv(type_id, obj="弹条", img_name="fault_test.jpg"):
    """生成单行表头 + 单行数据的 CSV 文本"""
    data = ",".join([
        "0", "2", "", img_name, img_name,
        "E:\\path\\L1\\00002_-27957000_1772950286098.jpg",
        "1", "", "", "", "3", "187188", "L", "U", "1",
        obj, str(type_id), "1175", "1188", "437", "279",
        "0", "", "", "", "", "", "", "0", "0", "", "",
        "2026/03/08 14:11:26", "-27957000", "1", "16", "1",
        "00002_-27957000_1772950286098.jpg",
    ])
    return HEADERS + "\n" + data + "\n"


def make_zip(path, type_id=16, obj="弹条", img_name="fault_test.jpg",
             jpg_bytes=b"\xff\xd8\xff\xe0FAKE_JPG_DATA\xff\xd9",
             csv_bytes=None):
    """生成一个正常的测试 zip"""
    if csv_bytes is None:
        csv_bytes = make_csv(type_id, obj, img_name).encode("utf-8")
    csv_name = Path(img_name).stem + ".csv"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(csv_name, csv_bytes)
        zf.writestr(img_name, jpg_bytes)


def make_zip_bad_csv(path, img_name="fault_test.jpg"):
    """生成 CSV 无法提取 (本地文件头损坏) 但 jpg 正常的 zip"""
    csv_name = Path(img_name).stem + ".csv"
    jpg_bytes = b"\xff\xd8\xff\xe0FAKE_JPG_DATA\xff\xd9"
    # 先正常写入
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(csv_name, "garbage")
        zf.writestr(img_name, jpg_bytes)
    # 破坏 csv 的本地文件头 magic number (第一个 PK\x03\x04)
    with open(path, "r+b") as f:
        data = f.read()
        idx = data.find(b"PK\x03\x04")
        data = data[:idx] + b"XX\x03\x04" + data[idx + 4:]
        f.seek(0)
        f.write(data)


def make_zip_bad_jpg(path, type_id=16, img_name="fault_test.jpg"):
    """生成 jpg 无法提取的 zip (破坏 jpg 的本地文件头 magic number)"""
    csv_bytes = make_csv(type_id, "弹条", img_name).encode("utf-8")
    csv_name = Path(img_name).stem + ".csv"
    jpg_bytes = b"\xff\xd8\xff\xe0FAKE_JPG_DATA\xff\xd9"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(csv_name, csv_bytes)
        zf.writestr(img_name, jpg_bytes)
    # 破坏 jpg 的本地文件头 magic number (PK\x03\x04 -> XX\x03\x04)
    # 这样 zipfile.open() 会抛 BadZipFile: Bad magic number for file header
    with open(path, "r+b") as f:
        data = f.read()
        idx1 = data.find(b"PK\x03\x04")
        idx2 = data.find(b"PK\x03\x04", idx1 + 1)
        # 把第二个文件头的 magic 改坏
        data = data[:idx2] + b"XX\x03\x04" + data[idx2 + 4:]
        f.seek(0)
        f.write(data)


def make_zip_bad_jpg_data(path, type_id=16, img_name="fault_test.jpg"):
    """生成 jpg 文件头正常但压缩数据损坏的 zip (zf.open() 成功但 read() 抛 CRC 错误)

    用于验证: 提取中途失败时不留空文件、不留空目录。
    """
    csv_bytes = make_csv(type_id, "弹条", img_name).encode("utf-8")
    csv_name = Path(img_name).stem + ".csv"
    jpg_bytes = b"\xff\xd8\xff\xe0FAKE_JPG_DATA\xff\xd9"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(csv_name, csv_bytes)
        zf.writestr(img_name, jpg_bytes)
    # 破坏 jpg 的压缩数据末尾 (在 jpg 数据区与中央目录头之间)
    # 这会让 zf.open() 成功 (文件头正常) 但 src.read() 抛 BadZipFile: Bad CRC-32
    with open(path, "r+b") as f:
        data = bytearray(f.read())
        i1 = data.find(b"PK\x03\x04")
        i2 = data.find(b"PK\x03\x04", i1 + 1)
        i_cd = data.find(b"PK\x01\x02")  # 中央目录头
        if i_cd > i2:
            # 破坏 jpg 数据区最后 4 字节
            for k in range(i_cd - 4, i_cd):
                data[k] = 0xFF
        f.seek(0)
        f.write(data)


def make_zip_no_csv(path, img_name="fault_test.jpg"):
    """生成只有 jpg 没有 csv 的 zip"""
    jpg_bytes = b"\xff\xd8\xff\xe0FAKE_JPG_DATA\xff\xd9"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(img_name, jpg_bytes)


def run_script(args, capture=True):
    """运行脚本，返回 (returncode, stdout_bytes, stderr_bytes)"""
    cmd = [sys.executable, str(SCRIPT)] + args
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    if capture:
        r = subprocess.run(cmd, capture_output=True, env=env)
        return r.returncode, r.stdout, r.stderr
    r = subprocess.run(cmd, env=env)
    return r.returncode, b"", b""


class TestClassCodeToName(unittest.TestCase):
    """测试 class_code_to_name 函数"""

    def test_known_codes(self):
        self.assertEqual(mod.class_code_to_name("16"), "弹条缺失")
        self.assertEqual(mod.class_code_to_name("32"), "弹条位移")
        self.assertEqual(mod.class_code_to_name("1024"), "道床裂开")
        self.assertEqual(mod.class_code_to_name("1536"), "道床异物")
        self.assertEqual(mod.class_code_to_name("8192"), "螺母缺失")

    def test_int_input(self):
        self.assertEqual(mod.class_code_to_name(48), "螺栓缺失")

    def test_empty(self):
        self.assertEqual(mod.class_code_to_name(""), "未知_元数据缺失")
        self.assertEqual(mod.class_code_to_name(None), "未知_元数据缺失")

    def test_unknown_code(self):
        self.assertEqual(mod.class_code_to_name("9999"), "未知_9999")

    def test_non_numeric(self):
        self.assertEqual(mod.class_code_to_name("abc"), "未知_无代码")


class TestParseZipFaultClass(unittest.TestCase):
    """测试 parse_zip_fault_class 函数"""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_normal(self):
        p = os.path.join(self.tmp, "fault_001.zip")
        make_zip(p, type_id=16, img_name="fault_001.jpg")
        result = mod.parse_zip_fault_class(p)
        self.assertIsNotNone(result)
        type_id, obj, img = result
        self.assertEqual(type_id, "16")
        self.assertEqual(obj, "弹条")
        self.assertEqual(img, "fault_001.jpg")

    def test_bad_csv(self):
        p = os.path.join(self.tmp, "fault_002.zip")
        make_zip_bad_csv(p, img_name="fault_002.jpg")
        result = mod.parse_zip_fault_class(p)
        # CSV 损坏时应返回 ("", "", img) 而不是 None
        self.assertIsNotNone(result)
        type_id, obj, img = result
        self.assertEqual(type_id, "")
        self.assertEqual(img, "fault_002.jpg")

    def test_no_csv(self):
        p = os.path.join(self.tmp, "fault_003.zip")
        make_zip_no_csv(p, img_name="fault_003.jpg")
        result = mod.parse_zip_fault_class(p)
        self.assertIsNotNone(result)
        type_id, obj, img = result
        self.assertEqual(type_id, "")
        self.assertEqual(img, "fault_003.jpg")

    def test_not_a_zip(self):
        p = os.path.join(self.tmp, "not_a_zip.zip")
        with open(p, "wb") as f:
            f.write(b"this is not a zip file")
        result = mod.parse_zip_fault_class(p)
        self.assertIsNone(result)


class TestSortZipsIntegration(unittest.TestCase):
    """端到端集成测试: 构造目录运行脚本"""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.src = os.path.join(self.tmp, "src")
        os.makedirs(self.src)
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _run(self, extra_args=None):
        out = os.path.join(self.tmp, "out")
        args = [self.src, "-o", out]
        if extra_args:
            args += extra_args
        rc, so, se = run_script(args)
        return rc, so.decode("utf-8", "replace"), se.decode("utf-8", "replace"), out

    def _count_jpgs(self, out_dir):
        result = {}
        for d in os.listdir(out_dir):
            full = os.path.join(out_dir, d)
            if os.path.isdir(full):
                result[d] = len([f for f in os.listdir(full) if f.endswith(".jpg")])
        return result

    def test_normal_classification(self):
        """正常 zip 应按中文缺陷名分类"""
        make_zip(os.path.join(self.src, "fault_001.zip"), type_id=16,
                 img_name="fault_001.jpg")
        make_zip(os.path.join(self.src, "fault_002.zip"), type_id=1024,
                 img_name="fault_002.jpg", obj="道床")
        make_zip(os.path.join(self.src, "fault_003.zip"), type_id=48,
                 img_name="fault_003.jpg", obj="螺母")
        rc, so, se, out = self._run()
        self.assertEqual(rc, 0, f"stdout={so}\nstderr={se}")
        counts = self._count_jpgs(out)
        self.assertEqual(counts.get("弹条缺失"), 1)
        self.assertEqual(counts.get("道床裂开"), 1)
        self.assertEqual(counts.get("螺栓缺失"), 1)
        # 验证图片内容正确
        with open(os.path.join(out, "弹条缺失", "fault_001.jpg"), "rb") as f:
            self.assertTrue(f.read().startswith(b"\xff\xd8\xff"))

    def test_unknown_code(self):
        """未知 type_id 应归到 未知_<code>"""
        make_zip(os.path.join(self.src, "fault_001.zip"), type_id=9999,
                 img_name="fault_001.jpg")
        rc, so, se, out = self._run()
        self.assertEqual(rc, 0)
        counts = self._count_jpgs(out)
        self.assertEqual(counts.get("未知_9999"), 1)

    def test_empty_class(self):
        """空 type_id 应归到 未知_元数据缺失"""
        # 构造 FAULTINF_CLASS 为空的 CSV
        csv_text = make_csv("", "弹条", "fault_001.jpg")
        p = os.path.join(self.src, "fault_001.zip")
        with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("fault_001.csv", csv_text)
            zf.writestr("fault_001.jpg", b"\xff\xd8FAKE\xff\xd9")
        rc, so, se, out = self._run()
        self.assertEqual(rc, 0)
        counts = self._count_jpgs(out)
        self.assertEqual(counts.get("未知_元数据缺失"), 1)

    def test_bad_csv_jpg_ok(self):
        """CSV 损坏但 jpg 正常 -> 归到 未知_元数据缺失"""
        make_zip_bad_csv(os.path.join(self.src, "fault_001.zip"),
                         img_name="fault_001.jpg")
        rc, so, se, out = self._run()
        self.assertEqual(rc, 0, f"stdout={so}\nstderr={se}")
        counts = self._count_jpgs(out)
        self.assertEqual(counts.get("未知_元数据缺失"), 1)

    def test_bad_jpg_cleanup(self):
        """jpg 损坏 -> 跳过, 不留空目录"""
        make_zip_bad_jpg(os.path.join(self.src, "fault_001.zip"),
                         type_id=16, img_name="fault_001.jpg")
        rc, so, se, out = self._run()
        # jpg 提取失败, total=0 -> exit 2
        self.assertEqual(rc, 2, f"stdout={so}\nstderr={se}")
        # 不应留下空目录
        if os.path.isdir(out):
            dirs = os.listdir(out)
            self.assertEqual(dirs, [], f"不应有空目录, 实际: {dirs}")

    def test_bad_jpg_data_no_empty_file(self):
        """jpg 文件头正常但数据损坏 (read() 抛 CRC 错误) -> 不留空文件, 不留空目录"""
        make_zip_bad_jpg_data(os.path.join(self.src, "fault_001.zip"),
                              type_id=16, img_name="fault_001.jpg")
        rc, so, se, out = self._run()
        # jpg 提取失败, total=0 -> exit 2
        self.assertEqual(rc, 2, f"stdout={so}\nstderr={se}")
        # 不应留下空目录
        if os.path.isdir(out):
            dirs = os.listdir(out)
            self.assertEqual(dirs, [], f"不应有空目录, 实际: {dirs}")
            # 更严格: 递归检查没有任何空文件残留
            for d in dirs:
                for f in os.listdir(os.path.join(out, d)):
                    fp = os.path.join(out, d, f)
                    self.assertGreater(os.path.getsize(fp), 0,
                                       f"残留空文件: {fp}")

    def test_duplicate_image_names(self):
        """同名图片应自动追加 _1/_2 避免覆盖"""
        make_zip(os.path.join(self.src, "fault_001.zip"), type_id=16,
                 img_name="same.jpg")
        make_zip(os.path.join(self.src, "fault_002.zip"), type_id=16,
                 img_name="same.jpg")
        rc, so, se, out = self._run()
        self.assertEqual(rc, 0)
        d = os.path.join(out, "弹条缺失")
        files = sorted(os.listdir(d))
        # 应有 same.jpg 和 same_1.jpg
        self.assertIn("same.jpg", files)
        self.assertIn("same_1.jpg", files)
        self.assertEqual(len(files), 2)

    def test_dry_run_no_files(self):
        """--dry-run 不应写出任何文件"""
        make_zip(os.path.join(self.src, "fault_001.zip"), type_id=16,
                 img_name="fault_001.jpg")
        out = os.path.join(self.tmp, "out")
        rc, so, se = run_script([self.src, "-o", out, "--dry-run"])[:2] + (b"",)
        so_text = so.decode("utf-8", "replace")
        self.assertEqual(rc, 0)
        self.assertIn("dry-run", so_text)
        self.assertIn("弹条缺失", so_text)
        # 输出目录不应被创建
        self.assertFalse(os.path.exists(out))

    def test_custom_output_dir(self):
        """-o 指定输出目录"""
        make_zip(os.path.join(self.src, "fault_001.zip"), type_id=16,
                 img_name="fault_001.jpg")
        custom = os.path.join(self.tmp, "custom_output")
        rc, so, se = run_script([self.src, "-o", custom])
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.isdir(os.path.join(custom, "弹条缺失")))

    def test_nonexistent_src_dir(self):
        """输入目录不存在 -> exit 1"""
        rc, so, se = run_script([os.path.join(self.tmp, "no_such_dir")])
        self.assertEqual(rc, 1)

    def test_empty_src_dir(self):
        """无 zip 的目录 -> exit 1"""
        rc, so, se = run_script([self.src])
        self.assertEqual(rc, 1)

    def test_mixed_scenario(self):
        """混合场景: 正常 + 损坏 + 未知 + 同名"""
        make_zip(os.path.join(self.src, "fault_001.zip"), type_id=16,
                 img_name="img.jpg")
        make_zip(os.path.join(self.src, "fault_002.zip"), type_id=16,
                 img_name="img.jpg")  # 同名
        make_zip(os.path.join(self.src, "fault_003.zip"), type_id=32,
                 img_name="fault_003.jpg")
        make_zip_bad_csv(os.path.join(self.src, "fault_004.zip"),
                         img_name="fault_004.jpg")
        make_zip_bad_jpg(os.path.join(self.src, "fault_005.zip"),
                         type_id=48, img_name="fault_005.jpg")
        rc, so, se, out = self._run()
        self.assertEqual(rc, 0, f"stdout={so}\nstderr={se}")
        counts = self._count_jpgs(out)
        # 弹条缺失: 2 (含同名)
        self.assertEqual(counts.get("弹条缺失"), 2)
        # 弹条位移: 1
        self.assertEqual(counts.get("弹条位移"), 1)
        # 未知_元数据缺失: 1 (bad csv)
        self.assertEqual(counts.get("未知_元数据缺失"), 1)
        # 螺栓缺失: 0 (bad jpg 被清理)
        self.assertNotIn("螺栓缺失", counts)
        # 合计 4
        total = sum(counts.values())
        self.assertEqual(total, 4)


class TestRealData(unittest.TestCase):
    """对真实数据目录的冒烟测试 (若存在)"""

    REAL_DIR = Path(__file__).resolve().parent.parent / "data" / "20260518" / "fault" / "20260518183516"

    def setUp(self):
        if not self.REAL_DIR.is_dir():
            self.skipTest(f"真实数据目录不存在: {self.REAL_DIR}")
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_real_data_dry_run(self):
        """真实数据 dry-run 应成功且分类数量正确"""
        rc, so, se = run_script([str(self.REAL_DIR), "--dry-run",
                                 "-o", os.path.join(self.tmp, "out")])
        so_text = so.decode("utf-8", "replace")
        self.assertEqual(rc, 0, f"stderr={se.decode()}")
        self.assertIn("压缩包数量: 148", so_text)
        # 7 个已知类别
        for name in ["弹条缺失", "弹条位移", "螺栓缺失", "垫片位移",
                     "轨枕剥离掉块", "道床裂开", "道床异物"]:
            self.assertIn(name, so_text)
        # dry-run 不实际提取 jpg, 损坏的 zip (fault_187188_70) 的 CSV 也读不出,
        # 会被算作 未知_元数据缺失, 合计 148
        self.assertIn("未知_元数据缺失", so_text)
        import re
        self.assertRegex(so_text, r"合计\s+148\s+张")

    def test_real_data_actual_run(self):
        """真实数据实际运行: 147 张成功, 1 个损坏 zip 被跳过, 无空目录"""
        out = os.path.join(self.tmp, "sorted")
        rc, so, se = run_script([str(self.REAL_DIR), "-o", out])
        so_text = so.decode("utf-8", "replace")
        se_text = se.decode("utf-8", "replace")
        self.assertEqual(rc, 0, f"stdout={so_text}\nstderr={se_text}")
        import re
        self.assertRegex(so_text, r"合计\s+147\s+张")
        self.assertIn("失败/跳过: 1 个", so_text)
        # 不应有空目录
        for d in os.listdir(out):
            full = os.path.join(out, d)
            self.assertTrue(os.path.isdir(full))
            files = os.listdir(full)
            self.assertGreater(len(files), 0, f"空目录: {d}")
        # 不应有 未知_元数据缺失 目录 (损坏 zip 的 jpg 也提取失败, 被清理)
        self.assertNotIn("未知_元数据缺失", os.listdir(out))


if __name__ == "__main__":
    unittest.main(verbosity=2)
