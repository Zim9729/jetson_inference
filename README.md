# proj2_20260411

`proj2_20260411` 是一个基于 **C++ / CMake** 的图像检测项目，当前支持：

- **Windows**：以 `proj2.dll + shell.exe` 的方式运行
- **Jetson / Linux**：以 `libproj2.so + shell_jetson` 的方式运行

核心检测能力位于 `proj2` 目录下，外层 `shell` 目录提供命令行入口，用于加载动态库并触发检测流程。

---

## 项目目标

这个项目主要用于：

- 读取 `config/project.xml` 和对应的配置文件
- 按配置运行图片检测
- 支持 `jpg/jpeg` 和 `json` 两种输入方式
- 支持目录批量处理
- 支持 **文件级断点续传**
- 在 Windows 和 Jetson 上保持一致的基本工作流

---

## 功能概览

### 1. 双平台支持

- **Windows**
  - 动态库：`proj2.dll`
  - 启动程序：`shell.exe`
- **Jetson / Linux**
  - 动态库：`libproj2.so`
  - 启动程序：`shell_jetson`

### 2. 输入方式

支持两类输入：

- **单个文件**
  - `.jpg` / `.jpeg`
  - `.json`
- **目录**
  - 只处理当前目录下的文件
  - **不会递归子目录**

### 3. 批量路径处理

`shell` 和 `shell_jetson` 都会优先读取 `config/project.xml` 中的 `<pthreading><path>` 配置：

- 如果配置了多个 `<path>`，会按顺序依次处理
- 如果没有可用路径，才会进入手动输入模式

### 4. 断点续传

项目已加入 **文件级 checkpoint 机制**：

- 每处理完一个文件，就立即写入 checkpoint
- 下次启动时会自动跳过已经完成的文件
- checkpoint 文件与目标路径对应，放在**目标路径所在目录旁边**
- 同一路径重复运行时，可以继续未完成的文件

---

## 目录结构

```text
proj2_20260411/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── cmake/
├── config/
│   ├── project.xml
│   ├── config_*.xml
│   └── config_code.xml
├── docs/
├── proj2/
│   ├── detect.cpp
│   ├── myxml.h
│   ├── mylog.cpp
│   ├── myjson.h
│   └── ...
├── public/
├── shell/
│   ├── shell.cpp
│   └── shell_jetson.cpp
├── test_data/
└── release/
```

### 主要模块说明

- **`proj2/`**
  - 检测库核心逻辑
  - 配置读取、模型初始化、图像处理、结果输出
- **`shell/`**
  - Windows / Jetson 的命令行入口
  - 动态加载检测库并调用 `detect_process`
- **`config/`**
  - 项目配置、业务配置、检测配置
- **`public/`**
  - 第三方依赖与头文件
  - 包括 `pugixml`、`nlohmann/json`、`Boost`、OpenCV、CUDA、TensorRT 等依赖路径或内容
- **`docs/`**
  - Jetson 支持、构建说明、设计与实施文档

---

## 核心运行流程

1. 启动 `shell` 或 `shell_jetson`
2. 加载动态库：
   - Windows：`proj2.dll`
   - Jetson：`libproj2.so`
3. 从 `config/project.xml` 读取项目名称和批处理路径
4. 根据 `project.xml` 中的 `<imgtype>` 判断处理 `jpg` 还是 `json`
5. 对目录或单文件进行检测
6. 每处理完一个文件后写入 checkpoint
7. 下次重启时自动跳过已处理文件

---

## 配置文件说明

### `config/project.xml`

这是最关键的入口配置文件，当前会被两部分读取：

- **检测库**：用于读取项目名称，进而定位 `config_<project>.xml`
- **shell 程序**：用于读取批处理路径和输入类型

一个典型示例如下：

```xml
<root>
    <project name="guang3"/>
    <pthreading>
        <exename exename="proj2.exe"/>
        <imgtype json0_jpg1="1"/>
        <thread_type thread_type="0"/>
        <thread_num thread_num="1"/>
        <path path="E:\0_project\proj2_20260411\test_data"/>
    </pthreading>
</root>
```

### 字段含义

- **`<project name="..."/>`**
  - 项目名称
  - 检测库会据此查找 `config/config_<project>.xml`
- **`<imgtype json0_jpg1="1"/>`**
  - `1`：处理 `jpg/jpeg`
  - `0`：处理 `json`
- **`<path path="..."/>`**
  - 要处理的目标路径
  - 可以配置多个，程序按顺序处理
- **`thread_type` / `thread_num`**
  - 当前仓库保留了这些字段
  - 主要用于原有批处理配置语义

### 目录与文件输入规则

- 如果 `path` 是目录：
  - 只处理当前目录下的文件
  - 不递归子目录
- 如果 `path` 是单个文件：
  - 直接处理该文件

---

## checkpoint / 断点续传说明

当前 Windows 和 Jetson 的 shell 都支持 **文件级断点续传**。

### 行为

