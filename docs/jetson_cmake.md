# Jetson 执行命令整理

下面按**最短可跑通路径**整理，默认你是在 **Jetson AGX Orin / JetPack 5.1.2** 上本机编译和运行。

## 1) 进入源码根目录

```bash
cd /path/to/proj2_20260411
```

## 2) 配置构建

Jetson 这台机器上的 CMake 是 **3.16**，所以下面按直接命令行 configure 的方式来，不依赖 `cmake --preset`。

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release
```

如果你的 OpenCV / TensorRT / CUDA 不在默认系统路径下，再加上相应前缀，例如：

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/usr;/usr/local;/usr/local/cuda"
```

## 3) 编译

```bash
cmake --build build-jetson -j
```

构建完成后，预期会生成：

- `libproj2.so`
- `shell_jetson`

## 3.1) 如果要在 Jetson 上源码编译，需要先拷贝什么

如果你是**在 Jetson 上直接源码编译**，建议把整个仓库拷过去；如果只问“最少要带哪些”，可以按下面分两类理解：

### 需要一起带到 Jetson 的仓库内容

- `proj2/`
- `shell/`
- `cmake/`
- `config/`
- `public/DetAlgorithm.h`
- `shell/shell_jetson.cpp`
- `public/pugixml1.15/pugixml.cpp`
- `public/json-develop/include/`
- `public/pugixml1.15/`
- `public/boost_MSVC14.4/include/`

注意：`public/` 在仓库里是被 `.gitignore` 忽略的，所以**不能只靠 Git 拉取后的工作区内容来判断是否完整**。像 `public/pugixml1.15/pugixml.cpp` 这种文件，必须通过完整的文件复制方式单独带到 Jetson，不能依赖 Git 自动同步。

说明：

- `public/DetAlgorithm.h` 是公共接口头文件，源码编译会用到
- `json-develop`、`boost`、`pugixml` 这几部分在当前工程里属于源码/头文件级依赖
- `pugixml1.15` 里还有源码文件，CMake 会直接编译它
- `shell_jetson.cpp` 是 Jetson 端的 Linux 验证壳，不拷过去就会在 `cmake` 配置阶段直接报 `Cannot find source file`

如果你在 Jetson 上执行 `cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release` 时看到：

- `Cannot find source file: .../shell/shell_jetson.cpp`
- `Cannot find source file: .../public/pugixml1.15/pugixml.cpp`

那就说明源码树没有完整同步，先把这两个文件补齐，再重新 configure。

### 不需要从这个仓库复制到 Jetson 的内容

- `public/OpenCV4.6.0/`
- `public/CUDA11.8/`
- `public/TensorRT-8.5.2.2/`
- `public/onnx2trt.exe`

说明：

- Jetson 源码编译应使用 **JetPack 自带的原生 CUDA / TensorRT**
- OpenCV 也应该使用 Jetson 上安装的系统包或原生库
- 上面这些目录主要是 Windows vendored 资源，不是 Jetson 源码编译必需品

### Jetson 侧应该已有的系统依赖

Jetson 上应安装好这些系统级依赖，而不是从仓库复制：

- CUDA / TensorRT（JetPack 提供）
- OpenCV 开发包
- `uuid` 开发库
- `dl`、`pthread` 等系统库

如果这些系统依赖不在默认路径里，再通过 `CMAKE_PREFIX_PATH` 或 `find_path` / `find_library` 的方式指向它们，而不是把 Windows 目录直接拷过去。

## 4) 准备运行目录

把下面这些放到同一个运行目录里：

