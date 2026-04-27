# 数据引擎模块深度解析（DataEngine 数据流与架构详解）

> 适用项目：ChemDCS / MYDSCProject
> 核心模块：DataEngine、DataParseThread、DoubleBuffer、LockFreeRingBuffer、TagConfigMgr
> 设计目标：高吞吐、不丢帧、无锁读取、频率解耦

---

## 一、总体架构：五级单向流水线

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       五级数据流水线（Data Pipeline）                     │
│                                                                         │
│  [硬件]                                                                  │
│    │  RS485 / TCP                                                       │
│    ▼                                                                     │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │ L1: ModbusManager / ModbusComm（通信交互层）                       │    │
│  │  - 多设备并发轮询                                                   │    │
│  │  - 原始字节收发                                                     │    │
│  │  - 自动重连 + 心跳                                                  │    │
│  └──────────────────────────┬───────────────────────────────────────┘    │
│                             │ enqueue (MPSC 无锁队列)                    │
│                             ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐    │
│  │ L2: DataParseThread（协议解析与实时处理层）                        │    │
│  │  - 寄存器→工程值转换                                              │    │
│  │  - 报警判断（死区处理）                                           │    │
│  │  - 写入 DoubleBuffer                                              │    │
│  └──────────┬──────────┬───────────┬────────────────────────────────┘    │
│             │          │           │                                    │
│      commit()│          │           │                                    │
│             ▼          │           │                                    │
│  ┌──────────────┐      │           │                                    │
│  │ DoubleBuffer │      │           │                                    │
│  │ (RCU无锁)    │      │           │                                    │
│  └──────┬───────┘      │           │                                    │
│         │              │           │                                    │
│         ▼              ▼           ▼                                    │
│  ┌──────────┐  ┌────────────┐  ┌──────────────────┐                    │
│  │ L5: UI   │  │ L3: 持久化 │  │ L4: 云端同步     │                    │
│  │ 定时读取 │  │ MySQL归档  │  │ MQTT/OPC UA      │                    │
│  │ 30fps    │  │ 5min/批    │  │ 聚合上传         │                    │
│  └──────────┘  └────────────┘  └──────────────────┘                    │
└─────────────────────────────────────────────────────────────────────────┘
```

### 层级职责

| 层级   | 名称                   | 线程    | 频率           | 职责        |
| ---- | -------------------- | ----- | ------------ | --------- |
| L1   | ModbusManager        | 通信线程池 | 100~1000ms   | 硬件字节收发    |
| L2   | DataParseThread      | 独立线程  | 10~50ms      | 解析 + 报警判断 |
| L2.5 | DoubleBuffer         | 无锁共享  | commit: 50ms | 读写解耦      |
| L3   | HistoryArchiveThread | 独立线程  | 5min         | 历史归档      |
| L5   | UI 层                 | 主线程   | 100~200ms    | 渲染显示      |

---

## 二、核心数据流详解

### 2.1 启动时序

```
DataEngine::initialize()
    │
    ├─ 1. new ModbusManager(this)
    │      └─ setRingBuffer(&m_ringBuffer)
    │
    ├─ 2. new DataParseThread(this)
    │      ├─ setDoubleBuffer(&m_doubleBuffer)
    │      └─ setRingBuffer(&m_ringBuffer)
    │
    ├─ 3. connect(modbusManager, deviceStatusChanged → 转发)
    │
    └─ 4. TagConfigMgr::instance() 标记已初始化

DataEngine::start()
    ├─ m_dataParseThread->start()     // 启动解析线程
    └─ m_modbusManager->startAll()    // 启动所有 Modbus 设备
```

### 2.2 完整数据链路（一条数据的生命周期）

```
Step 1: Modbus 通信层（L1）
────────────────────────────────────────────────────────
ModbusComm 轮询线程发送请求帧 → 接收响应帧
→ 解析寄存器值 → 打包成 RawModbusData
→ m_ringBuffer.enqueue(rawData)

struct RawModbusData {
    int serverAddress;      // 从站地址
    int startAddress;       // 起始寄存器地址
    int valueCount;         // 寄存器数量
    quint16 values[128];    // 原始寄存器值
    qint64 timestamp;       // ← 关键：L1 打时间戳
    int deviceId;           // 设备ID
};

Step 2: 数据解析（L2，DataParseThread）
────────────────────────────────────────────────────────
// DataParseThread::run() 主循环
while (m_running) {
    // 从无锁队列批量取出
    m_ringBuffer->dequeueBatch(batch, 256);
    if (!batch.empty()) {
        processBatch(batch);      // 解析+报警判断
    }
    // 定期交换双缓冲区
    if (时间达到 swap 间隔) {
        m_doubleBuffer->commit(); // 原子发布新数据
        emit dataUpdated();       // 通知 UI
    }
    QThread::msleep(1或20);       // 无数据时多睡一会
}

Step 3: 寄存器→工程值转换
────────────────────────────────────────────────────────
// RawModbusData.values[0] = 0x8000 (原始值)
// TagInfo: engHigh=100, engLow=0
// 公式: 值 = engLow + (rawValue / 65535) * (engHigh - engLow)
// 0x8000 = 32768 → 0 + (32768/65535) * 100 = 50.0

float registerToValue(quint16 rawValue, const TagInfo& tag) {
    float range = tag.engHigh - tag.engLow;
    return tag.engLow + (rawValue / 65535.0f) * range;
}

Step 4: 变化率校验 + 报警判断 + 写入 DoubleBuffer
────────────────────────────────────────────────────────
// validateRateOfChange → 防跳变，标记 Uncertain
DataQuality quality = DataQuality::Good;
if (!validateRateOfChange(tag.tagId, pv, tag, raw.timestamp, quality)) {
    m_totalJumpDetected.fetchAndAddRelaxed(1);
}

// 构造快照并写入 DoubleBuffer
DoubleBuffer::TagSnapshot snapshot;
snapshot.tagId = tag.tagId;
snapshot.currentValue = pv;
snapshot.alarmstate = tag.alarmLimit;
snapshot.quality = quality;               // ← 经校验的质量码
snapshot.timestamp = raw.timestamp;
m_doubleBuffer->write(tag.tagId, snapshot);

// 报警判断（传入完整 TagInfo，避免重复查找）
checkAlarmOptimized(tag, pv);

Step 5: 双缓冲区提交（原子发布 + 对象池复用）
────────────────────────────────────────────────────────
m_doubleBuffer->commit();
// 内部实现：
// 1. map = acquireFromPool()  — 从对象池借用（无锁 CAS）
// 2. *map = move(m_writeBuffer)  — 移动数据
// 3. newSnap = shared_ptr<const Map>(map, custom_deleter)
// 4. atomic_store(&m_readOnlySnapshot, newSnap)  — 原子发布
// 5. m_writeBuffer.clear(); reserve(512)
// 6. UI 释放快照时，自定义 deleter 将 map 归还对象池

Step 6: UI 读取（L5，主线程）
────────────────────────────────────────────────────────
// MYDSCProject 的 QTimer (33ms = 30fps)
void onTimer() {
    auto snap = m_doubleBuffer->readAll();
    // 遍历 snap 刷新图元
    for (const auto& [tagId, tag] : *snap) {
        updateUI(tagId, tag);
    }
}
```

---

## 三、LockFreeRingBuffer 详解（MPSC 无锁队列）

### 3.1 为什么需要它？

**Modbus 多设备场景**：假设有 5 个 Modbus 设备，每个 100ms 轮询一次。数据采集在 Modbus 线程池中，解析在独立线程中。如果用 `QQueue` + `QMutex`，高频率下锁竞争严重。无锁队列解决了这个问题。

### 3.2 核心原理

```cpp
template<typename T, size_t Capacity = 8192>
class LockFreeRingBuffer {
    // Capacity 必须是 2 的幂（用位运算替代取模）
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity 必须是2的幂");
```

**序列号方案**（而非 ABA-prone 的指针方案）：

```
初始状态:
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │   │   │   │   │   │   │   │   │
  └───┴───┴───┴───┴───┴───┴───┴───┘
  seq:0  1   2   3   4   5   6   7       (sequence 初始 = index)
  enq:0                                (enqueuePos)
  deq:0                                (dequeuePos)

入队一个元素后:
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │ A │   │   │   │   │   │   │   │
  └───┴───┴───┴───┴───┴───┴───┴───┘
  seq:1  1   2   3   4   5   6   7
  enq:1
  deq:0

出队一个元素后:
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │ A │   │   │   │   │   │   │   │
  └───┴───┴───┴───┴───┴───┴───┴───┘
  seq:9  1   2   3   4   5   6   7    (seq = pos + 1 + mask)
  enq:1
  deq:1
```

**关键技术点**：

```cpp
// 入队判定：sequence == enqueuePos 说明该槽可用
intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
if (diff == 0) {
    slot.data = data;                                // 写入数据
    slot.sequence.store(pos + 1, memory_order_release); // 更新序列号
    m_enqueuePos.store(pos + 1, memory_order_relaxed);
}

// 出队判定：sequence == dequeuePos 说明该槽数据已就绪
// 出队后 sequence = pos + 1 + mask(即 Capacity)
// 这样当 enqueuePos 绕回来时，seq 仍然比 enqueuePos 小 1，diff < 0 表示队列满
```

### 3.3 MPSC 设计

```cpp
// 多生产者（M）：每个 Modbus 设备可能在不同线程中写入
// 用 mutex 保护 enqueue 操作（多生产者互斥）
alignas(64) std::mutex m_enqueueMutex;  // 缓存行对齐，避免 false sharing

// 单消费者（SC）：只有 DataParseThread 一个消费者，dequeue 无需锁
// 这是性能关键点——消费端无竞争
```

### 3.4 背压机制

```
队列满时 → enqueue 返回 false
         → ModbusComm 丢弃当前帧
         → 保证 L1 通信线程永不阻塞
         → 最旧数据被覆盖（通过序列号方案，不会真的覆盖，只是入队失败）
         → L2 处理不过来时自然丢帧，不会导致连锁崩溃
```

### 3.5 性能数据

| 操作                | 延迟    | 说明               |
| ----------------- | ----- | ---------------- |
| enqueue           | ~50ns | 多生产者有锁（mutex 竞争） |
| dequeue           | ~20ns | 单消费者完全无锁         |
| dequeueBatch(256) | ~5μs  | 批量出队摊还开销         |

---

## 四、DoubleBuffer 详解（RCU 无锁双缓冲区）

### 4.1 设计演进

```
v1: 传统双缓冲（两个 map + 原子索引）
    问题：交换索引不是原子的，存在"读写同一块内存"的瞬态窗口
    → 偶现 Core Dump，几个月才重现一次，极难排查

v2: QReadWriteLock
    问题：UI 读操作也要加读锁，虽然不互斥但仍有开销
    写操作时阻塞所有读操作 → UI 卡顿感知

v3 (当前): RCU 方案（shared_ptr 原子发布） ✅
    写线程完全自由，读线程零等待
    引用计数保证内存安全
```

### 4.2 核心实现

```cpp
class DoubleBuffer {
public:
    struct TagSnapshot {
        quint32 tagId;
        float currentValue = 0.0f;
        float setPoint = 0.0f;
        float outputValue = 0.0f;
        AlarmLimit alarmstate = AlarmLimit::Normal;
        DataQuality quality = DataQuality::Good;
        qint64 timestamp = 0;
    };

