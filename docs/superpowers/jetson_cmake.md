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
- `public/json-develop/include/`
- `public/pugixml1.15/`
- `public/boost_MSVC14.4/include/`

说明：

- `public/DetAlgorithm.h` 是公共接口头文件，源码编译会用到
- `json-develop`、`boost`、`pugixml` 这几部分在当前工程里属于源码/头文件级依赖
- `pugixml1.15` 里还有源码文件，CMake 会直接编译它

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

程序会提示输入 jpg 路径，例如：

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

## 7) 常见检查点

- **[cmake](cci:9://file:///e:/0_project/proj2_20260411/cmake:0:0-0:0) 配置失败**
  - 多半是 OpenCV / TensorRT / CUDA 头文件或库路径不对
- **`dlopen("./libproj2.so")` 失败**
  - 确认 `libproj2.so` 和 `shell_jetson` 在同一目录
- **engine 不兼容**
  - 需要在 Jetson 上重新生成 `.engine`
- **输入图片路径无效**
  - 确认给的是实际存在的 jpg 文件绝对路径

# 推荐的最简命令串

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

# 结论

这套命令就是当前 Jetson 第一阶段的**配置 → 编译 → 运行**路径。  
**总结：我已经把 Jetson 上的执行命令整理好了。**