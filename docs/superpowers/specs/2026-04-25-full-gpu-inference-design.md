# 全 GPU 推理优化设计文档

**日期**: 2026-04-25  
**目标**: 在 Windows 和 Jetson 平台上实现全 GPU 推理，减少串行等待、消除冗余拷贝、优化内存访问  
**约束**: 模型文件不可修改，代码复杂度可控

---

## 1. 背景与现状分析

### 1.1 当前性能瓶颈

从 perf 分析数据（Windows 平台）:

| 阶段 | 平均耗时 | 占比 |
|------|---------|------|
| `detect_process_total` | ~321ms | 100% |
| `area0` | 36ms | 11% |
| `area1` | 19ms | 6% |
| `detail0` | 31ms | 10% |
| `detail1` | 25ms | 8% |
| `element0` | 17ms | 5% |
| `element1` | 17ms | 5% |
| `element2` | 37ms | 12% |
| `read_image` | ~5ms | 2% |
| `save_result_image` | ~3ms | 1% |
| `save_json` | ~1ms | <1% |

### 1.2 核心问题

1. **串行执行**: 7 个模型依次推理，总时间 = 各模型耗时之和
2. **重复内存拷贝**: 每个模型都有独立的 H2D/D2H 传输
3. **CPU 预处理**: `cv::resize`、`cv::dnn::blobFromImage` 在 CPU 上执行
4. **CPU 后处理**: NMS 在 CPU 上执行

---

## 2. 平台架构差异

### 2.1 Windows (独立 GPU - dGPU)

```
┌──────────┐         PCIe Bus          ┌──────────┐
│   CPU    │  ←─────────────────────→  │   GPU    │
│  内存    │   cudaMemcpy (H2D/D2H)   │  显存    │
└──────────┘                            └──────────┘
```

**特点**:
- CPU 内存与 GPU 显存物理分离
- 必须通过 `cudaMemcpy` 传输数据
- PCIe 带宽是瓶颈（~16GB/s for PCIe 4.0 x16）
- 优化重点：**减少拷贝次数、批量传输**

### 2.2 Jetson (集成 GPU - iGPU)

```
┌─────────────────────────────────────────────────┐
│           共享物理内存 (Unified Memory)           │
│  ┌──────────┐                      ┌──────────┐  │
│  │   CPU    │ ←── 零拷贝访问 ───→ │   GPU    │  │
│  │  虚拟地址 │    (相同物理页)     │  虚拟地址 │  │
│  └──────────┘                      └──────────┘  │
└─────────────────────────────────────────────────┘
```

**特点**:
- CPU 和 GPU 共享同一物理内存
- 支持 `cudaMallocManaged` 零拷贝
- 无 PCIe 传输开销
- 优化重点：**统一内存分配、避免数据迁移**

### 2.3 设计策略差异

| 优化点 | Windows 策略 | Jetson 策略 |
|--------|-------------|-------------|
| **输入图像** | 预分配 pinned memory，批量 H2D | `cudaMallocManaged`，直接访问 |
| **预处理** | CUDA kernel (必须，减少 CPU-GPU 往返) | CUDA kernel (可选，也可 CPU) |
| **模型输入** | 各模型独立显存缓冲区 | 共享 unified memory 缓冲区 |
| **Stream 并行** | 多 Stream 重叠计算与传输 | 多 Stream 最大化 GPU 利用率 |
| **后处理** | 尽量在 GPU 完成 NMS | 可 CPU 处理（无传输开销） |

---

## 3. 设计方案

### 3.1 架构总览

