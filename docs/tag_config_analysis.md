# 位号配置模块深度解析（TagConfigMgr 设计与数据流详解）

> 适用项目：ChemDCS / MYDSCProject  
> 核心模块：TagConfigMgr、TagDef、TagConfigDialog、ConfigManager  
> 涉及模块：DataParseThread、DoubleBuffer、TagConfigMgr、AlarmEngine  
> 设计标准：ISA-18.2（报警管理）、ISA-101（HMI 人机界面）

---

## 一、什么是"位号"（Tag）？

在 DCS 中，**位号（Tag）** 是每一个测量点或控制点的唯一标识。类似于数据库中的主键记录，或者面向对象中的"对象实例"。

**现实对应关系：**

| 位号名 | 真实设备 | 用途 |
|--------|----------|------|
| `TIC_101` | 1号反应釜热电偶 | 测量反应釜温度 |
| `PIC_101` | 1号反应釜压力变送器 | 测量反应釜压力 |
| `FV_101` | 进料调节阀 | 控制进料流量 |
| `PUMP_101` | 进料泵 | 启停状态监测 |
| `HS_101` | 紧急停车按钮 | 数字输入信号 |

**命名规范：** 行业内通常采用 `{仪表类型}_{工段/设备编号}` 格式：
- `TIC` = Temperature Indicator Controller（温度指示控制器）
- `PIC` = Pressure Indicator Controller（压力指示控制器）
- `FIC` = Flow Indicator Controller（流量指示控制器）
- `LT` = Level Transmitter（液位变送器）
- `FV` = Flow Valve（流量调节阀）
- `XV` = Shutoff Valve（切断阀）
- `HS` = Hand Switch（手动开关）

---

## 二、TagInfo 结构体详解（核心数据结构）

**文件：** TagDef.h:235-286

```cpp
struct TagInfo {
    quint32 tagId;           // 唯一ID（数据库主键 / Modbus 地址索引）
    QString tagName;         // 位号名： "TIC_101"
    QString description;     // 描述： "1号反应釜温度"
    QString unit;            // 工程单位： "℃"
    TagType tagType;         // AI/AO/DI/DO/PID 五种类型

    // 实时数据（运行期更新）
    float currentValue;      // 当前过程值（PV）
    float setPoint;          // 设定值（SP，PID 控制目标）
    float outputValue;       // 输出值（OP，阀门开度等）
    AlarmLimit alarmLimit;   // 当前超限等级
    DataQuality quality;     // 数据质量码
    qint64 timestamp;        // 数据时间戳

    // 量程
    float engHigh;           // 量程上限
    float engLow;            // 量程下限

    // ISA-18.2 报警限值（4 级）
    float highHighLimit;     // 高高报（HH）
    float highLimit;         // 高报（H）
    float lowLimit;          // 低报（L）
    float lowLowLimit;       // 低低报（LL）

    // ISA-18.2 报警参数
    float deadband;          // 滞环死区（防抖动）
    int onDelayMs;           // 确认延时（默认 3 秒）
    AlarmPriority priority;  // 报警优先级
    AlarmClassification classification; // 报警分类

    // Modbus 映射
    int modbusServerAddr;    // Modbus 服务器地址（设备地址）
    int modbusRegAddr;       // 寄存器地址
    int modbusRegCount;      // 寄存器数量

    // PID 参数
    float kp, ki, kd;        // PID 系数
    bool autoMode;           // 自动/手动模式

    // Rationalization 记录
    AlarmRationalization rationalization;
};
```

### 设计要点分析

**1. 为什么实时数据和静态配置放在一起？**

考：TagInfo 既包含量程/限值等静态属性，又包含 currentValue 等实时数据。为什么不拆成两个结构体？

答：TagInfo 是**完整描述**一个测点的数据结构。拆分会增加代码复杂度，而且大部分使用场景同时需要静态属性和实时数据。但实际运行时，**高频写入的实时值**通过 DoubleBuffer（只存 TagSnapshot，轻量级）传递，TagInfo 的实时字段仅在低频查询时使用。

**2. 五种位号类型（TagType）：**

| 类型 | 方向 | 典型例子 | 数据特征 |
|------|------|----------|----------|
| AI (Analog Input) | 采集 | 温度、压力、流量 | 连续值，需量程转换 |
| AO (Analog Output) | 输出 | 阀门开度 | 连续值，控制器输出 |
| DI (Digital Input) | 采集 | 泵运行、开关状态 | 0/1 离散值 |
| DO (Digital Output) | 输出 | 切断阀开关 | 0/1 离散值 |
| PID | 控制回路 | 温度 PID 调节 | 包含 PV/SP/OP 及 PID 参数 |

**3. 报警限值的层级关系：**

```
工程值
  ↑
  │  ════════════════ HH (高高报) ← 最严重，可能触发联锁
  │
  │  ════════════════ H  (高报)   ← 预警，需要关注
  │
  │  正常操作范围
  │
  │  ════════════════ L  (低报)   ← 预警
  │
  │  ════════════════ LL (低低报) ← 最严重
  ↓
```

**约束：** HH > H > 正常 > L > LL，这是 TagConfigDialog::validateForm() 中的校验逻辑。

---

## 三、TagConfigMgr 单例设计（配置管理中心）

**文件：** TagConfigMgr.h TagConfigMgr.cpp

### 3.1 整体职责

TagConfigMgr 只管理**位号的静态配置**（量程、报警限值、单位、Modbus 映射等），不存储实时数据。实时数据由 DoubleBuffer 负责。

### 3.2 架构分工

```
┌─────────────────────┐    ┌─────────────────────────┐
│   DoubleBuffer      │    │     TagConfigMgr         │
│   实时数据存储        │    │     静态配置管理          │
│   - value/sp/out    │    │     - 量程/单位           │
│   - quality         │    │     - 报警限值/死区       │
│   - 无锁 RCU 读取    │    │     - Modbus映射         │
└─────────────────────┘    │     - PID参数            │
       高频路径              │     - Rationalization   │
                            └─────────────────────────┘
                                    低频路径

使用场景：
- UI 显示：从 DoubleBuffer 读实时值，从 TagConfigMgr 读单位/量程
- 报警判断：从 DoubleBuffer 读当前值，从 TagConfigMgr 读报警限值
- 历史归档：从 DoubleBuffer 读实时值，从 TagConfigMgr 读位号信息
```

### 3.3 索引设计（三套索引）

```cpp
QHash<quint32, TagInfo> m_tags;              // 主索引：tagId → TagInfo
QHash<QString, quint32> m_nameIndex;         // 名称索引：tagName → tagId
QHash<quint32, quint32> m_modbusAddrIndex;   // Modbus 索引：(serverAddr<<16)|regAddr → tagId
QHash<int, QVector<quint32>> m_deviceIndex;  // 设备索引：deviceId → [tagId, ...]
```

为什么需要三套索引：

| 索引类型 | 使用场景 | 调用频率 |
|---------|----------|---------|
| tagId 主索引 | 绝大部分查询 | 毫秒级（UI 刷新） |
| tagName 名称索引 | 按名称查找（导入/配置） | 低频 |
| Modbus 地址索引 | DataParseThread 查找寄存器对应位号 | 高频（20ms 周期） |
| 设备索引 | 设备断线时批量标记 Bad | 事件触发 |

### 3.4 线程安全：QReadWriteLock

```cpp
mutable QReadWriteLock m_rwlock;
```

- **读操作**（getTag, getAlarmLimits, getRange 等）：使用 `QReadLocker`，多个读线程并发
- **写操作**（addTag, removeTag, updateAlarmLimits 等）：使用 `QWriteLocker`，写时互斥