    using SnapshotMap = std::unordered_map<quint32, TagSnapshot>;
    using ImmutableSnapshot = std::shared_ptr<const SnapshotMap>;

    // 写（DataParseThread 调用，完全无锁）
    void write(quint32 tagId, const TagSnapshot& snapshot) {
        m_writeBuffer[tagId] = snapshot;  // 直接操作本地 map
    }

    // 原子提交（DataParseThread 调用，对象池复用）
    void commit() {
        SnapshotMap* map = acquireFromPool();     // 1. 从池中借用
        *map = std::move(m_writeBuffer);          // 2. 移动数据
        auto deleter = [this](const SnapshotMap* m) {
            const_cast<SnapshotMap*>(m)->clear();
            returnToPool(const_cast<SnapshotMap*>(m));  // 归还对象池
        };
        ImmutableSnapshot newSnap(map, deleter);
        std::atomic_store(&m_readOnlySnapshot, std::move(newSnap));  // 3. 原子发布
        m_writeBuffer.clear();
        m_writeBuffer.reserve(512);
    }

    // 4 槽位 CAS 无锁对象池
    SnapshotMap* acquireFromPool() {
        for (int i = 0; i < POOL_SIZE; ++i) {
            SnapshotMap* expected = m_pool[i].load(std::memory_order_relaxed);
            if (expected && m_pool[i].compare_exchange_weak(expected, nullptr,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return expected;
            }
        }
        return new SnapshotMap();  // 池空时降级创建
    }

    // 读（UI 线程调用，无锁）
    ImmutableSnapshot readAll() const {
        return std::atomic_load(&m_readOnlySnapshot);  // 获取当前快照
        // 拿到 shared_ptr 后，即使后台 commit() 也不影响这块内存
        // 引用计数保护，用完自动释放
    }
};
```

### 4.3 快照生命周期

```
时间线:

DataParseThread           UI 线程
    │                       │
    ├─ write(tag1, 50.0)   │
    ├─ write(tag2, 30.0)   │
    ├─ commit() ──────────▶│─ readAll() → 获取快照1
    │  ╰→ 发布快照1        │   ╰→ 开始遍历渲染
    │                       │   (遍历中...)
    ├─ write(tag1, 51.0)   │
    ├─ write(tag2, 29.5)   │
    ├─ commit() ──────────▶│   (仍然在遍历快照1，不受影响)
    │  ╰→ 发布快照2        │   
    │                       ├─ 遍历完成，快照1引用归零自动释放
    │                       ├─ readAll() → 获取快照2
    │                       │   ╰→ 展示最新数据
```

### 4.4 为什么是 `shared_ptr<const Map>` 而不是 `shared_ptr<Map>`？

```cpp
using ImmutableSnapshot = std::shared_ptr<const SnapshotMap>;
//                               ^^^^^
// 关键：const 保证 UI 线程只能读，不能写
// 即使 UI 线程持有了快照，也不能修改它
// 修改必须在 m_writeBuffer 上进行，由 DataParseThread 单线程控制
```

### 4.5 性能对比

| 方案                      | 写延迟             | 读延迟       | 安全性      | 内存开销        |
| ----------------------- | --------------- | --------- | -------- | ----------- |
| QMutex                  | ~100ns          | ~100ns    | 安全       | 低           |
| QReadWriteLock          | ~200ns          | ~30ns     | 安全       | 低           |
| **DoubleBuffer(RCU+池)** | **~0.5μs**(池复用) | **<10ns** | **绝对安全** | **中等(COW)** |

---

## 五、DataParseThread 详解

### 5.1 线程主循环

```cpp
void DataParseThread::run()
{
    m_running.storeRelaxed(1);
    qint64 lastSwapTime = 0;
    std::vector<RawModbusData> batch;
    batch.reserve(256);

    while (m_running) {
        batch.clear();
        // 从无锁队列批量取数据（最多 256 条）
        if (m_ringBuffer)
            m_ringBuffer->dequeueBatch(batch, 256);

        if (!batch.empty()) {
            processBatch(batch);  // 解析 + 报警判断
            m_totalProcessed.fetchAndAddRelaxed(batch.size());
        }

        // 定期 swap 双缓冲区
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastSwapTime >= m_swapInterval) {
            m_doubleBuffer->commit();
            emit dataUpdated();    // 通知 UI 刷新
            lastSwapTime = now;
        }

        // 智能休眠：有数据时只睡 1ms，无数据时睡 20ms
        batch.empty() ? QThread::msleep(m_processInterval)
                      : QThread::msleep(1);
    }
}
```

**设计要点**：

- 批量处理（最多 256 条/次）比逐条处理效率高 10 倍以上
- swap 频率 = 50ms（20fps 数据更新率）
- 智能休眠：有数据时忙等（1ms），无数据时省电（20ms）

### 5.2 报警判断（含死区）

```cpp
void DataParseThread::checkAlarmOptimized(const TagInfo& tag, float value)
{
    // Step 1: 判断当前超限等级
    AlarmLimit newLimit = AlarmLimit::Normal;
    if (value >= tag.highHighLimit)      newLimit = AlarmLimit::HighHigh;
    else if (value >= tag.highLimit)     newLimit = AlarmLimit::High;
    else if (value <= tag.lowLowLimit)   newLimit = AlarmLimit::LowLow;
    else if (value <= tag.lowLimit)      newLimit = AlarmLimit::Low;

    AlarmLimit oldLimit = tag.alarmLimit;

    // Step 2: 死区滞环处理（防抖动）
    if (oldLimit != AlarmLimit::Normal && newLimit == AlarmLimit::Normal) {
        bool canClear = false;
        if (oldLimit == AlarmLimit::High || oldLimit == AlarmLimit::HighHigh)
            canClear = (value < tag.highLimit - tag.deadband);
        else if (oldLimit == AlarmLimit::Low || oldLimit == AlarmLimit::LowLow)
            canClear = (value > tag.lowLimit + tag.deadband);

        if (!canClear) return;  // 未离开死区，维持报警
    }

    // Step 3: 状态变化时通知 AlarmEngine
    if (newLimit != oldLimit) {
        // 更新 DoubleBuffer 中的报警状态
        m_doubleBuffer->write(tagId, snapshot);
        // 调用 ISA-18.2 报警引擎
        if (newLimit != AlarmLimit::Normal)
            AlarmEngine::instance().triggerAlarm(...);
        else
            AlarmEngine::instance().clearAlarm(tagId, value);
    }
}
```

### 5.3 索引设计

```cpp
// 寄存器地址 → TagInfo 映射（O(1) 查找）
// key = (serverAddr << 32) | regAddr
QHash<quint64, TagInfo> m_tagByRegAddr;

// 使用示例（processBatch 中）：
quint64 key = (raw.serverAddress << 32) | (regAddr);
auto it = m_tagByRegAddr.find(key);
if (it == m_tagByRegAddr.end()) continue;  // 未配置的寄存器，跳过
const TagInfo& tag = it.value();
```

**为什么用 `(serverAddr << 32) | regAddr` 做 key？**

- 一次性定位到 tag，不需要先找 device 再找 register
- O(1) 查找，满足 10ms 处理周期的性能要求
- 64-bit key 在 QHash 中效率高，且不会碰撞（modbus server: 1-247, reg: 0-65535，无重叠）

### 5.4 变化率校验（Rate-of-Change）

工业现场电磁干扰可能导致寄存器值瞬时跳变（如温度从 50℃ 突变为 65000℃）。DataParseThread 内置了变化率校验机制，在 `processBatch` 中将工程值通过 `validateRateOfChange()` 进行校验，检测异常跳变并标记质量码为 Uncertain。

```cpp
bool DataParseThread::validateRateOfChange(quint32 tagId, float value,
                                          const TagInfo& tag,
                                          qint64 timestamp, DataQuality& outQuality)
{
    auto& state = m_rocState[tagId];

    // 首次采样，记录基准值
    if (state.lastTimestamp == 0) {
        state.lastValidValue = value;
        state.lastTimestamp = timestamp;
        return true;
    }

    float delta = fabs(value - state.lastValidValue);
    float elapsedSec = static_cast<float>(timestamp - state.lastTimestamp) / 1000.0f;
    if (elapsedSec <= 0.0f) return true;

    // 最大允许变化率：满量程的 50%/秒
    float maxRoc = (tag.engHigh - tag.engLow) * 0.5f;
    if (maxRoc <= 0.0f) maxRoc = 50.0f;
    float actualRoc = delta / elapsedSec;

    if (actualRoc > maxRoc) {
        outQuality = DataQuality::Uncertain;  // 标记为不确定
        LOG_WARN("DataParseThread", QString("异常跳变检测: ..."));
        return false;
    }

    state.lastValidValue = value;
    state.lastTimestamp = timestamp;
    return true;
}
```

**校验逻辑**：相邻两次采样值的变化率 > 满量程 50%/秒 → 标记为 `DataQuality::Uncertain`。跳变计数通过 `m_totalJumpDetected` 原子计数器统计，线程退出时输出到日志。注意跳变不会丢弃数据，而是降级质量码——操作员仍可见该值，但 UI 可据此显示警告色（如黄色闪烁）。

---

## 六、DataEngine（调度中枢）

### 6.1 职责边界

```
DataEngine 的职责：
✅ 初始化所有子组件（ModbusManager、DataParseThread、DoubleBuffer）
✅ 加载位号配置（JSON → TagInfo → 分发到各组件）
✅ 启动/停止整个数据管道
✅ 操作员下发的转发（setSetPoint / setOutput / setAutoMode）
✅ 权限检查（操作前验证用户权限）

DataEngine 不做的：
❌ 不参与实时数据处理（全在 DataParseThread 中）
❌ 不直接操作 Modbus 通信（全在 ModbusManager 中）
❌ 不存储实时数据（全在 DoubleBuffer 中）
```

### 6.2 配置加载

```cpp
bool DataEngine::loadTagConfig(const QString& jsonPath)
{
    // 1. 解析 JSON
    // 2. 字段验证（tagId/tagName 必填）
    // 3. 重复检查
    // 4. 量程合理性验证（engHigh <= engLow 自动修正）
    // 5. 报警限值验证（限值超出量程自动截断）
    // 6. 报警限值顺序验证（HH >= H >= L >= LL）
    // 7. Modbus 地址验证（范围 1-247 / 0-65535 / 1-125）
    // 8. modbusDeviceId 解析（关联 ModbusManager::addDevice 分配的 deviceId）
    // 9. PID 参数验证（不能为负数）
    // 10. 分发到 TagConfigMgr + DataParseThread
}
```

**面试考点****——量程合理性验证**：

```cpp
// 量程反了自动修正，而不是报错退出
if (tag.engHigh <= tag.engLow) {
    float temp = tag.engHigh;
    tag.engHigh = tag.engLow + 100.0f;
    tag.engLow = temp;
}
```

工业现场常见 scenario：现场仪表工程师配置时把 engHigh 和 engLow 填反了。直接报错→操作员无法开工；自动修正→保证系统可用，同时在日志中告警。

### 6.3 操作员下发流程

```cpp
void DataEngine::setSetPoint(quint32 tagId, float sp)
{
    // 1. 权限检查（操作员及以上）
    if (!AuthManager::instance().canOperate()) return;

    // 2. 二次确认（ISA-101 关键操作确认）
    if (!AuthManager::instance().confirmCriticalAction("SET_SP",
            QString("位号 %1 设定值 → %2").arg(tag.tagName).arg(sp)))
        return;

    // 3. 限值保护（sp 必须在量程范围内）
    sp = qBound(tag.engLow, sp, tag.engHigh);

    // 4. 更新 DoubleBuffer（直接写入）
    auto snap = m_doubleBuffer.readTag(tagId);
    snap.setPoint = sp;
    m_doubleBuffer.write(tagId, snap);

    // 5. 下发到硬件（工程值→寄存器值转换）
    float range = tag.engHigh - tag.engLow;
    float normalized = (sp - tag.engLow) / range;
    quint16 regValue = static_cast<quint16>(normalized * 65535.0f);
    m_modbusManager->writeRegister(tag.modbusDeviceId,
        tag.modbusServerAddr, tag.modbusRegAddr + 1, regValue);

    // 6. 操作审计日志（持久化到 operation_log 表）
    AuthManager::instance().logAction("SET_SP",
        QString("tagId=%1, %2: SP=%3 %4")
            .arg(tagId).arg(tag.tagName).arg(sp).arg(tag.unit));
}
```

---

## 七、数据存储分层：DoubleBuffer + TagConfigMgr

### 7.1 无桥接层的直接架构

RealtimeDb 桥接层已移除。当前架构中，两者各司其职：

```
┌────────────────────────────────────────────┐
│ 高频路径：DoubleBuffer（RCU 无锁）          │
│ 存储：TagSnapshot（currentValue/SP/OUT/    │
│       alarm/quality/timestamp）            │
│ 读取：atomic_load<shared_ptr>，< 10ns      │
│ 写入：DataParseThread 直接写写缓冲区        │
│ 使用者：UI 图元刷新、操作员下发、趋势采样    │
├────────────────────────────────────────────┤
│ 静态路径：TagConfigMgr（单例）              │
│ 存储：完整 TagInfo（量程/单位/报警限值/    │
│       Modbus映射/PID参数/Rationalization） │
│ 保护：QReadWriteLock                       │
│ 使用者：配置对话框、报警引擎、操作员下发     │
└────────────────────────────────────────────┘
```

**为什么分开？** DoubleBuffer 存储高频变化的实时值（每秒数百次写入），TagConfigMgr 存储低频访问的静态配置（仅在配置变更时修改）。读写模式完全不同，合并存储会相互拖累。

### 7.2 数据更新路径

```
DataParseThread::processBatch()
    │
    ├─ 1. 寄存器→工程值转换 (registerToValue)
    ├─ 2. 变化率校验 (validateRateOfChange)
    ├─ 3. 构造 TagSnapshot → m_doubleBuffer.write(tagId, snapshot)
    ├─ 4. 报警判断 (checkAlarmOptimized)
    │
    └─ 达到 swap 间隔 → m_doubleBuffer.commit() → emit dataUpdated()
                                                        │
                                                        ▼
                                              MYDSCProject::onDataUpdated()
                                                        │
                                              readAll() → 遍历快照
                                              → 调用各图元 onTagValueChanged()
```

---

## 八、HistoryArchiveThread（历史归档）

### 8.1 采样与归档策略

```
采样频率：1 秒一次（从 DoubleBuffer 读取快照）
DB 缓存窗口：5 分钟（300 条记录/tag）
写入策略：每 5 分钟或 10000 条记录批量写入 MySQL
内存缓存：环形缓冲区，每 tag 最多 1800 条（30 分钟，供趋势图快速查询）

┌──────────┐  每秒一次  ┌──────────────────┐  每5分钟  ┌──────────────┐
│DoubleBuf │──────────▶│ HistoryArchive   │──────────▶│  MySQL       │
│          │  采样     │  Thread          │  批量写入 │  history_data│
└──────────┘           │                   │           └──────────────┘
                       │  ┌─────────────┐  │
                       │  │ 内存环形缓存 │  │  queryTrend() 先查缓存
                       │  │ 1800条/tag  │──┤  (30分钟内数据)
                       │  │ (30分钟)    │  │  未命中再走 SQL
                       │  └─────────────┘  │
                       └──────────────────┘
```

**queryTrend 查询策略**：优先从内存环形缓存 `TagHistoryRing` 中查询（~μs 级），缓存未命中时回退到 DatabaseManager SQL 查询（~100ms 级）。采样时同步写入缓存，保证缓存与数据库一致。

### 8.2 MySQL 写入优化

```cpp
// 使用事务 + 批量预处理，性能提升 100 倍
if (!m_db.transaction()) return false;

QSqlQuery query(m_db);
query.prepare("INSERT INTO history_data (...) VALUES (...)");
for (const auto& record : records) {
    query.bindValue(":tag_id", record.tagId);
    query.bindValue(":value",  record.value);
    // ... 绑定所有字段
    if (!query.exec()) {
        m_db.rollback();     // 失败则回滚整批
        return false;
    }
}
m_db.commit();              // 提交事务
```

---

## 九、频率解耦策略（面试重要考点）

### 9.1 不同层级的不同频率

| 层级                 | 频率           | 原因                   |
| ------------------ | ------------ | -------------------- |
| Modbus 采集          | 100ms~1000ms | 设备性能 + 总线负载限制        |
| DataParseThread 处理 | 10~50ms      | CPU 占用与实时性平衡         |
| DoubleBuffer swap  | 50ms         | UI 感知 20fps 即可       |
| UI 渲染              | 33ms (30fps) | 人眼极限 ~60fps，30fps 流畅 |
| 历史归档               | 5min         | 磁盘 I/O 限制，不需要实时      |

### 9.2 如果没有频率解耦会怎样？

```
原始问题场景：
┌──────────┐   100ms   ┌──────────┐
│ Modbus   │──────────▶│  UI 直接  │
│ 采集     │ 每次更新   │  刷新     │
└──────────┘           └──────────┘

结果：
- UI 每 100ms 刷新一次 → CPU 忙于重绘
- 有 5 个设备各 100ms → UI 每秒刷新 50 次
- 图元数量 200+ → 每次刷新遍历所有图元 → 卡顿
- 数据解析和 UI 渲染在同一线程 → 互相阻塞

改进后：
┌──────────┐ 100ms  ┌──────────────┐ 50ms  ┌──────────┐
│ Modbus   │──────▶│ DataParse    │──────▶│  UI      │
│          │        │ Thread       │ swap  │  33ms读取│
└──────────┘        └──────────────┘       └──────────┘
- 解析和 UI 在不同线程
- DoubleBuffer 解耦读写频率
- UI 按自己的节奏读取，不被采集频率影响
```

---

## 十、完整数据流图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         完整数据链路（含异常路径）                        │
│                                                                         │
│  PLC/RTU                                                               │
│    │  RS485                                                            │
│    ▼                                                                   │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  ModbusComm 轮询线程                                              │   │
│  │  ① 发送请求帧                                                     │   │
│  │  ② 接收响应帧（或超时）                                            │   │
│  │  ③ 解析寄存器值 → RawModbusData                                    │   │
│  │  ④ 打时间戳（关键：L1 打戳）                                        │   │
│  │  ⑤ m_ringBuffer.enqueue(data)  → 满则丢帧                          │   │
│  └──────────────────────────┬───────────────────────────────────────┘   │
│                             │                                           │
│                             ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  DataParseThread (独立线程)                                       │   │
│  │  ① dequeueBatch → 批量取数据                                     │   │
│  │  ② 寄存器值 → 工程值 (量程转换)                                   │   │
│  │  ③ checkAlarmOptimized → 报警判断+死区                             │   │
│  │  ④ m_doubleBuffer.write(tagId, snapshot)                          │   │
│  │  ⑤ 达到 swap 间隔 → commit() 原子发布                              │   │
│  │  ⑥ emit dataUpdated()                                            │   │
│  └──────────┬──────────────────┬────────────────────────────────────┘   │
│             │                  │                                       │
│        commit()             报警信号                                    │
│             ▼                  ▼                                       │
│  ┌──────────────┐    ┌──────────────────┐                             │
│  │ DoubleBuffer │    │ AlarmEngine      │                             │
│  │ RCU 无锁     │    │ ISA-18.2 状态机  │                             │
│  │ 读端: <10ns  │    │ On-Delay         │                             │
│  └──────┬───────┘    │ 音频             │                             │
│         │            └──────────────────┘                             │
│         ▼                                                             │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  UI 层（主线程）                                                  │   │
│  │  ① QTimer 33ms 定时读取 readAll()                                │   │
│  │  ② 遍历 SnapshotMap 刷新图元颜色/数值                              │   │
│  │  ③ 收到 dataUpdated 或 alarmTriggered 信号时刷新报警列表           │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  异常路径处理                                                     │   │
│  │  ────────                                                        │   │
│  │  A. 设备断线 → markDeviceBad(deviceId)                           │   │
│  │     所有该设备位号 quality = Bad → UI 显示灰色/红叉                 │   │
│  │                                                                    │   │
│  │  B. 所有设备离线 → DataEngine::onAllDevicesOffline()              │   │
│  │     DoubleBuffer 遍历写 Bad → 所有位号标 Bad                       │   │
│  │     emit commStatusChanged(false) → UI 显示"通信中断"              │   │
│  │                                                                    │   │
│  │  C. 队列满 → enqueue 返回 false → ModbusComm 丢弃当前帧            │   │
│  │     L1 通信线程永远不阻塞 → 背压保护                                │   │
│  │                                                                    │   │
│  │  D. 数据库不可用 → DatabaseManager 自动降级 SQLite                 │   │
│  │     history_data -> ./data/dcs.db (WAL 模式)                       │   │
│  │     UI 无感知，后台重连                                            │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 十一、面试高频问题汇总

### Q1: 为什么不用信号槽传递实时数据？

> 信号槽是 Qt 的异步机制，内部涉及事件的构造、排队、分发。Modbus 采集频率 100ms 时，每 100ms 发射一个信号，UI 槽函数每次触发都会刷新界面。当有 300 个位号时，频繁的信号构造/销毁和界面刷新导致 CPU 飙升。改用无锁队列 + 双缓冲区 + 定时拉取模式后，UI 按自己的节奏（30fps）主动读取数据，不再被采集频率牵着走。

### Q2: DoubleBuffer 的 RCU 方案安全吗？

> 绝对安全。`shared_ptr<const SnapshotMap>` 保证 UI 线程拿到快照后，即使后台线程立即 commit() 生成新的快照，UI 线程手中的快照也通过引用计数安全存在。对象池复用 SnapshotMap 避免了频繁堆分配，commit() 延迟从 ~1μs 降至 ~0.5μs，长期运行零内存碎片。

### Q3: 数据延迟有多大？

> 最坏情况延迟 = 采集间隔(100ms) + 解析线程处理间隔(20ms) + swap 间隔(50ms) + UI 读取间隔(33ms) ≈ 200ms。对于化工 DCS 场景完全足够（温度/压力/液位变化以秒计）。如果是高速场景（如振动监测 1kHz），需要调整各层级频率。

### Q4: 死区的设计意图？

> 死区是为了防止信号在报警限值附近频繁触发/清除报警。例如 highLimit=80℃，信号在 79~81℃ 波动时，没有死区会反复触发/消除报警。设 deadband=5℃ 后，值升到 81 触发报警，必须降到 75（80-5）才消除，中间不做任何报警动作。这是 ISA-18.2 推荐的标准做法。

### Q5: 为什么 DataParseThread 用 `checkAlarmOptimized` 而不是 `checkAlarm`？

> `checkAlarm` 每次都遍历 `m_tagByRegAddr` 查找 TagInfo，时间复杂度 O(n)。`checkAlarmOptimized` 直接传入已经找到的 TagInfo 引用，时间复杂度 O(1)。processBatch 中遍历寄存器时已经查过索引得到 TagInfo，不需要重复查一次。

### Q6: 索引 `(serverAddr << 32) | regAddr` 会导致碰撞吗？

> 不会。Modbus 协议中 serverAddr 范围 1-247（8 bit），regAddr 范围 0-65535（16 bit），拼接成 64-bit key 后总共只用了低 24 bit，高 40 bit 全是 0，不可能碰撞。

### Q7: 如何保证时间戳准确？

> 时间戳在 L1 数据从串口/网口缓冲区读出的瞬间就打上（ModbusComm 收到完整帧后立刻打戳），"时间锚定"在 L1 完成，不在 L2 解析后才打戳——因为线程调度延迟不可控。这是五级流水线架构的铁律之一。

---

## 十二、DataEngine 操作员下发接口

### 12.1 接口列表

DataEngine 作为调度中枢，提供操作员下发指令的入口：

```cpp
class DataEngine : public QObject {
public:
    // 设置设定值（SP）— 操作员调整控制目标
    void setSetPoint(quint32 tagId, float sp);

