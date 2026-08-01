#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
人工补录缺陷工具 - GUI 可视化标注
打开原图 -> 画缺陷框(红) + 扣件区域框(黄) -> 选择缺陷类型 -> 生成与 C++ 输出一致的 ZIP 包

依赖: pip install opencv-python Pillow
"""

import os
import re
import io
import csv
import json
import copy
import zipfile
import datetime
import threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import cv2
import numpy as np
from PIL import Image, ImageTk


# ============================================================
# 配置常量 (与 C++ 代码保持一致)
# ============================================================
TRAIN_NUM_DEFAULT = "187188"
ROUTE_NO_DEFAULT = "3"
DEFAULT_COUNT_FASTENING = 2
CAMERA_CHOICES = ["L1", "L2", "R1", "R2"]

# 内置缺陷类型编码 (XLBH_type, XLBH_name), 来源于 config_code.xml
BUILTIN_XLBH_CODES = [
    ("2", "钢轨表面擦伤"),
    ("3", "钢轨剥离掉块"),
    ("16", "弹条缺失"),
    ("24", "弹条断裂"),
    ("32", "弹条位移"),
    ("48", "螺栓缺失"),
    ("49", "螺栓松脱"),
    ("50", "垫片位移"),
    ("51", "翻浆冒泥"),
    ("128", "轨枕裂开"),
    ("192", "轨枕剥离掉块"),
    ("1024", "道床裂开"),
    ("1536", "道床异物"),
    ("8192", "螺母缺失"),
]


def natural_sort_key(s):
    """自然排序 key: 数字部分按数值比较, 统一转为字符串避免混合类型 TypeError"""
    parts = []
    for t in re.split(r'(\d+)', str(s)):
        if t.isdigit():
            parts.append((0, int(t)))
        else:
            parts.append((1, t.lower()))
    return parts


# ============================================================
# 文件名/路径解析 (复刻 C++ 逻辑)
# ============================================================
def parse_baslib_index_from_name(image_name):
    """文件名第一个 _ 之前的部分 -> int"""
    stem = Path(image_name).stem
    idx = stem.find("_")
    text = stem if idx == -1 else stem[:idx]
    try:
        return int(text)
    except ValueError:
        return 0


def parse_location_mm_from_name(image_name):
    """文件名第一个 _ 和第二个 _ 之间的部分 -> int (毫米里程)"""
    stem = Path(image_name).stem
    first = stem.find("_")
    if first == -1:
        return 0
    second = stem.find("_", first + 1)
    if second == -1:
        return 0
    text = stem[first + 1:second]
    try:
        return int(text)
    except ValueError:
        return 0


def parse_timestamp_ms_from_name(image_name):
    """文件名最后一个 _ 之后的部分 -> int (毫秒时间戳)"""
    stem = Path(image_name).stem
    idx = stem.rfind("_")
    if idx == -1 or idx + 1 >= len(stem):
        return 0
    text = stem[idx + 1:]
    try:
        return int(text)
    except ValueError:
        return 0


def timestamp_ms_to_datetime_text(timestamp_ms):
    """毫秒时间戳 -> 'YYYY/MM/DD HH:MM:SS' (使用本地时区, 与 C++ 一致)"""
    if timestamp_ms <= 0:
        return ""
    seconds = timestamp_ms // 1000
    try:
        dt = datetime.datetime.fromtimestamp(seconds)
        return dt.strftime("%Y/%m/%d %H:%M:%S")
    except (OSError, ValueError, OverflowError):
        return ""


def generate_time_from_image_name(image_name):
    ts = parse_timestamp_ms_from_name(image_name)
    text = timestamp_ms_to_datetime_text(ts)
    return text if text else datetime.datetime.now().strftime("%Y/%m/%d %H:%M:%S")


def get_raw_camera_dir_from_path(path_text):
    """路径的父目录名，以 L/R 开头则返回，否则空"""
    if not path_text:
        return ""
    p = Path(path_text)
    camera_dir = p.parent.name
    if len(camera_dir) >= 2 and camera_dir[0] in "LlRr":
        return camera_dir
    return ""


def camera_position_from_path(path_text):
    """从路径解析相机位置: L/R"""
    camera_dir = get_raw_camera_dir_from_path(path_text)
    if not camera_dir:
        return ""
    first = camera_dir[0]
    if first in "Ll":
        return "L"
    if first in "Rr":
        return "R"
    return ""


def parse_camera_num_from_path(path_text):
    """从路径解析相机编号: camera_dir[1:] -> int"""
    camera_dir = get_raw_camera_dir_from_path(path_text)
    if len(camera_dir) < 2:
        return 0
    try:
        return int(camera_dir[1:])
    except ValueError:
        return 0


def sanitize_filename(text):
    """过滤文件名中的非法字符 (防止目录穿越和非法路径)"""
    return re.sub(r'[\\/:*?"<>|]', '_', text)


def normalize_type_code(type_code):
    """去掉 XLBH- 前缀"""
    prefix = "XLBH-"
    if type_code.startswith(prefix):
        type_code = type_code[len(prefix):]
    return type_code


def infer_fault_object_name(type_id, type_name=""):
    """根据 type_name 和 type_id 推断故障对象名 (复刻 C++ infer_fault_object_name)
    先按 type_name 关键字匹配, 再按 type_id 映射, default 返回 type_name
    """
    # 1. 先按 type_name 关键字匹配
    if type_name:
        if "钢轨" in type_name or "轨面" in type_name:
            return "钢轨顶面"
        if "弹条" in type_name:
            return "弹条"
        if "螺母" in type_name or "螺栓" in type_name:
            return "螺母"
        if "道床" in type_name:
            return "道床"
        if "感应板" in type_name:
            return "感应板"

    # 2. 再按 type_id 映射
    if type_id in (2, 3):
        return "钢轨顶面"
    if type_id in (16, 24, 32):
        return "弹条"
    if type_id in (48, 49, 50, 8192):
        return "螺母"
    if type_id in (51, 128, 192, 1024, 1536):
        return "道床"

    # 3. default 返回原始 type_name (与 C++ 一致)
    return type_name if type_name else "其他"


def fault_object_id_from_name(object_name):
    """故障对象名 -> ID"""
    mapping = {
        "钢轨顶面": 0,
        "弹条": 1,
        "螺母": 2,
        "道床": 3,
        "感应板": 4,
    }
    return mapping.get(object_name, 0)


def clamp_rect(x, y, w, h, img_w, img_h):
    """边界裁剪 (复刻 C++ clamp_rect)"""
    if img_w <= 0 or img_h <= 0:
        return None
    x1 = max(0, x)
    y1 = max(0, y)
    x2 = min(img_w, x + w)
    y2 = min(img_h, y + h)
    if x2 <= x1 or y2 <= y1:
        return None
    return (x1, y1, x2 - x1, y2 - y1)


# ============================================================
# 序号分配 (复刻 C++ allocate_defect_serial_block)
# ============================================================
def find_existing_defect_serial_max(defect_folder, train_num_text):
    """扫描目录中已有的 fault_<train_num>_<serial>.* 文件，返回最大序号"""
    max_serial = -1
    pattern = re.compile(
        r"fault_" + re.escape(train_num_text) + r"_(\d+)$"
    )
    if not os.path.isdir(defect_folder):
        return max_serial
    for name in os.listdir(defect_folder):
        p = Path(name)
        if p.suffix.lower() not in (".csv", ".jpg", ".zip"):
            continue
        m = pattern.match(p.stem)
        if m:
            serial = int(m.group(1))
            if serial > max_serial:
                max_serial = serial
    return max_serial


def allocate_defect_serial(defect_folder, train_num_text):
    """分配下一个可用序号 (最小为 1)"""
    return max(1, find_existing_defect_serial_max(defect_folder, train_num_text) + 1)


# ============================================================
# CSV 生成 (复刻 C++ build_one_defect_csv_text)
# ============================================================
CSV_HEADERS = [
    "ID",
    "FAULTINF_BASLIB_INDEX",
    "FAULTINF_BASLIB_IMGNAME",
    "FAULTINF_IMGNAME",
    "FAULTINF_PART_IMGNAME",
    "FAULTINF_IMGPATH",
    "FAULTINF_OVER_NUM",
    "FAULTINF_LEVEL",
    "FAULTINF_START_STATION",
    "FAULTINF_STOP_STATION",
    "FAULTINF_ROUTENO",
    "FAULTINF_TRAIN_NUM",
    "FAULTINF_CAM_POSITION",
    "FAULTINF_TRAINCATETYPE",
    "FAULTINF_RECOGNITION_NUM",
    "FAULTINF_OBJECT",
    "FAULTINF_CLASS",
    "FAULTINF_POS_X",
    "FAULTINF_POS_Y",
    "FAULTINF_POS_W",
    "FAULTINF_POS_H",
    "FAULTINF_PROC_STATUS",
    "FAULTINF_PROC_RESULT",
    "FAULTINF_DOWNLOAD_TIME",
    "FAULTINF_FEEDBACK_TIME",
    "FAULTINF_MAINTENANCE",
    "FAULTINF_OPERATOR_NAME",
    "FAULTINF_CONFIRM_TIME",
    "FAULTINF_REPAIR_FAULT_IDENTIFICATION_COUNT",
    "FAULTINF_TEMP_IMAGE_CHECK_COUNT",
    "FAULTINF_DETE_KM_MARK",
    "FAULTINF_BASIS_KM_MARK",
    "FAULTINF_GENERATE_TIME",
    "FAULTINF_LOCATION_MM",
    "FAULTINF_OBJECT_ID",
    "FAULTINF_TYPE_ID",
    "FAULTINF_CAM_NUM",
    "FAULTINF_DETE_IMAGE_NAME",
]


def build_one_defect_csv_text(export_image_name, defect_id_text,
                              image_path, source_image_name,
                              type_code, pos_x, pos_y, pos_w, pos_h,
                              cam_position_override="", cam_num_override=-1,
                              train_cate_type_override="",
                              fault_object_override="", type_name="",
                              route_no=ROUTE_NO_DEFAULT, train_num=TRAIN_NUM_DEFAULT):
    """构建单缺陷 CSV 文本 (38 字段, 与 C++ build_one_defect_csv_text 一致)
    可通过 override 参数手动指定相机号、方向和故障对象, 为空则自动推断
    """
    type_id = int(type_code) if str(type_code).isdigit() else 0
    if cam_num_override >= 0:
        cam_num = cam_num_override
    else:
        cam_num = parse_camera_num_from_path(image_path)
    if cam_position_override:
        cam_position = cam_position_override
    else:
        cam_position = camera_position_from_path(image_path)
    if not cam_position:
        cam_position = f"camera{cam_num}" if cam_num > 0 else "camera0"
    if train_cate_type_override:
        train_cate_type = train_cate_type_override
    else:
        train_cate_type = "U"
    if fault_object_override:
        fault_object = fault_object_override
    else:
        fault_object = infer_fault_object_name(type_id, type_name)
    fault_object_id = fault_object_id_from_name(fault_object)
    location_mm = parse_location_mm_from_name(source_image_name)
    dete_km_mark = str(location_mm) if location_mm > 0 else ""
    basis_km_mark = dete_km_mark
    baslib_index = parse_baslib_index_from_name(source_image_name)
    generate_time = generate_time_from_image_name(source_image_name)

    row_values = [
        defect_id_text,
        baslib_index,
        "",
        export_image_name,
        export_image_name,
        image_path,
        1,
        "",
        "",
        "",
        route_no if route_no else ROUTE_NO_DEFAULT,
        train_num if train_num else TRAIN_NUM_DEFAULT,
        cam_position,
        train_cate_type,
        1,
        fault_object,
        type_code,
        pos_x,
        pos_y,
        pos_w,
        pos_h,
        0,
        "",
        "",
        "",
        "",
        "",
        "",
        0,
        0,
        dete_km_mark,
        basis_km_mark,
        generate_time,
        location_mm,
        fault_object_id,
        type_id,
        cam_num,
        source_image_name,
    ]

    output = io.StringIO()
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(CSV_HEADERS)
    writer.writerow(row_values)
    return output.getvalue()


# ============================================================
# 图片渲染 (复刻 C++ render_one_defect_image)
# ============================================================
def render_one_defect_image(image, defect_box, area_box=None,
                            count_fastening=DEFAULT_COUNT_FASTENING,
                            expected_count_fastening=DEFAULT_COUNT_FASTENING):
    """
    渲染单缺陷图片 (复刻 C++ render_one_defect_image)
    - 按比例缩放整图高度
    - 画扣件区域框 (黄色, 2px)
    - 画缺陷框 (红色, 2px)
    - 使用 clamp_rect 确保画框不超出图片边界
    返回渲染后的完整图片 (非裁剪)
    """
    safe_expected = expected_count_fastening if expected_count_fastening > 0 else DEFAULT_COUNT_FASTENING
    safe_count = count_fastening if count_fastening > 0 else safe_expected
    scale_y = safe_count / float(safe_expected)

    if scale_y != 1.0:
        new_h = max(1, round(image.shape[0] * scale_y))
        scaled = cv2.resize(image, (image.shape[1], new_h))
    else:
        scaled = image

    img_h, img_w = scaled.shape[:2]

    has_area = area_box is not None and area_box[2] > 0 and area_box[3] > 0
    has_defect = defect_box is not None and defect_box[2] > 0 and defect_box[3] > 0
    if not has_area and not has_defect:
        return scaled

    full = scaled.copy()

    # 画扣件区域框 (黄色, 2px) - 使用 clamp_rect
    if area_box is not None and area_box[2] > 0 and area_box[3] > 0:
        ax, ay, aw, ah = area_box
        ay_s = round(ay * scale_y)
        ah_s = round(ah * scale_y)
        clamped = clamp_rect(ax, ay_s, aw, ah_s, img_w, img_h)
        if clamped:
            cx, cy, cw, ch = clamped
            cv2.rectangle(full, (cx, cy), (cx + cw, cy + ch),
                          (0, 255, 255), 2)

    # 画缺陷框 (红色, 2px) - 使用 clamp_rect
    if defect_box is not None and defect_box[2] > 0 and defect_box[3] > 0:
        dx, dy, dw, dh = defect_box
        dy_s = round(dy * scale_y)
        dh_s = round(dh * scale_y)
        clamped = clamp_rect(dx, dy_s, dw, dh_s, img_w, img_h)
        if clamped:
            cx, cy, cw, ch = clamped
            cv2.rectangle(full, (cx, cy), (cx + cw, cy + ch),
                          (0, 0, 255), 2)

    return full


# ============================================================
# ZIP 打包
# ============================================================
def create_defect_zip(zip_path, csv_text, jpg_bytes):
    """创建 ZIP 包, 包含 CSV 和 JPG (与 C++ 一致: ZIP 内只有 csv + jpg)"""
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        stem = Path(zip_path).stem
        zf.writestr(f"{stem}.csv", csv_text)
        if jpg_bytes:
            zf.writestr(f"{stem}.jpg", jpg_bytes)


# ============================================================
# GUI 应用
# ============================================================
class ManualDefectTool:
    def __init__(self, root):
        self.root = root
        self.root.title("人工补录缺陷工具")
        self.root.geometry("1400x850")

        # 数据
        self.xlbh_codes = []
        self.image_path = None
        self.cv_image = None
        self.display_image = None
        self.tk_image = None
        self.current_defect_box = None  # (x, y, w, h) 当前正在绘制的缺陷框
        self.current_area_box = None    # (x, y, w, h) 当前正在绘制的区域框
        self.defect_list = []           # [{"box": (x,y,w,h), "area_box": (x,y,w,h), "type_code": str, "type_name": str}, ...]
        self.drawing = False
        self.draw_start = None
        self.scale_factor = 1.0         # 显示缩放比例 (fit-to-canvas)
        self.zoom_factor = 1.0           # 用户额外缩放倍数
        self.img_offset_x = 0            # 图片在画布中的 x 偏移
        self.img_offset_y = 0            # 图片在画布中的 y 偏移
        self._display_pending_id = None  # _display_image 延迟调用的 ID
        self.preview_window = None
        self._pv_on_close = None
        self._pv_state = None       # 预览窗口状态字典
        self._pv_render_fn = None   # 预览渲染函数 (pv_render_defect)
        self.panning = False
        self.pan_start = None
        self._dirty = False               # 标注是否有未保存的修改
        self._auto_scroll_after_id = None  # auto_scroll 节流
        self._save_debounce_id = None     # 标注保存防抖
        self._gen_thread = None    # 生成线程
        self._gen_cancel = False   # 生成取消标志

        # 文件夹批量输入
        self.image_file_list = []       # 所有图片路径
        self.current_image_index = -1   # 当前图片索引
        # 每张图片的标注数据: {image_path: {"defects": [...], "area_box": (x,y,w,h)}}
        self.annotations = {}

        # 构建UI
        self._build_ui()

        # 加载内置缺陷类型编码
        self._load_builtin_codes()

        # 键盘快捷键 (labelimg 风格)
        self.root.bind("<Control-z>", lambda e: self._undo())
        self.root.bind("<Control-Z>", lambda e: self._undo())
        self.root.bind("<Delete>", lambda e: self._shortcut_or_text(e, self._delete_selected_defect))
        self.root.bind("<plus>", lambda e: self._shortcut_or_text(e, lambda: self._zoom_by(1.2)))
        self.root.bind("<equal>", lambda e: self._shortcut_or_text(e, lambda: self._zoom_by(1.2)))
        self.root.bind("<minus>", lambda e: self._shortcut_or_text(e, lambda: self._zoom_by(1/1.2)))
        self.root.bind("<f>", lambda e: self._shortcut_or_text(e, self._reset_zoom))
        self.root.bind("<a>", lambda e: self._shortcut_or_text(e, self.prev_image))
        self.root.bind("<d>", lambda e: self._shortcut_or_text(e, self.next_image))
        self.root.bind("<space>", lambda e: self._shortcut_or_text(e, self._toggle_mode))
        self.root.bind("<Control-s>", lambda e: self._save_json())
        self.root.bind("<Control-S>", lambda e: self._save_json())
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _is_text_widget_focused(self):
        """检查当前焦点是否在文本输入控件上 (Entry/Combobox/Spinbox)"""
        focus = self.root.focus_get()
        if focus is None:
            return False
        return isinstance(focus, (tk.Entry, ttk.Entry, ttk.Combobox, ttk.Spinbox))

    def _shortcut_or_text(self, event, callback):
        """单字符快捷键: 焦点在输入控件时不拦截, 让控件正常处理"""
        if self._is_text_widget_focused():
            return
        callback()

    def _on_close(self):
        """关闭窗口: 保存标注并清理资源"""
        if self._gen_thread and self._gen_thread.is_alive():
            if not messagebox.askyesno("确认", "正在生成缺陷文件, 关闭将取消生成。继续?"):
                return
            self._gen_cancel = True
            self._gen_thread.join(timeout=3)
        self._save_current_annotations()
        if self._dirty and not self.autosave_var.get():
            if messagebox.askyesno("未保存", "当前图片标注未保存，是否保存？"):
                self._flush_annotations()
            else:
                self._dirty = False
        else:
            self._flush_annotations()
        if self.preview_window and self._pv_on_close:
            try:
                self._pv_on_close()
            except Exception:
                self.preview_window = None
                self._pv_state = None
                self._pv_render_fn = None
                self._pv_on_close = None
        self.root.destroy()

    def _refresh_preview_if_open(self):
        """如果预览窗口打开, 刷新当前缺陷的渲染 (保留缩放/索引等状态)"""
        if self.preview_window and self.preview_window.winfo_exists() and self._pv_render_fn:
            idx = self._pv_state["index"] if self._pv_state else 0
            self._pv_render_fn(idx)

    def _build_ui(self):
        # 顶部工具栏 (第一行: 文件操作 + 基本参数)
        top_frame = ttk.Frame(self.root, padding=(5, 3))
        top_frame.pack(side=tk.TOP, fill=tk.X)

        ttk.Button(top_frame, text="打开图片", command=self.open_image).pack(side=tk.LEFT, padx=3)
        ttk.Button(top_frame, text="打开文件夹", command=self.open_folder).pack(side=tk.LEFT, padx=3)
        ttk.Button(top_frame, text="上一张", command=self.prev_image).pack(side=tk.LEFT, padx=3)
        ttk.Button(top_frame, text="下一张", command=self.next_image).pack(side=tk.LEFT, padx=3)
        ttk.Button(top_frame, text="保存JSON(Ctrl+S)", command=self._save_json).pack(side=tk.LEFT, padx=3)
        self.autosave_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(top_frame, text="自动保存", variable=self.autosave_var).pack(side=tk.LEFT, padx=3)

        ttk.Label(top_frame, text="  列车号:").pack(side=tk.LEFT, padx=(10, 2))
        self.train_num_var = tk.StringVar(value=TRAIN_NUM_DEFAULT)
        ttk.Entry(top_frame, textvariable=self.train_num_var, width=10).pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame, text="  线路号:").pack(side=tk.LEFT, padx=(5, 2))
        self.route_no_var = tk.StringVar(value=ROUTE_NO_DEFAULT)
        ttk.Entry(top_frame, textvariable=self.route_no_var, width=5).pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame, text="  缺陷类型:").pack(side=tk.LEFT, padx=(10, 2))
        self.type_var = tk.StringVar()
        self.type_combo = ttk.Combobox(top_frame, textvariable=self.type_var,
                                       state="readonly", width=25)
        self.type_combo.pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame, text="  count_fastening:").pack(side=tk.LEFT, padx=(10, 2))
        self.count_var = tk.IntVar(value=DEFAULT_COUNT_FASTENING)
        ttk.Spinbox(top_frame, from_=1, to=10, width=5,
                    textvariable=self.count_var).pack(side=tk.LEFT, padx=2)

        # 顶部工具栏 (第二行: 输出参数 + 操作按钮)
        top_frame2 = ttk.Frame(self.root, padding=(5, 3))
        top_frame2.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(top_frame2, text="  相机号:").pack(side=tk.LEFT, padx=(3, 2))
        self.cam_var = tk.StringVar(value="L1")
        ttk.Combobox(top_frame2, textvariable=self.cam_var,
                     values=CAMERA_CHOICES, width=6,
                     state="readonly").pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame2, text="  故障对象:").pack(side=tk.LEFT, padx=(10, 2))
        self.object_var = tk.StringVar(value="自动")
        object_items = ["自动", "钢轨顶面", "弹条", "螺母", "道床", "感应板"]
        ttk.Combobox(top_frame2, textvariable=self.object_var, values=object_items,
                     width=10, state="readonly").pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame2, text="  上下行:").pack(side=tk.LEFT, padx=(10, 2))
        self.direction_var = tk.StringVar(value="U")
        ttk.Combobox(top_frame2, textvariable=self.direction_var,
                     values=["U", "D"], width=4).pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame2, text="  起始序号:").pack(side=tk.LEFT, padx=(10, 2))
        self.start_serial_var = tk.StringVar()
        ttk.Entry(top_frame2, textvariable=self.start_serial_var, width=8).pack(side=tk.LEFT, padx=2)

        ttk.Label(top_frame2, text="  输出目录:").pack(side=tk.LEFT, padx=(10, 2))
        self.output_var = tk.StringVar()
        ttk.Entry(top_frame2, textvariable=self.output_var, width=35).pack(side=tk.LEFT, padx=2)
        ttk.Button(top_frame2, text="浏览...", command=self.browse_output).pack(side=tk.LEFT, padx=2)

        ttk.Button(top_frame2, text="预览渲染", command=self._preview_render).pack(side=tk.LEFT, padx=(10, 3))
        ttk.Button(top_frame2, text="生成压缩包", command=self.generate_all,
                   style="Accent.TButton").pack(side=tk.LEFT, padx=(5, 3))

        # 模式切换 + 操作按钮
        mode_frame = ttk.Frame(self.root, padding=2)
        mode_frame.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(mode_frame, text="标注模式:").pack(side=tk.LEFT, padx=3)
        self.label_mode_var = tk.StringVar(value="single")
        ttk.Radiobutton(mode_frame, text="单标签", variable=self.label_mode_var,
                        value="single", command=self._on_label_mode_change).pack(side=tk.LEFT, padx=5)
        ttk.Radiobutton(mode_frame, text="多标签", variable=self.label_mode_var,
                        value="multi", command=self._on_label_mode_change).pack(side=tk.LEFT, padx=5)
        ttk.Separator(mode_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)
        ttk.Label(mode_frame, text="绘制:").pack(side=tk.LEFT, padx=3)
        self.mode_var = tk.StringVar(value="defect")
        ttk.Radiobutton(mode_frame, text="缺陷框(红)", variable=self.mode_var,
                        value="defect").pack(side=tk.LEFT, padx=5)
        ttk.Radiobutton(mode_frame, text="扣件区域框(黄)", variable=self.mode_var,
                        value="area").pack(side=tk.LEFT, padx=5)
        ttk.Button(mode_frame, text="添加到缺陷列表", command=self._add_current_defect).pack(side=tk.LEFT, padx=10)
        ttk.Button(mode_frame, text="撤销(Ctrl+Z)", command=self._undo).pack(side=tk.LEFT, padx=5)
        ttk.Button(mode_frame, text="删除选中", command=self._delete_selected_defect).pack(side=tk.LEFT, padx=5)
        ttk.Button(mode_frame, text="清除当前区域框", command=lambda: self._clear_box("area")).pack(side=tk.LEFT, padx=5)
        ttk.Button(mode_frame, text="清除全部", command=self._clear_all).pack(side=tk.LEFT, padx=5)
        ttk.Separator(mode_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)
        ttk.Button(mode_frame, text="放大", command=lambda: self._zoom_by(1.2)).pack(side=tk.LEFT, padx=2)
        ttk.Button(mode_frame, text="缩小", command=lambda: self._zoom_by(1/1.2)).pack(side=tk.LEFT, padx=2)
        ttk.Button(mode_frame, text="重置缩放", command=self._reset_zoom).pack(side=tk.LEFT, padx=2)
        ttk.Label(mode_frame, text="  (滚轮缩放 Ctrl+滚轮水平 右键平移 +/-/f/a/d/空格 Ctrl+S保存JSON)").pack(side=tk.LEFT, padx=5)

        # 主体: 左侧画布 + 右侧缺陷列表
        body_frame = ttk.Frame(self.root)
        body_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # 左侧画布 (支持滚动)
        canvas_frame = ttk.Frame(body_frame)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.canvas = tk.Canvas(canvas_frame, bg="#2b2b2b", cursor="crosshair",
                                 highlightthickness=0)
        canvas_scroll_y = ttk.Scrollbar(canvas_frame, orient=tk.VERTICAL,
                                         command=self.canvas.yview)
        canvas_scroll_x = ttk.Scrollbar(canvas_frame, orient=tk.HORIZONTAL,
                                         command=self.canvas.xview)
        self.canvas.configure(yscrollcommand=canvas_scroll_y.set,
                              xscrollcommand=canvas_scroll_x.set)
        canvas_scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        canvas_scroll_x.pack(side=tk.BOTTOM, fill=tk.X)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.canvas.bind("<Button-1>", self._on_mouse_down)
        self.canvas.bind("<B1-Motion>", self._on_mouse_move)
        self.canvas.bind("<ButtonRelease-1>", self._on_mouse_up)
        self.canvas.bind("<MouseWheel>", self._on_wheel)
        self.canvas.bind("<Control-MouseWheel>", self._on_wheel_horizontal)
        # 右键平移 (labelimg 风格)
        self.canvas.bind("<Button-3>", self._on_pan_start)
        self.canvas.bind("<B3-Motion>", self._on_pan_move)
        self.canvas.bind("<ButtonRelease-3>", self._on_pan_end)
        # 阻止中键默认行为
        self.canvas.bind("<Button-2>", lambda e: "break")

        # 右侧缺陷列表
        right_frame = ttk.Frame(body_frame, width=350)
        right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=(5, 5))
        right_frame.pack_propagate(False)

        ttk.Label(right_frame, text="已标注缺陷列表:", padding=5).pack(anchor=tk.W)
        list_frame = ttk.Frame(right_frame)
        list_frame.pack(fill=tk.BOTH, expand=True)
        self.defect_tree = ttk.Treeview(list_frame,
                                        columns=("idx", "type", "coords"),
                                        show="headings", height=15)
        self.defect_tree.heading("idx", text="#")
        self.defect_tree.heading("type", text="缺陷类型")
        self.defect_tree.heading("coords", text="坐标(x,y,w,h)")
        self.defect_tree.column("idx", width=30, anchor=tk.CENTER)
        self.defect_tree.column("type", width=120)
        self.defect_tree.column("coords", width=180)
        self.defect_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        tree_scroll = ttk.Scrollbar(list_frame, orient=tk.VERTICAL,
                                    command=self.defect_tree.yview)
        tree_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.defect_tree.configure(yscrollcommand=tree_scroll.set)

        # 图片导航信息 + 进度条
        nav_frame = ttk.Frame(self.root, padding=2)
        nav_frame.pack(side=tk.BOTTOM, fill=tk.X)
        self.nav_var = tk.StringVar(value="")
        ttk.Label(nav_frame, textvariable=self.nav_var, anchor=tk.W).pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.nav_progress = ttk.Progressbar(nav_frame, length=200, mode="determinate")
        self.nav_progress.pack(side=tk.RIGHT, padx=(5, 0))
        self.nav_annotated_var = tk.StringVar(value="")
        ttk.Label(nav_frame, textvariable=self.nav_annotated_var, anchor=tk.E, width=15).pack(side=tk.RIGHT, padx=(5, 0))

        # 状态栏
        self.status_var = tk.StringVar(value="请打开图片或文件夹开始标注  (Ctrl+Z撤销, Delete删除选中)")
        ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN,
                  anchor=tk.W).pack(side=tk.BOTTOM, fill=tk.X)

    def _load_builtin_codes(self):
        """加载内置缺陷类型编码到下拉框"""
        self.xlbh_codes = list(BUILTIN_XLBH_CODES)
        display_items = [f"{name} ({code})" for code, name in self.xlbh_codes]
        self.type_combo["values"] = display_items
        if display_items:
            self.type_combo.current(0)

    def _get_annotation_file_for_image(self, image_path):
        """返回单张图片的标注文件路径 (与图片同目录, 同名 .json)"""
        return Path(image_path).with_suffix('.json')

    def _save_annotations_to_file(self):
        """将当前图片的标注保存到独立的 JSON 文件 (labelimg 风格)"""
        if self.image_path is None:
            return
        ann = self.annotations.get(self.image_path)
        if ann is None:
            return
        try:
            json_path = self._get_annotation_file_for_image(self.image_path)
            with open(json_path, "w", encoding="utf-8") as f:
                json.dump(ann, f, ensure_ascii=False, indent=2)
        except Exception as e:
            self.status_var.set(f"警告: 标注保存失败: {e}")

    @staticmethod
    def _normalize_box(box):
        """将 box 坐标归一化为 tuple, 确保 JSON 加载后与运行时一致"""
        if box is None:
            return None
        return tuple(box)

    def _normalize_annotation(self, ann):
        """归一化标注数据: 将所有 box 坐标转为 tuple (JSON 加载时为 list)"""
        for item in ann.get("defects", []):
            if "box" in item:
                item["box"] = self._normalize_box(item["box"])
            if "area_box" in item:
                item["area_box"] = self._normalize_box(item["area_box"])
        if "current_defect_box" in ann:
            ann["current_defect_box"] = self._normalize_box(ann["current_defect_box"])
        if "current_area_box" in ann:
            ann["current_area_box"] = self._normalize_box(ann["current_area_box"])
        return ann

    def _load_annotations_for_images(self, image_paths):
        """从每张图片对应的 .json 文件加载标注到内存"""
        loaded = 0
        for img_path in image_paths:
            json_path = self._get_annotation_file_for_image(img_path)
            if json_path.exists():
                try:
                    with open(json_path, "r", encoding="utf-8") as f:
                        ann = json.load(f)
                    if "defects" in ann:
                        self.annotations[img_path] = self._normalize_annotation(ann)
                        loaded += 1
                except Exception as e:
                    print(f"警告: 加载标注文件失败 {json_path}: {e}")
        return loaded

    def open_image(self):
        path = filedialog.askopenfilename(
            title="选择检测原图",
            filetypes=[("Image files", "*.jpg *.jpeg *.png *.bmp"), ("All files", "*.*")])
        if not path:
            return

        # 保存旧图片的标注
        self._save_current_annotations()
        self._flush_annotations()

        # 切换到新图片
        self.annotations.clear()
        self.image_file_list = [path]
        self.current_image_index = 0
        self._load_annotations_for_images(self.image_file_list)
        self._load_image_by_index(0)

    def open_folder(self):
        folder = filedialog.askdirectory(title="选择包含检测图片的文件夹")
        if not folder:
            return

        # 递归扫描文件夹及子文件夹中的图片文件 (先过滤再排序)
        extensions = {".jpg", ".jpeg", ".png", ".bmp"}

        files = [str(f) for f in Path(folder).rglob("*")
                 if f.is_file() and f.suffix.lower() in extensions]
        files.sort(key=natural_sort_key)

        if not files:
            messagebox.showwarning("警告", f"文件夹中未找到图片文件:\n{folder}")
            return

        # 保存旧文件夹的标注
        self._save_current_annotations()
        self._flush_annotations()

        # 切换到新文件夹
        self.annotations.clear()
        self.image_file_list = files
        restored = self._load_annotations_for_images(files)
        self.current_image_index = 0
        self._load_image_by_index(0)
        msg = f"已加载 {len(files)} 张图片: {folder}"
        if restored > 0:
            msg += f"  (已恢复 {restored} 张图片的标注)"
        self.status_var.set(msg)

    def prev_image(self):
        if not self.image_file_list or self.current_image_index <= 0:
            return
        if not self._check_save_before_switch():
            return
        self._load_image_by_index(self.current_image_index - 1)

    def next_image(self):
        if not self.image_file_list or self.current_image_index >= len(self.image_file_list) - 1:
            return
        if not self._check_save_before_switch():
            return
        self._load_image_by_index(self.current_image_index + 1)

    def _save_current_annotations(self):
        """保存当前图片的标注数据到 annotations 字典 (内存), 自动保存时延迟持久化到文件"""
        if self.image_path is None:
            return
        new_ann = {
            "defects": copy.deepcopy(self.defect_list),
            "current_defect_box": copy.deepcopy(self.current_defect_box),
            "current_area_box": copy.deepcopy(self.current_area_box),
            "cam": self.cam_var.get(),
            "direction": self.direction_var.get(),
            "object": self.object_var.get(),
            "count_fastening": self.count_var.get(),
            "label_mode": self.label_mode_var.get(),
        }
        old_ann = self.annotations.get(self.image_path)
        if old_ann == new_ann:
            return
        self.annotations[self.image_path] = new_ann
        self._dirty = True
        # 自动保存: 防抖 500ms 写文件
        if self.autosave_var.get():
            if self._save_debounce_id is not None:
                self.root.after_cancel(self._save_debounce_id)
            self._save_debounce_id = self.root.after(500, self._flush_annotations)

    def _flush_annotations(self):
        """立即将标注持久化到 JSON 文件"""
        if self._save_debounce_id is not None:
            self.root.after_cancel(self._save_debounce_id)
            self._save_debounce_id = None
        self._save_annotations_to_file()
        self._dirty = False

    def _save_json(self):
        """Ctrl+S: 保存当前图片标注到 JSON 文件"""
        self._save_current_annotations()
        self._flush_annotations()
        if self.image_path:
            self.status_var.set(f"已保存: {Path(self.image_path).stem}.json")

    def _check_save_before_switch(self):
        """切换图片前检查未保存的标注, 返回 True 表示可以切换"""
        self._save_current_annotations()
        if self._dirty and not self.autosave_var.get():
            answer = messagebox.askyesnocancel("未保存", "当前图片标注未保存，是否保存？")
            if answer is None:
                return False  # 取消
            if answer:
                self._flush_annotations()
            else:
                self._dirty = False  # 不保存, 放弃修改
        else:
            self._flush_annotations()
        return True

    def _load_image_by_index(self, index):
        """加载指定索引的图片, 并恢复其标注数据"""
        if index < 0 or index >= len(self.image_file_list):
            return

        path = self.image_file_list[index]
        try:
            cv_img = cv2.imdecode(np.fromfile(path, dtype=np.uint8), cv2.IMREAD_COLOR)
        except Exception as e:
            messagebox.showerror("错误", f"读取图片失败:\n{path}\n{e}")
            return
        if cv_img is None:
            messagebox.showerror("错误", f"无法读取图片:\n{path}")
            return
        self.current_image_index = index
        self.image_path = path
        self.cv_image = cv_img

        # 恢复已保存的标注数据和配置
        ann = self.annotations.get(path)
        if ann:
            self.defect_list = copy.deepcopy(ann.get("defects", []))
            self.current_defect_box = copy.deepcopy(ann.get("current_defect_box"))
            self.current_area_box = copy.deepcopy(ann.get("current_area_box"))
            # 恢复独立配置
            if "cam" in ann:
                self.cam_var.set(ann["cam"])
            if "direction" in ann:
                self.direction_var.set(ann["direction"])
            if "object" in ann:
                self.object_var.set(ann["object"])
            if "count_fastening" in ann:
                self.count_var.set(ann["count_fastening"])
            if "label_mode" in ann:
                self.label_mode_var.set(ann["label_mode"])
        else:
            self.defect_list = []
            self.current_defect_box = None
            self.current_area_box = None
            # 自动检测相机号
            auto_cam_pos = camera_position_from_path(path)
            auto_cam_num = parse_camera_num_from_path(path)
            if auto_cam_pos and auto_cam_num > 0:
                detected_cam = f"{auto_cam_pos}{auto_cam_num}"
                if detected_cam in CAMERA_CHOICES:
                    self.cam_var.set(detected_cam)
            self.object_var.set("自动")
            self.direction_var.set("U")
            self.count_var.set(DEFAULT_COUNT_FASTENING)

        # 重置缩放
        self.zoom_factor = 1.0

        self._refresh_defect_tree()
        self._display_image()

        # 设置输出目录
        if not self.output_var.get():
            parent = Path(path).parent.parent.parent
            run_date = datetime.datetime.now().strftime("%Y%m%d")
            self.output_var.set(str(parent / "fault" / f"{run_date}_fault"))

        # 更新导航信息 + 进度条
        total = len(self.image_file_list)
        self.nav_var.set(f"图片 {index + 1}/{total}: {Path(path).name}")
        if total > 0:
            self.nav_progress["maximum"] = total
            self.nav_progress["value"] = index + 1
        file_set = set(self.image_file_list)
        annotated_count = sum(1 for p, a in self.annotations.items() if a.get("defects") and p in file_set)
        self.nav_annotated_var.set(f"已标注: {annotated_count}/{total}")
        self.status_var.set(f"已打开: {path}  ({self.cv_image.shape[1]}x{self.cv_image.shape[0]})  缺陷数: {len(self.defect_list)}")

    def _display_image(self):
        if self.cv_image is None:
            return

        canvas_w = self.canvas.winfo_width()
        canvas_h = self.canvas.winfo_height()
        if canvas_w < 10 or canvas_h < 10:
            if self._display_pending_id is not None:
                self.root.after_cancel(self._display_pending_id)
            self._display_pending_id = self.root.after(100, self._display_image)
            return
        self._display_pending_id = None

        h, w = self.cv_image.shape[:2]
        fit_scale = min(canvas_w / w, canvas_h / h, 1.0)
        self.scale_factor = fit_scale * self.zoom_factor
        new_w = int(w * self.scale_factor)
        new_h = int(h * self.scale_factor)

        display = cv2.resize(self.cv_image, (new_w, new_h))
        display = cv2.cvtColor(display, cv2.COLOR_BGR2RGB)
        self.display_image = Image.fromarray(display)
        self.tk_image = ImageTk.PhotoImage(self.display_image)
        self.canvas.delete("all")
        # 居中放置: 图片中心对齐画布中心
        offset_x = (canvas_w - new_w) // 2
        offset_y = (canvas_h - new_h) // 2
        # 确保偏移非负 (图片大于画布时从 0 开始)
        offset_x = max(0, offset_x)
        offset_y = max(0, offset_y)
        self.img_offset_x = offset_x
        self.img_offset_y = offset_y
        self.canvas.create_image(offset_x, offset_y, anchor=tk.NW, image=self.tk_image)
        # 设置滚动区域为图片实际占据的区域
        self.canvas.configure(scrollregion=(0, 0, max(canvas_w, offset_x + new_w),
                                             max(canvas_h, offset_y + new_h)))
        self._draw_boxes_on_canvas()

    def _on_wheel(self, event):
        """鼠标滚轮缩放 (以鼠标位置为中心)"""
        if self.cv_image is None:
            return
        # event.delta: Windows 上 +120/-120
        if event.delta > 0:
            factor = 1.2
        else:
            factor = 1 / 1.2

        old_zoom = self.zoom_factor
        new_zoom = old_zoom * factor
        # 限制缩放范围: 0.1x ~ 20x
        new_zoom = max(0.1, min(20.0, new_zoom))
        if new_zoom == old_zoom:
            return

        # 以鼠标位置为中心缩放: 记录鼠标在图片像素坐标系中的位置
        canvas_x = self.canvas.canvasx(event.x)
        canvas_y = self.canvas.canvasy(event.y)
        old_scale = self.scale_factor
        old_ox = self.img_offset_x
        old_oy = self.img_offset_y
        img_px_x = (canvas_x - old_ox) / old_scale
        img_px_y = (canvas_y - old_oy) / old_scale

        self.zoom_factor = new_zoom
        self._display_image()

        # 缩放后该像素在新画布中的目标位置
        new_scale = self.scale_factor
        new_ox = self.img_offset_x
        new_oy = self.img_offset_y
        target_canvas_x = img_px_x * new_scale + new_ox
        target_canvas_y = img_px_y * new_scale + new_oy
        # 滚动差值: 使目标位置对齐鼠标屏幕位置
        dx = target_canvas_x - event.x
        dy = target_canvas_y - event.y
        self.canvas.xview_moveto(0)
        self.canvas.yview_moveto(0)
        self.canvas.xview_scroll(int(dx), "units")
        self.canvas.yview_scroll(int(dy), "units")

    def _on_wheel_horizontal(self, event):
        """Ctrl+滚轮: 水平滚动 (labelimg 风格)"""
        if event.delta > 0:
            self.canvas.xview_scroll(-3, "units")
        else:
            self.canvas.xview_scroll(3, "units")

    def _on_pan_start(self, event):
        """右键开始平移 (labelimg 风格)"""
        # 如果正在画框, 取消画框
        if self.drawing:
            self.drawing = False
            self.canvas.delete("temp_box")
            self.draw_start = None
        self.panning = True
        self.pan_start = (event.x, event.y)
        self.canvas.configure(cursor="hand2")

    def _on_pan_move(self, event):
        """右键拖拽平移"""
        if not self.panning:
            return
        dx = event.x - self.pan_start[0]
        dy = event.y - self.pan_start[1]
        if abs(dx) > 1:
            self.canvas.xview_scroll(int(-dx), "units")
        if abs(dy) > 1:
            self.canvas.yview_scroll(int(-dy), "units")
        self.pan_start = (event.x, event.y)

    def _on_pan_end(self, event):
        """右键结束平移"""
        self.panning = False
        self.canvas.configure(cursor="crosshair")

    def _zoom_by(self, factor):
        """按倍数缩放"""
        if self.cv_image is None:
            return
        new_zoom = self.zoom_factor * factor
        new_zoom = max(0.1, min(20.0, new_zoom))
        if new_zoom == self.zoom_factor:
            return
        self.zoom_factor = new_zoom
        self._display_image()

    def _reset_zoom(self):
        """重置缩放为适应窗口"""
        if self.cv_image is None:
            return
        self.zoom_factor = 1.0
        self._display_image()
        # 居中: 滚动到使图片居中的位置
        self.canvas.xview_moveto(0)
        self.canvas.yview_moveto(0)

    def _draw_boxes_on_canvas(self):
        if self.cv_image is None:
            return
        s = self.scale_factor
        ox = self.img_offset_x
        oy = self.img_offset_y

        # 先删除所有旧画框 (保留底图)
        self.canvas.delete("box")

        # 画已确认的缺陷列表 (每个缺陷画自己的区域框 + 缺陷框)
        for i, item in enumerate(self.defect_list):
            # 区域框 (黄色)
            ab = item.get("area_box")
            if ab is not None:
                ax, ay, aw, ah = ab
                self.canvas.create_rectangle(
                    ax * s + ox, ay * s + oy, (ax + aw) * s + ox, (ay + ah) * s + oy,
                    outline="#ffff00", width=2, tags=("box", f"area_{i}"))
            # 缺陷框 (红色, 编号)
            x, y, w, h = item["box"]
            self.canvas.create_rectangle(
                x * s + ox, y * s + oy, (x + w) * s + ox, (y + h) * s + oy,
                outline="#ff0000", width=2, tags=("box", f"defect_{i}"))
            self.canvas.create_text(
                x * s + ox + 5, y * s + oy + 10, text=str(i + 1),
                fill="#ff0000", font=("Arial", 10, "bold"),
                tags=("box", f"defect_label_{i}"))

        # 画当前待确认的区域框 (黄色虚线)
        if self.current_area_box is not None:
            x, y, w, h = self.current_area_box
            self.canvas.create_rectangle(
                x * s + ox, y * s + oy, (x + w) * s + ox, (y + h) * s + oy,
                outline="#ffff00", width=2, dash=(4, 2), tags=("box", "current_area"))

        # 画当前待确认的缺陷框 (橙色虚线)
        if self.current_defect_box is not None:
            x, y, w, h = self.current_defect_box
            self.canvas.create_rectangle(
                x * s + ox, y * s + oy, (x + w) * s + ox, (y + h) * s + oy,
                outline="#ff8800", width=2, dash=(4, 2), tags=("box", "current_defect"))

    def _on_label_mode_change(self):
        """切换单标签/多标签模式"""
        if self.label_mode_var.get() == "single":
            # 切到单标签: 如果有标注先确认
            if self.defect_list or self.current_defect_box or self.current_area_box:
                if not messagebox.askyesno("确认", "切换到单标签模式将清空当前所有标注。继续?"):
                    self.label_mode_var.set("multi")
                    return
            # 先清空再保存, 确保存储的是清空后的状态
            self.current_defect_box = None
            self.current_area_box = None
            self.defect_list = []
            if self.image_path is not None:
                self._save_current_annotations()
            self._refresh_defect_tree()
            self._draw_boxes_on_canvas()
            self.status_var.set("已切换到单标签模式 (每张图一个缺陷)")
        else:
            self.status_var.set("已切换到多标签模式 (每张图多个缺陷)")

    def _is_single_mode(self):
        return self.label_mode_var.get() == "single"

    def _toggle_mode(self):
        """空格键切换绘制模式 (labelimg 风格)"""
        if self.mode_var.get() == "defect":
            self.mode_var.set("area")
        else:
            self.mode_var.set("defect")

    def _on_mouse_down(self, event):
        if self.cv_image is None or self.panning:
            return
        self.drawing = True
        # 存储 scroll 坐标 (而非屏幕坐标), 避免自动滚动后偏移
        self.draw_start = self._canvas_to_scroll_coords((event.x, event.y))

    def _canvas_to_scroll_coords(self, pos):
        """将屏幕坐标转为 scroll 坐标 (canvas content 坐标)"""
        x = self.canvas.canvasx(pos[0])
        y = self.canvas.canvasy(pos[1])
        return x, y

    def _auto_scroll_on_edge(self, event):
        """画框时鼠标接近画布边缘自动滚动 (labelimg 风格, 带节流)"""
        if self._auto_scroll_after_id is not None:
            return
        margin = 20
        scroll_step = 10

        def do_scroll():
            self._auto_scroll_after_id = None
            if not self.drawing:
                return
            canvas_w = self.canvas.winfo_width()
            canvas_h = self.canvas.winfo_height()
            ex, ey = self.canvas.winfo_pointerx() - self.canvas.winfo_rootx(), \
                     self.canvas.winfo_pointery() - self.canvas.winfo_rooty()
            if ex < margin:
                self.canvas.xview_scroll(-scroll_step, "units")
            elif ex > canvas_w - margin:
                self.canvas.xview_scroll(scroll_step, "units")
            if ey < margin:
                self.canvas.yview_scroll(-scroll_step, "units")
            elif ey > canvas_h - margin:
                self.canvas.yview_scroll(scroll_step, "units")

        self._auto_scroll_after_id = self.root.after(80, do_scroll)

    def _on_mouse_move(self, event):
        if not self.drawing:
            return
        self.canvas.delete("temp_box")
        # draw_start 已是 scroll 坐标
        sx, sy = self.draw_start
        ex, ey = self._canvas_to_scroll_coords((event.x, event.y))
        color = "#ff0000" if self.mode_var.get() == "defect" else "#ffff00"
        self.canvas.create_rectangle(
            sx, sy, ex, ey, outline=color, width=2, tags=("box", "temp_box"))
        # 画框时鼠标接近边缘自动滚动 (labelimg 风格)
        self._auto_scroll_on_edge(event)

    def _on_mouse_up(self, event):
        if not self.drawing or self.cv_image is None:
            return
        self.drawing = False
        self.canvas.delete("temp_box")

        # draw_start 已是 scroll 坐标
        sx, sy = self.draw_start
        ex, ey = self._canvas_to_scroll_coords((event.x, event.y))

        s = self.scale_factor
        ox1, oy1 = (sx - self.img_offset_x) / s, (sy - self.img_offset_y) / s
        ox2, oy2 = (ex - self.img_offset_x) / s, (ey - self.img_offset_y) / s

        # clamp 到图片边界, 避免负坐标
        img_h, img_w = self.cv_image.shape[:2]
        x1 = max(0, int(min(ox1, ox2)))
        y1 = max(0, int(min(oy1, oy2)))
        x2 = min(img_w, int(max(ox1, ox2)))
        y2 = min(img_h, int(max(oy1, oy2)))
        w = x2 - x1
        h = y2 - y1

        if w < 5 or h < 5:
            self.status_var.set("框太小, 已忽略")
            return

        box = (x1, y1, w, h)
        mode = self.mode_var.get()
        if mode == "defect":
            self.current_defect_box = box
            self._save_current_annotations()
            if self._is_single_mode():
                self._auto_add_single_defect()
                return
            self.status_var.set(f"缺陷框: x={x1} y={y1} w={w} h={h}  -> 点击'添加到缺陷列表'确认")
        else:
            self.current_area_box = box
            if self._is_single_mode():
                # 单标签模式: 自动更新已有缺陷的区域框
                if self.defect_list:
                    self.defect_list[0]["area_box"] = copy.deepcopy(box)
                    self._save_current_annotations()
                    self._refresh_defect_tree()
                    self._refresh_preview_if_open()
                self.status_var.set(f"区域框: x={x1} y={y1} w={w} h={h}  (已自动关联)")
            else:
                self._save_current_annotations()
                self.status_var.set(f"区域框: x={x1} y={y1} w={w} h={h}  -> 点击'添加到缺陷列表'确认")

        self._draw_boxes_on_canvas()

    def _parse_type_from_combo(self):
        """从类型下拉框解析 (type_code, type_name)"""
        selected = self.type_combo.get()
        m = re.search(r"\((\d+)\)$", selected)
        if m:
            type_code = m.group(1)
            type_name = selected[:m.start()].strip()
        else:
            type_code = selected
            type_name = selected
        return normalize_type_code(type_code), type_name

    def _auto_add_single_defect(self):
        """单标签模式: 画完缺陷框后自动添加, 无需点击按钮"""
        if self.current_defect_box is None or self.current_defect_box[2] < 5 or self.current_defect_box[3] < 5:
            return
        if not self.xlbh_codes:
            messagebox.showwarning("警告", "缺陷类型编码为空")
            return
        type_code, type_name = self._parse_type_from_combo()

        self.defect_list = [{
            "box": self.current_defect_box,
            "area_box": copy.deepcopy(self.current_area_box),
            "type_code": type_code,
            "type_name": type_name,
        }]
        self.current_defect_box = None
        self.current_area_box = None
        self._save_current_annotations()
        self._refresh_defect_tree()
        self._draw_boxes_on_canvas()
        self._refresh_preview_if_open()
        x, y, w, h = self.defect_list[0]["box"]
        self.status_var.set(f"已自动添加缺陷: {type_name}({type_code})  x={x} y={y} w={w} h={h}")

    def _add_current_defect(self):
        """将当前缺陷框和区域框添加到缺陷列表"""
        if self.current_defect_box is None or self.current_defect_box[2] < 5 or self.current_defect_box[3] < 5:
            messagebox.showwarning("警告", "请先在'缺陷框'模式下绘制一个框")
            return
        type_code, type_name = self._parse_type_from_combo()

        new_item = {
            "box": self.current_defect_box,
            "area_box": copy.deepcopy(self.current_area_box),
            "type_code": type_code,
            "type_name": type_name,
        }

        if self._is_single_mode():
            # 单标签模式: 替换已有缺陷
            self.defect_list = [new_item]
        else:
            # 多标签模式: 追加
            self.defect_list.append(new_item)
        self.current_defect_box = None
        self.current_area_box = None
        self._save_current_annotations()
        self._refresh_defect_tree()
        self._draw_boxes_on_canvas()
        self._refresh_preview_if_open()
        self.status_var.set(f"已添加缺陷 #{len(self.defect_list)}: {type_name} ({type_code})")

    def _refresh_defect_tree(self):
        self.defect_tree.delete(*self.defect_tree.get_children())
        for i, item in enumerate(self.defect_list):
            x, y, w, h = item["box"]
            coords = f"({x},{y},{w},{h})"
            self.defect_tree.insert("", tk.END, iid=str(i),
                                    values=(i + 1, f"{item['type_name']}({item['type_code']})", coords))

    def _undo(self):
        """撤销最后一个操作"""
        if self.current_defect_box is not None:
            self.current_defect_box = None
            self._save_current_annotations()
            self._draw_boxes_on_canvas()
            self.status_var.set("已撤销当前缺陷框")
        elif self.current_area_box is not None:
            self.current_area_box = None
            self._save_current_annotations()
            self._draw_boxes_on_canvas()
            self.status_var.set("已撤销当前区域框")
        elif self.defect_list:
            removed = self.defect_list.pop()
            self._save_current_annotations()
            self._refresh_defect_tree()
            self._draw_boxes_on_canvas()
            self._refresh_preview_if_open()
            self.status_var.set(f"已撤销缺陷: {removed['type_name']}")
        else:
            self.status_var.set("无可撤销操作")

    def _delete_selected_defect(self):
        """删除列表中选中的缺陷"""
        sel = self.defect_tree.selection()
        if not sel:
            return
        idx = int(sel[0])
        if 0 <= idx < len(self.defect_list):
            removed = self.defect_list.pop(idx)
            self._save_current_annotations()
            self._refresh_defect_tree()
            self._draw_boxes_on_canvas()
            self._refresh_preview_if_open()
            self.status_var.set(f"已删除: {removed['type_name']}")

    def _clear_box(self, box_type):
        if box_type == "area":
            self.current_area_box = None
            # 单标签模式: 同步清除已有缺陷的区域框
            if self._is_single_mode() and self.defect_list:
                self.defect_list[0]["area_box"] = None
                self._refresh_defect_tree()
                self._refresh_preview_if_open()
            self._save_current_annotations()
            self._draw_boxes_on_canvas()
            self.status_var.set("已清除当前区域框")

    def _clear_all(self):
        if not self.defect_list and not self.current_area_box and not self.current_defect_box:
            has_other = any(ann.get("defects") for ann in self.annotations.values())
            if not has_other:
                self.status_var.set("没有需要清除的标注")
                return
        if not messagebox.askyesno("确认", "确定清除所有图片的标注吗?此操作不可撤销。"):
            return
        if self.preview_window and self._pv_on_close:
            try:
                self._pv_on_close()
            except Exception:
                self.preview_window = None
                self._pv_state = None
                self._pv_render_fn = None
                self._pv_on_close = None
        self.current_defect_box = None
        self.current_area_box = None
        self.defect_list = []
        # 删除所有图片的独立标注文件
        for img_path in self.image_file_list:
            json_path = self._get_annotation_file_for_image(img_path)
            if json_path.exists():
                try:
                    json_path.unlink()
                except Exception as e:
                    print(f"警告: 删除标注文件失败 {json_path}: {e}")
        self.annotations.clear()
        if self._save_debounce_id is not None:
            self.root.after_cancel(self._save_debounce_id)
            self._save_debounce_id = None
        self._refresh_defect_tree()
        self._draw_boxes_on_canvas()
        self.status_var.set("已清除所有图片的标注")

    def browse_output(self):
        path = filedialog.askdirectory(title="选择输出 fault 目录")
        if path:
            self.output_var.set(path)

    def _preview_render(self):
        """预览渲染效果 (支持缩放/平移/切换缺陷, 与主画布交互一致)"""
        if self.cv_image is None:
            messagebox.showwarning("警告", "请先打开图片")
            return
        if not self.defect_list:
            messagebox.showwarning("警告", "缺陷列表为空, 请先添加缺陷")
            return

        # 弹出预览窗口 (先正确关闭旧窗口)
        if self.preview_window and self._pv_on_close:
            try:
                self._pv_on_close()
            except Exception:
                self.preview_window = None
                self._pv_state = None
                self._pv_render_fn = None
                self._pv_on_close = None

        self.preview_window = tk.Toplevel(self.root)
        self.preview_window.title("渲染预览")
        win_w, win_h = 900, 650
        self.preview_window.geometry(f"{win_w}x{win_h}")

        # 顶部: 缺陷切换栏
        pv_top = ttk.Frame(self.preview_window, padding=3)
        pv_top.pack(side=tk.TOP, fill=tk.X)
        pv_info_var = tk.StringVar()
        ttk.Label(pv_top, textvariable=pv_info_var).pack(side=tk.LEFT, padx=5)

        # 预览画布 (支持滚动)
        pv_body = ttk.Frame(self.preview_window)
        pv_body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        preview_canvas = tk.Canvas(pv_body, bg="#2b2b2b",
                                   highlightthickness=0)
        scroll_y = ttk.Scrollbar(pv_body, orient=tk.VERTICAL,
                                 command=preview_canvas.yview)
        scroll_x = ttk.Scrollbar(pv_body, orient=tk.HORIZONTAL,
                                 command=preview_canvas.xview)
        preview_canvas.configure(yscrollcommand=scroll_y.set,
                                 xscrollcommand=scroll_x.set)
        scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        scroll_x.pack(side=tk.BOTTOM, fill=tk.X)
        preview_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # 预览状态
        pv = {"zoom": 1.0, "scale": 1.0, "offset_x": 0, "offset_y": 0,
              "img": None, "tk_img": None, "panning": False, "pan_start": None,
              "index": 0, "display_pending_id": None, "alive": True}
        self._pv_state = pv

        def pv_on_close():
            pv["alive"] = False
            if pv["display_pending_id"] is not None:
                self.preview_window.after_cancel(pv["display_pending_id"])
                pv["display_pending_id"] = None
            self.preview_window.destroy()
            self.preview_window = None
            self._pv_state = None
            self._pv_render_fn = None

        self._pv_on_close = pv_on_close
        self.preview_window.protocol("WM_DELETE_WINDOW", pv_on_close)
        self.preview_window.focus_set()

        def pv_render_defect(idx):
            """渲染指定索引的缺陷 (动态读取 count_fastening)"""
            if idx < 0 or idx >= len(self.defect_list):
                return
            item = self.defect_list[idx]
            cf = self.count_var.get()
            rendered = render_one_defect_image(
                self.cv_image, item["box"], item.get("area_box"),
                count_fastening=cf,
                expected_count_fastening=DEFAULT_COUNT_FASTENING
            )
            pv["img"] = rendered
            pv["index"] = idx
            pv["zoom"] = 1.0
            pv_info_var.set(
                f"缺陷 {idx + 1}/{len(self.defect_list)}: {item['type_name']}({item['type_code']})  "
                f"缩放比例={cf}/{DEFAULT_COUNT_FASTENING}={cf/float(DEFAULT_COUNT_FASTENING):.2f}"
            )
            pv_display()

        self._pv_render_fn = pv_render_defect

        def pv_display():
            if pv["img"] is None or not pv["alive"]:
                return
            cw = preview_canvas.winfo_width()
            ch = preview_canvas.winfo_height()
            if cw < 10 or ch < 10:
                if pv["display_pending_id"] is not None:
                    self.preview_window.after_cancel(pv["display_pending_id"])
                pv["display_pending_id"] = self.preview_window.after(100, pv_display)
                return
            pv["display_pending_id"] = None
            h, w = pv["img"].shape[:2]
            fit = min(cw / w, ch / h, 1.0)
            pv["scale"] = fit * pv["zoom"]
            nw = int(w * pv["scale"])
            nh = int(h * pv["scale"])
            disp = cv2.resize(pv["img"], (nw, nh))
            disp = cv2.cvtColor(disp, cv2.COLOR_BGR2RGB)
            pil = Image.fromarray(disp)
            pv["tk_img"] = ImageTk.PhotoImage(pil)
            preview_canvas.delete("all")
            ox = max(0, (cw - nw) // 2)
            oy = max(0, (ch - nh) // 2)
            pv["offset_x"] = ox
            pv["offset_y"] = oy
            preview_canvas.create_image(ox, oy, anchor=tk.NW, image=pv["tk_img"])
            preview_canvas.configure(scrollregion=(0, 0, max(cw, ox + nw),
                                                    max(ch, oy + nh)))

        def pv_wheel(event):
            factor = 1.2 if event.delta > 0 else 1 / 1.2
            old = pv["zoom"]
            new = max(0.1, min(20.0, old * factor))
            if new == old:
                return
            cx = preview_canvas.canvasx(event.x)
            cy = preview_canvas.canvasy(event.y)
            old_scale = pv["scale"]
            old_ox = pv["offset_x"]
            old_oy = pv["offset_y"]
            img_px_x = (cx - old_ox) / old_scale
            img_px_y = (cy - old_oy) / old_scale
            pv["zoom"] = new
            pv_display()
            new_scale = pv["scale"]
            new_ox = pv["offset_x"]
            new_oy = pv["offset_y"]
            target_cx = img_px_x * new_scale + new_ox
            target_cy = img_px_y * new_scale + new_oy
            dx = target_cx - event.x
            dy = target_cy - event.y
            preview_canvas.xview_moveto(0)
            preview_canvas.yview_moveto(0)
            preview_canvas.xview_scroll(int(dx), "units")
            preview_canvas.yview_scroll(int(dy), "units")

        def pv_wheel_h(event):
            if event.delta > 0:
                preview_canvas.xview_scroll(-3, "units")
            else:
                preview_canvas.xview_scroll(3, "units")

        def pv_pan_start(event):
            pv["panning"] = True
            pv["pan_start"] = (event.x, event.y)
            preview_canvas.configure(cursor="hand2")

        def pv_pan_move(event):
            if not pv["panning"]:
                return
            dx = event.x - pv["pan_start"][0]
            dy = event.y - pv["pan_start"][1]
            if abs(dx) > 1:
                preview_canvas.xview_scroll(int(-dx), "units")
            if abs(dy) > 1:
                preview_canvas.yview_scroll(int(-dy), "units")
            pv["pan_start"] = (event.x, event.y)

        def pv_pan_end(event):
            pv["panning"] = False
            preview_canvas.configure(cursor="crosshair")

        def pv_reset(event=None):
            pv["zoom"] = 1.0
            pv_display()
            preview_canvas.xview_moveto(0)
            preview_canvas.yview_moveto(0)

        def pv_prev():
            if pv["index"] > 0:
                pv_render_defect(pv["index"] - 1)

        def pv_next():
            if pv["index"] < len(self.defect_list) - 1:
                pv_render_defect(pv["index"] + 1)

        # 切换按钮
        ttk.Button(pv_top, text="上一个", command=pv_prev).pack(side=tk.LEFT, padx=5)
        ttk.Button(pv_top, text="下一个", command=pv_next).pack(side=tk.LEFT, padx=5)

        preview_canvas.bind("<MouseWheel>", pv_wheel)
        preview_canvas.bind("<Control-MouseWheel>", pv_wheel_h)
        preview_canvas.bind("<Button-3>", pv_pan_start)
        preview_canvas.bind("<B3-Motion>", pv_pan_move)
        preview_canvas.bind("<ButtonRelease-3>", pv_pan_end)
        def pv_zoom_in(e=None):
            pv["zoom"] = min(20.0, pv["zoom"] * 1.2)
            pv_display()

        def pv_zoom_out(e=None):
            pv["zoom"] = max(0.1, pv["zoom"] / 1.2)
            pv_display()

        self.preview_window.bind("<plus>", lambda e: (pv_zoom_in(), "break")[1])
        self.preview_window.bind("<equal>", lambda e: (pv_zoom_in(), "break")[1])
        self.preview_window.bind("<minus>", lambda e: (pv_zoom_out(), "break")[1])
        self.preview_window.bind("<f>", lambda e: (pv_reset(), "break")[1])
        self.preview_window.bind("<a>", lambda e: (pv_prev(), "break")[1])
        self.preview_window.bind("<d>", lambda e: (pv_next(), "break")[1])

        # 渲染第一个缺陷
        pv_render_defect(0)

        self.status_var.set(f"预览: {len(self.defect_list)} 个缺陷  (滚轮缩放 右键平移 a/d切换 f重置)")

    def generate_all(self):
        """生成所有图片中所有缺陷的 ZIP (csv + jpg)"""
        if self._gen_thread and self._gen_thread.is_alive():
            messagebox.showwarning("警告", "正在生成中, 请等待完成或点击取消")
            return
        if not self.output_var.get():
            messagebox.showwarning("警告", "请指定输出目录")
            return

        # 保存当前图片的标注 (立即持久化)
        self._save_current_annotations()
        self._flush_annotations()

        # 收集所有有标注的图片, 按文件名自然排序
        annotated_images = sorted(
            [img_path for img_path, ann in self.annotations.items() if ann.get("defects")],
            key=natural_sort_key
        )

        if not annotated_images:
            messagebox.showwarning("警告", "没有任何图片包含标注缺陷")
            return

        output_dir = self.output_var.get()
        train_num = sanitize_filename(self.train_num_var.get().strip())
        if not train_num:
            messagebox.showwarning("警告", "列车号不能为空")
            return

        # 预估总数
        total_defects = sum(len(self.annotations[img].get("defects", [])) for img in annotated_images)

        # 在主线程中读取所有 UI 值 (tkinter 非线程安全)
        route_no = self.route_no_var.get()
        # 深拷贝标注数据, 避免线程间竞争
        annotations_snapshot = copy.deepcopy(self.annotations)

        # 分配连续序号 (支持自定义起始序号)
        start_serial_input = self.start_serial_var.get().strip()
        if start_serial_input:
            try:
                first_serial = int(start_serial_input)
                if first_serial < 1:
                    messagebox.showwarning("警告", "起始序号必须大于 0")
                    return
            except ValueError:
                messagebox.showwarning("警告", f"起始序号格式无效: {start_serial_input}")
                return
            # 检查与已有文件冲突, 自动跳到最大序号+1
            existing_max = find_existing_defect_serial_max(output_dir, train_num)
            if first_serial <= existing_max:
                first_serial = existing_max + 1
                if not messagebox.askyesno("序号已存在",
                        f"起始序号 {start_serial_input} 与已有文件冲突 (最大序号 {existing_max})\n"
                        f"已自动调整为 {first_serial}\n\n继续生成?"):
                    return
        else:
            first_serial = allocate_defect_serial(output_dir, train_num)

        last_serial = first_serial + total_defects - 1
        if not messagebox.askyesno("确认生成",
                f"将生成 {total_defects} 个缺陷 ({len(annotated_images)} 张图片)\n"
                f"序号范围: {first_serial} - {last_serial}\n"
                f"输出目录: {output_dir}\n\n确认开始?"):
            return

        os.makedirs(output_dir, exist_ok=True)
        self._gen_cancel = False
        self.status_var.set(f"生成中... 0/{total_defects}  (Esc 取消)")

        # 绑定 Esc 取消
        def on_cancel(e=None):
            self._gen_cancel = True
            self.status_var.set("正在取消生成...")
        self.root.bind("<Escape>", on_cancel)

        def worker():
            serial = first_serial
            generated_files = []
            errors = []
            processed = 0

            for img_path in annotated_images:
                if self._gen_cancel:
                    break
                ann = annotations_snapshot[img_path]
                defects = ann.get("defects", [])
                source_image_name = Path(img_path).name

                # 每张图片独立配置
                cam_val = ann.get("cam", "L1")
                if cam_val and len(cam_val) >= 1:
                    cam_position_override = cam_val[0]
                    if len(cam_val) > 1 and cam_val[1:].isdigit():
                        cam_num_override = int(cam_val[1:])
                    else:
                        cam_num_override = -1
                else:
                    cam_position_override = ""
                    cam_num_override = -1

                obj_val = ann.get("object", "自动")
                fault_object_override = obj_val if obj_val and obj_val != "自动" else ""

                dir_val = ann.get("direction", "U")
                train_cate_type_override = dir_val if dir_val else "U"

                count_fastening = ann.get("count_fastening", DEFAULT_COUNT_FASTENING)
                if not isinstance(count_fastening, (int, float)) or count_fastening <= 0:
                    count_fastening = DEFAULT_COUNT_FASTENING

                # 每张图片只读取一次
                try:
                    cv_img = cv2.imdecode(np.fromfile(img_path, dtype=np.uint8), cv2.IMREAD_COLOR)
                    if cv_img is None:
                        errors.append(f"无法读取图片: {source_image_name}")
                        continue
                except Exception as e:
                    errors.append(f"读取图片失败 {source_image_name}: {e}")
                    continue

                img_h, img_w = cv_img.shape[:2]

                for item in defects:
                    if self._gen_cancel:
                        break
                    try:
                        defect_id_text = str(serial)
                        defect_export_stem = f"fault_{train_num}_{defect_id_text}"
                        export_image_name = f"{defect_export_stem}.jpg"
                        type_code = item["type_code"]
                        defect_box = item["box"]
                        area_box = item.get("area_box")
                        dx, dy, dw, dh = defect_box

                        # 校验框是否越界, 越界则 clamp
                        clamped = clamp_rect(dx, dy, dw, dh, img_w, img_h)
                        if clamped is None:
                            errors.append(f"框完全越界, 跳过: {source_image_name} #{serial}")
                            processed += 1
                            continue
                        dx, dy, dw, dh = clamped

                        # 渲染图片
                        rendered = render_one_defect_image(
                            cv_img, (dx, dy, dw, dh), area_box,
                            count_fastening=count_fastening,
                            expected_count_fastening=DEFAULT_COUNT_FASTENING
                        )
                        success, jpg_buf = cv2.imencode(".jpg", rendered)
                        if not success:
                            errors.append(f"JPG编码失败, 跳过: {source_image_name} #{serial}")
                            processed += 1
                            continue
                        jpg_bytes = jpg_buf.tobytes()

                        # 生成 CSV
                        csv_text = build_one_defect_csv_text(
                            export_image_name=export_image_name,
                            defect_id_text=defect_id_text,
                            image_path=img_path,
                            source_image_name=source_image_name,
                            type_code=type_code,
                            pos_x=dx, pos_y=dy, pos_w=dw, pos_h=dh,
                            cam_position_override=cam_position_override,
                            cam_num_override=cam_num_override,
                            train_cate_type_override=train_cate_type_override,
                            fault_object_override=fault_object_override,
                            type_name=item.get("type_name", ""),
                            route_no=route_no,
                            train_num=train_num,
                        )

                        # 写 ZIP
                        zip_path = os.path.join(output_dir, f"{defect_export_stem}.zip")
                        create_defect_zip(zip_path, csv_text, jpg_bytes)

                        generated_files.append(f"  {defect_export_stem}.zip  <- {source_image_name}")
                        serial += 1
                    except Exception as e:
                        errors.append(f"生成失败 {source_image_name} #{serial}: {e}")

                    processed += 1
                    if processed % 10 == 0 or processed == total_defects:
                        cancelled = self._gen_cancel
                        self.root.after(0, lambda p=processed, t=total_defects, c=cancelled:
                            self.status_var.set(
                                f"生成中... {p}/{t}" + ("  (正在取消...)" if c else "  (Esc 取消)")))

            # 回主线程显示结果
            self.root.after(0, lambda: self._on_generate_done(
                generated_files, errors, len(annotated_images),
                first_serial, serial - 1, output_dir, self._gen_cancel))

        self._gen_thread = threading.Thread(target=worker, daemon=True)
        self._gen_thread.start()

    def _on_generate_done(self, generated_files, errors, num_images,
                          first_serial, last_serial, output_dir, cancelled):
        """生成完成后在主线程回调"""
        self.root.unbind("<Escape>")
        self._gen_thread = None
        self._gen_cancel = False

        total_count = len(generated_files)
        summary = "\n".join(generated_files[:20])
        if len(generated_files) > 20:
            summary += f"\n  ... (共 {len(generated_files)} 个)"
        if errors:
            summary += f"\n\n失败 {len(errors)} 个:\n" + "\n".join(errors[:10])
            if len(errors) > 10:
                summary += f"\n  ... (共 {len(errors)} 个失败)"
        if cancelled:
            summary = "(已取消)\n" + summary
        self.status_var.set(f"已生成 {total_count} 个缺陷 ZIP ({num_images} 张图片)"
                            + (f", {len(errors)} 个失败" if errors else "")
                            + ("  (已取消)" if cancelled else ""))
        if total_count > 0:
            messagebox.showinfo("完成",
                f"已生成 {total_count} 个缺陷 ({num_images} 张图片):\n\n{summary}\n\n"
                f"序号范围: {first_serial} - {last_serial}\n"
                f"输出目录: {output_dir}")
        else:
            messagebox.showwarning("完成",
                f"未生成任何缺陷文件。\n\n{summary}\n\n"
                f"输出目录: {output_dir}")


# ============================================================
# 主入口
# ============================================================
def main():
    root = tk.Tk()
    ManualDefectTool(root)
    root.mainloop()


if __name__ == "__main__":
    main()