为什么不用 QMutex：TagConfigMgr 的读操作远多于写操作（UI 每 100ms 读取全部位号，配置修改可能几分钟才一次）。QReadWriteLock 允许多个读线程同时进入，比 QMutex 效率高很多。

### 3.5 设备 ID 推导

```cpp
int deviceId = tag.tagId >> 24;
```

使用 tagId 的高 8 位作为设备 ID。例如：
- tagId = 0x0101 → 高 8 位 = 0x01 = 设备 1
- tagId = 0x0201 → 高 8 位 = 0x02 = 设备 2

这种编码方式：
- **优点：** 无需额外字段，索引简单
- **缺点：** 设备 ID 不能超过 255，单设备位号不能超过 16M（实际不可能达到）

---

## 四、JSON 配置持久化

### 4.1 配置文件格式

**文件：** config/tags.json

```json
[
  {
    "tagId": 101,
    "tagName": "TIC_101",
    "description": "1号反应釜温度",
    "unit": "℃",
    "tagType": "AI",
    "engHigh": 200.0,
    "engLow": 0.0,
    "highHighLimit": 180.0,
    "highLimit": 150.0,
    "lowLimit": 20.0,
    "lowLowLimit": 5.0,
    "deadband": 3.0,
    "modbusServerAddr": 1,
    "modbusRegAddr": 0,
    "modbusRegCount": 1
  }
]
```

### 4.2 加载流程

```
ConfigManager::loadTags()
  → TagConfigMgr::loadFromJson()
    → QFile::readAll()
    → QJsonDocument::fromJson()
    → TagConfigMgr::clear()          // 清空旧配置
    → TagConfigMgr::addTags()        // 批量添加
      → TagConfigMgr::addTag()       // 逐条添加，同时建立三套索引
```

**关键细节：** `clear()` 先清空旧配置再加载，确保配置一致性。`addTag()` 内部会检查 tagId 是否已存在，防止重复插入。

### 4.3 字段默认值处理

```cpp
tag.engHigh = static_cast<float>(obj["engHigh"].toDouble(100.0));
tag.highLimit = static_cast<float>(obj["highLimit"].toDouble(80.0));
```

使用 QJsonValue::toDouble(defaultValue) 提供默认值，保证 JSON 文件中缺失字段时不会得到 0，而是合理的工程默认值。

---

## 五、TagConfigDialog 界面设计

### 5.1 布局结构

```
┌──────────────────────────────────────────────────────┐
│  位号配置编辑器                                        │
├──────────────────┬───────────────────────────────────┤
│  位号列表         │  基本信息                          │
│  ┌──────────────┐│  ┌──────────────────────────────┐ │
│  │ ID  名称  描述 ││  │ 位号名称: [TIC_101] (只读)   │ │
│  │ 101 TIC_101 ..││  │ 描述:    [1号反应釜温度]      │ │
│  │ 102 PIC_101 ..││  │ 单位:    [℃]                │ │
│  │ 103 FIC_101 ..││  │ 类型:    [AI] (禁用)        │ │
│  │ 104 LT_101  ..││  └──────────────────────────────┘ │
│  │ ...           ││  量程与报警限值                    │
│  └──────────────┘│  ┌──────────────────────────────┐ │
│                   │  │ 量程下限: [0.0] 上限:[200.0] │ │
│                   │  │ 高高报: [180.0] (红色)       │ │
│                   │  │ 高报:   [150.0] (橙色)       │ │
│                   │  │ 低报:   [20.0]  (橙色)       │ │
│                   │  │ 低低报: [5.0]   (红色)       │ │
│                   │  │ 回差:   [3.0]                │ │
│                   │  └──────────────────────────────┘ │
│                   │  Modbus 映射                      │
│                   │  ┌──────────────────────────────┐ │
│                   │  │ 服务器地址: [1]               │ │
│                   │  │ 寄存器地址: [0]               │ │
│                   │  │ 寄存器数量: [1]               │ │
│                   │  └──────────────────────────────┘ │
│                   │        [应用到选中] [保存到文件]   │
├──────────────────┴───────────────────────────────────┤
│              [关闭]                                   │
└──────────────────────────────────────────────────────┘
```

### 5.2 交互逻辑

1. **左侧列表**：显示所有位号概要，点击某行 → 右侧表单显示该位号完整配置
2. **右侧表单**：编辑后点击"应用到选中位号" → 更新到 TagConfigMgr 内存
3. **保存到文件**：将所有位号配置写回 tags.json

### 5.3 校验逻辑

TagConfigDialog::validateForm() 做了两层校验：

```cpp
// 量程校验：下限必须小于上限
if (engLow >= engHigh) → 报错

// 报警限值校验：必须满足 HH > H > L > LL
if (hh <= h) → 报错  // 高高报必须大于高报
if (ll >= l) → 报错  // 低低报必须小于低报
```

**为什么需要校验：** 报警限值的大小关系直接决定了报警引擎能否正确工作。如果 HH < H，那值从低到高首先触发 HH（更高等级），逻辑就乱套了。

---

## 六、与其他模块的数据流交互

### 6.1 完整数据流图