- 对于目录输入：
  - 程序会先扫描目录下的目标文件
  - 已经处理过的文件会被跳过
- 对于单文件输入：
  - 程序会记录该文件是否已完成
- 每个处理成功的文件都会立即写入 checkpoint
- 重启后会继续处理未完成的文件
- `det_state=0` 的文件会正常继续处理，不会因为空结果而中断后续文件

### checkpoint 文件位置

checkpoint 文件会生成在**目标路径所在目录旁边**，命名格式为：

```text
.proj2_checkpoint_<FNV1a64>.txt
```

例如：

- 如果目标路径是 `E:\0_project\proj2_20260411\test_data`
- checkpoint 就会出现在 `E:\0_project\proj2_20260411\` 这一层附近

### 注意事项

- checkpoint 文件是和目标路径绑定的
- 改变输入目录后，会生成新的 checkpoint 文件
- checkpoint 只记录当前目录下已处理的文件
- 不会递归保存子目录中的文件状态

---

## Windows 构建

### 环境要求

建议使用：

- **Visual Studio 2019 x64** 或兼容版本
- **CMake 3.16+**
- Windows 64 位系统
- 已准备好项目依赖文件

### 构建方式 1：使用 CMake Presets

仓库中提供了 `CMakePresets.json`，可以直接使用：

```powershell
cmake --preset vs2019-x64
cmake --build --preset vs2019-x64-release
```

### 构建方式 2：命令行构建

```powershell
cmake -S . -B build/cmake-vs2019-x64 -G "Visual Studio 16 2019" -A x64
cmake --build build/cmake-vs2019-x64 --config Release
```

### Windows 构建产物

构建完成后，输出会放到 `release/` 目录下，通常包括：

- `proj2.dll`
- `shell.exe`
- 相关运行依赖文件

---

## Windows 运行

### 运行目录

建议把以下内容放到同一个运行目录：

- `shell.exe`
- `proj2.dll`
- `config/`
- 所需的运行依赖 DLL

### 运行方式

进入输出目录后启动：

```powershell
cd release
.\shell.exe
```

### 运行逻辑

- 如果 `config/project.xml` 中配置了 `<auto_detect enable="1" .../>`，程序会优先进入自动轮询模式
- 自动轮询模式下，`path` 必须指向总目录，程序会自动查找指定日期前缀的批次目录并持续处理
- 如果 `auto_detect` 中配置了 `run_date="YYYYMMDD"`，程序会优先按这个日期前缀查找批次目录
- 如果没有配置 `run_date`，或者 `run_date` 格式不合法，程序会给出警告并回退到当天
- 如果没有开启自动检测，但配置了 `<path>`，程序会按原来的路径顺序处理这些路径
- 如果没有配置路径，程序会提示你手动输入路径

### 自动检测模式

当 `config/project.xml` 中启用自动检测后：

```xml
<auto_detect enable="1" poll_interval_ms="5000" run_date="20260411"/>
```

程序会进入持续轮询模式，并按以下规则工作：

- `path` 指向总目录
- 在总目录下查找 `run_date` 对应日期前缀开头的批次目录，例如 `run_date="20260411"` 就匹配 `20260411*`
- 如果同一天有多个批次目录，则按名称从小到大依次处理
- 每个批次目录只扫描 `E1`、`E2`、`E3`、`E4`
- 扫描会持续进行，直到程序退出
- 已经处理过的文件会通过 checkpoint 自动跳过

建议在“数据会持续采集”的场景下启用这个模式。

一个典型配置示例如下：

```xml
<pthreading>
    <path path="E:\0_project\proj2_20260411\data"/>
    <auto_detect enable="1" poll_interval_ms="5000" run_date="20260411"/>
</pthreading>
```

这里的 `data` 是总目录，不是某个 `20260411xxxxxx` 批次目录。
程序会先找到 `run_date` 指定日期的批次目录，再进入每个批次目录下的 `E1` 到 `E4` 继续处理。

当单个文件没有缺陷结果时，程序会输出 `flaws=0`，不会再打印空的 `outdata`。

---

## Jetson / Linux 构建

### 环境要求

建议在 **Jetson AGX Orin / JetPack 5.1.2** 上原生构建。

依赖通常包括：

- CMake 3.16+
- CUDA
- TensorRT
- OpenCV 开发包
- `uuid`
- `dl`
- `pthread`
- `pugixml`
- `nlohmann/json`
- Boost 头文件

### 构建命令

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release
cmake --build build-jetson -j
```

