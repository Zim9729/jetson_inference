---
description: Jetson 自动挂载 Windows 共享目录
---

# Jetson 自动挂载 Windows 共享目录

本文说明如何在 Jetson 上开机自动挂载 Windows 的 SMB 共享目录，并把挂载路径提供给 `shell_jetson` 使用。

## 目标场景

你的 Windows 共享信息如下：

- Windows IP：`192.168.145.1`
- 共享名：`Data`
- 用户名：`administrator`
- Jetson 挂载点：`/mnt/windows_share`

挂载完成后，程序配置中的 `path` 直接指向挂载目录即可。

```xml
<path path="/mnt/windows_share"/>
```

## 前提条件

- Windows 侧已经开启文件共享
- Jetson 能访问到 Windows 主机的网络
- 共享账号和密码可用
- Jetson 已安装 `cifs-utils`

## 第 1 步：安装挂载工具

在 Jetson 上执行：

```bash
sudo apt update
sudo apt install -y cifs-utils
```

## 第 2 步：创建挂载点

建议使用统一路径：

```bash
sudo mkdir -p /mnt/windows_share
```

如果你想放在用户目录，也可以改成别的绝对路径，但后续 `fstab` 和程序配置要保持一致。

## 第 3 步：创建凭据文件

不要把密码直接写进 `fstab`，建议创建凭据文件：

```bash
sudo vim /etc/smb-credentials
```

写入内容：

```ini
username=rk
password=123
domain=WORKGROUP
```

然后限制权限：

```bash
sudo chmod 600 /etc/smb-credentials
```

### 说明

- 如果 `domain=WORKGROUP` 不生效，可以删掉这一行再试
- 如果你的 Windows 账户属于域环境，需要改成实际域名
- 密码文件不要提交到仓库，也不要放进普通项目目录

## 第 4 步：配置开机自动挂载

编辑 `/etc/fstab`：

```bash
sudo vim  /etc/fstab
```

追加下面这一行：

```fstab
//192.168.145.1/Data  /mnt/windows_share  cifs  credentials=/etc/smb-credentials,iocharset=utf8,vers=3.0,uid=nvidia,gid=nvidia,file_mode=0664,dir_mode=0775,_netdev,nofail,x-systemd.automount,x-systemd.requires=network-online.target  0  0
```

## 参数说明

- `//192.168.145.1/Data`
  - Windows SMB 共享地址
- `/mnt/windows_share`
  - Jetson 挂载点
- `cifs`
  - 使用 SMB/CIFS 协议挂载
- `credentials=/etc/smb-credentials`
  - 从凭据文件读取用户名和密码
- `vers=3.0`
  - SMB 协议版本，通常与 Windows 兼容
- `uid=nvidia,gid=nvidia`
  - 挂载后文件归 `nvidia` 用户访问
- `file_mode=0664,dir_mode=0775`
  - 设定默认权限
- `_netdev`
  - 告诉系统这是网络挂载
- `nofail`
  - 开机时即使挂载失败也不阻塞启动
- `x-systemd.automount`
  - 访问目录时再触发挂载
- `x-systemd.requires=network-online.target`
  - 等网络就绪后再挂载

## 第 5 步：测试挂载

先检查 `fstab` 是否可用：

```bash
sudo mount -a
```

查看是否挂载成功：

```bash
df -h | grep windows_share
```

或者：

```bash
mount | grep windows_share
```

## 第 6 步：把程序路径指向挂载目录

在 `config/project.xml` 中把路径改成挂载点：

```xml
<pthreading>
    <path path="/mnt/windows_share"/>
</pthreading>
```

如果你启用了自动检测，也可以这样写：

```xml
<pthreading>
    <path path="/mnt/windows_share"/>
    <auto_detect enable="1" poll_interval_ms="5000" run_date="20260411"/>
</pthreading>
```

## 第 7 步：建议的目录结构

如果你要配合自动检测，Windows 共享里建议保持这种结构：

```text
Data/
  20260411083000/
    E1/
    E2/
    E3/
    E4/
  20260411120000/
    E1/
    E2/
    E3/
    E4/
```

这样程序就可以在 Jetson 上通过 `/mnt/windows_share` 自动扫描当天批次数据。

## 常见问题

### 1. 开机时挂载失败

可能原因：

- 网络还没起来
- Windows 主机没开机
- SMB 服务未就绪

处理建议：

- 保留 `x-systemd.automount`
- 保留 `nofail`
- 检查 Windows 和 Jetson 的网络连通性

### 2. 权限不足

可能原因：

- `uid/gid` 不对
- Windows 共享权限不足
- 凭据文件密码错误

也可能是你手动挂载时没有带上可写权限参数，导致 `/mnt/windows_share` 对当前程序用户不可写。

处理建议：