```
┌──────────────────────────────────────────────────────────────────┐
│                    TagConfigMgr 交互全景图                         │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  读取: getRange/getAlarmLimits/getModbusMapping/getTag            │
│                                                                   │
│  ┌──────────────┐     tagId/TagInfo      ┌──────────────────┐    │
│  │ AlarmEngine  │◄───────────────────── │  TagConfigMgr    │    │
│  │ (报警引擎)    │    报警限值/死区/优先级   │  (静态配置中心)   │    │
│  └──────────────┘                        └────────┬─────────┘    │
│                                                    │              │
│                    ┌───────────────────────────────┼──────────┐   │
│                    │ 加载时读取                     │          │   │
│                    ▼                               ▼          │   │
│  ┌──────────────────────────┐    ┌──────────────────────────┐  │   │
│  │  DataParseThread         │    │  TagConfigDialog         │  │   │
│  │  (数据解析线程)            │    │  (配置编辑界面)           │  │   │
│  │   - 按Modbus地址找位号    │    │   - 显示位号信息          │  │   │
│  │   - 量程转换              │    │   - 编辑限值/参数         │  │   │
│  │   - 报警判断              │    │   - 保存到JSON           │  │   │
│  └──────────┬───────────────┘    └──────────────────────────┘  │   │
│             │                                                   │   │
│             ▼                                                   │   │
│  ┌──────────────────┐      ┌──────────────────────────────┐    │   │
│  │ DoubleBuffer     │─────▶│  UI (swap + readTag)       │    │   │
│  │ (RCU无锁双缓冲)   │      │  (直接读取，无桥接层)        │    │   │
│  └──────────────────┘      └──────────────────────────────┘    │   │
│         │                                                      │   │
│         ▼                                                      │   │
│  ┌──────────────────┐                                          │   │
│  │  UI 线程         │  从DoubleBuffer读实时值                    │   │
│  │  (数据显示)       │  从TagConfigMgr读量程/单位                │   │
│  └──────────────────┘                                          │   │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 6.2 高频路径 vs 低频路径

| 路径 | 频率 | 读取来源 | 写入来源 | 并发控制 |
|------|------|---------|---------|---------|
| 实时值更新 | 20ms~100ms | DoubleBuffer (无锁) | DataParseThread | 原子 shared_ptr |
| 配置读取 | 100ms~1s | TagConfigMgr | TagConfigDialog | QReadWriteLock |
| 报警判断 | 20ms | TagConfigMgr + DoubleBuffer | AlarmEngine | QReadWriteLock |
| 配置持久化 | 人工操作 | TagConfigMgr | TagConfigDialog | QWriteLocker |

### 6.3 DataParseThread 如何使用 TagConfigMgr

DataParseThread 在初始化时调用 `setTagConfig()`，将 TagInfo 数据复制到内部索引：

```cpp
void DataParseThread::setTagConfig(const QVector<TagInfo>& tags) {
    for (const auto& tag : tags) {
        quint64 key = (static_cast<quint64>(tag.modbusServerAddr) << 32) 
                     | static_cast<quint64>(tag.modbusRegAddr);
        m_tagByRegAddr.insert(key, tag);       // (serverAddr<<32 | regAddr) → TagInfo
        m_tagsByDevice[tag.modbusServerAddr].append(tag.tagId); // 设备分组
    }
}
```

**为什么不在解析线程运行时实时查 TagConfigMgr？**
- 性能考量：每次查 QHash 比查 QReadWriteLock 快得多
- 数据一致性：解析线程启动时加载一次，运行期间配置不变（配置修改极少，修改时重启解析线程）

### 6.4 Modbus 寄存器到工程值的转换

```cpp
float DataParseThread::registerToValue(quint16 rawValue, const TagInfo& tag) const {
    float range = tag.engHigh - tag.engLow;
    float value = tag.engLow + (static_cast<float>(rawValue) / 65535.0f) * range;
    return value;
}
```

算法说明：
- Modbus 寄存器是 16 位无符号整数，范围 0~65535
- 0 对应量程下限 (engLow)，65535 对应量程上限 (engHigh)
- 线性映射：`工程值 = engLow + (rawValue / 65535) × (engHigh - engLow)`

例如温度 TIC_101（量程 0~200℃），寄存器读到 32768：
```
工程值 = 0 + (32768 / 65535) × 200 = 100.0℃
```

### 6.5 报警判断时如何使用 TagInfo

```cpp
void DataParseThread::checkAlarmOptimized(const TagInfo& tag, float value) {
    // 使用 TagInfo 中的限值做判断
    if (value >= tag.highHighLimit) {
        newLimit = AlarmLimit::HighHigh;
    } else if (value >= tag.highLimit) {
        newLimit = AlarmLimit::High;
    } else if (value <= tag.lowLowLimit) {
        newLimit = AlarmLimit::LowLow;
    } else if (value <= tag.lowLimit) {
        newLimit = AlarmLimit::Low;
    }

    // 死区滞环处理：值必须越过死区才认为恢复正常
    if (oldLimit != AlarmLimit::Normal && newLimit == AlarmLimit::Normal) {
        if (oldLimit == AlarmLimit::High || oldLimit == AlarmLimit::HighHigh) {
            canClear = (value < tag.highLimit - tag.deadband);
        } else if (oldLimit == AlarmLimit::Low || oldLimit == AlarmLimit::LowLow) {
            canClear = (value > tag.lowLimit + tag.deadband);
        }
    }
}
```

**死区滞环（Deadband Hysteresis）图示：**

```
值回正常路径：
                            ┌───────────── highLimit + deadband
                            │  ← 死区，需穿越此区才恢复
  ────────────── highLimit ─┘
                ↑
     值上升触发高报         ← 值下降时不能直接恢复，必须低于 highLimit - deadband

  ────────────── highLimit - deadband ─┐
                                       │  ← 死区
                            ┌──────────┘
                            ← 这里才真正恢复 Normal
```

---

## 七、DoubleBuffer 直接访问（RealtimeDb 已移除）

**文件：** DataEngine.h / DoubleBuffer.h

RealtimeDb 作为早期的适配器/桥接层已完全移除。当前架构中各模块直接访问数据：

```cpp
// 静态配置查询 → 直接调用 TagConfigMgr
TagInfo tag = TagConfigMgr::instance().getTag(tagId);

// 实时值读取 → 直接从 DoubleBuffer 读快照
auto snapshot = m_doubleBuffer.readTag(tagId);
float pv = snapshot.currentValue;

// 实时值写入 → 直接写 DoubleBuffer
snapshot.setPoint = sp;
m_doubleBuffer.write(tagId, snapshot);
```

**为什么移除 RealtimeDb？**

1. `updateValue()` 从未被外部调用 — DataParseThread 直接写 DoubleBuffer
2. `registerCallback()` 是死代码 — UI 更新走的是 onDataUpdated() → swap() → readTag()
3. 所有"转发"都是薄层委托 — getTag() 直接调 TagConfigMgr，无额外逻辑
4. 去掉一层减少调用链长度，无性能损失，代码更直接

---

## 八、面试重难点解析

### Q1: 为什么 TagConfigMgr 设计为单例？

DCS 系统中所有模块（UI、报警、历史归档、Modbus 通信）都需要访问位号配置。单例确保：
1. 全局唯一：配置数据只有一份，不会出现不同模块看到不同配置的 bug
2. 统一生命周期：系统启动时加载，运行时查询，关闭时自动销毁
3. 单点控制：所有配置修改通过 TagConfigMgr 发射 configChanged 信号，各模块统一响应

### Q2: 位号配置变化时如何通知其他模块？

TagConfigMgr 发射信号：
```cpp
signals:
    void tagAdded(quint32 tagId);
    void tagRemoved(quint32 tagId);
    void configChanged(quint32 tagId);  // 限值/量程等修改
```

各模块按需连接：
- AlarmEngine：配置变化 → 重新计算报警状态
- DataParseThread：一般不实时监听（重启时重新加载）

### Q3: 实时数据和静态配置为什么分拆到 DoubleBuffer 和 TagConfigMgr？

面试考察：是否理解**频率解耦**和**读写分离**。

| | DoubleBuffer | TagConfigMgr |
|---|---|---|
| 数据类型 | 实时值 (变化极快) | 静态配置 (几乎不变) |
| 更新频率 | 20ms~100ms | 几分钟~几小时 |
| 读取频率 | 20ms (UI 刷新) | 100ms~1s |
| 写锁开销 | 无锁 (RCU) | QWriteLocker |
| 读锁开销 | 无锁 (原子指针) | QReadLocker |

如果把实时值放 TagConfigMgr，那么每 20ms 就需要获取写锁，而 UI、报警、归档都在读，QReadWriteLock 的写锁会阻塞所有读，导致 UI 卡顿。

### Q4: 量程转换公式 `value = engLow + (raw / 65535) × range` 有什么缺陷？

线性转换适用于大部分模拟量（4-20mA → 工程值），但以下场景不适用：
1. **非线性传感器**（如热电偶的 Seebeck 效应）：需要查表或多项式插值
2. **流量计的平方根特性**：差压式流量计 ΔP ∝ Q²，需要开平方
3. **DI/DO 数字量**：不用量程转换，直接 0/1 映射

项目中目前仅实现了线性转换，对 DI/DO 虽然不报错但没有实际意义。

### Q5: 位号配置支持哪些修改操作？

```
addTag()        — 添加新位号（检查 id 是否重复）
removeTag()     — 删除位号（同时清理三个索引）
updateAlarmLimits()  — 更新报警限值
updateRange()   — 更新量程
clear()         — 清空所有配置
loadFromJson()  — 从文件加载（先 clear 再 addTags）
saveToJson()    — 保存到文件
```

**为什么没有专门的 updateTagName()？**
tagName 被设计为不可变字段（创建时确定，运行期不允许改名），因为它是位号的"身份证号"。如果真要改名，应该先 removeTag() 再 addTag()。

### Q6: m_nameIndex 和 m_modbusAddrIndex 可能出现不一致吗？

在 addTag() 和 removeTag() 中是事务性的（写锁保护下同步更新三个索引）。但以下代码存在风险：

```cpp
TagConfigMgr::addTag(const TagInfo& tag) {
    m_tags.insert(tag.tagId, tag);
    m_nameIndex.insert(tag.tagName, tag.tagId);       // ①
    quint32 modbusKey = ...;
    m_modbusAddrIndex.insert(modbusKey, tag.tagId);   // ②
    m_deviceIndex[deviceId].append(tag.tagId);         // ③
}
```

如果 ① 成功但 ③ 异常（虽然实际不可能），索引会出现不一致。好在 QHash::insert 不会抛异常，实际安全。

### Q7: 系统启动时位号配置加载流程？

```
1. main() 启动
2. ConfigManager::initialize(engine, scene)
   ├── 确定配置目录 (./config)
   ├── ConfigManager::loadTags()
   │   ├── TagConfigMgr::loadFromJson("config/tags.json")
   │   └── TagConfigMgr::clear() + addTags()
   └── DataEngine 启动
       └── DataParseThread::setTagConfig(getAllTags())
           └── 建立 Modbus 地址→TagInfo 索引