    // 设置输出值（OUT）— 手动模式下直接控制阀门开度
    void setOutput(quint32 tagId, float out);

    // 切换自动/手动模式 — PID 回路的手自动切换
    void setAutoMode(quint32 tagId, bool autoMode);
};
```

### 12.2 下发流程

```
操作员在 UI 点击（如修改 SP）
    │
    ▼
MYDSCProject::onSetPointChanged(tagId, newSp)
    │
    ├─ 1. 权限检查（AuthManager::canOperate()）
    │     └─ 权限不足 → emit permissionDenied → 弹窗提示
    │
    ├─ 2. 二次确认（关键操作）
    │     └─ AuthManager::confirmCriticalAction()
    │
    ├─ 3. 调用 DataEngine::setSetPoint(tagId, sp)
    │     ├─ 更新 DoubleBuffer 中的 SP 值
    │     ├─ 通过 ModbusManager::writeRegister() 下发到 PLC
    │     └─ 记录操作日志（AuthManager::logAction）
    │
    └─ 4. 发射信号通知 UI 更新显示
```

### 12.3 setSetPoint 完整实现

```cpp
void DataEngine::setSetPoint(quint32 tagId, float sp)
{
    // Step 1: 权限检查
    if (!AuthManager::instance().canOperate()) return;

    // Step 2: 从 TagConfigMgr 获取位号信息
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) return;