- `libproj2.so`
- `shell_jetson`
- [config/](cci:9://file:///e:/0_project/proj2_20260411/config:0:0-0:0)
- Jetson 可用的 `.engine` 文件

例如：

```bash
mkdir -p runtime
cp build-jetson/libproj2.so runtime/
cp build-jetson/shell_jetson runtime/
cp -r config runtime/
cp -r engines runtime/
```

> 关键点：`shell_jetson` 目前是通过 `./libproj2.so` 的方式加载库，所以**运行时两者必须在同一目录**。

## 5) 进入运行目录并执行

```bash
cd runtime
./shell_jetson
```

如果 `config/project.xml` 里启用了 `<auto_detect enable="1" .../>`，程序会优先进入自动轮询模式：

- `path` 指向总目录
- 程序会自动查找当天日期前缀的批次目录，例如 `20260411*`
- 同一天多个批次目录会按名称从小到大依次处理
- 每个批次目录只扫描 `E1` / `E2` / `E3` / `E4`
- 已处理过的文件会通过 checkpoint 自动跳过

如果没有开启自动检测，但 `config/project.xml` 里配置了 `<pthreading><path>`，程序会按这些路径顺序处理；如果没有可用路径，才会提示手动输入，例如：

```bash
/home/nvidia/test_data/1.jpg
```

## 6) 如果动态库找不到，再补环境变量

如果运行时提示找不到依赖库，可以先临时加：

```bash
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/lib/aarch64-linux-gnu:$LD_LIBRARY_PATH
```

然后重新运行：

```bash
./shell_jetson
```

## 6.1) 断点续传说明

Jetson 版 `shell_jetson` 现在支持**文件级断点续传**：

- 如果输入的是**目录**，程序会按顺序处理目录下的 `.jpg` / `.jpeg` 或 `.json` 文件
- 如果输入的是**单个文件**，程序也会记录这个文件的完成状态
- 每处理完一个文件，程序都会立即更新 checkpoint
- 下次重新启动时，会自动跳过 checkpoint 中已经完成的文件

checkpoint 文件会生成在**目标路径所在目录旁边**，命名格式为：

```text
.proj2_checkpoint_<FNV1a64>.txt
```

其中 `<FNV1a64>` 是对目标路径规范化后的哈希值。

注意事项：

- checkpoint 文件是给同一个目标路径复用的，不要手动改名
- 如果你改了输入目录路径，程序会生成新的 checkpoint 文件
- 断点续传只针对当前路径下已处理的文件，不会递归子目录

## 6.2) 自动检测说明

Jetson 版 `shell_jetson` 现在也支持**自动检测当天批次目录**：

- 在 `config/project.xml` 中配置总目录，例如：

```xml
<pthreading>
    <path path="/home/nvidia/data"/>
    <auto_detect enable="1" poll_interval_ms="5000"/>
</pthreading>
```

- `path` 必须是**总目录**，不是 `20260411xxxxxx` 这种批次目录
- 程序会在总目录下查找当天日期前缀的所有批次目录
- 例如今天是 `20260411`，就会匹配 `20260411*`
- 如果同一天有多个批次目录，会按名称顺序依次处理
- 每个批次目录内部只扫描 `E1`、`E2`、`E3`、`E4`
- 程序会持续轮询，直到退出

推荐目录结构如下：

```text
data/
  20260411083000/
    E1/
    E2/
    E3/
    E4/
  20260411120000/
```

如果你想让 Jetson 在采集过程中不断补扫新增图片，就应该使用这个模式。

## 6.3) Jetson 开机自启 / 崩溃自启

这部分已经单独整理到：

- `docs/superpowers/specs/2026-04-14-jetson-autostart.md`

里面包含：

- 完整的 `systemd` 配置步骤
- 最短可复制命令版
- 崩溃自启策略
- 常见问题和排查方法

## 7) 常见检查点

- **[cmake](cci:9://file:///e:/0_project/proj2_20260411/cmake:0:0-0:0) 配置失败**
  - 多半是 OpenCV / TensorRT / CUDA 头文件或库路径不对
- **`dlopen("./libproj2.so")` 失败**
  - 确认 `libproj2.so` 和 `shell_jetson` 在同一目录
- **engine 不兼容**
  - 需要在 Jetson 上重新生成 `.engine`
- **输入图片路径无效**
  - 确认给的是实际存在的 jpg 文件绝对路径

## 8) 推荐的最简命令串

如果你只想要一版最短流程，可以直接用：

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release
cmake --build build-jetson -j
mkdir -p runtime
cp build-jetson/libproj2.so build-jetson/shell_jetson runtime/
cp -r config runtime/
cd runtime
./shell_jetson
```

## 结论

这套命令就是当前 Jetson 第一阶段的**配置 → 编译 → 运行**路径。  
**总结：我已经把 Jetson 上的执行命令整理好了。**