3. DataParseThread::start()
   └── 开始从 LockFreeRingBuffer 读取 Modbus 数据
       根据 Modbus 地址查找对应 TagInfo
       执行量程转换 + 报警判断
```

### Q8: 如何添加一个新位号？

**方法一：通过配置文件**
1. 在 tags.json 中新增 JSON 对象（确保 tagId 不重复）
2. 重启系统（或调用 ConfigManager::loadTags() 重新加载）

**方法二：通过 UI（未来扩展）**
1. 打开位号配置对话框
2. 点击"新增位号"按钮
3. 填写位号信息（tagId 自动生成）
4. 编辑报警限值、Modbus 映射等
5. 点击"保存到文件"

### Q9: 项目中 17 个位号怎么分配的？

从 tags.json 可以看到：

| 设备 | 位号 | 类型 | 数量 |
|------|------|------|------|
| 设备1 (serverAddr=1) | TIC_101, PIC_101, FIC_101, LT_101, LT_102, TI_103 | AI | 6 |
| | FV_101, FV_102 | AO | 2 |
| | PUMP_101, PUMP_102, HS_101 | DI | 3 |
| | XV_101 | DO | 1 |
| 设备2 (serverAddr=2) | TIC_201, PIC_201, FIC_201, LT_201 | AI | 4 |

共 12 个 AI + 2 个 AO + 3 个 DI + 1 个 DO = 17 个位号，2 台 Modbus 设备。

### Q10: 位号 ID 编码规则有什么讲究？

```json
tagId = 101  → 设备1(Tag0x01) 的第01号点
tagId = 201  → 设备2(Tag0x02) 的第01号点
tagId = 112  → 设备1(Tag0x01) 的第12号点
```

设计初衷：
- **设备 ID = tagId >> 24**：取高 8 位
- **点位序号 = tagId & 0xFFFFFF**：取低 24 位

这个设计在小规模原型中够用（单设备最多 16M 个点），但商用系统中应该使用专门的 device_id 字段。

---

## 九、常见 Bug 与踩坑

### 1. getUxnit 拼写错误

**文件：** TagConfigMgr.h:61

```cpp
QString getUxnit(quint32 tagId) const;  // 应该是 getUnit
```

笔误拼写为 getUxnit，使用时需要保持一致。

### 2. updateAlarmLimits 不更新 tagName 索引

当前 updateAlarmLimits() 不会修改 tagName，所以不影响 m_nameIndex。但如果未来增加了 tagName 修改功能，记得同步更新 m_nameIndex。

### 3. loadFromJson 先 clear 再 addTags

```cpp
clear();      // 清空旧配置
addTags(tags); // 添加新配置
```

clear() 会清除所有索引，addTags 重新建立。如果先 addTags 再 clear，结果就是空配置。

### 4. 配置修改不通知 DataParseThread

当前 DataParseThread 只在初始化时调用 setTagConfig() 加载配置。如果在运行中修改了 TagConfigMgr 的配置（如修改报警限值），DataParseThread 内部的 m_tagByRegAddr 还是旧数据。需要重启解析线程才能生效。

这在实际运行中不是大问题，因为配置修改极少，而且一般修改会伴随系统确认。但商用版本需要增加配置热更新机制。

---

## 十、关键代码路径速查

| 功能 | 文件 | 行号 | 说明 |
|------|------|------|------|
| TagInfo 结构体 | TagDef.h | 235-286 | 核心数据结构 |
| 单例获取 | TagConfigMgr.cpp | 7-11 | static 局部变量 |
| 添加位号 | TagConfigMgr.cpp | 13-38 | 建立三套索引 |
| 按 Modbus 地址查找 | TagConfigMgr.cpp | 150-156 | 解析线程高频调用 |
| 量程转换 | DataParseThread.cpp | 139-145 | raw→工程值 |
| 报警判断 | DataParseThread.cpp | 163-224 | 含死区处理 |
| 加载 JSON | TagConfigMgr.cpp | 222-271 | 配置持久化 |
| 对话框校验 | TagConfigDialog.cpp | 233-257 | HH>H>L>LL |
| DoubleBuffer 直接访问 | DataEngine.cpp | 92-106 | 操作员下发直接写 DoubleBuffer |

---

## 十一、TagInfo 便捷访问方法

### 11.1 pv() / sp() / out() 快捷方法

TagInfo 提供了语义化的便捷访问方法，避免直接操作 `currentValue` / `setPoint` / `outputValue`：

```cpp
struct TagInfo {
    // 实时数据字段
    float currentValue = 0.0f;
    float setPoint = 0.0f;
    float outputValue = 0.0f;

    // 便捷访问方法
    float pv()  const { return currentValue; }   // Process Variable 过程变量
    float sp()  const { return setPoint; }        // Set Point 设定值
    float out() const { return outputValue; }     // Output 输出值

    // 报警状态快捷方法
    AlarmLimit alarm() const { return alarmLimit; }
    DataQuality qual() const { return quality; }
    bool isAlarm() const { return alarmLimit != AlarmLimit::Normal; }
    bool isGood() const { return quality == DataQuality::Good; }
};
```

### 11.2 为什么用 pv/sp/out 而不是 currentValue/setPoint？

| 写法 | 优点 | 缺点 |
|------|------|------|
| `currentValue` | 语义明确 | 名字太长，DCS 行业不常用 |
| `pv()` | DCS 行业标准术语 | 新手可能不理解 |
| `sp()` | PID 控制标准术语 | — |

**pv/sp/out 是 DCS/PID 控制领域的通用术语**，所有 DCS 工程师都熟悉。使用行业术语可以减少沟通成本。

### 11.3 使用示例

```cpp
// DataParseThread 中使用
void DataParseThread::processTag(const TagInfo& tag)
{
    // 使用便捷方法
    float pv = tag.pv();
    float sp = tag.sp();
    float out = tag.out();

    // 报警判断
    if (tag.isAlarm()) {
        AlarmEngine::instance().triggerAlarm(tag.tagId, tag.alarm(), pv, /* ... */);
    }

    // 数据质量检查
    if (!tag.isGood()) {
        LOG_WARN("DataParseThread", QString("位号 %1 数据质量差: %2")
                     .arg(tag.tagName)
                     .arg(static_cast<int>(tag.qual())));
    }
}
```

---

## 十二、AlarmRationalization 完整字段说明

### 12.1 ISA-18.2 报警合理化

AlarmRationalization 是 ISA-18.2 标准的核心概念，要求每个报警都经过"合理化"审查——确认这个报警是否真的需要、优先级是否合理、操作员应该采取什么行动。

### 12.2 TagInfo 中的 Rationalization 字段

```cpp
struct TagInfo {
    // ... 基本字段 ...