    // Step 3: 范围校验
    sp = qBound(tag.engLow, sp, tag.engHigh);

    // Step 4: 二次确认
    if (!AuthManager::instance().confirmCriticalAction("SET_SP",
            QString("位号 %1 设定值 → %2 %3")
                .arg(tag.tagName).arg(sp).arg(tag.unit))) return;

    // Step 5: 更新 DoubleBuffer
    DoubleBuffer::TagSnapshot snapshot;
    snapshot.tagId = tagId;
    snapshot.setPoint = sp;
    m_doubleBuffer.write(tagId, snapshot);

    // Step 6: 下发到 PLC
    if (m_modbusManager) {
        float range = tag.engHigh - tag.engLow;
        float normalized = (sp - tag.engLow) / range;
        quint16 regValue = static_cast<quint16>(qBound(0.0f, normalized, 1.0f) * 65535.0f);
        m_modbusManager->writeRegister(
            tag.modbusDeviceId,             // 设备ID
            tag.modbusServerAddr,            // 从站地址
            tag.modbusRegAddr + 1,           // SP 寄存器地址
            regValue
        );
    }

    // Step 7: 审计日志
    AuthManager::instance().logAction("SET_SP",
        QString("tagId=%1, %2: SP=%3 %4")
            .arg(tagId).arg(tag.tagName).arg(sp).arg(tag.unit));
}
```

### 12.4 手自动切换（ISA-101 关键操作）

```cpp
void DataEngine::setAutoMode(quint32 tagId, bool autoMode)
{
    // 切到手动模式 = 危险操作，需要二次确认
    if (!autoMode) {
        if (!AuthManager::instance().confirmCriticalAction(
                "切换手动模式",
                QString("位号 %1 即将切换到手动模式，请确认").arg(tagId))) {
            return;  // 操作员取消
        }
    }

    // 更新 DoubleBuffer
    DoubleBuffer::TagSnapshot snapshot;
    snapshot.tagId = tagId;
    snapshot.autoMode = autoMode;
    m_doubleBuffer.write(tagId, snapshot);

    // 下发到 PLC
    if (m_modbusManager) {
        m_modbusManager->writeRegister(/* ... */);
    }

    // 记录操作日志
    AuthManager::instance().logAction(
        autoMode ? "SET_AUTO_MODE" : "SET_MANUAL_MODE",
        QString("tagId=%1").arg(tagId));
}
```

**为什么切手动需要二次确认？** 化工 DCS 中，手动模式下操作员直接控制阀门开度，如果操作不当（如突然全开/全关），可能导致管道超压、反应失控等事故。ISA-101 标准要求关键操作必须有确认步骤。

---

## 十三、无桥接层：直接数据更新路径

### 13.1 RealtimeDb 已移除

RealtimeDb 作为早期的适配器/桥接层，已完全移除。移除原因：

1. **updateValue() 从未被外部调用**：DataParseThread 直接写 DoubleBuffer，不经过任何中间层
2. **回调机制是死代码**：UI 更新走的是 MYDSCProject::onDataUpdated() → 遍历图元 → onTagValueChanged()，不依赖 registerCallback
3. **一切转发都是薄层**：getTag() 直接委托给 TagConfigMgr，updateSetPoint() 直接操作 DoubleBuffer，桥接层无价值

### 13.2 当前数据更新路径（无桥接）

```
┌─────────────────┐     ┌──────────────┐     ┌─────────────┐
│ DataParseThread │────▶│ DoubleBuffer │◀────│  UI Thread  │
│ (解析线程)      │写入 │ (双缓冲区)   │读取 │ (主线程)    │
│                 │     │              │     │             │
│ 解析Modbus数据  │     │ 写缓冲区     │     │ swap()      │
│ 报警判断        │     │ 读缓冲区     │     │ readTag()   │
│ 直接write()     │     │              │     │ 遍历图元    │
└─────────────────┘     └──────────────┘     └─────────────┘
```

### 13.3 DataParseThread 直接写 DoubleBuffer

```cpp
// DataParseThread::processData() 中
DoubleBuffer::TagSnapshot snapshot;
snapshot.tagId = tag.tagId;
snapshot.currentValue = scaledValue;
snapshot.quality = DataQuality::Good;
snapshot.timestamp = QDateTime::currentMSecsSinceEpoch();
snapshot.setPoint = tag.setPoint;
snapshot.alarmState = alarmState;
m_doubleBuffer->write(tag.tagId, snapshot);
```

写完一批数据后，发射 `dataUpdated()` 信号，UI 线程收到后调用 `doubleBuffer->swap()` 切换缓冲区。

### 13.4 UI 线程读取（无回调，直接读快照）

```cpp
// MYDSCProject::onDataUpdated()
void MYDSCProject::onDataUpdated()
{
    m_dataEngine->doubleBuffer()->swap();  // 切换到最新数据
    
    // 遍历所有图元，逐个更新
    for (auto* item : m_graphicsItems) {
        auto snapshot = m_dataEngine->doubleBuffer()->readTag(item->tagId());
        item->onTagValueChanged(snapshot);  // 直接从 snapshot 取值
    }
}
```

**关键点**：swap() 后读取的都是同一批数据，不会出现半个新半个旧的情况。无锁读取，UI 线程不会阻塞解析线程。

---

## 十四、DataEngine 生命周期管理

### 14.1 初始化顺序（关键！）

```
DataEngine::initialize()
    │
    ├─ 1. 创建无锁循环队列（m_ringBuffer）
    │     └─ LockFreeRingBuffer<RawModbusData, 8192>
    │
    ├─ 2. 创建双缓冲区（m_doubleBuffer）
    │     └─ DoubleBuffer
    │
    ├─ 3. 创建 ModbusManager（绑定队列）
    │     ├─ m_modbusManager = new ModbusManager(this)
    │     └─ m_modbusManager->setRingBuffer(&m_ringBuffer)
    │
    ├─ 4. 创建 DataParseThread（绑定队列+缓冲区）
    │     ├─ m_dataParseThread = new DataParseThread(this)
    │     ├─ m_dataParseThread->setRingBuffer(&m_ringBuffer)
    │     └─ m_dataParseThread->setDoubleBuffer(&m_doubleBuffer)
    │
    ├─ 5. 连接信号
    │     ├─ connect(m_dataParseThread, dataUpdated → this, dataUpdated)
    │     ├─ connect(m_dataParseThread, alarmTriggered → this, alarmTriggered)
    │     └─ connect(m_modbusManager, deviceStatusChanged → this, deviceStatusChanged)
    │
    └─ 6. 连接 DataParseThread 信号到 DataEngine 槽函数