```
┌─────────────────────────────────────────────────────────────────────┐
│                         InferencePipeline                           │
├─────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────────────────┐  │
│  │  ImageLoader │──▶│  Preprocess │──▶│   GPU Inference Engine   │  │
│  │  (ThreadPool)│   │ (CUDA/CPU)  │   │  (7 Models, 7 Streams) │  │
│  └─────────────┘   └─────────────┘   │ ┌─────┬─────┬─────┬────┐ │  │
│                                      │ │ m0  │ m1  │ m2  │... │ │  │
│  ┌─────────────┐   ┌─────────────┐   │ │ s0  │ s1  │ s2  │... │ │  │
│  │ ResultSaver │◀──│ Postprocess │◀──│ └─────┴─────┴─────┴────┘ │  │
│  │             │   │ (NMS/Filter)│   └─────────────────────────┘  │
│  └─────────────┘   └─────────────┘                               │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 核心优化点

#### 优化点 1: 多模型并行 (Multi-Stream)

**当前串行逻辑** (`detect.cpp`):
```cpp
// 当前：完全串行，总时间 = 36+19+31+25+17+17+37 = 182ms (仅模型推理)
area_obj->process(img, areas0);       // 36ms
area_obj1->process(img, areas1);      // 19ms
koujian_obj->process(img, areas, v0); // 31ms
koujian_obj1->process(img, areas, v1);// 25ms
element_objs[0]->process(img, areas, r0); // 17ms
element_objs[1]->process(img, areas, r1); // 17ms
element_objs[2]->process(img, areas, r2); // 37ms
```

**优化后并行逻辑**:
```cpp
// 阶段1: area0 + area1 并行 (无依赖)
std::thread t0([&]{ area_obj->process(img, areas0); });
std::thread t1([&]{ area_obj1->process(img, areas1); });
t0.join(); t1.join();
merge(areas0, areas1, areas);  // 合并ROI结果

// 阶段2: 其余5个模型并行 (都依赖areas)
std::thread t2([&]{ koujian_obj->process(img, areas, v0); });
std::thread t3([&]{ koujian_obj1->process(img, areas, v1); });
std::thread t4([&]{ element_objs[0]->process(img, areas, r0); });
std::thread t5([&]{ element_objs[1]->process(img, areas, r1); });
std::thread t6([&]{ element_objs[2]->process(img, areas, r2); });
t2.join(); t3.join(); t4.join(); t5.join(); t6.join();

// 总时间 ≈ max(36,19) + max(31,25,17,17,37) = 36 + 37 = 73ms
// 理论加速: 182ms → 73ms (60%提升)
```

#### 优化点 2: 零拷贝内存 (Jetson 专用)

**Windows (传统方式)**:
```cpp
// 分配 pinned memory 加速传输
float* h_input;
cudaMallocHost(&h_input, size);  // Pinned memory
// CPU 填充数据
cudaMemcpyAsync(d_input, h_input, size, H2D, stream);
```

**Jetson (零拷贝方式)**:
```cpp
// 分配 Unified Memory，CPU/GPU 共享
float* unified_ptr;
cudaMallocManaged(&unified_ptr, size);  // 零拷贝
// CPU 直接填充，GPU 直接读取（无显式拷贝）
// GPU 内核执行前可能需要 cudaStreamSynchronize
```

#### 优化点 3: GPU 预处理

**CUDA Kernel 实现**:
```cpp
// 替代 cv::resize + blobFromImage
__global__ void preprocess_kernel(
    uint8_t* src, int src_w, int src_h,
    float* dst, int dst_w, int dst_h,
    float scale, int x_offset, int y_offset
) {
    // 1. 双线性插值 resize
    // 2. BGR→RGB 转换
    // 3. /255.0 归一化
    // 4. NHWC→NCHW 排列
    // 全部在 GPU 上完成，无中间拷贝
}
```

### 3.3 接口设计

**核心类**: `FullGpuInference`

```cpp
// full_gpu_inference.h
#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <future>

namespace inference {

struct ModelResult {
    std::vector<cv::Vec6f> detections;
    int model_id;
    float confidence;
};

struct InferenceConfig {
    int num_models = 7;
    int num_threads = 7;          // 并行线程数
    bool use_gpu_preprocess = true;
    bool use_gpu_postprocess = false;  // NMS 通常在 CPU 更快（结果少）
    int pipeline_depth = 1;       // 1=单张延迟优先, >1=吞吐量优先
    
    // 平台特定
    bool use_unified_memory = false;  // true for Jetson, false for Windows
    int cuda_streams_per_model = 1;
};

class FullGpuInference {
public:
    explicit FullGpuInference(const InferenceConfig& config);
    ~FullGpuInference();
    