如果系统库不在默认路径下，可以补充前缀，例如：

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/usr;/usr/local;/usr/local/cuda"
```

### Jetson 构建产物

构建完成后，通常会生成：

- `libproj2.so`
- `shell_jetson`

---

## Jetson / Linux 运行

### 运行目录

建议把以下内容放到同一个运行目录：

- `libproj2.so`
- `shell_jetson`
- `config/`
- Jetson 可用的 `.engine` 文件

### 运行方式

```bash
cd /path/to/runtime
./shell_jetson
```

### 依赖库缺失时

如果启动时报错找不到依赖库，可以临时加环境变量：

```bash
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/lib/aarch64-linux-gnu:$LD_LIBRARY_PATH
```

然后重新执行：

```bash
./shell_jetson
```

### 运行逻辑

- 如果 `config/project.xml` 中配置了 `<auto_detect enable="1" .../>`，程序会优先进入自动轮询模式
- 自动轮询模式下，`path` 必须指向总目录，程序会自动查找当天批次目录并持续处理
- 如果没有开启自动检测，但配置了 `<path>`，程序会按原来的路径顺序处理这些路径
- 如果没有配置路径，程序会提示你手动输入路径

### Jetson 开机自启 / 崩溃自启

如果你想让 `shell_jetson` 在 Jetson 上开机自动运行，或者崩溃后自动重启，建议用 `systemd` 管理服务。

最简思路是：

- 把 `shell_jetson`、`libproj2.so`、`config/` 放到同一个运行目录
- 新建 `systemd` service
- 配置 `Restart=always` 或 `Restart=on-failure`
- 用 `systemctl enable --now` 开机自启

详细步骤和可复制的 `service` 示例，请看：

- `docs/superpowers/jetson_cmake.md`

---

## 输入类型说明

### JPG 模式

当 `json0_jpg1="1"` 时：

- 处理 `.jpg`
- 处理 `.jpeg`

### JSON 模式

当 `json0_jpg1="0"` 时：

- 处理 `.json`

---

## 常见使用示例

### 示例 1：处理一个目录中的图片

在 `config/project.xml` 中配置：

```xml
<pthreading>
    <path path="E:\0_project\proj2_20260411\data"/>
    <auto_detect enable="1" poll_interval_ms="5000"/>
</pthreading>
```

然后启动程序即可。

如果开启了自动检测，程序会自动在 `data` 下查找当天批次目录并持续轮询；如果关闭自动检测，则仍然按原来的路径一次性处理。

### 示例 2：处理单个图片

如果没有配置 `<path>`，启动后手动输入：

```text
E:\0_project\proj2_20260411\test_data\1.jpg
```

### 示例 3：处理多个路径

```xml
<pthreading>
    <imgtype json0_jpg1="1"/>
    <path path="E:\0_project\proj2_20260411\test_data_1"/>
    <path path="E:\0_project\proj2_20260411\test_data_2"/>
</pthreading>
```

程序会按顺序处理 `test_data_1` 和 `test_data_2`。

如果启用了自动检测，并且这些路径都是总目录，那么程序会分别在每个总目录下查找当天批次目录并持续轮询。

### 示例 4：自动检测的目录结构

自动检测需要的数据目录形状如下：

```text
data/
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

程序会按目录名从早到晚顺序处理同一天的所有批次目录。

---

## 排障指南

### 1. `proj2.dll` / `libproj2.so` 加载失败

检查：

- 动态库是否与 shell 在同一目录
- 依赖库是否齐全
- Jetson 上是否设置了正确的 `LD_LIBRARY_PATH`

### 2. 读取 `config/project.xml` 失败

检查：

- `config/project.xml` 是否存在
- XML 是否格式正确
- `<project>` 和 `<pthreading>` 节点是否存在

### 3. 不处理预期文件

检查：

- `json0_jpg1` 是否和文件类型一致
- 目录下文件后缀是否为 `.jpg` / `.jpeg` / `.json`
- 是否已经被 checkpoint 跳过

### 4. checkpoint 没生效

检查：

- checkpoint 是否写到了目标目录旁边
- 目标路径是否被改名或移动
- 文件路径字符串是否变化

### 5. 自动检测没有找到数据

检查：

- `path` 是否指向总目录，而不是某个 `E1`/`E2` 目录
- 当天目录名是否真的以当天日期前缀开头，例如 `20260411*`
- 批次目录下是否存在 `E1`、`E2`、`E3`、`E4`
- `auto_detect enable="1"` 是否已开启
- `poll_interval_ms` 是否过小或过大

### 6. Jetson 上运行失败但 Windows 正常

检查：

- `.engine` 是否为 Jetson 兼容版本
- CUDA / TensorRT 是否匹配 JetPack 版本
- `libproj2.so` 是否使用 Jetson 环境重新构建

---

## 相关文档

- `docs/superpowers/jetson_cmake.md`
- `docs/superpowers/specs/2026-04-13-jetson-support-design.md`
- `docs/superpowers/plans/2026-04-13-jetson-support-implementation.md`
- `docs/superpowers/specs/2026-04-14-auto-detect-design.md`
- `docs/superpowers/plans/2026-04-14-auto-detect-implementation.md`

---

## 备注

- 项目当前是 **64 位** 构建
- `shell` / `shell_jetson` 才是实际运行入口
- `proj2` 目录下是核心检测逻辑
- `config/project.xml` 是运行配置入口，也是批处理路径入口

如果你需要，我还可以继续把这个 README 再整理成更偏“对外发布版”或“开发者手册版”的风格。