```

**为什么顺序重要？** DataParseThread 需要 RingBuffer 和 DoubleBuffer 的指针，ModbusManager 需要 RingBuffer 的指针。如果先创建 DataParseThread 再创建 RingBuffer，指针就是空的。

### 14.2 启动流程

```
DataEngine::start()
    ├─ m_dataParseThread->start()     // 启动解析线程
    └─ m_modbusManager->startAll()    // 启动所有 Modbus 设备轮询
```

### 14.3 停止流程

```
DataEngine::stop()
    ├─ m_modbusManager->stopAll()     // 停止所有 Modbus 轮询
    │   └─ 每个设备: thread->quit() → thread->wait(3000)
    │
    └─ m_dataParseThread->stop()      // 停止解析线程
        ├─ m_running.storeRelaxed(0)
        └─ wait(5000)
```

### 14.4 信号连接关系

```
DataParseThread ──dataUpdated──▶ DataEngine ──dataUpdated──▶ MYDSCProject (UI刷新)
DataParseThread ──alarmTriggered──▶ DataEngine ──alarmTriggered──▶ AlarmEngine
ModbusManager ──deviceStatusChanged──▶ DataEngine ──deviceStatusChanged──▶ MYDSCProject
ModbusManager ──allDevicesOffline──▶ DataEngine::onAllDevicesOffline()
```

---

## 十五、RawModbusData 设计要点

### 15.1 结构体定义（ModbusManager.h）

```cpp
struct RawModbusData {
    int deviceId = 0;               // 设备编号
    int serverAddress = 0;          // Modbus 从站地址
    int startAddress = 0;           // 寄存器起始地址
    qint64 timestamp = 0;           // 采集时间戳（毫秒，L1 层锚定）
    int valueCount = 0;             // 有效数据数量
    quint16 values[128] = { 0 };    // 寄存器原始值（最多 128 个寄存器）
};
```

### 15.2 为什么必须是 POD-like 类型？

| 设计约束               | 原因                               |
| ------------------ | -------------------------------- |
| 不包含 QString        | 无锁队列使用 memcpy 传递数据，QString 需要堆分配 |
| 不包含 QHash/QVector  | 这些类型有间接寻址，memcpy 后指针无效           |
| `values[128]` 固定数组 | 避免动态分配，Modbus 单次最多读 125 个寄存器     |
| `= { 0 }` 零初始化     | 确保未使用的字段是确定的零值                   |

### 15.3 时间戳锚定

```cpp
// ModbusComm 收到完整帧后立刻打戳
void ModbusComm::onPollTimeout()
{
    // ... 读取寄存器 ...
    RawModbusData data;
    data.timestamp = QDateTime::currentMSecsSinceEpoch();  // ← L1 层打戳
    data.serverAddress = m_serverAddress;
    data.startAddress = m_startAddress;
    data.valueCount = count;
    memcpy(data.values, values, count * sizeof(quint16));
    m_ringBuffer->enqueue(data);
}
```

**铁律**：时间戳必须在数据从硬件缓冲区读出的瞬间打上，不能在 L2 解析后才打。线程调度延迟不可控，L2 打戳可能导致时间偏差 10-50ms。

---

## 十六、关键代码路径速查

| 功能          | 文件                       | 方法                      | 说明              |
| ----------- | ------------------------ | ----------------------- | --------------- |
| 数据引擎初始化     | DataEngine.cpp           | `initialize()`          | 按顺序创建5个组件       |
| 数据引擎启动      | DataEngine.cpp           | `start()`               | 启动解析线程+Modbus轮询 |
| 数据引擎停止      | DataEngine.cpp           | `stop()`                | 先停通信再停解析        |
| 设置SP        | DataEngine.cpp           | `setSetPoint()`         | 权限检查+范围校验+下发    |
| 设置OUT       | DataEngine.cpp           | `setOutput()`           | 手动模式专用          |
| 切换自动/手动     | DataEngine.cpp           | `setAutoMode()`         | 二次确认+操作日志       |
| 设备离线处理      | DataEngine.cpp           | `onAllDevicesOffline()` | 标记所有位号 Bad      |
| 数据更新转发      | DataEngine.cpp           | `onDataUpdated()`       | 转发到 UI          |
| 寄存器→工程值     | DataParseThread.cpp      | `registerToValue()`     | 线性量程转换          |
| 报警判断        | DataParseThread.cpp      | `checkAlarmOptimized()` | 含死区处理           |
| 批量数据出队      | DataParseThread.cpp      | `processBatch()`        | 256条/批          |
| 双缓冲区提交      | DoubleBuffer.h           | `commit()`              | 原子发布快照          |
| 双缓冲区读取      | DoubleBuffer.h           | `readAll()`             | 无锁读取            |
| 无锁队列入队      | lockFreeRingBuffer.h     | `enqueue()`             | MPSC 多生产者       |
| 无锁队列出队      | lockFreeRingBuffer.h     | `dequeueBatch()`        | 单消费者批量          |
| Modbus 写寄存器 | ModbusManager.cpp        | `writeRegister()`       | 操作员下发通道         |
| 历史归档        | HistoryArchiveThread.cpp | `run()`                 | 5分钟批量写入MySQL    |

---

## 十七、DataEngine 信号系统完整设计（7 个信号）

### 17.1 全信号一览表

```cpp
// DataEngine.h — signals 区域
signals:
    void dataUpdated();                              // 数据更新（转发自 DataParseThread）
    void alarmTriggered(quint32 tagId, AlarmLimit state, float value, float limit);  // 报警触发
    void alarmCleared(quint32 tagId);                 // 报警恢复
    void commStatusChanged(bool connected);           // 通信状态变化（所有设备离线/恢复）
    void deviceStatusChanged(int deviceId, bool connected);  // 单设备状态变化
    void archiveCompleted(int recordCount, qint64 durationMs);   // 归档完成（已弃用）
    void archiveFailed(const QString& error);         // 归档失败（已弃用）
