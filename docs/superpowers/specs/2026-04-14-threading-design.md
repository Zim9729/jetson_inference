---
description: project.xml 多线程参数接入文件级并发设计
---

# `thread_type` / `thread_num` 接入设计

## 背景

当前 `config/project.xml` 里已经有这两个参数：

```xml
<thread_type thread_type="1"/>
<thread_num thread_num="1"/>
```

但现有代码只读取了 `imgtype`、`path` 和 `auto_detect`，`thread_type` / `thread_num` 还没有真正接入运行逻辑。

本设计的目标是把这两个参数接成**文件级并发**，同时保证**最终输出顺序不乱**，并且不破坏现有的顺序处理、自动检测和 checkpoint 逻辑。

## 目标

- `thread_type=0` 时保持当前行为：按 `path` 顺序处理
- `thread_type=1` 时启用**同一目录内的文件级并发**
- 并发数量由 `thread_num` 控制
- 最终输出、日志回收和 checkpoint 更新都按原始文件顺序执行
- 失败文件不写入完成 checkpoint，但也不能阻塞后续文件的有序回收

## 非目标

- 不做跨目录的乱序并发
- 不改自动检测的目录发现规则
- 不引入进程池，优先用线程级并发
- 不改变 `jpg/json` 处理语义

## 语义定义

### `thread_type`

- `0`
  - 保持现有顺序处理逻辑
  - 每个 `path` 仍按原来的方式顺序执行
- `1`
  - 对单个 `path` 或单个自动检测批次目录内部的文件进行并发处理
  - 并发发生在“文件级”，不是“path 级”

### `thread_num`

- 仅当 `thread_type=1` 时生效
- 表示 worker 数量
- `thread_num <= 0` 时回退到 `1`
- `thread_num=1` 时仍走并发框架，但等价于单 worker

## 设计原则

1. **内部可以并发，外部必须有序**
   - worker 负责计算
   - 主线程负责按序输出和 checkpoint
2. **checkpoint 只在最终输出成功后写入**
   - 避免“结果还没真正落盘，但文件已经被标记完成”的问题
3. **错误不扩散**
   - 某个文件失败，只影响该文件
   - 不中断整批处理

## 总体流程

### `thread_type=0`

保持当前流程：

1. 读取 `project.xml`
2. 按 `path` 顺序处理
3. 对每个文件直接调用现有处理逻辑
4. 处理成功后写 checkpoint

### `thread_type=1`

对每个输入单元采用以下流程：

1. 扫描一个 `path` 或一个自动检测批次目录，得到确定顺序的文件列表
2. 读取该单元对应的 checkpoint，跳过已完成文件
3. 给待处理文件按扫描顺序编号
4. 将任务投递到固定数量的 worker
5. worker 只负责单文件处理，不直接写最终输出
6. 主线程按编号顺序回收结果，统一打印/写文件/更新 checkpoint

## 结果回收策略

为了保证顺序，结果必须经过“有序回收器”。

### 规则

- 每个任务都有一个单调递增的序号 `seq`
- worker 完成后只提交 `{seq, status, payload}`
- 主线程维护 `next_expected_seq`
- 只有当 `seq == next_expected_seq` 时才允许最终输出
- 如果更早的结果还没到，后面的结果先缓存

### 失败处理

- 如果某个文件处理失败：
  - 仍按顺序把失败信息输出
  - 该文件**不写完成 checkpoint**
  - 主线程继续推进到下一个序号
- 如果 worker 抛错或异常退出：
  - 统一转成失败结果
  - 不中断整批任务

## checkpoint 策略

### 现状保留

现有 checkpoint 文件命名方式保留：

```text
.proj2_checkpoint_<FNV1a64>.txt
```

### 新约束

- checkpoint 写入必须由主线程或单一序列化路径负责
- 不能让多个 worker 同时写同一个 checkpoint
- 只有在该序号的最终输出成功后，才把该文件加入 checkpoint

### 恢复行为

- 已成功回收并写入 checkpoint 的文件，下次启动自动跳过
- 失败文件不会进入 checkpoint，下次仍会重试

## 与自动检测的关系

- 自动检测仍然负责找到当天批次目录
- `thread_type=1` 只影响批次目录内部的文件处理方式
- 因为输出必须有序，所以不建议跨批次目录合并并发
- 推荐的单位边界是：
  - **自动检测找到一个批次目录**
  - **在这个批次目录内部做文件级并发**

## 配置兼容性

### 需要更新的地方

- `config/project.xml` 中 `thread_type` 注释要改成新的真实含义
- `thread_num` 注释要明确它是 `thread_type=1` 时的 worker 数量

### 推荐默认值

- `thread_type=0`
  - 默认保持旧行为
- `thread_num=1`
  - 默认单 worker，避免误配造成负担

## 实现边界

建议把并发逻辑拆成三个小边界：

### 1. 配置解析

读取：

- `thread_type`
- `thread_num`
- `imgtype`
- `path`
- `auto_detect`

### 2. 任务调度

负责：

- 扫描文件
- 编号
- 丢给 worker

### 3. 有序回收

负责：

- 缓存乱序完成的结果
- 顺序输出
- 顺序写 checkpoint

这样可以避免把并发逻辑塞进单个大函数里，后续也更容易维护。

## 失败与降级

- `thread_num <= 0`：自动降级为 `1`
- 任务队列为空：直接结束，不报错
- 某个文件处理失败：记录失败并继续
- 并发执行异常：降级到单线程顺序模式，保证程序还能跑

## 测试建议

### 基础测试

- `thread_type=0` 时行为应与现有版本一致
- `thread_type=1, thread_num=1` 时结果应与顺序模式一致

### 并发测试

- 目录中准备多个文件，确认处理结果和输出顺序一致
- 故意让某个文件失败，确认失败不会阻塞后续文件
- 检查 checkpoint 是否只记录成功完成的文件

### 自动检测测试

- 开启 `auto_detect` 后，确认每个批次目录仍按顺序回收结果
- 确认并发不影响当天批次目录的发现逻辑

## 验收标准

- `thread_type=0`：完全保留当前顺序处理行为
- `thread_type=1`：同一目录内文件并发处理生效
- 最终输出顺序不乱
- checkpoint 只标记真正成功完成的文件
- 自动检测和已有的 `jpg/json` 逻辑不受影响