    // 禁止拷贝（持有 GPU 资源）
    FullGpuInference(const FullGpuInference&) = delete;
    FullGpuInference& operator=(const FullGpuInference&) = delete;
    
    // 初始化所有模型
    bool initialize(const std::vector<std::string>& model_paths);
    
    // 单张推理（延迟优先）
    std::vector<ModelResult> infer_single(const cv::Mat& image);
    
    // 批量推理（吞吐量优先）
    std::vector<std::vector<ModelResult>> infer_batch(
        const std::vector<cv::Mat>& images);
    
    // 同步等待所有模型完成
    void synchronize();
    
    // 获取性能统计
    struct PerfStats {
        float avg_preprocess_ms;
        float avg_inference_ms;
        float avg_postprocess_ms;
        float throughput_fps;
    };
    PerfStats get_performance_stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // PIMPL 隐藏平台差异
};

// 平台检测辅助函数
InferenceConfig get_optimal_config_for_platform();
// Windows: use_unified_memory=false, use_gpu_preprocess=true
// Jetson: use_unified_memory=true, use_gpu_preprocess=true

}  // namespace inference
```

### 3.4 平台适配层 (PIMPL 实现)

**Windows 实现** (`full_gpu_inference_win.cpp`):
```cpp
class FullGpuInference::Impl {
    struct ModelContext {
        MyYolov5Det model;           // 原有模型结构
        cudaStream_t stream;         // 每个模型独立 Stream
        float* d_input;              // GPU 输入缓冲区
        float* d_output;             // GPU 输出缓冲区
        float* h_output_pinned;      // Pinned 内存用于异步回传
    };
    std::vector<ModelContext> models_;
    
    // 预分配 pinned memory 输入池
    float* h_input_pool_;
    
    void preprocess_cpu_to_gpu(const cv::Mat& img, float* d_input, cudaStream_t stream);
    // 1. CPU resize/blobFromImage
    // 2. cudaMemcpyAsync H2D
};
```

**Jetson 实现** (`full_gpu_inference_jetson.cpp`):
```cpp
class FullGpuInference::Impl {
    struct ModelContext {
        MyYolov5Det model;
        cudaStream_t stream;
        float* unified_input;        // Unified Memory 输入
        float* unified_output;         // Unified Memory 输出
        // 无需单独 h_output，直接读取 unified_output
    };
    
    void preprocess_unified(const cv::Mat& img, float* unified_ptr);
    // 1. CPU 直接写入 unified memory
    // 2. 可选：cudaStreamAttachMemAsync 优化迁移
};
```

---

## 4. 实施计划（按平台通用性排序）

### 阶段 1: 多模型并行化（Windows + Jetson 共同）

**目标**: 7 个模型并行推理，减少串行等待时间

**当前状态**: Windows 侧已完成阶段 1/2 并行化实现与验证；最新稳定结果 `227.43ms`，相对基线 `320.71ms` 提升约 `29.1%`，尚未达到最初预期的 `180ms` 目标。

**适用平台**: Windows (dGPU) 和 Jetson (iGPU) 均可使用

**原理**: 无论 CPU/GPU 内存架构如何，多 CUDA Stream 并行都能提升 GPU 利用率

**修改文件**:
- `proj2/detect.cpp`: 修改 `detect_process` 或 `in_process` 函数
- `proj2/thread_pool.h` (可选): 轻量级线程池封装，当前版本未创建，实际直接在 `detect.cpp` 中使用 `std::async`

**代码变更**:
```cpp
// 阶段1: area0 + area1 并行（无依赖）
auto f0 = std::async(std::launch::async, [&]{ area_obj->process(img, areas0); });
auto f1 = std::async(std::launch::async, [&]{ area_obj1->process(img, areas1); });
f0.wait(); f1.wait();
merge(areas0, areas1, areas);

