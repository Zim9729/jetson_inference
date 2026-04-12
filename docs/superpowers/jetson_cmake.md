# Jetson 执行命令整理

下面按**最短可跑通路径**整理，默认你是在 **Jetson AGX Orin / JetPack 5.1.2** 上本机编译和运行。

## 1) 进入源码根目录

```bash
cd /path/to/proj2_20260411
```

## 2) 配置构建

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