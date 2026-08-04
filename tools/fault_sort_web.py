#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
缺陷压缩包分类工具 - Web 界面

基于 Flask 的本地 Web 应用，提供:
  - 输入/输出目录选择
  - 实时进度显示 (SSE)
  - 分类结果统计
  - 图片预览

用法:
    python fault_sort_web.py [--host HOST] [--port PORT] [--debug]

打包为 exe 后直接双击运行即可。
"""

import json
import os
import sys
import threading
import webbrowser
from pathlib import Path
from queue import Queue, Empty

from flask import Flask, Response, render_template, request, jsonify

# 确保能 import sort_fault_zips (PyInstaller 打包后也能找到)
_TOOLS_DIR = Path(__file__).resolve().parent
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

import sort_fault_zips as core

app = Flask(
    __name__,
    template_folder=str(_TOOLS_DIR / "templates"),
    static_folder=str(_TOOLS_DIR / "static"),
)

# ============================================================
# 全局任务状态 (单用户本地应用, 不需要多任务并发)
# ============================================================
class TaskState:
    IDLE = "idle"
    RUNNING = "running"
    DONE = "done"
    ERROR = "error"

    def __init__(self):
        self.reset()

    def reset(self):
        self.status = self.IDLE
        self.total = 0
        self.processed = 0
        self.succeeded = 0
        self.failed = 0
        self.current_file = ""
        self.current_folder = ""
        self.stats = {}
        self.error = None
        self.src_dir = ""
        self.out_dir = ""
        self._sse_queue: Queue = Queue()

    def push_event(self, event_type, data):
        self._sse_queue.put((event_type, data))

    def drain_events(self):
        events = []
        while True:
            try:
                events.append(self._sse_queue.get_nowait())
            except Empty:
                break
        return events


task = TaskState()


def run_sort_thread(src_dir, out_dir):
    """在后台线程中执行分类任务"""
    task.reset()
    task.status = TaskState.RUNNING
    task.src_dir = src_dir
    task.out_dir = out_dir
    task.push_event("start", {"total": 0, "src_dir": src_dir, "out_dir": out_dir})

    def progress_callback(index, total, zip_name, folder_name, status):
        task.total = total
        task.processed = index
        task.current_file = zip_name
        task.current_folder = folder_name
        if status == "ok":
            task.succeeded += 1
        elif status in ("failed", "skipped"):
            task.failed += 1
        task.push_event("progress", {
            "index": index,
            "total": total,
            "zip_name": zip_name,
            "folder": folder_name,
            "status": status,
            "succeeded": task.succeeded,
            "failed": task.failed,
        })

    try:
        rc = core.sort_zips(src_dir, out_dir, dry_run=False,
                            progress_callback=progress_callback)
        task.stats = _collect_stats(out_dir) if Path(out_dir).is_dir() else {}
        task.status = TaskState.DONE
        task.push_event("done", {
            "return_code": rc,
            "stats": task.stats,
            "succeeded": task.succeeded,
            "failed": task.failed,
            "total": task.total,
        })
    except Exception as e:
        task.status = TaskState.ERROR
        task.error = str(e)
        task.push_event("error", {"message": str(e)})


def _collect_stats(out_dir):
    """收集输出目录的分类统计"""
    stats = {}
    out_path = Path(out_dir)
    if not out_path.is_dir():
        return stats
    for d in sorted(out_path.iterdir()):
        if d.is_dir():
            jpgs = list(d.glob("*.jpg"))
            if jpgs:
                stats[d.name] = len(jpgs)
    return stats


# ============================================================
# 路由
# ============================================================
@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/browse", methods=["POST"])
def browse():
    """列出指定路径下的子目录, 供前端目录选择"""
    data = request.get_json(force=True)
    path = data.get("path", "")
    if not path:
        # 返回根驱动器列表 (Windows)
        if sys.platform == "win32":
            import string
            drives = []
            for letter in string.ascii_uppercase:
                d = f"{letter}:\\"
                if Path(d).exists():
                    drives.append(d)
            return jsonify({"dirs": drives, "current": ""})
        return jsonify({"dirs": ["/"], "current": ""})

    p = Path(path)
    if not p.is_dir():
        return jsonify({"error": f"路径不存在: {path}"}), 400

    try:
        dirs = sorted([str(d) for d in p.iterdir() if d.is_dir()])
    except PermissionError:
        return jsonify({"error": f"无权限访问: {path}"}), 403

    return jsonify({"dirs": dirs, "current": str(p)})


@app.route("/api/start", methods=["POST"])
def start_sort():
    """启动分类任务"""
    if task.status == TaskState.RUNNING:
        return jsonify({"error": "已有任务正在运行"}), 409

    data = request.get_json(force=True)
    src_dir = data.get("src_dir", "").strip()
    out_dir = data.get("out_dir", "").strip()

    if not src_dir:
        return jsonify({"error": "请指定输入目录"}), 400
    if not Path(src_dir).is_dir():
        return jsonify({"error": f"输入目录不存在: {src_dir}"}), 400
    if not out_dir:
        out_dir = str(Path(src_dir).resolve()) + "_sorted"

    # 在后台线程运行
    t = threading.Thread(target=run_sort_thread, args=(src_dir, out_dir),
                         daemon=True)
    t.start()
    return jsonify({"ok": True, "out_dir": out_dir})


@app.route("/api/status")
def status():
    """查询当前任务状态 (轮询备用)"""
    return jsonify({
        "status": task.status,
        "total": task.total,
        "processed": task.processed,
        "succeeded": task.succeeded,
        "failed": task.failed,
        "current_file": task.current_file,
        "current_folder": task.current_folder,
        "stats": task.stats,
        "error": task.error,
    })


@app.route("/api/events")
def events():
    """SSE 事件流, 实时推送进度"""
    def stream():
        while True:
            events = task.drain_events()
            for event_type, data in events:
                yield f"event: {event_type}\ndata: {json.dumps(data, ensure_ascii=False)}\n\n"
                if event_type in ("done", "error"):
                    return
            # 无事件时发心跳, 保持连接
            yield ": heartbeat\n\n"
            threading.Event().wait(0.3)

    return Response(stream(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache",
                             "X-Accel-Buffering": "no"})


@app.route("/api/preview/<path:folder>/<path:filename>")
def preview(folder, filename):
    """预览分类后的图片"""
    safe_folder = Path(folder).name
    safe_filename = Path(filename).name
    img_path = Path(task.out_dir) / safe_folder / safe_filename
    if not img_path.is_file():
        return jsonify({"error": "文件不存在"}), 404
    import mimetypes
    mime, _ = mimetypes.guess_type(str(img_path))
    if mime is None:
        mime = "image/jpeg"
    with open(img_path, "rb") as f:
        data = f.read()
    return Response(data, mimetype=mime)


@app.route("/api/list_images/<path:folder>")
def list_images(folder):
    """列出某个分类文件夹下的图片"""
    safe_folder = Path(folder).name
    d = Path(task.out_dir) / safe_folder
    if not d.is_dir():
        return jsonify({"error": "文件夹不存在"}), 404
    images = sorted([f.name for f in d.glob("*.jpg")])
    return jsonify({"folder": safe_folder, "images": images, "count": len(images)})


# ============================================================
# 入口
# ============================================================
def main():
    import argparse
    parser = argparse.ArgumentParser(description="缺陷压缩包分类工具 - Web 界面")
    parser.add_argument("--host", default="127.0.0.1", help="监听地址")
    parser.add_argument("--port", type=int, default=18000, help="监听端口")
    parser.add_argument("--debug", action="store_true", help="调试模式")
    parser.add_argument("--no-browser", action="store_true", help="不自动打开浏览器")
    args = parser.parse_args()

    url = f"http://{args.host}:{args.port}"
    print(f"缺陷压缩包分类工具已启动: {url}")
    print(f"按 Ctrl+C 退出")

    if not args.no_browser and not args.debug:
        threading.Timer(1.0, lambda: webbrowser.open(url)).start()

    app.run(host=args.host, port=args.port, debug=args.debug,
            threaded=True, use_reloader=False)


if __name__ == "__main__":
    main()