    // ISA-18.2 报警合理化字段
    AlarmPriority priority = AlarmPriority::Major;     // 报警优先级
    AlarmClassification classification = AlarmClassification::Process;  // 报警分类
    float deadband = 0.0f;           // 死区（防止限值附近抖动）
    int onDelayMs = 3000;            // 确认延时（毫秒，默认 3 秒）
    QString alarmResponse;           // 操作员响应动作描述
    QString alarmConsequence;        // 不响应的后果描述
};
```

### 12.3 字段详解

| 字段 | 类型 | 默认值 | 含义 | ISA-18.2 对应 |
|------|------|--------|------|--------------|
| `priority` | AlarmPriority | Major | 报警优先级 | Level 2: Alarm Rationalization |
| `classification` | AlarmClassification | Process | 报警分类 | Level 2: Alarm Classification |
| `deadband` | float | 0.0 | 死区范围 | Level 1: Deadband |
| `onDelayMs` | int | 3000 | 触发延时 | Level 1: On-Delay |
| `alarmResponse` | QString | "" | 操作员应采取的行动 | Level 2: Response Action |
| `alarmConsequence` | QString | "" | 不响应的后果 | Level 2: Consequence |

### 12.4 报警优先级（AlarmPriority）

```cpp
enum class AlarmPriority : quint8 {
    Critical = 0,   // 紧急：需要立即响应，否则可能造成人员伤亡或重大设备损坏
    Major    = 1,   // 重要：需要及时响应，否则可能影响生产
    Minor    = 2,   // 一般：需要关注，但可以稍后处理
    Advisory = 3    // 建议：信息性提示，不需要立即行动
};
```

**优先级分配原则（ISA-18.2 推荐比例）：**

| 优先级 | 占比 | 示例 |
|--------|------|------|
| Critical | < 5% | 反应釜温度高高报、压力安全阀开启 |
| Major | ~15% | 关键温度高报、流量低报 |
| Minor | ~30% | 一般温度高报、液位低报 |
| Advisory | ~50% | 泵运行状态变化、阀门位置变化 |

### 12.5 报警分类（AlarmClassification）

```cpp
enum class AlarmClassification : quint8 {
    Process   = 0,   // 工艺报警：温度/压力/流量超限
    Equipment = 1,   // 设备报警：泵故障、阀门卡涩
    Safety    = 2,   // 安全报警：安全联锁触发
    Environmental = 3  // 环保报警：排放超标
};
```

### 12.6 alarmResponse 和 alarmConsequence 示例

```json
{
    "tagName": "TIC_101",
    "description": "1号反应釜温度",
    "alarmResponse": "检查冷却水流量，必要时手动开大冷却水阀门FV_101",
    "alarmConsequence": "温度持续升高可能导致反应失控，引发安全阀起跳甚至爆炸"
}
```

**为什么需要这两个字段？** ISA-18.2 标准要求每个报警都必须有明确的操作员响应动作。如果操作员不知道该做什么，这个报警就是无效的。据统计，工业系统中约 50% 的报警操作员不知道该如何响应，这些报警应该被消除或重新设计。

---

## 十三、TagConfigMgr 索引机制详解

### 13.1 三套索引

TagConfigMgr 维护三套索引，支持不同维度的快速查找：

```cpp
class TagConfigMgr {
private:
    QHash<quint32, TagInfo> m_tags;              // 索引1: tagId → TagInfo
    QHash<QString, quint32> m_nameIndex;          // 索引2: tagName → tagId
    QHash<quint32, TagInfo*> m_tagByRegAddr;      // 索引3: modbusRegAddr → TagInfo*
};
```

### 13.2 索引使用场景

| 索引 | 查找方式 | 使用场景 | 调用频率 |
|------|---------|---------|---------|
| `m_tags` | `getTag(tagId)` | UI 显示、报警查询 | 中等 |
| `m_nameIndex` | `getTagByName(tagName)` | P&ID 图元绑定、配置对话框 | 低 |
| `m_tagByRegAddr` | `getTagByRegAddr(regAddr)` | DataParseThread 解析 | **极高**（每个数据包） |

### 13.3 m_tagByRegAddr 的构建

```cpp
void TagConfigMgr::addTag(const TagInfo& tag)
{
    m_tags.insert(tag.tagId, tag);
    m_nameIndex.insert(tag.tagName, tag.tagId);

    // 构建 Modbus 寄存器地址索引
    // key = (serverAddress << 16) | startAddress
    quint32 regKey = (tag.modbusServerAddr << 16) | tag.modbusRegAddr;
    m_tagByRegAddr.insert(regKey, &m_tags[tag.tagId]);
}
```

**为什么 key 用 `(serverAddr << 16) | regAddr`？** 因为不同从站可能有相同的寄存器地址。例如从站 1 的寄存器 0 和从站 2 的寄存器 0 是不同的点位。组合 key 确保唯一性。

### 13.4 DataParseThread 中的高频调用

```cpp
// DataParseThread::processRawData 中每条数据都会调用
TagInfo* tag = TagConfigMgr::instance().getTagByRegAddr(
    (raw.serverAddress << 16) | raw.startAddress);

if (tag) {
    // 寄存器值 → 工程值
    float engValue = registerToValue(raw.values[0], tag->engLow, tag->engHigh);

    // 更新 TagInfo
    tag->currentValue = engValue;
    tag->timestamp = raw.timestamp;
    tag->quality = DataQuality::Good;

    // 报警判断
    checkAlarmOptimized(tag, engValue);
}
```

**性能关键路径**：每个 Modbus 数据包（约 10-20 个寄存器）都会调用 `getTagByRegAddr`。QHash 的 O(1) 查找确保不会成为瓶颈。

---

## 十四、配置热更新机制（待实现）

### 14.1 当前问题

当前 DataParseThread 只在初始化时加载配置，运行中修改 TagConfigMgr 的配置不会自动同步到 DataParseThread。需要重启解析线程才能生效。

### 14.2 商用版本设计方案

```
操作员修改报警限值
    │
    ├─ 1. TagConfigMgr::updateAlarmLimits() 更新内存
    │
    ├─ 2. TagConfigMgr 发射 configChanged() 信号
    │
    ├─ 3. DataParseThread 接收信号
    │     ├─ 暂停处理（m_running = false）
    │     ├─ 重新调用 setTagConfig() 加载最新配置
    │     └─ 恢复处理（m_running = true）
    │
    └─ 4. AlarmEngine::setAlarmLimit() 记录变更日志
```

### 14.3 线程安全注意事项

热更新时需要注意：
1. DataParseThread 正在遍历 `m_tagByRegAddr` 时不能修改它
2. 需要用读写锁（QReadWriteLock）保护配置数据
3. 或者采用 Copy-on-Write 策略：创建新配置 → 原子替换指针

---

## 十五、关键代码路径速查（补充）

| 功能 | 文件 | 方法 | 说明 |
|------|------|------|------|
| TagInfo 便捷访问 | TagDef.h | `pv()` / `sp()` / `out()` | DCS 行业术语 |
| 报警状态检查 | TagDef.h | `isAlarm()` / `isGood()` | 便捷判断方法 |
| 优先级定义 | TagDef.h | `AlarmPriority` | 4 级优先级 |
| 分类定义 | TagDef.h | `AlarmClassification` | 4 类分类 |
| 死区处理 | DataParseThread.cpp | `checkAlarmOptimized()` | 含 deadband 判断 |
| On-Delay 处理 | AlarmEngine.cpp | `triggerAlarm()` | 延迟触发 |
| 合理化字段 | TagDef.h | `alarmResponse` / `alarmConsequence` | ISA-18.2 |
| 索引构建 | TagConfigMgr.cpp | `addTag()` | 三套索引 |
| 寄存器地址索引 | TagConfigMgr.cpp | `getTagByRegAddr()` | 高频调用 |
| 配置持久化 | TagConfigMgr.cpp | `saveToJson()` / `loadFromJson()` | JSON 格式 |
| 对话框校验 | TagConfigDialog.cpp | `validateInput()` | HH>H>L>LL |
| 报警限值修改 | TagConfigMgr.cpp | `updateAlarmLimits()` | 运行时修改 |
| 变更记录 | AlarmEngine.cpp | `setAlarmLimit()` | ISA-18.2 Level 4 |

---

## 十六、TagConfigMgr 完整接口一览（20 个方法）

### 16.1 接口分类表

```cpp
class TagConfigMgr : public QObject {
public:
    // ===== 单例 =====
    static TagConfigMgr& instance();