```

**总计：7 个信号**，其中 2 个已弃用（纯内存存储后不再需要 MySQL 归档通知）。

### 17.2 信号连接关系图

```
ModbusManager ──deviceStatusChanged──▶ DataEngine ──deviceStatusChanged──▶ UI
              ──allDevicesOffline─────▶ DataEngine ──commStatusChanged───▶ UI

DataParseThread ──dataUpdated─────────▶ DataEngine ──dataUpdated──────────▶ UI
               ──alarmTriggered──────▶ DataEngine ──alarmTriggered───────▶ AlarmEngine + UI
               ──alarmCleared────────▶ DataEngine ──alarmCleared─────────▶ AlarmEngine + UI
```

**关键设计**：DataEngine 作为**信号中继站**，不处理业务逻辑，只做信号转发。这保证了各模块解耦——DataParseThread 不知道 UI 存在，ModbusManager 不知道 AlarmEngine 存在。

### 17.3 onDataUpdated 转发逻辑

```cpp
void DataEngine::onDataUpdated()
{
    // 极简实现：直接转发
    emit dataUpdated();
}
```

**为什么需要这个中间槽函数？** 未来可能在此处添加：

- 数据更新频率统计（PerformanceMonitor）
- 数据变更日志记录
- 条件性转发（如某些模式下暂停刷新）

---

## 十八、操作员下发完整流程（setSetPoint / setOutput / setAutoMode）

### 18.1 setSetPoint — 设置设定值（SP）

```cpp
void DataEngine::setSetPoint(quint32 tagId, float sp)
{
    // Step 1: 权限检查（必须操作员级别以上）
    if (!AuthManager::instance().canOperate()) {
        LOG_WARN("DataEngine", "权限不足: 需要操作员权限才能修改设定值");
        return;
    }

    // Step 2: 查找位号信息
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) {
        LOG_WARN("DataEngine", QString("无效的位号ID: %1").arg(tagId));
        return;
    }

    // Step 3: SP 必须在量程范围内
    sp = qBound(tag.engLow, sp, tag.engHigh);

    // Step 4: 二次确认（ISA-101 关键操作）
    if (!AuthManager::instance().confirmCriticalAction("SET_SP",
            QString("位号 %1 设定值 → %2 %3")
                .arg(tag.tagName).arg(sp).arg(tag.unit))) {
        return;
    }

    // Step 5: 更新 DoubleBuffer（内存中的 SP 值）
    auto snapshot = m_doubleBuffer.readTag(tagId);
    snapshot.setPoint = sp;
    m_doubleBuffer.write(tagId, snapshot);

    // Step 6: 下发到 Modbus 设备
    if (m_modbusManager) {
        float range = tag.engHigh - tag.engLow;
        float normalized = (sp - tag.engLow) / range;
        normalized = qBound(0.0f, normalized, 1.0f);
        quint16 regValue = static_cast<quint16>(normalized * 65535.0f);

        m_modbusManager->writeRegister(
            tag.modbusDeviceId,          // 设备ID
            tag.modbusServerAddr,         // 从站地址
            tag.modbusRegAddr + 1,        // SP 寄存器地址
            regValue
        );
    }

    // Step 7: 审计日志（持久化到 operation_log 表）
    AuthManager::instance().logAction("SET_SP",
        QString("tagId=%1, %2: SP=%3 %4")
            .arg(tagId).arg(tag.tagName).arg(sp).arg(tag.unit));
}
```

`writeRegister` 的第一个参数是 `tag.modbusDeviceId`（设备ID），第二个参数是 `tag.modbusServerAddr`（Modbus 从站地址）。TagInfo 中新增的 `modbusDeviceId` 字段在 `loadTagConfig()` 时从 JSON 配置解析，对应 `ModbusManager::addDevice()` 时分配的 deviceId，二者必须一致。

### 18.2 setOutput — 设置输出值（OUT）

```cpp
void DataEngine::setOutput(quint32 tagId, float out)
{
    if (!AuthManager::instance().canOperate()) return;

    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) return;

    // 二次确认
    if (!AuthManager::instance().confirmCriticalAction("SET_OUT",
            QString("位号 %1 输出值 → %2%").arg(tag.tagName).arg(out)))
        return;

    out = qBound(0.0f, out, 100.0f);
    auto snapshot = m_doubleBuffer.readTag(tagId);
    snapshot.outputValue = out;
    m_doubleBuffer.write(tagId, snapshot);

    if (m_modbusManager) {
        float range = tag.engHigh - tag.engLow;
        float normalized = (out - tag.engLow) / range;
        quint16 regValue = static_cast<quint16>(qBound(0.0f, normalized, 1.0f) * 65535.0f);
        m_modbusManager->writeRegister(
            tag.modbusDeviceId, tag.modbusServerAddr,
            tag.modbusRegAddr + 2, regValue);
    }

    AuthManager::instance().logAction("SET_OUT",
        QString("tagId=%1, %2: OUT=%3%").arg(tagId).arg(tag.tagName).arg(out));
}
```

### 18.3 setAutoMode — 切换自动/手动模式

```cpp
void DataEngine::setAutoMode(quint32 tagId, bool autoMode)
{
    if (!AuthManager::instance().canOperate()) return;

    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) return;

    // ISA-101：切换到手动模式需要二次确认
    if (!autoMode) {
        if (!AuthManager::instance().confirmCriticalAction(
                "SET_MANUAL_MODE",
                QString("位号 %1 即将切换到手动模式，请确认").arg(tag.tagName)))
            return;
    }

    if (m_modbusManager) {
        quint16 modeValue = autoMode ? 1 : 0;
        m_modbusManager->writeRegister(
            tag.modbusDeviceId, tag.modbusServerAddr,
            tag.modbusRegAddr + 3, modeValue);
    }

    AuthManager::instance().logAction(
        autoMode ? "SET_AUTO_MODE" : "SET_MANUAL_MODE",
        QString("tagId=%1, %2").arg(tagId).arg(tag.tagName));
}
```

### 18.4 下发寄存器布局约定

```
Modbus 寄存器区域布局（每个 PID 回路占用 4 个连续寄存器）：

┌──────────────┬──────────────┬──────────────┬──────────────┐
│  Reg+0 (PV)  │  Reg+1 (SP)  │  Reg+2 (OUT) │  Reg+3 (MODE)│
│  过程值      │  设定值      │  输出值      │  自动/手动   │
│  只读        │  可写        │  可写(MAN)   │  可写        │
│  0~65535    │  0~65535     │  0~65535     │  0/1         │
└──────────────┴──────────────┴──────────────┴──────────────┘
```

---

## 十九、loadTagConfig 完整验证逻辑

### 19.1 加载流程全景

```
loadTagConfig("config/tags.json")
  │
  ├─ 1. QFile::open() → 文件存在性检查
  ├─ 2. QJsonDocument::fromJson() → JSON 合法性检查
  ├─ 3. 逐条解析 QJsonObject → 字段完整性检查
  │     ├─ tagId 必填 → 缺少则跳过并警告
  │     ├─ tagName 必填 → 空名称则跳过并警告
  │     ├─ tagId 重复检测 → 重复则跳过并警告
  │     ├─ tagType 解析 → 未知类型默认 AI 并警告
  │     │
  │     ├─ 【量程合理性验证】engHigh > engLow？
  │     │     └─ 不满足 → 自动交换并警告
  │     │
  │     ├─ 【报警限值范围验证】HH ≤ engHigh 且 LL ≥ engLow？
  │     │     └─ 超出 → 自动裁剪到量程范围并警告
  │     │
  │     ├─ 【报警限值顺序验证】HH > H > L > LL？
  │     │     └─ 不满足 → 用 qMax 强制修正并警告
  │     │
  │     ├─ 【Modbus 地址验证】
  │     │     ├─ serverAddr ∈ [1, 247] → 超出用 qBound 修正
  │     │     ├─ regAddr ∈ [0, 65535] → 超出用 qBound 修正
  │     │     └─ regCount ∈ [1, 125] → 超出用 qBound 修正
  │     │
  │     └─ 【PID 参数验证】（仅 PID 类型）
  │           └─ kp/ki/kd ≥ 0？→ 负数强制为默认值
  │
  ├─ 4. 分发给 DataParseThread（setTagConfig）
  └─ 6. 返回有效位号数量
```

### 19.2 验证规则汇总表

| 规则         | 检查条件                    | 违规处理        | 日志级别  |
| ---------- | ----------------------- | ----------- | ----- |
| tagId 存在   | `obj.contains("tagId")` | 跳过该条目       | ERROR |
| tagName 非空 | `!tagName.isEmpty()`    | 跳过该条目       | ERROR |
| tagId 唯一   | 循环比对已加载列表               | 跳过该条目       | ERROR |
| 量程合理       | `engHigh > engLow`      | 交换高低值       | WARN  |
| HH 在量程内    | `highHigh ≤ engHigh`    | 裁剪到 engHigh | WARN  |
| LL 在量程内    | `lowLow ≥ engLow`       | 裁剪到 engLow  | WARN  |
| 限值有序       | `HH>H>L>LL`             | qMax 逐级修正   | WARN  |
| 从站地址合法     | `1≤serverAddr≤247`      | qBound 钳位   | WARN  |
| 寄存器地址合法    | `0≤regAddr≤65535`       | qBound 钳位   | WARN  |
| 寄存器数量合法    | `1≤regCount≤125`        | qBound 钳位   | WARN  |
| PID 参数非负   | `kp,ki,kd ≥ 0`          | 强制为 0       | WARN  |

### 19.3 设计哲学：容错优于崩溃

**核心原则**：配置文件中有任何错误都不应导致系统启动失败。

- **ERROR 级别**（跳过）：缺少关键字段，无法继续处理
- **WARN 级别**（修正）：数值不合理但有安全默认值可用

这种"宽松解析"策略在工业现场非常重要——操作员可能手工编辑 JSON 配置文件，输入错误是常态。

---

## 二十、操作员下发完整流程（移除 RealtimeDb 后）

### 20.1 为什么 RealtimeDb 被移除

```
移除前：
  操作员下发 ──▶ RealtimeDb ──转发──▶ DoubleBuffer
                （冗余桥接层）

