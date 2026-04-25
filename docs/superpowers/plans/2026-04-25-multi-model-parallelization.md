# 多模型并行化 (Stage 1) 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use @superpowers:subagent-driven-development (recommended) or @superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将7个模型的串行推理改为并行执行，Windows和Jetson通用，预期减少40%+推理时间

**Architecture:** 使用C++11 `std::future` + `std::async` 实现轻量级并行，无需外部线程池库。分两个阶段并行：阶段1(area0+area1)无依赖并行；阶段2(detail0/1+element0/1/2)依赖areas结果但彼此独立。

**Tech Stack:** C++11, CUDA, OpenCV, std::async/std::future

**Spec Reference:** @e:\0_project\proj2_20260411\docs\superpowers\specs\2026-04-25-full-gpu-inference-design.md

---

## 文件结构映射

| 文件 | 职责 | 操作 |
|------|------|------|
| `proj2/thread_pool.h` | 简单的并行执行工具函数 | 新建 |
| `proj2/detect.cpp` | 修改 `detect_process()` 实现并行逻辑 | 修改 783-879行 |
| `proj2/detect.h` | 可能需要的辅助结构体前向声明 | 查看确认 |

---

## 前置检查

### Task 0: 确认当前代码状态

**Files:**
- Read: `e:\0_project\proj2_20260411\proj2\detect.cpp:783-879`
- Read: `e:\0_project\proj2_20260411\proj2\detect.h`

- [ ] **Step 1: 确认 detect_process 函数结构**

阅读 `detect.cpp` 783-879行，确认：
1. `area_obj->process()` 调用位置 (约798行)
2. `area_obj1->process()` 调用位置 (约803行)  
3. `koujian_obj->process()` 调用位置 (约819行)
4. `koujian_obj1->process()` 调用位置 (约824行)
5. `element_objs[i]->process()` 循环位置 (约834-841行)

- [ ] **Step 2: 确认数据结构定义**

阅读 `detect.h`，确认以下类型定义：
- `flawOutInfo` 结构体
- `imgInfo` 结构体
- `Cdetect` 类中模型指针成员：`area_obj`, `area_obj1`, `koujian_obj`, `koujian_obj1`, `element_objs[]`

---

## 核心实现

### Task 1: 创建并行工具头文件

**Files:**
- Create: `e:\0_project\proj2_20260411\proj2\parallel_utils.h`

- [ ] **Step 1: 编写 parallel_utils.h**

```cpp
#pragma once
#include <future>
#include <vector>
#include <functional>

namespace parallel {

// 执行多个任务并等待全部完成
// 使用std::async实现，自动利用线程池
template<typename Func, typename... Args>
void parallel_execute(std::vector<std::future<void>>& futures, Func&& func, Args&&... args) {
    futures.emplace_back(std::async(std::launch::async, std::forward<Func>(func), std::forward<Args>(args)...));
}

// 等待所有future完成
inline void wait_all(std::vector<std::future<void>>& futures) {
    for (auto& f : futures) {
        if (f.valid()) {
            f.wait();
        }
    }
    futures.clear();
}

// 简单包装：执行并立即等待
// 适用于少量任务（2-7个）
template<typename... Funcs>
void run_parallel(Funcs&&... funcs) {
    std::vector<std::future<void>> futures;
    (futures.emplace_back(std::async(std::launch::async, std::forward<Funcs>(funcs))), ...);
    for (auto& f : futures) {
        f.wait();
    }
}

} // namespace parallel
```

- [ ] **Step 2: 验证文件创建成功**

Run: `ls -la e:\0_project\proj2_20260411\proj2\parallel_utils.h`
Expected: 文件存在，大小约 800-1000 bytes

- [ ] **Step 3: Commit**

```bash
git add proj2/parallel_utils.h
git commit -m "feat: add parallel utils for multi-model inference"
```

---

### Task 2: 修改 detect_process 实现阶段1并行 (area0 + area1)

**Files:**
- Modify: `e:\0_project\proj2_20260411\proj2\detect.cpp:783-879`

- [ ] **Step 1: 添加头文件包含**

在 `detect.cpp` 顶部添加：
```cpp
#include "parallel_utils.h"
#include <future>
```

- [ ] **Step 2: 备份原 detect_process 函数**

将原783-879行完整注释掉，作为备份保留：
```cpp
/*
// 原串行实现备份
int Cdetect::detect_process(imgInfo param, std::vector<flawOutInfo>&vOutflaws)
{
    // ... 原代码 ...
}
*/
```

- [ ] **Step 3: 编写新的并行 detect_process (阶段1: area0+area1)**