    // ===== 位号 CRUD（5个）=====
    bool addTag(const TagInfo& tag);              // 添加单个位号
    bool removeTag(quint32 tagId);                 // 移除位号
    void addTags(const QVector<TagInfo>& tags);    // 批量添加
    void clear();                                  // 清空全部

    // ===== 查询（6个）=====
    TagInfo getTag(quint32 tagId) const;           // 按 ID 查
    TagInfo getTagByName(const QString&) const;    // 按名称查
    QList<TagInfo> getAllTags() const;             // 获取全部
    QStringList getAllTagNames() const;            // 获取所有名称
    int tagCount() const;                          // 数量

    // ===== 便捷查询（4个）=====
    QPair<float, float> getRange(quint32) const;   // 量程
    QString getUxnit(quint32) const;               // 单位（⚠️拼写：Uxnit 非 Unit）
    AlarmLimits getAlarmLimits(quint32) const;     // 报警限值（结构体）
    ModbusMapping getModbusMapping(quint32) const; // Modbus 映射（结构体）
    quint32 findTagByModbusAddr(int, int) const;   // 反向查找：Modbus地址→tagId
    QVector<quint32> getTagsByDevice(int) const;   // 按设备分组

    // ===== 配置修改（2个）=====
    bool updateAlarmLimits(quint32, const AlarmLimits&);  // 更新报警限值
    bool updateRange(quint32, float, float);               // 更新量程

    // ===== 持久化（2个）=====
    bool loadFromJson(const QString& jsonPath);     // 从 JSON 加载
    bool saveToJson(const QString& jsonPath) const; // 保存到 JSON

signals:
    void tagAdded(quint32 tagId);
    void tagRemoved(quint32 tagId);
    void configChanged(quint32 tagId);
};
```

**总计：20 个公共方法 + 3 个信号**

### 16.2 各方法线程安全分析

| 方法 | 锁类型 | 耗时 | 调用频率 |
|------|--------|------|---------|
| `getTag()` | QReadLocker | O(1) | **极高**（UI 每 100ms） |
| `getAllTags()` | QReadLocker | O(N) | 中等 |
| `getRange()` | QReadLocker | O(1) | 高 |
| `getAlarmLimits()` | QReadLocker | O(1) | 高 |
| `findTagByModbusAddr()` | QReadLocker | O(1) | **极高**（解析线程每 20ms） |
| `addTag()` | QWriteLocker | O(1)×4 | 低（启动时一次性） |
| `removeTag()` | QWriteLocker | O(1)×4 | 极低 |
| `updateAlarmLimits()` | QWriteLocker | O(1) | 低 |
| `loadFromJson()` | QWriteLocker×2 | O(N) | 极低（启动时） |
| `saveToJson()` | QReadLocker | O(N) | 低（保存时） |

**关键发现**：`findTagByModbusAddr()` 是最高频的读操作（DataParseThread 每个周期都调用），使用 QReadLocker 保证多读者并发。

---

## 十七、四套索引机制深度对比

### 17.1 索引结构总览

```cpp
// 主索引
QHash<quint32, TagInfo> m_tags;                    // tagId → 完整信息

// 名称索引
QHash<QString, quint32> m_nameIndex;                // tagName → tagId

// Modbus 地址索引（注意 key 类型差异！）
QHash<quint32, quint32> m_modbusAddrIndex;          // (serverAddr<<16 \| regAddr) → tagId

// 设备索引
QHash<int, QVector<quint32>> m_deviceIndex;         // deviceId → [tagId列表]
```

### 17.2 ⚠️ 关键发现：Modbus 索引 key 不一致！

| 位置 | key 构造方式 | 位宽 | 最大覆盖范围 |
|------|------------|------|-------------|
| **TagConfigMgr** | `(serverAddr << 16) \| regAddr` | 32-bit | serverAddr: 0~255, regAddr: 0~65535 |
| **DataParseThread** | `(serverAddr << 32) \| regAddr` | 64-bit | serverAddr: 0~2^32, regAddr: 0~2^32 |

**这是一个潜在 Bug！**

- TagConfigMgr 用 16-bit 存 serverAddr，最大支持 255 个从站
- DataParseThread 用 32-bit 存 serverAddr，理论上支持更多
- 但两者 key 格式不同，如果 serverAddr > 255，两边算出的 key 会不一致 → **查找失败**

**实际影响**：当前项目 serverAddr 范围 1~247（Modbus 标准），16-bit 够用。但如果未来扩展到虚拟从站（>255），这个不一致会导致问题。

### 17.3 各索引维护时机

```
addTag() 时：
  m_tags.insert(tagId, tag)                    // 主索引
  m_nameIndex.insert(tagName, tagId)           // 名称索引
  m_modbusAddrInsert((sa<<16)|ra, tagId)       // Modbus 索引（⚠️ 16位）
  m_deviceIndex[deviceId].append(tagId)        // 设备索引

removeTag() 时：
  m_tags.erase(it)                             // 主索引
  m_nameIndex.remove(name)                     // 名称索引
  m_modbusAddrIndex.remove(key)                // Modbus 索引
  m_deviceIndex[deviceId].removeOne(tagId)     // 设备索引（空则删除整个 entry）

clear() 时：
  全部 .clear()
```

### 17.4 设备 ID 推导逻辑

```cpp
// TagConfigMgr::addTag() 和 removeTag() 中：
int deviceId = tag.tagId >> 24;
```

**编码规则**：tagId 的高 8 位作为设备 ID。

示例：
| tagId (hex) | 高 8 位 | 设备 ID | 含义 |
|------------|---------|---------|------|
| 0x01000001 | 0x01 | 1 | 设备 1 的第 1 个位号 |
| 0x01000102 | 0x01 | 1 | 设备 1 的第 258 个位号 |
| 0x02000001 | 0x02 | 2 | 设备 2 的第 1 个位号 |
| 0xFF000001 | 0xFF | 255 | 设备 255 的第 1 个位号 |

**限制**：设备 ID ≤ 255，单设备位号数 ≤ 16,777,215（24 bit）。工业现场完全够用。

---

## 十八、配置热更新机制与风险

### 18.1 当前热更新路径

```
操作员修改限值（TagConfigDialog）
  │
  ▼
TagConfigMgr::updateAlarmLimits(tagId, limits)
  │  ├─ QWriteLocker 加锁
  │  ├─ 直接修改 m_tags[tagId] 的字段
  │  └─ emit configChanged(tagId)
  │
  ▼
DataParseThread ??? 
  ⚠️ 问题：DataParseThread 在 setTagConfig() 时复制了一份 m_tagByRegAddr
      运行期间不会自动刷新！
```

### 18.2 ⚠️ 数据一致性问题

**核心矛盾**：

| 组件 | 数据来源 | 刷新时机 |
|------|---------|---------|
| TagConfigMgr | JSON 文件 / UI 编辑 | 随时可改 |
| DataParseThread::m_tagByRegAddr | `setTagConfig()` 传入 | 仅启动时一次 |
| DoubleBuffer | `processBatch()` 写入 | 每周期更新 |

**场景复现**：
1. 系统运行中，操作员通过 TagConfigDialog 将 TIC_101 的高报限值从 80 改为 90
2. `updateAlarmLimits()` 成功修改了 TagConfigMgr 内存中的数据
3. 但 DataParseThread 内部的 `m_tagByRegAddr` 仍然是旧值（高报 = 80）
4. 当温度达到 85℃ 时：
   - TagConfigMgr 认为不报警（85 < 90 新限值）
   - DataParseThread 认为应该报警（85 > 80 旧限值）
5. **结果：行为不确定，取决于哪边的数据被使用**

### 18.3 解决方案（三种）

**方案 A：禁止运行时修改**（最简单）
```cpp
bool TagConfigMgr::updateAlarmLimits(quint32 tagId, const AlarmLimits& limits)
{
    if (DataEngine::instance().isRunning()) {
        LOG_ERROR("TagConfigMgr", "系统运行中，禁止修改报警限值");
        return false;
    }
    // ... 正常修改
}
```

**方案 B：通知 DataParseThread 刷新**（推荐）
```cpp
// TagConfigMgr 修改后发射信号
emit configChanged(tagId);