移除后：
  操作员下发 ──▶ TagConfigMgr::getTag()  （静态配置查询）
            ──▶ m_doubleBuffer.write()   （直接写实时值）
            ──▶ m_modbusManager->writeRegister() （下发到 PLC）
```

RealtimeDb 只做薄层转发：getTag() 委托给 TagConfigMgr，updateSetPoint() 操作 DoubleBuffer。去掉这一层后调用链更短，无性能损失。

### 20.2 当前接口转发目标

| 方法类别       | 示例方法                                 | 直接目标           | 说明       |
| ---------- | ------------------------------------ | -------------- | -------- |
| **静态配置查询** | `getTag()` / `getAllTags()`          | TagConfigMgr   | 单例直接调用   |
| **实时值写入**  | `setSetPoint()` / `setOutput()`      | DoubleBuffer   | 写入写缓冲区   |
| **质量码管理**  | `markAllBad()`                       | DoubleBuffer   | 批量标记 Bad |
| **UI 更新**   | `onDataUpdated()`                    | DoubleBuffer   | swap + 遍历图元 |

### 20.3 setSetPoint 直接 DoubleBuffer 写入

```cpp
void DataEngine::setSetPoint(quint32 tagId, float sp)
{
    // 查静态配置
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) return;
    sp = qBound(tag.engLow, sp, tag.engHigh);

    // 直接写 DoubleBuffer
    auto snapshot = m_doubleBuffer.readTag(tagId);
    snapshot.setPoint = sp;
    m_doubleBuffer.write(tagId, snapshot);

    // 下发到 PLC
    if (m_modbusManager) {
        m_modbusManager->writeRegister(...);
    }
}
```

### 20.4 markAllBad — 设备离线批量标记（内联到 DataEngine）

```cpp
void DataEngine::onAllDevicesOffline()
{
    auto allTags = TagConfigMgr::instance().getAllTags();  // 获取全部位号
    for (const auto& tag : allTags) {
        auto snapshot = m_doubleBuffer.readTag(tag.tagId);  // 先读快照
        snapshot.quality = DataQuality::Bad;                  // 修改质量码
        m_doubleBuffer.write(tag.tagId, snapshot);            // 写回
    }
    emit commStatusChanged(false);
}
```

**性能**：N 个位号需要 N 次 `readTag + write`。对于 500 个位号约需 500μs，设备离线场景下完全可接受（非高频操作）。

### 20.5 UI 数据刷新流程（替代旧回调机制）

```cpp
// MYDSCProject::onDataUpdated() — 收到 dataUpdated 信号后
void MYDSCProject::onDataUpdated()
{
    m_dataEngine->doubleBuffer()->swap();

    for (auto* item : m_graphicsItems) {
        auto snap = m_dataEngine->doubleBuffer()->readTag(item->tagId());
        if (snap.tagId != 0) {
            item->onTagValueChanged(snap);
        }
    }
}

// BaseGraphicsItem::onTagValueChanged() — 图元自行解析快照
void BaseGraphicsItem::onTagValueChanged(const DoubleBuffer::TagSnapshot& snap)
{
    m_currentValue = snap.currentValue;
    m_alarmState = snap.alarmState;
    updateAppearance();  // 虚函数，子类实现具体渲染
}
```

**优势**：swap() 后所有图元读取同一批数据（一致性），readTag 无锁（不阻塞解析线程），不依赖回调注册/注销（无生命周期问题）。

---

## 廿一、LockFreeRingBuffer 序列号方案深度剖析

### 21.1 核心数据结构

```cpp
template<typename T, size_t Capacity = 8192>
class LockFreeRingBuffer {
    struct Slot {
        T data;                          // 数据（POD-like 类型）
        std::atomic<size_t> sequence;    // 序列号（关键！）
    };

    Slot m_slots[Capacity];             // 固定大小数组（缓存友好）
    alignas(64) std::mutex m_enqueueMutex;  // 入队锁（多生产者）
    alignas(64) std::atomic<size_t> m_enqueuePos;  // 入队位置
    alignas(64) std::atomic<size_t> m_dequeuePos;  // 出队位置
    static constexpr size_t m_mask = Capacity - 1;  // 位掩码
};
```

### 21.2 alignas(64) 的作用

```cpp
alignas(64) std::mutex m_enqueueMutex;
alignas(64) std::atomic<size_t> m_enqueuePos;
alignas(64) std::atomic<size_t> m_dequeuePos;
```

**缓存行对齐（Cache Line Padding）**：x86 CPU 缓存行大小为 64 字节。将不同的原子变量放在不同缓存行，避免**伪共享（False Sharing）**：

```
无对齐时（问题场景）：
┌─────────────────────┬─────────────────────┐
│ enqueuePos (8B)     │ dequeuePos (8B)     │  ← 同一缓存行
│ ... 56B padding ... │ ... 56B padding ... │
└─────────────────────┴─────────────────────┘
CPU0 写 enqueuePos → 整个缓存行失效 → CPU1 读 dequeuePos 时 cache miss

有对齐后（解决）：
┌─────────────────────────────────────────┐
│ enqueuePos (8B) + 56B padding           │  ← 缓存行 A
├─────────────────────────────────────────┤
│ dequeuePos (8B) + 56B padding           │  ← 缓存行 B（独立）
└─────────────────────────────────────────┘
CPU0 写 enqueuePos → 只影响缓存行 A → CPU1 读 dequeuePos 命中缓存行 B
```

### 21.3 static_assert 编译期约束

```cpp
static_assert(std::is_nothrow_copy_assignable_v<T> || std::is_nothrow_move_assignable_v<T>,
    "T must be nothrow assignable");
static_assert((Capacity & (Capacity - 1)) == 0, "Capacity 必须是2的幂");
static_assert(Capacity >= 2, "Capacity 不能小于2");
```

| 约束                 | 原因                                        | 违反后果      |
| ------------------ | ----------------------------------------- | --------- |
| nothrow assignable | 入队/出队时如果拷贝抛异常，队列状态会被破坏                    | 数据损坏且无法恢复 |
| Capacity 是 2 的幂    | 用位运算 `& mask` 替代 `% Capacity`（取模慢 5-10 倍） | 功能正常但性能下降 |
| Capacity ≥ 2       | 至少能容纳 1 个元素                               | 无意义       |

### 21.4 RawModbusData 设计要点

```cpp
struct RawModbusData {
    int deviceId = 0;                // 设备编号
    int serverAddress = 0;           // Modbus 从站地址
    int startAddress = 0;            // 寄存器起始地址
    qint64 timestamp = 0;            // 采集时间戳（L1 层锚定！）
    int valueCount = 0;              // 有效数据数量
    quint16 values[128] = { 0 };     // 固定大小数组（不用 vector！）
};
```

**为什么 values 是固定数组而不是 QVector？**

1. **POD-like 要求**：LockFreeRingBuffer 要求 T 可以安全 memcpy，QVector 有堆分配的内部指针，memcpy 后指针悬空
2. **栈上分配**：128 × 2B = 256B，栈空间完全够用
3. **避免堆分配抖动**：每次入队都不需要 new/delete，延迟稳定

**为什么 timestamp 在 L1 层锚定？**
时间戳应该在数据产生的时刻立即打上，而不是等到 L2 解析时才打。这样即使队列积压，每条数据的时间戳也是准确的。

---

## 廿二、DataEngine 生命周期管理

### 22.1 完整生命周期状态机

```
                    ┌──────────────┐
                    │   Created    │  构造函数完成
                    │ m_initialized│  = false
                    │ m_running    │  = false
                    └──────┬───────┘
                           │ initialize()
                           ▼
                    ┌──────────────┐
                    │ Initialized  │  所有组件创建完毕
                    │ m_initialized│  = true ✓
                    │ m_running    │  = false
                    └──────┬───────┘
                           │ loadTagConfig()
                           │ addModbusDevice()
                           ▼
                    ┌──────────────┐
                    │  Configured  │  配置加载完成
                    │              │  （可选状态）
                    └──────┬───────┘
                           │ start()
                           ▼
                    ┌──────────────┐
         ┌────────▶│   Running    │◀────────┐
         │         │ m_running    │         │
         │         │  = true ✓    │         │
         │         └──────┬───────┘         │
         │                │ stop()          │
         │                ▼                 │
         │         ┌──────────────┐        │
         │         │   Stopped    │────────┘
         │         │ m_running    │  可以 restart()
         │         │  = false     │
         │         └──────┬───────┘
         │                │ ~DataEngine()
         │                ▼
         │         ┌──────────────┐
         └────────▶│ Destroyed    │
                   │ 所有资源释放  │
                   └──────────────┘