- 确认 `nvidia` 是实际运行用户
- 确认 Windows 共享权限允许访问
- 确认 `/etc/smb-credentials` 内容正确

### 排查步骤

1. 看看当前挂载参数：

```bash
mount | grep windows_share
```

2. 看看目录归属和权限：

```bash
ls -ld /mnt/windows_share
```

3. 直接测试创建目录：

```bash
mkdir /mnt/windows_share/test_dir
```

4. 再测试写文件：

```bash
touch /mnt/windows_share/test_file.txt
```

如果这里报 `Permission denied`，通常就是挂载参数或 Windows 共享权限有问题。

### 解决方法

- 如果是 Jetson 这边权限不够，建议重新挂载时带上：

```bash
sudo umount /mnt/windows_share
sudo mount -t cifs //192.168.145.1/Data /mnt/windows_share \
  -o credentials=/etc/smb-credentials,vers=3.0,uid=nvidia,gid=nvidia,file_mode=0664,dir_mode=0775
```

- 如果你已经写进 `/etc/fstab`，确认里面有：

```fstab
uid=nvidia,gid=nvidia,file_mode=0664,dir_mode=0775
```

- 如果程序不是 `nvidia` 用户运行，把 `uid/gid` 改成实际运行用户对应的值。

- 如果 root 能写、普通用户不能写，说明挂载权限有问题，不建议长期靠 root 运行程序。

- 如果 root 也不能写，重点检查 Windows 共享权限和 NTFS 安全权限：
  - 共享权限要允许写入
  - 安全权限要允许修改/创建文件夹
  - `administrator` 账号密码要正确

### 推荐的写权限挂载方式

如果你的程序需要在共享目录下创建文件夹、写文件，建议最终挂载参数使用：

```fstab
//192.168.145.1/Data  /mnt/windows_share  cifs  credentials=/etc/smb-credentials,iocharset=utf8,vers=3.0,uid=nvidia,gid=nvidia,file_mode=0664,dir_mode=0775,_netdev,nofail,x-systemd.automount,x-systemd.requires=network-online.target  0  0
```

这样 Jetson 上的 `nvidia` 用户就可以直接写入共享目录。

### 3. SMB 版本不兼容

如果 `vers=3.0` 不行，可以试：

```fstab
vers=2.1
```

如果还是不行，再根据 Windows 共享设置继续调整。

### 4. 程序读不到数据

检查：

- `/mnt/windows_share` 是否真的已经挂载
- `config/project.xml` 是否指向正确路径
- Windows 共享里是否存在当天批次目录

## 现场排障版

如果你现在就是在现场排 `Permission denied`，可以按下面顺序快速判断：

### 1. 看挂载是不是生效了

```bash
mount | grep windows_share
ls -ld /mnt/windows_share
```

- 如果看不到挂载结果，先执行 `sudo mount -a`
- 如果目录不是你期望的挂载点，先检查 `/etc/fstab`

### 2. 直接试创建文件夹和文件

```bash
mkdir /mnt/windows_share/test_dir
touch /mnt/windows_share/test_file.txt
```

- 如果这里失败，优先看挂载参数
- 如果 root 能写、普通用户不能写，说明是挂载权限问题
- 如果 root 也不能写，重点看 Windows 共享权限

### 3. 直接用推荐挂载参数重挂一次

```bash
sudo umount /mnt/windows_share
sudo mount -t cifs //192.168.145.1/Data /mnt/windows_share \
  -o credentials=/etc/smb-credentials,vers=3.0,uid=nvidia,gid=nvidia,file_mode=0664,dir_mode=0775
```

### 4. 还不行就检查 Windows 侧

- 确认 `administrator` 对共享目录有写权限
- 确认 NTFS 安全权限允许创建文件夹和写文件
- 确认共享名 `Data` 没写错

### 5. 最后再确认程序配置

```xml
<path path="/mnt/windows_share"/>
```

如果你启用了自动检测，也确认 `auto_detect` 仍然指向这个挂载目录。

## 推荐的最短执行顺序

如果你只想快速配置，可以按下面顺序执行：

```bash
sudo apt update
sudo apt install -y cifs-utils
sudo mkdir -p /mnt/windows_share
sudo nano /etc/smb-credentials
sudo chmod 600 /etc/smb-credentials
sudo nano /etc/fstab
sudo mount -a
```

然后把 `config/project.xml` 里的 `path` 改成：

```xml
<path path="/mnt/windows_share"/>
```

## 与程序启动配合

挂载完成后，你可以让程序通过 `systemd` 自启。推荐顺序是：

1. Jetson 开机
2. 系统自动挂载 Windows 共享
3. `shell_jetson` 启动并扫描 `/mnt/windows_share`

如果你还没有给 `shell_jetson` 配置自启，可以参考同目录下的 Jetson 运行说明文档。
