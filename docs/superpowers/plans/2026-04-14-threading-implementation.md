---
description: project.xml 多线程参数接入文件级并发实现计划
---

# `thread_type` / `thread_num` 文件级并发实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `config/project.xml` 里的 `thread_type` / `thread_num` 真正生效，在单个目录内启用文件级并发，同时保证最终输出、日志和 checkpoint 仍按原始文件顺序收敛。

**Architecture:** 保留现有顺序处理作为 `thread_type=0` 的默认行为；当 `thread_type=1` 时，在每个目录/批次内部生成带序号的文件任务，交给固定数量的 worker 并发处理。worker 只负责计算，主线程负责按序回收结果、打印输出和更新 checkpoint，避免乱序和重复处理。自动检测逻辑继续负责找到目录边界，并发只作用于目录内部的文件处理。

**Tech Stack:** C++17、`std::thread` / `std::mutex` / `std::condition_variable`、`std::filesystem`、pugixml、现有 Windows shell、现有 Jetson shell、现有 checkpoint helpers、CMake。

---

## File Structure Lock-In

**Create:**

- （如现有代码拆分后需要）`e:/0_project/proj2_20260411/shell/threading_utils.h`

**Modify:**

- `e:/0_project/proj2_20260411/config/project.xml`
- `e:/0_project/proj2_20260411/shell/shell.cpp`
- `e:/0_project/proj2_20260411/shell/shell_jetson.cpp`

**Avoid modifying unless a build or runtime failure forces it:**

- `e:/0_project/proj2_20260411/proj2/detect.cpp`
- `e:/0_project/proj2_20260411/proj2/myxml.h`
- `e:/0_project/proj2_20260411/CMakeLists.txt`
- `e:/0_project/proj2_20260411/shell/auto_detect.h`

## Execution Strategy

先把配置读进来，再接并发执行，最后验证顺序和 checkpoint：

1. 让 `project.xml` 中的线程参数描述和默认值与设计一致
2. 在 Windows / Jetson 两端读取 `thread_type` 与 `thread_num`
3. 为 `thread_type=1` 增加文件级任务队列、worker 池和有序回收
4. 保留 `thread_type=0` 的旧顺序处理路径不变
5. 在真实目录树上验证输出顺序、checkpoint 和自动检测都不被破坏

### Task 1: 对齐配置语义并读取线程参数

**Files:**

- Modify: `e:/0_project/proj2_20260411/config/project.xml`
- Modify: `e:/0_project/proj2_20260411/shell/shell.cpp`
- Modify: `e:/0_project/proj2_20260411/shell/shell_jetson.cpp`

- [ ] **Step 1: 更新 XML 注释和默认值**

把 `thread_type` 和 `thread_num` 的注释改成最终语义：

- `thread_type=0`：保持现有顺序处理
- `thread_type=1`：启用文件级并发
- `thread_num`：并发 worker 数量

必要时把默认值保守地保持为 `thread_type=0`、`thread_num=1`，避免旧配置行为变化。

- [ ] **Step 2: 为 Windows / Jetson 各自增加解析字段**

扩展现有 XML 解析结构，让两个 shell 都能读到：

- `thread_type`
- `thread_num`
- 现有的 `imgtype`
- 现有的 `path`
- 现有的 `auto_detect`

确保缺省或缺失时能回退到单线程顺序模式。

- [ ] **Step 3: 做一次编译级检查**

确认新增字段不会破坏原来的 XML 读取分支，也不会影响 `thread_type=0` 的旧配置。

- [ ] **Step 4: 记录配置兼容性**

确认旧配置在没有 `thread_type` / `thread_num` 或值非法时，仍然能正常进入顺序处理。

### Task 2: 建立文件级任务分发和有序回收骨架

**Files:**

- Modify: `e:/0_project/proj2_20260411/shell/shell.cpp`
- Modify: `e:/0_project/proj2_20260411/shell/shell_jetson.cpp`
- Create: `e:/0_project/proj2_20260411/shell/threading_utils.h`（仅在抽取共用并发逻辑时使用）

- [ ] **Step 1: 写出任务和结果的最小数据结构**

定义最小数据结构来表示：

- 排序序号 `seq`
- 文件路径
- 处理结果状态（成功/失败）
- 需要回收的输出内容

如果公共逻辑容易复用，再抽成轻量头文件；如果不需要，就保留在各自 shell 内部。