```

### 22.2 stop() 安全停止策略

```cpp
void DataEngine::stop()
{
    if (!m_running) return;          // 幂等：多次调用安全

    m_running = false;

    // Step 1: 先停通信（防止新数据进入队列）
    if (m_modbusManager) {
        m_modbusManager->stopAll();
    }

    // Step 2: 再停解析线程（等待队列排空或超时）
    if (m_dataParseThread) {
        m_dataParseThread->stop();
        m_dataParseThread->wait(3000);  // 最多等 3 秒
    }

    LOG_INFO("DataEngine", "数据引擎已停止");
}
```

**为什么先停通信再停解析？**
如果反过来：先停解析线程，通信还在往队列里写数据 → 队列满 → 通信线程阻塞在 enqueue → 无法响应 stopAll() → 死锁风险。

### 22.3 onAllDevicesOffline 处理策略

```cpp
void DataEngine::onAllDevicesOffline()
{
    // 所有 Modbus 设备断连
    // 将所有位号质量码标记为 Bad
    auto allTags = TagConfigMgr::instance().getAllTags();
    for (const auto& tag : allTags) {
        auto snapshot = m_doubleBuffer.readTag(tag.tagId);
        snapshot.quality = DataQuality::Bad;
        m_doubleBuffer.write(tag.tagId, snapshot);
    }
    emit commStatusChanged(false);         // 通知 UI 显示"通讯中断"
    LOG_ERROR("DataEngine", "所有通讯设备离线，所有位号质量码已标记为Bad");
}
```

**UI 层收到后的典型行为**：

- 标题栏变红闪烁 "通讯中断"
- 所有数值显示变为灰色或 "---"
- 报警列表新增一条 "通讯中断" 系统级报警
- 定时尝试重连（由 ModbusManager 内部处理）

---

## 廿三、工业实战经验与不足分析

### 23.1 商用 DCS 数据引擎架构对比

```
┌──────────────────────────────────────────────────────────────────────┐
│                     本项目架构（原型）                                 │
│                                                                      │
│  Modbus TCP ──▶ LockFreeRingBuffer ──▶ DataParseThread ──▶ DoubleBuffer │
│                                      │                               │
│                                      ├─ AlarmEngine (ISA-18.2)      │
│                                      └─ HistoryArchiveThread (归档)  │
│                                                                       │
│  存储：MySQL / SQLite（二选一降级）                                    │
│  协议：仅 Modbus TCP                                                  │
│  位号上限：~500（受内存索引限制）                                       │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│              商用 DCS（如 Honeywell Experion PKS / Yokogawa CENTUM）   │
│                                                                      │
│  多协议支持 ──▶ 统一数据总线 ──▶ 分布式数据处理 ──▶ 多级缓存          │
│  (Modbus/OPC UA │  (DDS/PubSub)  │  (集群部署)    │  (L1/L2/L3)      │
│   /Profibus/    │                 │                │                  │
│   /HART/EtherCAT│                 │                │                  │
│   /IEC61850)    │                 │                │                  │
│                                                                       │
│  存储：专用时序数据库（PI / Historian / eDNA）                         │
│  位号上限：100,000+                                                    │
│  冗余：双网口 / 双控制器 / 双电源                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### 23.2 本项目已有的优势（值得保留的设计）

| #   | 优势            | 说明                                                 |
| --- | ------------- | -------------------------------------------------- |
| 1   | **MPSC 无锁队列** | 多设备并发入队，单消费者无锁出队，实测 50ns/次                         |
| 2   | **RCU 双缓冲区**  | shared_ptr + 对象池原子发布，UI 零等待读取，长期运行零碎片              |
| 3   | **频率四层解耦**    | 采集(100ms) → 解析(20ms) → swap(50ms) → UI(100ms)，各层独立 |
| 4   | **批量处理**      | dequeueBatch(256) 比 逐条处理效率高 10 倍以上                 |
| 5   | **智能休眠**      | 有数据睡 1ms，无数据睡 20ms，CPU 占用 < 5%                     |
| 6   | **容错配置加载**    | 错误字段自动修正而非崩溃退出                                     |
| 7   | **SQLite 降级** | MySQL 不可用时自动降级，保证数据不丢失                             |
| 8   | **权限拦截**      | 操作员下发前强制权限检查                                       |
| 9   | **变化率校验**     | validateRateOfChange() 防跳变，异常值标记 Uncertain          |
| 10  | **内存历史缓存**   | TagHistoryRing 环形缓冲区，趋势图查询 μs 级响应                 |
| 11  | **操作安全确认**   | ISA-101 二次确认 + 审计日志持久化到 operation_log 表             |

### 23.3 当前实现的不足（商用化必须解决）

#### 不足 1：仅支持 Modbus TCP

**现状**：只有 ModbusComm 一种驱动。
**工业场景**：大型工厂同时使用 Siemens S7（Profinet）、Allen-Bradley（EtherNet/IP）、Yokawera（MLSB）、OPC UA 等。
**影响**：无法接入主流 PLC 品牌。
**解决方案**：抽象 IDeviceDriver 接口，实现多协议插件：

```cpp
class IDeviceDriver {
public:
    virtual bool connect() = 0;
    virtual bool readRegisters(int addr, int count, quint16* values) = 0;
    virtual bool writeRegister(int addr, quint16 value) = 0;
};
class ModbusDriver : public IDeviceDriver { ... };
class OpcUaDriver  : public IDeviceDriver { ... };
class S7Driver     : public IDeviceDriver { ... };
```

#### 不足 2：无数据压缩传输

**现状**：RawModbusData.values[128] 即使只有 10 个有效值也传递 128×2=256B。
**工业场景**：300 个位号，100ms 周期，原始数据带宽约 2.4MB/s。
**影响**：网络带宽浪费，尤其无线/远程场景。
**解决方案**：

- 使用 `valueCount` 字段只传递有效部分
- 高频变化位号全量传输，低频变化位号增量传输（Gorilla 压缩算法）
- 采用 protobuf 替代 raw struct 减少序列化开销

#### 不足 3：无双机热备

**现状**：单进程运行，进程崩溃即服务中断。
**工业场景**：DCS 系统 7×24 运行，计划内维护也需要零中断。
**影响**：每次升级/重启都有几分钟的服务空白期。
**解决方案**：

```
Active (主) ◄──── 心跳/数据同步 ────▶ Standby (备)
    │                                        │
    │ 崩溃                                   │ 检测到主宕机
    └────────────────────────────────────────┘
                     │
                     ▼
              Standby 自动接管（秒级切换）
```

### 23.4 性能瓶颈预判

| 场景     | 当前容量           | 商用要求       | 瓶颈点                          |
| ------ | -------------- | ---------- | ---------------------------- |
| 位号数量   | ~500（QHash 索引） | 100,000+   | 索引需改为 B+树或分片                 |
| 采集周期   | 100ms 最快       | 1~10ms     | Modbus TCP 本身限制，需换协议         |
| 历史数据点  | 最近 1h 常驻内存（环形缓存） | 24h+ 全量时序库 | HistoryArchiveThread TagHistoryRing，3600 条/tag |
| 下发延迟   | 同步阻塞           | 异步+确认      | writeRegister 应改为异步          |
| UI 刷新率 | 10~20fps       | 60fps      | DoubleBuffer commit 频率不够     |
| 断网恢复   | 手动重连           | 自动重连+断点续传  | 需增加离线缓存队列                    |

### 23.5 安全性不足

| 项目    | 当前状态             | 商用要求                                     |
| ----- | ---------------- | ---------------------------------------- |
| 下发权限  | 仅检查 canOperate() | 需要"双人复核"（Four-Eyes Principle）            |
| SP 范围 | 量程钳位             | 需要 SP 上下限独立于量程（如 80±5℃）                  |
| 速率限制  | 无                | 防止快速连续下发（DDoS 自身）                        |
| 操作追溯  | 持久化审计日志（operation_log 表） | 操作审计日志通过 AuthManager::logAction() → DatabaseManager 写入 |
| 远程访问  | 无                | VPN + TLS + 白名单 IP                       |

### 23.6 可靠性不足

| 项目    | 当前状态    | 商用要求                             |
| ----- | ------- | -------------------------------- |
| 进程守护  | 无       | Windows Service / systemd + 自动重启 |
| 数据持久化 | 依赖外部 DB | 本地 WAL 日志 + 定期 checkpoint        |
| 看门狗   | 无       | 外部 Watchdog + 心跳超时重启             |
| 时间同步  | 取系统时间   | NTP + PTP（精度 < 1ms）              |
| 断电保护  | 无       | UPS + 断电前紧急保存                    |

---

## 廿四、面试加分项速查表

| 问题                      | 关键词           | 答案要点                                          |
| ----------------------- | ------------- | --------------------------------------------- |
| 为什么用无锁队列？               | MPSC          | 多设备并发入队，单消费者出队，消除锁竞争                          |
| RCU 双缓冲原理？              | shared_ptr    | 写线程 move→原子发布→读线程引用计数保护                       |
| 为什么 alignas(64)？        | False Sharing | 不同原子变量放不同缓存行，避免伪共享                            |
| Capacity 为什么是 2 的幂？     | 位运算优化         | `& mask` 替代 `% Capacity`，快 5-10 倍             |
| RawModbusData 为什么用固定数组？ | POD-like      | 避免 QVector 堆分配，memcpy 安全                      |
| 四层频率解耦？                 | 采/解/Swap/UI   | 各层独立定时，互不影响                                   |
| 智能休眠策略？                 | 自适应           | 有数据 1ms，无数据 20ms，CPU < 5%                     |
| 批量处理优势？                 | 256条/批        | amortized O(1)，比逐条快 10 倍                      |
| 配置容错原则？                 | 宽松解析          | ERROR 跳过/WARN 修正，不因配置错误崩溃                     |
| SQLite 降级意义？            | 高可用           | MySQL 不可用时本地存储，保证数据不丢                         |
| 商用最大差距？                 | 多协议/热备/压缩     | 单协议/单机/无压缩                                    |
| writeRegister 参数设计？      | deviceId + serverAddr | TagInfo 区分 modbusDeviceId（设备ID）和 modbusServerAddr（从站地址） |