// 阶段2: detail0/1 + element0/1/2 并行（依赖 areas）
auto f2 = std::async([&]{ koujian_obj->process(img, areas, v0); });
auto f3 = std::async([&]{ koujian_obj1->process(img, areas, v1); });
auto f4 = std::async([&]{ element_objs[0]->process(img, areas, r0); });
auto f5 = std::async([&]{ element_objs[1]->process(img, areas, r1); });
auto f6 = std::async([&]{ element_objs[2]->process(img, areas, r2); });
// 等待全部完成...
```

**验证指标**:
| 平台 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| Windows | 320.71ms | 227.43ms | -29.1% |
| Jetson | ~350ms | 待验证 | 待验证 |

**时间预估**: 2-3 天

---

### 阶段 2: GPU 预处理优化（Windows + Jetson 共同）

**目标**: 将 `cv::resize` + `blobFromImage` 从 CPU 移到 GPU

**适用平台**: 两平台均可使用 CUDA 预处理

**原理**:
- **Windows**: CPU预处理 → GPU拷贝 → 推理（2次数据传输）
- **Jetson**: CPU预处理 → 共享内存 → 推理（1次隐式传输）

优化后统一为：
- 原始图像上传 GPU（1次 H2D）
- GPU 完成 resize + normalize + NCHW
- 直接送入 TensorRT（无额外拷贝）

**新增文件**:
- `proj2/cuda_preprocess.cu`: CUDA 预处理内核
- `proj2/gpu_preprocessor.h/.cpp`: 预处理管理器

**修改文件**:
- `proj2/CMakeLists.txt`: 添加 CUDA 编译
- `proj2/yolov5Trt.cpp`: 替换 `OneDetection` 中的预处理逻辑

**验证指标**:
| 平台 | 阶段1后 | 阶段2后 | 额外提升 |
|------|---------|---------|----------|
| Windows | 180ms | ~150ms | -17% |
| Jetson | 200ms | ~170ms | -15% |

**时间预估**: 3-4 天

---

### 阶段 3: Jetson 零拷贝优化（Jetson 专用）

**目标**: 利用 Jetson 共享内存架构，实现真正的零拷贝

**适用平台**: 仅 Jetson (ARM64 + 集成 GPU)

**优化点**:

| 优化项 | 当前做法 | 优化后 | 收益 |
|--------|----------|--------|------|
| **输入图像** | `cudaMemcpy` H2D | `cudaMallocManaged` 统一内存 | 消除 ~5ms/模型 |
| **模型输出** | `cudaMemcpy` D2H | 直接读取统一内存 | 消除 ~3ms/模型 |
| **多模型输入** | 7份独立拷贝 | 1份共享，GPU直读 | 显存节省 6x |

**新增文件**:
- `proj2/jetson_memory_pool.h/.cpp`: Jetson 专用内存池

**修改文件**:
- `proj2/detect.cpp`: Jetson 平台使用内存池

**验证指标**:
| 平台 | 阶段2后 | 阶段3后 | 额外提升 |
|------|---------|---------|----------|
| Windows | 150ms | N/A | 不适用 |
| Jetson | 170ms | ~100ms | -41% |

**时间预估**: 2-3 天

---

### 阶段 4: Pipeline 批量处理（可选，两平台通用）

**目标**: 最大化吞吐量（批量处理场景）

**适用场景**: 一次性处理 100+ 张图片

**架构**:
```
Thread 1: Load Img0 → Load Img1 → Load Img2 → ...
   ↓           ↓           ↓
Thread 2: Preprocess0 → Preprocess1 → Preprocess2 → ...
   ↓           ↓           ↓
GPU     : Infer0      → Infer1      → Infer2      → ...
   ↓           ↓           ↓
Thread 3: Postprocess0 → Postprocess1 → Postprocess2 → ...
   ↓           ↓           ↓
