---
description: Jetson shell_jetson 开机自启与崩溃自启
---

# Jetson `shell_jetson` 开机自启与崩溃自启

本文说明如何在 Jetson 上使用 `systemd` 管理 `shell_jetson`，实现开机自动运行和崩溃后自动重启。

## 目标

- **开机自启**：Jetson 启动后自动拉起 `shell_jetson`
- **崩溃自启**：程序异常退出后自动重启
- **日志查看**：通过 `journalctl` 查看运行日志

## 前提条件

在配置自启之前，先准备好运行目录：

```text
/home/nvidia/proj2_runtime/
  ├── shell_jetson
  ├── libproj2.so
  ├── config/
  └── .engine 文件
```

要求：

- `shell_jetson` 和 `libproj2.so` 必须在同一目录
- `config/project.xml` 必须位于 `config/` 目录下
- `project.xml` 里的 `path` 要指向实际数据目录
- 如果你启用了自动检测，`auto_detect enable="1"` 也要配好
- 如果 `path` 指向的是网络共享挂载点，建议先保证挂载点可访问，再启动 `shell_jetson`
- 如果共享目录使用的是 `x-systemd.automount`，**不要**再用 `RequiresMountsFor=` 硬等挂载，否则开机时共享还没 ready 可能直接触发依赖失败

## 第 1 步：创建 `systemd` 服务文件

创建 service 文件：

```bash
sudo nano /etc/systemd/system/proj2-shell.service
```

写入下面内容：

```ini
[Unit]
Description=proj2 shell_jetson
After=network-online.target remote-fs.target
Wants=network-online.target

[Service]
Type=simple
User=nvidia
WorkingDirectory=/home/nvidia/proj2_runtime
ExecStart=/home/nvidia/proj2_runtime/shell_jetson
Restart=always
RestartSec=5
Environment=LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/lib/aarch64-linux-gnu

[Install]
WantedBy=multi-user.target
```

### 参数说明

- `WorkingDirectory`
  - 必须指向 `libproj2.so`、`shell_jetson`、`config/` 所在目录
- `ExecStart`
  - 建议写绝对路径
- `After=network-online.target remote-fs.target`
  - 确保网络和远程挂载尽量先准备好，再启动程序
- `x-systemd.automount`
  - 建议放在 `/etc/fstab` 的共享挂载项里，让共享在第一次访问时自动挂载
- `RequiresMountsFor=/mnt/windows_share`
  - **只适合真正稳定、启动时一定可用的挂载点**；如果是 Windows 共享，通常建议去掉，交给 automount 和程序重试处理
- `Restart=always`
  - 程序退出后持续重启
- `RestartSec=5`
  - 重启前等待 5 秒
- `Environment=LD_LIBRARY_PATH=...`
  - 补齐动态库搜索路径

如果你的实际运行用户不是 `nvidia`，把 `User=` 改成对应用户。

## 第 2 步：让 service 生效

```bash
sudo systemctl daemon-reload
sudo systemctl enable proj2-shell.service
```

如果你想保存后立刻启动，可以直接执行：

```bash
sudo systemctl enable --now proj2-shell.service
```

## 第 3 步：手动启动、重启和停止

```bash
sudo systemctl start proj2-shell.service
sudo systemctl restart proj2-shell.service
sudo systemctl stop proj2-shell.service
```

## 第 4 步：查看运行状态和日志

查看状态：

```bash
sudo systemctl status proj2-shell.service
```

看实时日志：

```bash
sudo journalctl -u proj2-shell.service -f
```

## 最短可复制命令版

如果你只想快速启用开机自启，可以直接执行：

```bash
sudo tee /etc/systemd/system/proj2-shell.service >/dev/null <<'EOF'
[Unit]
Description=proj2 shell_jetson
After=network-online.target remote-fs.target
Wants=network-online.target
RequiresMountsFor=/mnt/windows_share

[Service]
Type=simple
User=nvidia
WorkingDirectory=/home/nvidia/proj2_runtime
ExecStart=/home/nvidia/proj2_runtime/shell_jetson
Restart=always
RestartSec=5
Environment=LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/lib/aarch64-linux-gnu

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now proj2-shell.service
```

查看状态和日志：

```bash
sudo systemctl status proj2-shell.service
sudo journalctl -u proj2-shell.service -f
```

## 崩溃自启策略

- **`Restart=always`**
  - 无论什么退出原因都会重启
- **`Restart=on-failure`**
  - 只有异常退出时才重启

如果你希望更保守一点，可以把 `Restart=always` 改成 `Restart=on-failure`。

## 常见问题

### 1. 服务启动后马上退出

检查：

- `WorkingDirectory` 是否正确
- `ExecStart` 是否是绝对路径
- `shell_jetson` 和 `libproj2.so` 是否在同一目录
- `config/project.xml` 是否存在

### 2. 动态库找不到

检查：

- `LD_LIBRARY_PATH` 是否写入 service
- Jetson 上是否安装了对应的 CUDA / TensorRT / OpenCV 依赖

### 3. 权限不足

检查：

- `User=` 是否是实际运行用户
- 运行目录是否对该用户可读可执行

## 与自动检测的配合

如果 `shell_jetson` 同时启用了自动检测，那么自启后它会自动扫描 `path` 对应目录下当天的批次数据。

推荐把程序、共享挂载和 `project.xml` 一起配好，并确保**先挂载、再启动 `shell_jetson`**，这样 Jetson 开机后可以自动完成：

1. 挂载 Windows 共享
2. 启动 `shell_jetson`
3. 自动扫描当天目录

如果你遇到“刚开机时路径不存在、稍后重启程序又恢复”的情况，通常就是共享挂载还没完成。此时优先检查：

- `/etc/fstab` 里是否配置了 `x-systemd.automount`
- 挂载点是否使用了 `_netdev`、`nofail`、`x-systemd.requires=network-online.target`
- `proj2-shell.service` 是否只保留了 `After=network-online.target remote-fs.target`，而没有再硬等 `RequiresMountsFor=`

如果你希望更稳，建议在 `fstab` 里同时保留 `x-systemd.automount`，这样网络共享即使开机时还没完全就绪，也会在第一次访问时自动挂载。

对于这种共享挂载场景，建议把 `proj2-shell.service` 里的 `RequiresMountsFor=` 去掉，只保留 `After=network-online.target remote-fs.target`，让 `shell_jetson` 自己在自动检测循环里等待路径可用。

## 相关文档

- `docs/superpowers/jetson_cmake.md`
- `docs/superpowers/specs/2026-04-14-auto-detect-design.md`