```cpp
int Cdetect::detect_process(imgInfo param, std::vector<flawOutInfo>&vOutflaws)
{
    if(m_ini_state != 1 || m_imgOutsize.width<=0 || m_imgOutsize.height<=0)
        return -1;
    
    std::vector<flawOutInfo>vflaws;
    vflaws.clear();
    cv::Mat img = param.img.clone();
    if(img.cols!= m_imgOutsize.width || img.rows!= m_imgOutsize.height)
        cv::resize(img,img,m_imgOutsize);
    if((int)img.channels() != 3)
        cvtColor(img,img,cv::COLOR_GRAY2BGR);

    std::string imgname = param.jpgname;
    
    // ========== 阶段1: area0 + area1 并行 ==========
    std::vector<std::pair<cv::Vec6f,nodeInfo>>areas_area0;
    std::vector<std::pair<cv::Vec6f,nodeInfo>>areas_area1;
    
    auto future_area0 = std::async(std::launch::async, [&]() {
        if(area_obj != nullptr && istate_area == 1) {
            area_obj->process(img.clone(), areas_area0, &imgname);
        }
    });
    
    auto future_area1 = std::async(std::launch::async, [&]() {
        if(area_obj1 != nullptr && istate_area1 == 1) {
            area_obj1->process(img.clone(), areas_area1, &imgname);
        }
    });
    
    // 等待两个area模型完成
    future_area0.wait();
    future_area1.wait();
    
    // 合并areas结果
    std::vector<std::pair<cv::Vec6f,nodeInfo>>areas = areas_area0;
    areas.insert(areas.end(), areas_area1.begin(), areas_area1.end());
    std::vector<std::pair<cv::Vec6f, nodeInfo>>areas_koujian(areas);
    
    // 后续逻辑保持不变...
    int istate_daocha = 0;
    judgedaocha_by_railcount(areas, istate_daocha); 

    std::pair<cv::Vec6f,nodeInfo>tmpbed;
    tmpbed.first = cv::Vec6f(0,0,img.cols-1,img.rows-1,1,99);
    tmpbed.second.vareaIDs.push_back(1200);
    tmpbed.second.type_name = "daochuang";
    areas.push_back(tmpbed);
    
    // ... (继续原逻辑)
```

- [ ] **Step 4: 编译验证（Windows）**

Run: `cd e:\0_project\proj2_20260411 && cmake --build build --config Release --target proj2`
Expected: 编译成功，无错误

- [ ] **Step 5: 运行单图测试**

Run: `cd e:\0_project\proj2_20260411\release && .\shell.exe` 
输入测试图片路径，观察是否正常运行

- [ ] **Step 6: Commit**

```bash
git add proj2/detect.cpp
git commit -m "feat: parallelize area0 and area1 (stage 1)"
```

---

### Task 3: 实现阶段2并行 (detail0/1 + element0/1/2)

**Files:**
- Modify: `e:\0_project\proj2_20260411\proj2\detect.cpp:815-841`

- [ ] **Step 1: 替换串行koujian和element处理逻辑**

找到并替换以下代码段（约815-841行）：

```cpp
// ========== 阶段2: detail0 + detail1 + elements 并行 ==========
std::vector<flawOutInfo>vkoujian_flaws_detail0;
std::vector<flawOutInfo>vkoujian_flaws_detail1;
std::vector<std::vector<flawOutInfo>>velement_flaws(MAX_DETECT_NUM);

std::vector<std::future<void>> futures;

// koujian_obj (detail0) 并行
if(koujian_obj != nullptr && istate_koujian == 1) {
    futures.emplace_back(std::async(std::launch::async, [&]() {
        std::vector<flawOutInfo>vtmps;
        koujian_obj->process(img, areas, vtmps, &imgname);
        vkoujian_flaws_detail0 = std::move(vtmps);
    }));
}

// koujian_obj1 (detail1) 并行
if(koujian_obj1 != nullptr && istate_koujian1 == 1) {
    futures.emplace_back(std::async(std::launch::async, [&]() {
        std::vector<flawOutInfo>vtmps;
        koujian_obj1->process(img, areas, vtmps, &imgname);
        vkoujian_flaws_detail1 = std::move(vtmps);
    }));
}

// element_objs 并行
for(int i=0; i<MAX_DETECT_NUM; i++) {
    if(element_objs[i] != nullptr && istate_elements[i] == 1) {
        futures.emplace_back(std::async(std::launch::async, [&, i]() {
            std::vector<flawOutInfo>vtmps;
            element_objs[i]->process(img, areas, vtmps, &imgname);
            velement_flaws[i] = std::move(vtmps);
        }));
    }
}

// 等待所有并行任务完成
for(auto& f : futures) {
    f.wait();
}

// 合并结果
std::vector<flawOutInfo>vkoujian_flaws;
vkoujian_flaws.insert(vkoujian_flaws.end(), vkoujian_flaws_detail0.begin(), vkoujian_flaws_detail0.end());
vkoujian_flaws.insert(vkoujian_flaws.end(), vkoujian_flaws_detail1.begin(), vkoujian_flaws_detail1.end());

if(m_xlbh_2koujian.combine_2koujian == 1)
    change_lianxu_koujian_node(img.cols,img.rows, vkoujian_flaws);

if ((int)vkoujian_flaws.size()>0)
    vflaws.insert(vflaws.end(), vkoujian_flaws.begin(), vkoujian_flaws.end());

// 合并element结果
for(int i=0; i<MAX_DETECT_NUM; i++) {
    if ((int)velement_flaws[i].size()>0)
        vflaws.insert(vflaws.end(), velement_flaws[i].begin(), velement_flaws[i].end());
}
```