// DataParseThread 连接信号并刷新内部缓存
connect(&TagConfigMgr::instance(), &TagConfigMgr::configChanged,
        this, [this](quint32 tagId) {
    auto tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId != 0) {
        quint64 key = (static_cast<quint64>(tag.modbusServerAddr) << 32)
                     | static_cast<quint64>(tag.modbusRegAddr);
        m_tagByRegAddr[key] = tag;  // 原子替换（单线程写入）
    }
});
```

**方案 C：Copy-on-Write + 原子指针替换**（最高级）
```cpp
// DataParseThread 持有一个原子指针
std::atomic<QHash<quint64, TagInfo>*> m_configSnapshot;

// TagConfigMgr 修改后构建新快照
auto* newConfig = new QHash<quint64, TagInfo>(oldConfig);
(*newConfig)[key] = updatedTag;
m_configSnapshot.store(newConfig, std::memory_order_release);
// DataParseThread 下一个周期自动使用新配置
// 旧配置由 GC 在安全时机释放
```

---

## 十九、JSON 配置格式完整说明

### 19.1 完整字段清单

```json
{
  "tagId": 101,
  "tagName": "TIC_101",
  "description": "1号反应釜温度",
  "unit": "℃",
  "tagType": "AI",
  "engHigh": 200.0,
  "engLow": 0.0,
  "highHighLimit": 180.0,
  "highLimit": 150.0,
  "lowLimit": 20.0,
  "lowLowLimit": 5.0,
  "deadband": 3.0,
  "modbusServerAddr": 1,
  "modbusRegAddr": 0,
  "modbusRegCount": 1
}
```

### 19.2 字段加载默认值对照表

| 字段 | 类型 | 必填 | 默认值 | DataEngine 加载 | TagConfigMgr 加载 |
|------|------|------|--------|-----------------|-------------------|
| tagId | int | ✅ 是 | — | 直接取值 | 直接取值 |
| tagName | string | ✅ 是 | — | 空→跳过(ERROR) | 无检查 |
| description | string | 否 | "" | — | — |
| unit | string | 否 | "" | — | — |
| tagType | string | 否 | "AI" | 未知→WARN+默认AI | 直接转 int |
| engHigh | double | 否 | 100.0 | — | 100.0 |
| engLow | double | 否 | 0.0 | — | 0.0 |
| highHighLimit | double | 否 | 90.0 | — | 90.0 |
| highLimit | double | 否 | 80.0 | — | 80.0 |
| lowLimit | double | 否 | 10.0 | — | 10.0 |
| lowLowLimit | double | 否 | 5.0 | — | 5.0 |
| deadband | double | 否 | 1.0 | — | 1.0 |
| modbusServerAddr | int | 否 | 1 | 超范围→qBound修正 | 1 |
| modbusRegAddr | int | 否 | 0 | 超范围→qBound修正 | 0 |
| modbusRegCount | int | 否 | 1 | 超范围→qBound修正 | 1 |

**发现差异**：DataEngine::loadTagConfig() 有完整的验证和容错逻辑；TagConfigMgr::loadFromJson() 几乎无验证。建议统一使用 DataEngine 的加载器。

### 19.3 saveToJson 输出格式

```cpp
// 使用 Indented 缩进格式（人类可读）
file.write(doc.toJson(QJsonDocument::Indented));
```

输出示例：
```json
[
  {
    "tagId": 101,
    "tagName": "TIC_101",
    "description": "1号反应釜温度",
    "unit": "℃",
    "tagType": 0,
    "engHigh": 200,
    "engLow": 0,
    ...
  }
]
```

**注意**：`tagType` 保存的是整数值（0=AI, 1=AO...），不是字符串。这与 loadTagConfig 中的字符串解析不同——**读写不对称**。

---

## 二十、TagDef.h 完整枚举与结构体速查

### 20.1 枚举汇总（6 个）

| 枚举名 | 值数量 | 用途 | 定义位置 |
|--------|-------|------|---------|
| `DataQuality` | 3 | 数据质量码（Good/Bad/Uncertain） | IEC 标准 |
| `AlarmLimit` | 5 | 报警限值等级（Normal/LL/L/H/HH） | ISA-18.2 |
| `AlarmPriority` | 4 | 报警优先级（Advisory/Minor/Major/Critical） | ISA-18.2 |
| `AlarmClassification` | 5 | 报警分类（Process/Safety/Enviro/Quality/Machinery） | ISA-18.2 |
| `TagType` | 5 | 位号类型（AI/AO/DI/DO/PID） | DCS 基础 |
| `AlarmState` | 6 | 报警状态机（5状态+Shelved） | ISA-18.2 |

### 20.2 结构体汇总（5 个）

| 结构体 | 大小估算 | 核心字段 | 用途 |
|--------|---------|---------|------|
| `TagInfo` | ~300B | 30+ 字段 | **最核心**，每个测点一个实例 |
| `AlarmEvent` | ~200B | alarmId/tagId/state/times | 报警事件完整记录 |
| `AlarmRationalization` | ~150B | consequence/action/responseTime | 合理化记录（预留） |
| `AlarmChangeRecord` | ~100B | who/when/what/why | 变更审计记录 |
| `AlarmKpiSnapshot` | ~100B | 10minRate/staleCount/peak | KPI 快照 |

### 20.3 TagInfo 便捷方法

```cpp
struct TagInfo {
    // DCS 行业术语便捷访问
    float pv()      const { return currentValue; }   // 过程值 (Process Value)
    float sp()      const { return setPoint; }        // 设定值 (Set Point)
    float out()     const { return outputValue; }     // 输出值 (Output)
    DataQuality qual() const { return quality; }       // 质量码
    AlarmLimit alarm()  const { return alarmLimit; }   // 当前报警等级