Thread 4: Save0       → Save1       → Save2       → ...
```

**时间预估**: 3-4 天（可选）

---

## 5. 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| **线程竞争导致结果不稳定** | 高 | 各模型使用独立 CUDA Stream，避免 Stream 0 阻塞 |
| **显存不足 (Jetson)** | 中 | 7 个模型同时加载，显存占用大；使用显存池动态分配 |
| **CPU 预处理成为瓶颈** | 中 | 如果图片很大（4K+），cv::resize 很慢；改用 GPU 预处理 |
| **Jetson 零拷贝性能不如预期** | 低 | Jetson 的 unified memory 可能有 page fault 开销；可回退到显式拷贝 |
| **模型输出格式不一致** | 低 | 各模型输出结构相同（`std::vector<cv::Vec6f>`），无需修改 |
| **Windows CUDA 环境配置** | 中 | 需要确保开发机器和部署机器 CUDA 版本一致 |

---

## 6. 预期收益总结（按新阶段顺序）

| 阶段 | 优化内容 | Windows | Jetson | 复杂度 |
|------|---------|---------|--------|--------|
| **基线** | - | 321ms | ~350ms | - |
| **阶段1** | 多模型并行（共同） | 180ms (-44%) | 200ms (-43%) | 低 |
| **阶段2** | GPU预处理（共同） | 150ms (-17%) | 170ms (-15%) | 中 |
| **阶段3** | 零拷贝（Jetson专用） | N/A | 100ms (-41%) | 中 |
| **阶段4** | Pipeline批量（共同） | 80ms 吞吐 | 60ms 吞吐 | 高 |

### 累计收益

| 平台 | 最终延迟 | 总提升 |
|------|---------|--------|
| **Windows** | **~150ms** | **-53%** |
| **Jetson** | **~100ms** | **-71%** |

### 分阶段验证策略

**阶段1完成后验证**（最低风险，最高收益）：
- 如果 Windows < 200ms 且 Jetson < 250ms → 继续阶段2
- 如果不达标 → 先按 A→B→C 继续压缩阶段1收益，再决定是否进入阶段2

**阶段2完成后验证**（中等风险，中等收益）：
- 如果 Windows < 160ms 且 Jetson < 180ms → 继续阶段3（Jetson专用）
- 如果不达标 → 检查预处理 kernel 是否优化到位

**阶段3完成后验证**（仅 Jetson）：
- 如果 Jetson < 120ms → 目标达成
- 如果不达标 → 检查 unified memory page fault 情况

---

## 7. 附录: 关键代码片段

### 7.1 CUDA 预处理内核 (cuda_preprocess.cu)

```cuda
__global__ void preprocess_kernel(
    const uint8_t* __restrict__ src,
    float* __restrict__ dst,
    int src_w, int src_h,
    int dst_w, int dst_h,
    float scale_x, float scale_y
) {
    int dx = blockIdx.x * blockDim.x + threadIdx.x;
    int dy = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (dx >= dst_w || dy >= dst_h) return;
    
    // 双线性插值坐标
    float sx = dx * scale_x;
    float sy = dy * scale_y;
    
    // ... 插值计算 ...
    
    // NCHW 排列: dst[0*H*W + dy*W + dx] = R/255.0
    //           dst[1*H*W + dy*W + dx] = G/255.0
    //           dst[2*H*W + dy*W + dx] = B/255.0
}
```

### 7.2 平台检测代码

```cpp
InferenceConfig get_optimal_config_for_platform() {
    InferenceConfig config;
    
    #ifdef _WIN32
        config.use_unified_memory = false;
        config.use_gpu_preprocess = true;
        config.num_threads = 7;
    #elif defined(__aarch64__) && defined(__linux__)
        // Jetson (ARM64 Linux)
        config.use_unified_memory = true;
        config.use_gpu_preprocess = true;
        config.num_threads = 7;
    #endif
    
    return config;
}
```

### 7.3 CMake 平台适配

```cmake
# CMakeLists.txt
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "Building for Jetson (ARM64)")
    add_definitions(-DJETSON_BUILD)
    set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS} -gencode arch=compute_87,code=sm_87)  # Orin
elseif(WIN32)
    message(STATUS "Building for Windows")
    add_definitions(-DWINDOWS_BUILD)
    set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS} -gencode arch=compute_86,code=sm_86)  # RTX 30xx
endif()
```

---

## 8. 后续步骤

1. **Review 本设计文档**: 确认方案符合预期
2. **阶段 1 实施**: 先实现基础并行化（改动最小）
3. **性能验证**: 对比优化前后 perf 日志
4. **阶段 2 实施**: 根据阶段 1 结果决定是否继续
5. **文档更新**: 实施过程中更新设计文档（如有偏差）