- [ ] **Step 2: 编译验证（Windows）**

Run: `cd e:\0_project\proj2_20260411 && cmake --build build --config Release --target proj2`
Expected: 编译成功

- [ ] **Step 3: Commit**

```bash
git add proj2/detect.cpp
git commit -m "feat: parallelize detail and element models (stage 2)"
```

---

## 验证测试

### Task 4: 性能验证测试

**Files:**
- Test: 使用现有 perf 系统验证

- [ ] **Step 1: 准备测试数据**

确保 `e:\0_project\proj2_20260411\data` 下有测试图片

- [ ] **Step 2: 运行基准测试（并行后）**

Run: `cd e:\0_project\proj2_20260411\release && .\shell.exe`
输入数据目录，处理多张图片

- [ ] **Step 3: 收集性能日志**

Run: `ls e:\0_project\proj2_20260411\release\perf\`
查看最新 `perf_*.csv` 文件

- [ ] **Step 4: 对比基线性能**

读取最新 perf CSV，与基线对比：
```powershell
$csv = Import-Csv (Get-ChildItem perf\perf_*.csv | Sort-Object LastWriteTime | Select-Object -Last 1)
$avg = ($csv | Where-Object {$_.component -eq "detect" -and $_.stage -eq "detect_process_total"} | Measure-Object -Property duration_ms -Average).Average
Write-Host "平均 detect_process_total: $([int]$avg)ms"
Write-Host "预期: 180ms (基线321ms)"
```

- [ ] **Step 5: 验证结果正确性**

检查生成的 JSON 结果与基线是否一致：
- 缺陷数量是否相同
- 缺陷位置是否相同
- 置信度是否相同

- [ ] **Step 6: Commit 测试记录**

```bash
git add -A
git commit -m "test: verify parallel inference performance and correctness"
```

---

### Task 5: Jetson 平台验证

**Files:**
- Test: Jetson 设备上的构建和运行

- [ ] **Step 1: 复制代码到 Jetson**

```bash
# 在 Jetson 上
cd /path/to/proj2_20260411
```

- [ ] **Step 2: Jetson 编译**

Run: `cd /path/to/proj2_20260411 && cmake --build build --target proj2`
Expected: 编译成功

- [ ] **Step 3: Jetson 运行测试**

Run: `./shell_jetson`
输入测试数据路径

- [ ] **Step 4: Jetson 性能对比**

检查 `perf/` 目录下的 CSV，对比基线
Expected: detect_process_total 从 ~350ms 降至 ~200ms

---

## 完成检查

### Task 6: 阶段1完成总结

- [ ] **Step 1: 更新文档**

在 `e:\0_project\proj2_20260411\docs\superpowers\specs\2026-04-25-full-gpu-inference-design.md` 的阶段1表格中添加：
```markdown
**阶段1实施状态**: ✅ 已完成
**实测结果**: 
- Windows: 321ms → XXXms (-XX%)
- Jetson: 350ms → XXXms (-XX%)
```

- [ ] **Step 2: Final Commit**

```bash
git add docs/superpowers/specs/2026-04-25-full-gpu-inference-design.md
git commit -m "docs: update stage1 completion status"
git log --oneline -5
```

- [ ] **Step 3: 决策是否进入阶段2**

检查实测结果：
- 如果 Windows < 200ms 且 Jetson < 250ms → **继续阶段2 (GPU预处理)**
- 如果不达标 → 分析原因，可能需要检查 CUDA Stream 配置

---

## 潜在问题及对策

| 问题 | 现象 | 对策 |
|------|------|------|
| **编译错误：std::async 找不到** | error C2039 | 确认 CMakeLists.txt 中设置了 `set(CMAKE_CXX_STANDARD 11)` 或更高 |
| **运行时崩溃** | 访问冲突 | 检查 lambda 捕获的变量生命周期，确保 img, areas 等在使用期间有效 |
| **结果不一致** | 缺陷数量变化 | 检查并行执行是否改变了执行顺序导致逻辑差异（areas合并顺序） |
| **性能无提升** | 耗时不变 | 检查是否真正并行（添加日志打印线程ID），可能GPU串行化了 |
| **Jetson编译失败** | CUDA相关错误 | 检查是否需要添加 `-lpthread` 链接选项 |

---

## 后续步骤

阶段1完成后，参考设计文档进入：
- **阶段2**: GPU预处理优化 (Windows + Jetson 共同)
- **阶段3**: Jetson零拷贝优化 (仅 Jetson)