    // 状态判断便捷方法
    bool isActive() const { ... }   // 是否活跃报警
    bool needsAttention() const { ... }  // 是否需要关注（未确认）
};
```

**为什么用缩写？** PV/SP/OUT 是 DCS 行业通用术语，来自 PID 控制理论。使用缩写使代码更接近工艺语言，方便仪表工程师阅读。

---

## 廿一、工业实战经验与不足分析

### 21.1 商用 DCS 位号管理架构对比

```
┌──────────────────────────────────────────────────────────────┐
│                  本项目架构（原型）                            │
│                                                              │
│  tags.json ──▶ TagConfigMgr(QHash) ──▶ DataParseThread(副本) │
│                   │                        │                 │
│                   ▼                        ▼                 │
│              TagConfigMgr(静态配置)   DoubleBuffer(实时值)    │
│                                                              │
│  位号数量：~500                                               │
│  配置方式：手工编辑 JSON                                       │
│  版本控制：无                                                 │
│  热更新：有但存在一致性问题                                     │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│            商用 DCS（如 Honeywell / Yokogawa / 浙大中控）       │
│                                                              │
│  工程数据库 ──▶ 组态软件 ──▶ 编译下载 ──▶ 控制器/操作站       │
│  (SQL Server)   (图形化)   (校验+加密)                         │
│       │                                                        │
│       ├─ 版本管理（Git/SVN 集成）                               │
│       ├─ 变更审批流程（电子签名）                                │
│       ├─ 影响分析（修改某位号会影响哪些画面/脚本）               │
│       └─ 回滚能力（一键恢复到上一版本）                          │
│                                                              │
│  位号数量：10,000~100,000                                      │
│  配置方式：图形化组态工具                                        │
│  热下载：支持（在线修改部分参数）                                 │
└──────────────────────────────────────────────────────────────┘
```

### 21.2 本项目已有的优势

| # | 优势 | 说明 |
|---|------|------|
| 1 | **四套索引** | tagId/名称/Modbus地址/设备 四维查找，各 O(1) |
| 2 | **读写锁分离** | 读多写少场景下 QReadWriteLock 比 QMutex 效率高 3-5 倍 |
| 3 | **JSON 人可读** | 配置文件可用文本编辑器直接修改，无需专用工具 |
| 4 | **容错加载** | 缺失字段有合理默认值，不因小错崩溃 |
| 5 | **便捷方法** | pv()/sp()/out() 符合 DCS 行业习惯 |
| 6 | **信号通知** | 配置变更后发射信号，UI 可实时响应 |

### 21.3 当前实现的不足

#### 不足 1：无版本控制集成

**现状**：tags.json 是一个普通文件，修改后无法追溯历史。
**工业场景**：每次修改位号配置都需要记录谁、什么时候、改了什么。
**影响**：误修改后无法回滚；事故调查时无法还原当时的配置。
**解决方案**：
- 每次 saveToJson() 自动备份为 `tags_YYYYMMDD_HHMMSS.json.bak`
- 或者集成 Git 版本控制，每次保存自动 commit
- 商用系统使用专门的工程数据库 + 变更记录表

#### 不足 2：无影响分析

**现状**：修改一个位号的限值，不知道哪些画面、哪些脚本会受影响。
**工业场景**：大型装置有上千个画面，一个位号可能被几十处引用。
**影响**：修改后可能遗漏某些关联位置，导致显示不一致。
**解决方案**：
```sql
-- 引用关系表
CREATE TABLE tag_references (
    tag_id INT NOT NULL,
    reference_type ENUM('DISPLAY', 'SCRIPT', 'ALARM', 'TREND', 'REPORT'),
    reference_location VARCHAR(255),  -- 如 "MainScreen.qml:Line 42"
    last_scanned BIGINT
);
-- 修改前先查：SELECT * FROM tag_references WHERE tag_id = 101;
```

#### 不足 3：无双语种支持

**现状**：description、unit 等字段只有一种语言。
**工业场景**：中外合资工厂需要中英文切换；出口项目需要多语言。
**影响**：海外部署时需要手动翻译所有配置。
**解决方案**：
```json
{
  "tagName": "TIC_101",
  "description": {"zh": "1号反应釜温度", "en": "Reactor #1 Temperature"},
  "unit": {"zh": "℃", "en": "°C"}
}
```

#### 不足 4：无位号导入导出工具

**现状**：只能整体加载/保存 JSON 文件。
**工业场景**：需要从 Excel 批量导入位号（仪表工程师习惯用 Excel 管理位号表）；需要导出部分位号给第三方系统。
**影响**：500 个位号手工输入 JSON 易出错且效率低。
**解决方案**：
- 开发 Excel 导入功能（.xlsx → JSON）
- 开发按区域/设备筛选导出功能
- 支持 CSV 格式（Excel 兼容性最好）

#### 不足 5：无配置校验向导

**现状**：DataEngine::loadTagConfig() 有验证但只在日志中报告。
**工业场景**：组态工程师需要一个可视化的校验报告。
**影响**：警告信息淹没在大量日志中，容易被忽略。
**解决方案**：
```cpp
struct ValidationResult {
    int totalTags = 0;
    int errorCount = 0;
    int warningCount = 0;
    QVector<Issue> issues;  // {severity, tagId, message, suggestion}
};

ValidationResult validateConfig(const QString& jsonPath);
// 返回结构化结果，UI 可以展示为表格/图表
```

#### 不足 6：getUxnit 拼写错误

**现状**：方法名为 `getUxnit()`，正确应为 `getUnit()`。
**影响**：代码可读性差，新人容易拼错。
**修复**：重命名方法并保留旧名作为 deprecated alias。

#### 不足 7：无位号分组/层级

**现状**：所有位号平铺在一个 QHash 中。
**工业场景**：大型装置按"车间→单元→设备→测点"层级组织位号。
**影响**：500 个位号在列表中查找困难，无法按区域过滤显示。
**解决方案**：
```json
{
  "tagId": 101,
  "tagName": "TIC_101",
  "path": "/一车间/反应工段/1号反应釜/温度",
  "area": "反应工段",
  "unit": "1号反应釜"
}
```

### 21.4 性能瓶颈预判

| 场景 | 当前容量 | 商用要求 | 瓶颈点 |
|------|---------|---------|--------|
| 位号总数 | ~500 | 100,000+ | QHash 在 10 万级别仍高效，但内存 ~300MB |
| 按名称查找 | O(1) | O(1) | OK |
| Modbus 地址反向查找 | O(1) | O(1) | OK，但需修复 key 一致性问题 |
| getAllTags() | O(N) 拷贝 | 分页查询 | 大数据量时应返回迭代器而非拷贝 |
| 配置文件大小 | ~50KB | ~50MB | 需要增量加载或数据库存储 |
| 热更新延迟 | 即时（但有 bug） | < 100ms | 需实现方案 B 或 C |

### 21.5 安全性不足

| 项目 | 当前状态 | 商用要求 |
|------|---------|---------|
| 配置修改权限 | 任何用户可调 API | 需要 Engineer 以上权限 |
| 配置文件保护 | 明文 JSON | 加密存储 + 传输 |
| 修改审计 | LOG_INFO | 写入不可篡改的审计表 |
| 导出控制 | 无限制 | 需审批才能导出（防知识产权泄露）|

### 21.6 可靠性不足

| 项目 | 当前状态 | 商用要求 |
|------|---------|---------|
| 配置备份 | 无自动备份 | 每次修改自动备份 + 定期快照 |
| 一致性检查 | 无 | 启动时校验所有引用关系完整性 |
| 坏配置恢复 | 手动编辑 JSON | 一键回滚到上一个已知良好版本 |
| 并发修改 | QWriteLocker 串行化 | 支持多人同时编辑不同区域（乐观锁）|

---

## 廿二、面试加分项速查表

| 问题 | 关键词 | 答案要点 |
|------|--------|---------|
| 为什么四套索引？ | 多维查找 | tagId/名称/Modbus/设备 各有使用场景 |
| QReadWriteLock vs QMutex？ | 读多写少 | 允许多读者并发，写者独占 |
| 设备 ID 怎么推导？ | tagId >> 24 | 高 8 位编码，最多 255 台设备 |
| 热更新有什么问题？ | 数据一致性 | DataParseThread 内部副本不自动刷新 |
| 三种解决方案？ | 禁止/通知/COW | 禁止最简/COW 最优 |
| Modbus 索引 key 差异？ | 16 vs 32 bit | TagConfigMgr 用 16，DataParseThread 用 32 |
| JSON 读写不对称？ | tagType | 加载是字符串，保存是整数 |
| 容错策略？ | ERROR/WARN | 缺失关键字段跳过，数值不合理修正 |
| 商用差距？ | 版本/影响/多语 | 无版本控制/无影响分析/无双语 |
| 便捷方法意义？ | DCS 术语 | pv/sp/out 对应过程值/设定值/输出值 |
| ⚠️ 拼写错误？ | Uxnit | 应为 Unit，遗留 typo |