- [ ] **Step 2: 先让线程池接口编译通过**

实现一个固定 worker 数量的任务队列接口，但先不切换主流程，只让它能被编译和单测/烟测引用。

- [ ] **Step 3: 写有序回收器**

实现一个按 `seq` 收集乱序结果、按原始顺序 flush 的缓冲层。

要求：

- 结果可以先乱序到达
- 主线程只在 `next_expected_seq` 到达时输出
- checkpoint 只在 flush 后更新

- [ ] **Step 4: 验证顺序策略**

用小规模文件序列确认：

- worker 完成顺序可以和输入顺序不同
- 最终输出顺序仍然与输入一致

### Task 3: 在 `thread_type=1` 下接入 Windows 端文件级并发

**Files:**

- Modify: `e:/0_project/proj2_20260411/shell/shell.cpp`

- [ ] **Step 1: 先保留旧逻辑为回退分支**

把现有顺序处理路径原样保留，作为 `thread_type=0` 和并发降级时的回退入口。

- [ ] **Step 2: 按目录生成有序任务列表**

对单个 `path` 内的待处理文件先排序、再编号，保证并发前的输入顺序稳定。

- [ ] **Step 3: 接入 worker 池**

当 `thread_type=1` 时：

- 按 `thread_num` 创建 worker
- 将文件任务投递到队列
- worker 只负责调用现有处理逻辑，不直接写 checkpoint

- [ ] **Step 4: 接入有序输出与 checkpoint**

主线程按序 flush 每个文件的结果：

- 成功则输出并写 checkpoint
- 失败则输出失败结果，但不写完成标记
- 保证最后落盘顺序与输入顺序一致

- [ ] **Step 5: 验证 Windows 一次性和自动检测路径**

确认：

- `thread_type=0` 仍保持原有行为
- `thread_type=1` 时单目录并发可运行
- 自动检测场景下，每个批次目录内部的文件仍然有序输出

### Task 4: 在 Jetson 端接入相同的文件级并发语义

**Files:**

- Modify: `e:/0_project/proj2_20260411/shell/shell_jetson.cpp`

- [ ] **Step 1: 复用同一套线程参数语义**

让 Jetson 端读取同样的 `thread_type` / `thread_num` 含义，避免两端配置语义分叉。

- [ ] **Step 2: 只在目录内部启用并发**

保持 Jetson 现有的加载库、自动检测和 checkpoint 路径处理不变，仅在 `thread_type=1` 时改成文件级并发。

- [ ] **Step 3: 保证 checkpoint 与有序输出一致**

确认 Jetson 端也是在主线程按序 flush 后再更新 checkpoint，避免恢复时出现乱序缺口。

- [ ] **Step 4: 做 Linux 烟测**

用现有 Jetson 运行目录或等价测试目录验证：

- 仍能加载库
- 仍能读到配置
- 并发结果顺序不乱
- checkpoint 可恢复

### Task 5: 用真实目录树做端到端验证并收口

**Files:**

- Test only: `shell/shell.cpp` runtime
- Test only: `shell/shell_jetson.cpp` runtime
- Test only: `config/project.xml`

- [ ] **Step 1: 准备一个小型测试目录树**

准备几个文件、多个同目录样本，确保：

- 输入顺序固定
- worker 完成顺序可随机
- 最终输出必须按输入顺序

- [ ] **Step 2: 验证失败文件处理**

人为制造一个失败文件，确认：

- 失败被记录
- 后续文件继续处理
- 失败文件不会进入 checkpoint

- [ ] **Step 3: 验证 `thread_num` 边界值**

测试：

- `thread_num=1`
- `thread_num=0`
- `thread_num` 大于文件数

确认都能安全退化或按预期工作。

- [ ] **Step 4: 验证自动检测 + 并发的组合行为**

确认自动检测找到的批次目录仍按序处理，且并发只发生在批次目录内部。

- [ ] **Step 5: 收尾提交**

把配置、Windows 端、Jetson 端的修改一起提交，保留一条清晰提交信息。

## Self-Review

- `thread_type=0` 的旧行为是否完全保留
- `thread_type=1` 是否只作用于文件级并发
- 输出 / 日志 / checkpoint 是否都在主线程按序回收
- 自动检测和 checkpoint 语义是否被破坏
- `thread_num` 非法值是否安全回退
