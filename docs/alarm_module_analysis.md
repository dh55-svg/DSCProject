# 报警模块深度分析（ISA-18.2 实现详解）

> 适用项目：ChemDCS / MYDSCProject
> 设计标准：ANSI/ISA-18.2-2016 报警管理标准
> 成熟度：Level 1~4 全覆盖

---

## 一、ISA-18.2 标准概述

### 1.1 什么是 ISA-18.2？

ISA-18.2 是**工业报警管理系统**的国际标准，定义了报警系统的全生命周期：

```
     Rationalization(合理化)
         │
         ▼
  ┌──────────────┐     ┌──────────────┐
  │  Level 1     │     │  Level 2     │
  │  技术实现     │◀───▶│  合理化      │
  │  状态机      │     │  优先级/分类 │
  │  On-Delay    │     │  Shelving    │
  │  死区        │     │  Rationalization
  └──────────────┘     └──────────────┘
         │                      │
         ▼                      ▼
  ┌──────────────┐     ┌──────────────┐
  │  Level 3     │     │  Level 4     │
  │  KPI 监控    │     │  变更管理    │
  │  10min报警率  │     │  审计追踪   │
  │  陈旧报警    │     │  审批流程    │
  │  高峰统计    │     │  报告导出    │
  └──────────────┘     └──────────────┘
```

### 1.2 本项目的实现覆盖

| 成熟度            | 状态   | 关键文件                              |
| -------------- | ---- | --------------------------------- |
| Level 1 - 技术实现 | ✅ 完整 | `AlarmEngine.cpp` 状态机、On-Delay、死区 |
| Level 2 - 合理化  | ✅ 完整 | `AlarmEngine.cpp` Shelving、优先级、分类 |
| Level 3 - KPI  | ✅ 完整 | `AlarmKpiMonitor.cpp` 滑动窗口统计      |
| Level 4 - 变更管理 | ✅ 完整 | `AlarmChangeLog.cpp` 审计 + 审批      |

---

## 二、核心枚举定义（TagDef.h）

### 2.1 AlarmLimit — 报警限值等级

```cpp
enum class AlarmLimit : quint8 {
    Normal   = 0,   // 正常
    LowLow   = 1,   // 低低报（最严重，可能引发安全事故）
    Low      = 2,   // 低报
    High     = 3,   // 高报
    HighHigh = 4    // 高高报（最严重）
};
```

**面试考点**：

- `Normal` = 0 是为了 `memset` 清零时默认是正常状态
- 枚举值顺序从低到高排列，`HighHigh > LowLow` 在比较运算符下成立，用于**报警升级判断**
- 对比旧设计的 `AlarmState`（同一个枚举混用了状态和限值），现在拆分为 `AlarmLimit`（限值等级）和 `AlarmState`（状态机状态）两个枚举

### 2.2 AlarmPriority — 报警优先级

```cpp
enum class AlarmPriority : quint8 {
    Advisory  = 0,   // 通知性，无声音，仅画面提示
    Minor     = 1,   // 一般报警，低频声音
    Major     = 2,   // 重要报警，高频声音 + 确认后消音
    Critical  = 3    // 紧急报警，声光持续直到确认
};
```

**设计逻辑**：优先级决定三件事——声音文件选择、画面闪烁频率、OAP（Operator Action Required）响应时间。

### 2.3 AlarmClassification — 报警分类

```cpp
enum class AlarmClassification : quint8 {
    Process       = 0,  // 工艺过程偏离
    Safety        = 1,  // 安全系统触发
    Environmental = 2,  // 环保排放超标
    Quality       = 3,  // 质量参数偏离
    Machinery     = 4   // 设备故障
};
```

### 2.4 AlarmState — ISA-18.2 八状态机

```cpp
enum class AlarmState : quint8 {
    Normal                      = 0,  // 正常
    ActiveUnacknowledged        = 1,  // 活跃未确认
    ActiveAcknowledged          = 2,  // 活跃已确认
    ReturnToNormalUnacknowledged = 3, // 回正常未确认
    ReturnToNormalAcknowledged   = 4, // 回正常已确认（将从列表移除）
    Shelved                     = 5   // 已屏蔽
};
```

**状态迁移图**：

```
                   超限(on-delay)
     ┌─────────────────────────────────────────────┐
     │                                              │
     ▼                                              │
  ┌────────┐     超限确认    ┌──────────┐           │
  │ Normal │◀──────────────│  Normal   │           │
  └────────┘               └──────────┘           │
      │                        ▲                   │
      │ 值超限持续(on-delay)    │ 操作员确认恢复    │
      ▼                        │                   │
  ┌────────────────┐   Ack   ┌────────────────┐    │
  │ActiveUnack'ed  │───────▶│ ActiveAck'ed   │    │
  └───────┬────────┘        └───────┬────────┘    │
          │ 值回正常                 │ 值回正常     │
          ▼                         ▼              │
  ┌────────────────┐   Ack   ┌────────────────┐    │
  │ ReturnToNormal │◀───────│ ReturnToNormal │    │
  │ Unack'ed       │        │ Ack'ed         │────┘
  └────────────────┘        └────────────────┘
          │ 屏蔽
          ▼
  ┌────────────────┐
  │ Shelved        │────到期自动恢复或手动取消屏蔽──▶ Normal
  └────────────────┘
```

---

## 三、报警事件结构体（TagDef.h）

```cpp
struct AlarmEvent {
    QString   alarmId;               // 唯一ID: "ALM_20260425143025_0001"
    quint32   tagId;                 // 关联位号ID
    QString   tagName;               // 位号名: "TIC_101"

    // ISA-18.2 报警属性
    AlarmLimit     limit      = AlarmLimit::Normal;
    AlarmPriority  priority   = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;

    // 触发信息
    QString   description;           // "高高报报警，当前值=165.0℃，限值=160.0℃"
    float     triggerValue = 0.0f;
    float     thresholdValue = 0.0f;
    qint64    triggerTime = 0;

    // ISA-18.2 状态机
    AlarmState state = AlarmState::ActiveUnacknowledged;

    // 确认信息
    bool      acknowledged = false;
    qint64    acknowledgeTime = 0;

    // 恢复信息
    bool      active = true;
    qint64    returnToNormalTime = 0;
    qint64    returnAckTime = 0;
    float     returnValue = 0.0f;

    // Shelving
    bool      shelved = false;
    qint64    shelvedTime = 0;
    QString   shelveReason;
    int       shelveDurationSec = 0;
};
```

### 便捷判断方法

```cpp
bool isActive() const {
    return state == AlarmState::ActiveUnacknowledged
        || state == AlarmState::ActiveAcknowledged;
}
bool needsAttention() const {
    return state == AlarmState::ActiveUnacknowledged
        || state == AlarmState::ReturnToNormalUnacknowledged;
}
```

**面试考点**：为什么把 `active` 和 `acknowledged` 暴露为 bool 字段而不是只依赖 `state`？

> **答**：state 是状态机的严谨表示，bool 字段是为了 SQL 查询和 UI 过滤更高效。例如 `WHERE acknowledged=0` 比 `WHERE state IN (1,3)` 更直观。两者保持同步，state 是 truth，bool 字段是缓存。

---

## 四、AlarmEngine 核心引擎

### 文件位置

```
AlarmEngine.h     — 类声明（~180 行）
AlarmEngine.cpp   — 完整实现（~700 行）
AlarmKpiMonitor.h/cpp   — Level 3 KPI 监控（独立文件）
AlarmChangeLog.h/cpp     — Level 4 变更审计（独立文件）
```

### 4.1 架构：为什么要拆成三个独立文件？

**原型问题**：报警引擎共 ~1000 行代码，三个模块写在一个文件里，不利于维护。拆分后：

```
                     ┌──────────────────┐
                     │   AlarmEngine    │
                     │   (核心状态机)    │
                     └────┬──────┬─────┘
                          │      │
                    ┌─────┘      └─────┐
                    ▼                  ▼
            ┌──────────────┐  ┌──────────────┐
            │KpiMonitor    │  │ChangeLog     │
            │(滑动窗口统计) │  │(变更审计)    │
            │m_kpiMonitor  │  │m_changeLog   │
            └──────────────┘  └──────────────┘
```

**面试可能被问**："为什么 KPI Monitor 和 ChangeLog 用成员对象而不是指针？"

> **答**：它们是 AlarmEngine 生命周期强绑定的子系统，不存在独立创建/销毁的场景。成员对象避免了堆分配的开销和空指针检查，构造顺序由编译器保证（声明顺序），且 AlarmEngine 是它们的 friend，可以访问其私有构造/析构函数。

### 4.2 单例模式

```cpp
AlarmEngine& AlarmEngine::instance()
{
    static AlarmEngine instance;   // C++11 起线程安全的局部静态
    return instance;
}
```

**面试考点**：

- C++11 保证了局部 static 变量初始化是**线程安全的**（编译器自动加锁）
- 缺点：程序退出时销毁顺序不确定。如果其他静态对象的析构函数调用了 `instance()`，会创建已销毁的实例。解决方案：`AlarmEngine` 不持有其他单例的引用，或者用 `std::shared_ptr` + 自定义删除器

### 4.3 构造函数中的定时器初始化

```cpp
AlarmEngine::AlarmEngine()
{
    // On-Delay 检查定时器：每 500ms 检查一次
    m_onDelayTimer = new QTimer(this);
    m_onDelayTimer->setInterval(500);
    connect(m_onDelayTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        // 遍历 on-delay 条目，检查哪些已超时
        QList<quint32> toTrigger;
        for (auto it = m_onDelayEntries.begin(); it != m_onDelayEntries.end(); ++it) {
            if (it->elapsed.hasExpired(it->onDelayMs)) {
                toTrigger.append(it.key());
            }
        }
        lock.unlock();
        // 锁外触发，避免死锁
        for (quint32 tagId : toTrigger) { /* 触发 */ }
    });

    // Shelve 检查 + KPI 统计定时器：每 10 秒
    m_shelveCheckTimer = new QTimer(this);
    m_shelveCheckTimer->setInterval(10000);
    connect(m_shelveCheckTimer, &QTimer::timeout, this, [this]() {
        // 1. 检查哪些屏蔽已到期
        // 2. 统计 totalActive / staleCount / shelvedCount
        // 3. m_kpiMonitor.setExternalStats(...)
        // 4. 对到期的 shelve 调用 unshelveAlarm
    });
}
```

**设计要点**：

- 构造函数中创建定时器但**不启动**（在 `initialize()` 中启动），避免对象未完全构造时定时器触发
- on-delay 检查频率 500ms，比最小 on-delay（一般 3000ms）快 6 倍，精度足够
- shelve 检查频率 10s，因为 shelve 时长通常以分钟计，不需要精确到秒

### 4.4 initialize() 初始化

```cpp
void AlarmEngine::initialize()
{
    // 加载音效文件
    m_soundCritical = new QSoundEffect(this);
    m_soundMajor    = new QSoundEffect(this);
    m_soundMinor    = new QSoundEffect(this);
    // 文件不存在时静默失败 QSoundEffect 不会崩溃

    loadSound(m_soundCritical, "./sounds/alarm_critical.wav", 1.0f);
    loadSound(m_soundMajor,    "./sounds/alarm_high.wav",     0.8f);
    loadSound(m_soundMinor,    "./sounds/alarm_low.wav",      0.5f);

    m_onDelayTimer->start();
    m_shelveCheckTimer->start();
}
```

---

## 五、Level 1：技术实现（核心状态机）

### 5.1 triggerAlarm — 触发报警

```cpp
void AlarmEngine::triggerAlarm(
    quint32 tagId, AlarmLimit limit,
    float triggerValue, float thresholdValue,
    AlarmPriority priority, AlarmClassification classification,
    int onDelayMs)
```

**完整逻辑流**：

```
triggerAlarm() 被调用
    │
    ├─ 已屏蔽？ → 直接 return
    │
    ├─ 已有同 tag 报警？
    │   ├─ 限值更高 → 升级（escalate）
    │   ├─ 限值相同或更低 → 去重，return
    │   └─ RTN 状态 → 重新触发（re-alarm）
    │
    └─ 新报警 → 进入 On-Delay 队列
        ├─ 已有 on-delay 等待？
        │   ├─ 限值升级 → 更新等级
        │   └─ 值变化 → 更新值
        └─ 新建 on-delay 条目 → 等待定时器检查
```

**关键代码——报警升级**：

```cpp
if (activeIt != m_activeAlarms.end() && activeIt->isActive()) {
    if (limit > activeIt->limit) {
        // 升级逻辑：更新限值、重置确认状态
        activeIt->state = AlarmState::ActiveUnacknowledged;
        activeIt->acknowledged = false;
        activeIt->acknowledgeTime = 0;
        // 已确认的报警升级后需要重新确认！
    }
}
```

### 5.2 On-Delay 机制（防噪声尖峰）

On-Delay 是 ISA-18.2 要求的核心机制：值超限后必须**持续超限一定时间**才算触发报警，防止信号中的噪声尖峰误报。

```
   值
   │     ╱╲
   │    ╱  ╲     ╱╲
   │   ╱    ╲   ╱  ╲          ═══ 限值线
   │  ╱      ╲ ╱    ╲╱╲  ╱╲
   │ ╱        ╲          ╲╱  ╲╱
   └─────────────────────────────────── 时间
         ↑                                   ↑
    超限但<3s，不触发             超限持续>3s，才触发
```

**代码实现**：

```cpp
// 进入 On-Delay
OnDelayEntry entry;
entry.limit    = limit;
entry.onDelayMs = onDelayMs;  // 每个 tag 可单独配置
entry.elapsed.start();
m_onDelayEntries.insert(tagId, entry);

// 定时器检查（每 500ms）
for (auto it = m_onDelayEntries.begin(); it != m_onDelayEntries.end(); ++it) {
    if (it->elapsed.hasExpired(it->onDelayMs)) {
        toTrigger.append(it.key());  // on-delay 满足，触发
    }
}
```

**Bug 修复经验（面试加分）**：

```cpp
// 错误写法（原型代码中的 bug）：
it->elapsed.hasExpired(it->elapsed.elapsed());  // 永远返回 true！
// 等价于 hasExpired(elapsed())，而 elapsed() >= elapsed() 始终成立

// 正确写法：
it->elapsed.hasExpired(it->onDelayMs);  // 检查是否超过设定的延迟时长
```

### 5.3 确认操作

```cpp
void AlarmEngine::acknowledgeAlarm(const QString& alarmId)
{
    // ActiveUnacknowledged → ActiveAcknowledged
    it->state = AlarmState::ActiveAcknowledged;
    it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
    it->acknowledged = true;
    emit alarmAcknowledged(id);
}
```

支持三种确认方式：
| 方法 | 使用场景 |
|------|----------|
| `acknowledgeAlarm(id)` | 报警列表点选确认 |
| `acknowledgeAlarmByTagId(tagId)` | P&ID 图元点击确认 |
| `acknowledgeAll()` | "确认全部"按钮 |

### 5.4 clearAlarm — 值回正常

```cpp
void AlarmEngine::clearAlarm(quint32 tagId, float returnValue)
{
    m_onDelayEntries.remove(tagId);  // 取消 pending 的 on-delay

    // 状态迁移：Active → ReturnToNormalUnacknowledged
    it->state = AlarmState::ReturnToNormalUnacknowledged;
    it->returnToNormalTime = QDateTime::currentMSecsSinceEpoch();
    it->returnValue = returnValue;
    it->active = false;
}
```

**注意：** 值回正常不会自动关闭报警。操作员必须**确认恢复**（`acknowledgeReturnToNormal`）后报警才彻底关闭。这是 ISA-18.2 的核心要求——操作员必须知道"问题已经解决"。

### 5.5 恢复确认

```cpp
void AlarmEngine::acknowledgeReturnToNormal(const QString& alarmId)
{
    // ReturnToNormalUnacknowledged → 从 m_activeAlarms 移除
    it->state = AlarmState::ReturnToNormalAcknowledged;
    m_activeAlarms.erase(it);
    emit alarmReturnToNormalAcknowledged(id);
    emit alarmCleared(id);
}
```

---

## 六、Level 2：合理化

### 6.1 Shelving（屏蔽）

```cpp
void AlarmEngine::shelveAlarm(quint32 tagId, const QString& reason, int durationSec)
{
    it->shelved = true;
    it->shelveReason = reason;
    it->shelveDurationSec = durationSec;
    it->state = AlarmState::Shelved;

    if (durationSec > 0) {
        m_shelveDeadlines[tagId] = now + durationSec * 1000;
    }
    // durationSec == 0 表示永久屏蔽
}
```

**面试考点**：

- **Shelve 和 Disable 的区别**：Shelve 是临时的，到期自动恢复；Disable 是永久的，需要工程师手动恢复。本项目实现了 Shelve，没有实现 Disable（可以通过 durationSec=0 + 手动 unshelve 近似）
- **Shelving 必须记录原因**：ISA-18.2 要求每一次屏蔽必须记录原因，否则操作员滥用屏蔽功能会导致报警系统失效
- **屏蔽到期自动恢复**：每 10 秒检查 `m_shelveDeadlines`

### 6.2 Rationalization 记录

```cpp
struct AlarmRationalization {
    QString consequence;              // 后果："超温可能导致反应釜压力超限爆炸"
    QString operatorAction;           // 操作："关闭加热阀 HV-101，打开冷却水"
    int    expectedResponseTimeSec;   // 预期响应时间：300
    QString designPhilosophy;         // 设计理念："检测超温→限制温度→防止爆炸"
    QString approver;                 // 工艺工程师审批人
};
```

**Rationalization 的目的**：每一个报警必须有明确的因果逻辑——为什么报警、操作员应该怎么做、多长时间内必须响应。没有 Rationalization 的报警是"垃圾报警"，会导致操作员对报警麻木。

---

## 七、Level 3：KPI 监控

### 文件位置：`AlarmKpiMonitor.h / .cpp`

### 7.1 滑动窗口统计

```cpp
AlarmKpiSnapshot AlarmKpiMonitor::snapshot(int totalActive, int staleCount, int shelvedCount) const
{
    // 10 分钟报警数
    for (const auto& e : m_events) {
        if (e.timestamp >= tenMinAgo) count10++;
    }

    // 1 小时峰值（10 分钟滑动窗口）
    for (qint64 windowStart = oneHourAgo; windowStart <= now - 600; windowStart += 60) {
        int winCount = 0;
        for (const auto& e : m_events) {
            if (e.timestamp >= windowStart && e.timestamp < winEnd) winCount++;
        }
        peak = max(peak, winCount);
    }

    // Top5 最频繁报警
    QMap<QString, int> freqMap;
    for (const auto& e : m_events) {
        if (e.timestamp >= oneHourAgo && !e.tagName.isEmpty()) freqMap[e.tagName]++;
    }
    // 按频率排序，取前 5
}
```

### 7.2 ISA-18.2 推荐阈值

| KPI      | 推荐阈值       | 超标含义    |
| -------- | ---------- | ------- |
| 10 分钟报警率 | ≤ 10       | 报警风暴    |
| 平均报警率    | ≤ 2/操作员/小时 | 报警过载    |
| 高峰报警率    | ≤ 10       | 瞬态工况异常  |
| 陈旧报警     | < 5        | 操作员无视报警 |

### 7.3 外部统计注入

```cpp
// AlarmKpiMonitor 不持有报警状态机的引用（解耦设计）
// 由 AlarmEngine 每 10 秒通过 setExternalStats 注入：
void AlarmKpiMonitor::setExternalStats(int totalActive, int staleCount, int shelvedCount)
{
    m_externalTotalActive  = totalActive;
    m_externalStaleCount   = staleCount;
    m_externalShelvedCount = shelvedCount;
}
```

**解耦目的**：`AlarmKpiMonitor` 不知道报警状态机的存在，只负责统计。它通过 `AlarmEngine` 注入的外部数据补齐 stale/total/shelved 字段。这样两个模块可以独立修改和测试。

---

## 八、Level 4：变更管理与审计

### 文件位置：`AlarmChangeLog.h / .cpp`

### 8.1 变更记录

```cpp
void AlarmChangeLog::recordChange(const AlarmChangeRecord& record)
{
    rec.changeTime = QDateTime::currentMSecsSinceEpoch();
    m_records.prepend(rec);     // 最新在前
    if (m_records.size() > 10000) m_records.removeLast();
    emit changeRecorded(rec);
}
```

**ISA-18.2 要求**：任何报警参数（限值、优先级、死区、on-delay）的修改，必须记录：

- 谁改的（operatorName）
- 什么时候改的（changeTime）
- 改了什么（fieldName + oldValue + newValue）
- 为什么改（reason）
- 谁审批的（approver）

### 8.2 报警参数修改流程

```cpp
bool AlarmEngine::setAlarmLimit(quint32 tagId, const QString& fieldName,
                                 float newValue, const QString& operatorName,
                                 const QString& reason)
{
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    // 获取旧值
    QString oldValue;
    if (fieldName == "highHighLimit") oldValue = QString::number(tag.highHighLimit, 'f', 1);

    AlarmChangeRecord rec;
    rec.tagId = tagId;
    rec.fieldName = fieldName;
    rec.oldValue = oldValue;
    rec.newValue = QString::number(newValue, 'f', 1);
    rec.operatorName = operatorName;
    rec.reason = reason;

    m_changeLog.recordChange(rec);
    emit alarmParameterChanged(tagId, fieldName, rec.oldValue, rec.newValue);
}
```

### 8.3 审计报告导出

```cpp
QString AlarmChangeLog::generateAuditReport(const QDateTime& from, const QDateTime& to) const
{
    // 生成格式化的文本报告
    // "========================================"
    // "ISA-18.2 报警参数变更审计报告"
    // "期间: 2026-04-01 00:00 ~ 2026-04-25 23:59"
    // "========================================"
    // "[2026-04-25 14:30:25]"
    // "  操作人: 张三"
    // "  位号ID: 101"
    // "  修改字段: highLimit"
    // "  变更: 80.0 → 85.0"
    // "  原因: 避免频繁误报警，工艺评审通过"
    // "  审批: 李四"
}
```

---

## 九、线程安全分析

### 9.1 锁设计

```
AlarmEngine::m_mutex           — 保护 m_activeAlarms, m_onDelayEntries 等
AlarmKpiMonitor::m_mutex       — 保护 m_events（时间戳列表）
AlarmChangeLog::m_mutex        — 保护 m_records（变更记录列表）
```

### 9.2 锁使用原则

```cpp
// 原则1：在可能耗时的操作前释放锁
lock.unlock();
playAlarmSound(priority);  // 播放声音不在持锁期间
emit alarmTriggered(event);  // 信号发送不在持锁期间

// 原则2：不要在持锁期间调用可能反锁的函数
lock.unlock();
for (quint32 tagId : toTrigger) {
    QMutexLocker lock2(&m_mutex);  // 重新获取
    onOnDelayTimeout(...);
}

// 原则3：KPI monitor 的锁独立于 AlarmEngine
m_kpiMonitor.recordAlarm(tagName);  // KPI 内部有自己的锁
```

### 9.3 面试考点

**问**：为什么不用 `QReadWriteLock`？

> **答**：AlarmEngine 的读写比例大约是 1:1（触发和确认频繁），`QReadWriteLock` 在写操作时阻塞所有读操作，且有锁升级/降级开销。对于本项目的规模，`QMutex` 已经足够，且代码更简单。

**问**：KPI 监控器为什么有自己的锁而不是复用 AlarmEngine 的锁？

> **答**：解耦。KPI 监控器被拆分为独立类后，它的数据（事件时间戳列表）只被自己访问。如果复用 AlarmEngine 的锁，KPI 的统计操作会阻塞报警触发，反之亦然。各自持锁让两个子系统互不影响。

---

## 十、数据流 —— 从信号到报警事件

### 完整链路

```
┌─────────────────────────────────────────────────────────────┐
│ DataParseThread（解析线程）                                    │
│                                                             │
│  checkAlarmOptimized(tag, value) {                          │
│      // 1. 判断超限等级                                       │
│      if (value >= tag.highHighLimit) → AlarmLimit::HighHigh │
│      // 2. 死区检查（防抖动）                                  │
│      if (deadband check fails) → skip                        │
│      // 3. 更新 DoubleBuffer                                 │
│      snapshot.alarmstate = newLimit;                         │
│      // 4. 调用 AlarmEngine                                  │
│      AlarmEngine::instance().triggerAlarm(...)               │
│  }                                                           │
└───────────────────────────┬─────────────────────────────────┘
                            │ 跨线程信号槽
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ AlarmEngine（主线程）                                          │
│  1. On-Delay 等待（可配置 3 秒）                              │
│  2. 状态迁移 Normal → ActiveUnacknowledged                   │
│  3. KPI recordAlarm(tagName)                                │
│  4. 播放声音                                                  │
│  5. 发射信号: alarmTriggered(event)                          │
│  6. DatabaseManager::insertAlarmRecord(...)                  │
└───────────┬─────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│ MYDSCProject（UI 线程连接信号）                                │
│  - 报警列表闪烁                                                │
│  - 弹窗提示                                                   │
│  - 标题栏报警计数更新                                          │
│  - P&ID 图元变色                                              │
└─────────────────────────────────────────────────────────────┘
```

### 操作员确认链路（含身份绑定 — 不足6修复）

```
操作员点击"确认"按钮
    → MYDSCProject 调用 AlarmEngine::acknowledgeAlarm(alarmId, operatorName)
    → AuthManager::canOperate() 权限检查（Operator 以下拒绝）
    → 状态迁移: ActiveUnacknowledged → ActiveAcknowledged
    → 记录 acknowledgeUser = operatorName（身份绑定）
    → 停止声音
    → 画面闪烁变常亮
    → emit alarmAcknowledged(id)
    → 标题栏计数更新
    → DatabaseManager::updateAlarmAck(...) 持久化确认记录
```

---

## 十一、配置文件（TagInfo 的报警参数）

```cpp
struct TagInfo {
    // ISA-18.2 报警限值
    float highHighLimit = 90.0f;
    float highLimit     = 80.0f;
    float lowLimit      = 10.0f;
    float lowLowLimit   = 5.0f;

    // 偏差报警限值（商业化增强）
    float deviationLimit = 10.0f;       // 偏差限值（|PV-SP| > 此值触发）
    bool  deviationEnabled = false;

    // 变化率报警限值（商业化增强）
    float rateOfChangeLimit = 0.0f;     // 变化率限值（单位/秒）
    int   rateOfChangePeriodMs = 60000;
    bool  rateOfChangeEnabled = false;

    // 报警参数
    float        deadband     = 1.0f;    // 死区：值回正常需越过限值 ±deadband
    int          onDelayMs    = 3000;    // On-Delay 触发延时：3 秒
    int          offDelayMs   = 0;       // Off-Delay 恢复延时（0=立即恢复，商业化增强）
    AlarmPriority priority    = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;

    // Rationalization
    AlarmRationalization rationalization;
};
```

### Deadband 死区机制

```
    值
  85 ═══ HighLimit (80)
    │
  80 ─── 触发线
    │   ↑      ↑
    │   │      │
    │ 超限触发  死区内不算回正常
    │
  75 ─── 死区下限 (80 - deadband)
    │
    │
    └──────────────────────── 时间
```

**代码实现（DataParseThread.cpp）**：

```cpp
// 从超限回正常时，必须越过 deadband 才算真正回正常
// 例如：highLimit=80, deadband=5, 值降到 76 才算回正常
// 如果不设死区，值在 79-81 之间波动会频繁触发/清除报警
AlarmLimit oldLimit = tag.alarmLimit;
if (newLimit == AlarmLimit::Normal && oldLimit != AlarmLimit::Normal) {
    // 超限→正常，检查死区
    float db = tag.deadband;
    switch (oldLimit) {
    case AlarmLimit::High:
    case AlarmLimit::HighHigh:
        if (value > tag.highLimit - db) return; // 未离开死区，维持报警
        break;
    case AlarmLimit::Low:
    case AlarmLimit::LowLow:
        if (value < tag.lowLimit + db) return; // 未离开死区，维持报警
        break;
    }
}
```

---

## 十二、音频系统

### 初始化

```cpp
// AlarmEngine::initialize() 中加载音效（详见 4.4 节）
// 文件不存在时静默失败，不崩溃
```

### 优先级→声音映射

```cpp
void AlarmEngine::playAlarmSound(AlarmPriority priority)
{
    switch (priority) {
    case AlarmPriority::Critical: m_soundCritical->play(); break;
    case AlarmPriority::Major:    m_soundMajor->play();    break;
    case AlarmPriority::Minor:
    case AlarmPriority::Advisory: m_soundMinor->play();    break;
    }
}
```

---

## 十三、面试高频问题汇总

### Q1: ISA-18.2 的八状态机怎么实现的？

> 核心是 `AlarmState` 枚举（8个状态值）+ `AlarmEngine` 的状态迁移方法。`triggerAlarm()` 处理 Normal→ActiveUnack（含条件抑制检查、Chattering 保护、On-Delay），`acknowledgeAlarm()` 处理未确认→已确认（含操作员身份绑定和权限检查），`clearAlarm()` 处理 Active→RTN（含 Off-Delay 防恢复抖动），`acknowledgeReturnToNormal()` 处理 RTN→关闭。非活跃状态 Shelved(5)/SuppressedByDesign(6)/OutOfService(7) 各有独立的进入/退出方法。每个迁移方法都先获取互斥锁，检查当前状态是否合法，然后迁移并发射对应信号。

### Q2: On-Delay 怎么实现的？踩过什么坑？

> 每个报警在触发时不是立即激活，而是创建一个 `OnDelayEntry` 包含 `QElapsedTimer`，插入到 `m_onDelayEntries` 映射表中。一个每 500ms 的定时器遍历检查哪些条目已超过配置的延迟时间。踩过的坑：原来用的是 `hasExpired(elapsed())`，因为 `elapsed() >= elapsed()` 永远成立，实际没有延时效果，改为了 `hasExpired(onDelayMs)` 才修复。

### Q3: 报警风暴怎么处理？

> 五层防御（商业化增强）：1) On-Delay 过滤噪声尖峰；2) Off-Delay 防恢复抖动；3) Deadband 防止值在限值附近来回触发；4) Chattering Protection — 同一报警 1 分钟内重复≥3 次自动 Shelve；5) Flood Protection — 10 分钟内报警数 > 10（FLOOD_THRESHOLD）时自动抑制所有非 Critical 报警，泛滥结束后自动恢复原状态。KPI Monitor 同时记录泛滥事件用于事后分析。

### Q4: 线程安全怎么保证的？

> AlarmEngine 所有 public 方法都通过 `QMutexLocker` 加锁。三个子系统（AlarmEngine、KpiMonitor、ChangeLog）各自持有独立的 mutex，避免相互阻塞。耗时操作（播放声音、发射信号）在锁外执行。跨线程调用（DataParseThread→AlarmEngine）通过 Qt 信号槽自动排队。

### Q5: 和开源方案相比有什么不同？

> Alerta 的 ISA-18.2 实现有 9 个状态（含 Latch）。我们的系统实现了 8 状态机：5 个活跃状态（Normal/ActiveUnack/ActiveAck/RTNUnack/RTNAck）+ 3 个非活跃状态（Shelved/SuppressedByDesign/OutOfService）。商业化增强包括：条件抑制规则引擎（SuppressionRule, 根据关联位号状态动态抑制）、洪水保护自动抑制、确认身份绑定+权限检查、Off-Delay 恢复延时。关于 Latch 状态——它是为安全联锁场景设计的，我们的系统通过优先级体系（Critical 报警确认后仍高亮）实现类似效果。

### Q6: 为什么报警模块拆分成三个文件？

> 从单一职责角度，AlarmEngine 负责状态机，KpiMonitor 负责统计，ChangeLog 负责审计。拆分后每个文件 200-300 行，便于单独理解和测试。KpiMonitor 不持有 AlarmEngine 的引用（通过 `setExternalStats` 注入数据），两个模块可以独立修改。ChangeLog 甚至是一个完全独立的单例，可以在不启动 AlarmEngine 的情况下单独使用。

### Q7: 死区和 On-Delay 都防抖动，有什么区别？

> 它们是两个层面的防护。On-Delay 是**时间维度**的：值超限必须持续 N 毫秒才算触发，防止瞬时噪声尖峰。Deadband 是**幅度维度**的：值回正常时必须越过一个缓冲区，防止值在限值附近来回波动。两者配合使用。可以这样理解：On-Delay 防触发时的误报，Deadband 防恢复时的频繁切换。

---

## 十四、报警 ID 生成规则

### 14.1 格式定义

```
ALM_{yyyyMMddHHmmss}_{序号}
```

示例：`ALM_20260425143025_0001`

### 14.2 源码实现（AlarmEngine.cpp）

```cpp
QString AlarmEngine::generateAlarmId()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return QString("ALM_%1_%2")
        .arg(QDateTime::fromMSecsSinceEpoch(now).toString("yyyyMMddHHmmss"))
        .arg(++m_alarmCounter, 4, 10, QChar('0'));
}
```

### 14.3 设计要点

| 要素               | 说明                     |
| ---------------- | ---------------------- |
| 时间戳              | 精确到秒，便于日志排查和按时间排序      |
| 4 位序号            | 同一秒内最多 9999 个报警，实际远达不到 |
| `m_alarmCounter` | 原子递增计数器，保证唯一性          |
| 字符串格式            | 便于数据库存储和日志搜索           |

---

## 十五、报警参数动态修改（Level 2+4）

### 15.1 setAlarmLimit — 修改报警限值

```cpp
bool AlarmEngine::setAlarmLimit(quint32 tagId, const QString& fieldName,
                                 float newValue, const QString& operatorName,
                                 const QString& reason)
{
    // Step 1: 获取旧值
    QString oldValue;
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (fieldName == "highHighLimit") oldValue = QString::number(tag.highHighLimit, 'f', 1);
    else if (fieldName == "highLimit")    oldValue = QString::number(tag.highLimit, 'f', 1);
    else if (fieldName == "lowLimit")     oldValue = QString::number(tag.lowLimit, 'f', 1);
    else if (fieldName == "lowLowLimit")  oldValue = QString::number(tag.lowLowLimit, 'f', 1);
    else if (fieldName == "deadband")     oldValue = QString::number(tag.deadband, 'f', 1);
    else if (fieldName == "onDelayMs")    oldValue = QString::number(tag.onDelayMs);

    // Step 2: 创建变更记录（ISA-18.2 Level 4）
    AlarmChangeRecord rec;
    rec.tagId       = tagId;
    rec.fieldName   = fieldName;
    rec.oldValue    = oldValue;
    rec.newValue    = QString::number(newValue, 'f', 1);
    rec.operatorName = operatorName;
    rec.reason      = reason;

    // Step 3: 记录变更日志
    m_changeLog.recordChange(rec);

    // Step 4: 发射信号通知 UI 和其他模块
    emit alarmParameterChanged(tagId, fieldName, rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}
```

### 15.2 setAlarmPriority — 修改报警优先级

```cpp
bool AlarmEngine::setAlarmPriority(quint32 tagId, AlarmPriority newPriority,
                                    const QString& operatorName,
                                    const QString& reason)
{
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    QString oldVal = QString::number(static_cast<int>(tag.priority));

    AlarmChangeRecord rec;
    rec.tagId       = tagId;
    rec.fieldName   = "priority";
    rec.oldValue    = oldVal;
    rec.newValue    = QString::number(static_cast<int>(newPriority));
    rec.operatorName = operatorName;
    rec.reason      = reason;

    m_changeLog.recordChange(rec);
    emit alarmParameterChanged(tagId, "priority", rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}
```

### 15.3 可修改字段一览

| fieldName       | 含义    | 类型    | 示例        |
| --------------- | ----- | ----- | --------- |
| `highHighLimit` | 高高报限值 | float | 180.0     |
| `highLimit`     | 高报限值  | float | 150.0     |
| `lowLimit`      | 低报限值  | float | 20.0      |
| `lowLowLimit`   | 低低报限值 | float | 5.0       |
| `deadband`      | 死区    | float | 3.0       |
| `onDelayMs`     | 确认延时  | int   | 3000      |
| `priority`      | 优先级   | enum  | 2 (Major) |

### 15.4 ISA-18.2 变更管理流程

```
操作员发起修改请求
    │
    ├─ 1. 验证操作员权限（AuthManager::hasPermission(Engineer)）
    │
    ├─ 2. 记录变更日志（AlarmChangeLog::recordChange）
    │     ├─ 谁改的（operatorName）
    │     ├─ 改了什么（fieldName + oldValue → newValue）
    │     └─ 为什么改（reason，必填）
    │
    ├─ 3. 修改生效（更新 TagConfigMgr）
    │
    ├─ 4. 发射信号通知 UI
    │
    └─ 5. 等待审批（AlarmChangeLog::approve）
          ├─ 审批人确认（approver）
          └─ 审批通过后才永久生效
```

---

## 十六、UnshelveAlarm 恢复逻辑

### 16.1 完整实现（AlarmEngine.cpp）

```cpp
void AlarmEngine::unshelveAlarm(quint32 tagId)
{
    QMutexLocker lock(&m_mutex);
    m_shelveDeadlines.remove(tagId);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;
    if (!it->shelved) return;

    // 清除屏蔽标记
    it->shelved = false;
    it->shelvedTime = 0;
    it->shelveReason.clear();

    // 根据当前报警是否仍然活跃，恢复到对应状态
    if (it->isActive()) {
        it->state = AlarmState::ActiveUnacknowledged;
    } else {
        it->state = AlarmState::ReturnToNormalUnacknowledged;
    }

    lock.unlock();
    emit alarmUnshelved(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}
```

### 16.2 恢复状态选择

取消屏蔽时，报警恢复到哪个状态取决于**报警条件是否仍然存在**：

| 屏蔽时状态         | 取消屏蔽时条件 | 恢复后状态                          |
| ------------- | ------- | ------------------------------ |
| Active（值仍超限）  | 值仍超限    | `ActiveUnacknowledged`         |
| Active（值已回正常） | 值已回正常   | `ReturnToNormalUnacknowledged` |

**为什么取消屏蔽后总是 Unacknowledged？** 因为屏蔽期间操作员看不到这个报警，取消屏蔽后需要操作员重新确认，确保"看到了"。

---

## 十七、查询接口完整列表

### 17.1 活跃报警查询

```cpp
// 所有活跃报警（排除已屏蔽的）
QList<AlarmEvent> activeAlarms() const;

// 需要操作员关注的报警（ActiveUnack + RTNUnack）
QList<AlarmEvent> unacknowledgedAlarms() const;

// 按位号查报警
AlarmEvent alarmByTagId(quint32 tagId) const;

// 所有屏蔽中的报警
QList<AlarmEvent> shelvedAlarms() const;
```

### 17.2 历史查询

```cpp
// 报警历史（最多 limit 条，默认 1000）
QList<AlarmEvent> alarmHistory(int limit = 1000) const;
```

### 17.3 统计计数

```cpp
// 活跃报警总数（排除屏蔽）
int activeAlarmCount() const;

// 按限值等级计数
int activeAlarmCount(AlarmLimit limit) const;

// 按优先级计数
int activeAlarmCount(AlarmPriority priority) const;

// 未确认报警数
int unacknowledgedCount() const;
```

### 17.4 KPI 和变更日志访问

```cpp
// KPI 监控器（Level 3）
AlarmKpiMonitor* kpiMonitor();

// 变更日志（Level 4）
AlarmChangeLog* changeLog();
```

---

## 十八、音频控制

### 18.1 开关控制

```cpp
// 全局音频开关（操作员可静音）
void setSoundEnabled(bool enabled);
bool soundEnabled() const;
```

### 18.2 声音文件映射

| 优先级      | 文件路径                          | 音量         | 行为       |
| -------- | ----------------------------- | ---------- | -------- |
| Critical | `./sounds/alarm_critical.wav` | 1.0 (100%) | 持续播放直到确认 |
| Major    | `./sounds/alarm_high.wav`     | 0.8 (80%)  | 确认后消音    |
| Minor    | `./sounds/alarm_low.wav`      | 0.5 (50%)  | 低频提示     |
| Advisory | `./sounds/alarm_low.wav`      | 0.5 (50%)  | 同 Minor  |

### 18.3 容错设计

```cpp
auto loadSound = [](QSoundEffect* effect, const QString& path, float vol) {
    if (QFile::exists(path)) {
        effect->setSource(QUrl::fromLocalFile(path));
        effect->setVolume(vol);
        return true;
    }
    return false;  // 文件不存在时静默失败，不崩溃
};
```

**设计原则**：音频是辅助功能，缺失不应影响系统运行。DCS 系统的可靠性 > 声音提示。

---

## 十九、与 PerformanceMonitor 的集成

### 19.1 性能埋点

AlarmEngine 内部通过 `PERF_START` / `PERF_STOP` 宏记录关键操作耗时：

```cpp
void AlarmEngine::triggerAlarm(...)
{
    PERF_START("AlarmEngine::triggerAlarm");
    // ... 触发逻辑 ...
    PERF_STOP("AlarmEngine::triggerAlarm");
}
```

### 19.2 可监控的性能指标

| 指标                              | 含义         | 预期值     |
| ------------------------------- | ---------- | ------- |
| `AlarmEngine::triggerAlarm`     | 报警触发耗时     | < 1ms   |
| `AlarmEngine::acknowledgeAlarm` | 确认操作耗时     | < 0.5ms |
| `AlarmEngine::activeAlarmCount` | 计数查询耗时     | < 0.1ms |
| `AlarmKpiMonitor::snapshot`     | KPI 快照计算耗时 | < 5ms   |
| `AlarmChangeLog::recordChange`  | 变更记录写入耗时   | < 0.5ms |

---

## 二十、关键代码路径速查

| 功能          | 文件                  | 方法                                | 说明                      |
| ----------- | ------------------- | --------------------------------- | ----------------------- |
| 报警触发        | AlarmEngine.cpp     | `triggerAlarm()`                  | 含 On-Delay、条件抑制、去重、升级     |
| On-Delay 超时 | AlarmEngine.cpp     | `onOnDelayTimeout()`              | 实际创建 AlarmEvent         |
| Off-Delay 超时 | AlarmEngine.cpp     | `onOffDelayTimeout()`             | 确认恢复（商业化增强）            |
| 值回正常        | AlarmEngine.cpp     | `clearAlarm()`                    | Active → RTNUnack (含 Off-Delay) |
| 确认报警(新版)   | AlarmEngine.cpp     | `acknowledgeAlarm(id, operator)`  | 含身份绑定+权限检查（不足6修复）      |
| 恢复确认        | AlarmEngine.cpp     | `acknowledgeReturnToNormal()`     | RTNUnack → 关闭           |
| 屏蔽报警        | AlarmEngine.cpp     | `shelveAlarm()`                   | 设置到期时间                  |
| 取消屏蔽        | AlarmEngine.cpp     | `unshelveAlarm()`                 | 恢复到 Unack 状态            |
| 设计抑制        | AlarmEngine.cpp     | `suppressByDesign()`              | 需工程师审批（不足1部分修复）        |
| 设备停用        | AlarmEngine.cpp     | `setOutOfService()`               | 含工单号关联                 |
| 条件抑制规则      | AlarmEngine.cpp     | `addSuppressionRule()` / `evaluateSuppression()` | 不足1修复     |
| 洪水保护        | AlarmEngine.cpp     | `checkFloodCondition()`           | 自动抑制/恢复（不足3修复）         |
| 修改限值        | AlarmEngine.cpp     | `setAlarmLimit()`                 | 含变更记录                   |
| 修改优先级       | AlarmEngine.cpp     | `setAlarmPriority()`              | 含变更记录                   |
| KPI 统计      | AlarmKpiMonitor.cpp | `snapshot()`                      | 滑动窗口 + Top5             |
| KPI 持久化     | AlarmEngine.cpp     | shelveCheckTimer 回调              | 每5分钟 insertKpiSnapshot（不足7修复） |
| 变更记录        | AlarmChangeLog.cpp  | `recordChange()`                  | 审计日志                    |
| 审批变更        | AlarmChangeLog.cpp  | `approve()`                       | 审批流程                    |
| 审计报告        | AlarmChangeLog.cpp  | `generateAuditReport()`           | 文本格式导出                  |
| 报警声音        | AlarmEngine.cpp     | `playAlarmSound()`                | 优先级→声音映射                |
| 报警ID生成      | AlarmEngine.cpp     | `generateAlarmId()`               | ALM_时间戳_序号              |

---

## 廿一、信号系统完整设计（16 个信号）

### 21.1 全信号一览表

```cpp
// AlarmEngine.h — signals 区域
signals:
    // ===== Level 1: 基础信号 (5个) =====
    void alarmTriggered(const AlarmEvent& event);           // 报警触发
    void alarmAcknowledged(const QString& alarmId);          // 确认完成
    void alarmReturnToNormalAcknowledged(const QString&);   // 恢复确认
    void alarmCleared(const QString& alarmId);              // 从列表移除
    void alarmEscalated(quint32, AlarmLimit, AlarmLimit);    // 升级

    // ===== Level 2: 增强信号 (8个) =====
    void alarmShelved(quint32 tagId, const QString&, int);   // 屏蔽
    void alarmUnshelved(quint32 tagId);                      // 取消屏蔽
    void alarmSuppressed(quint32 tagId, const QString& reason);   // 设计抑制
    void alarmUnsuppressed(quint32 tagId);                        // 取消抑制
    void alarmOutOfService(quint32 tagId, const QString& reason); // 停用
    void alarmReturnedToService(quint32 tagId);                   // 恢复服务
    void alarmAnnotated(const QString& alarmId, const QString&);  // 操作员注释
    void chatteringAlarmDetected(quint32 tagId, int repeatCount); // 震荡检测

    // ===== Level 3: KPI 信号 (2个) =====
    void alarmCountChanged(int activeCount, int unackCount);  // 计数变化
    void alarmFloodDetected(const AlarmFloodEvent& floodEvent); // 洪水检测

    // ===== Level 4: 审计信号 (1个) =====
    void alarmParameterChanged(quint32, QString, QString, QString); // 参数变更
    void changeRecorded(const AlarmChangeRecord& record);     // 变更记录
```

**总计：16 个信号**，按 ISA-18.2 成熟度模型分层（商业化增强新增 Suppression/OOS/Annotation/Chattering/Flood 信号）。

### 21.2 信号的 UI 连接方式

```cpp
// MYDSCProject.cpp — 主窗口连接所有报警信号
void MYDSCProject::connectAlarmSignals()
{
    auto& engine = AlarmEngine::instance();

    // Level 1: 触发 → 弹窗+闪烁+声音
    connect(&engine, &AlarmEngine::alarmTriggered,
            this, &MYDSCProject::onAlarmTriggered);

    // Level 1: 确认 → 更新报警列表行样式
    connect(&engine, &AlarmEngine::alarmAcknowledged,
            this, &MYDSCProject::onAlarmAcknowledged);

    // Level 1: 清除 → 从表格移除行
    connect(&engine, &AlarmEngine::alarmCleared,
            this, &MYDSCProject::onAlarmCleared);

    // Level 3: 计数变化 → 标题栏显示 "(未确认: 5)"
    connect(&engine, &AlarmEngine::alarmCountChanged,
            this, &MYDSCProject::onAlarmCountChanged);

    // Level 4: 参数变更 → 刷新位号配置对话框
    connect(&engine, &AlarmEngine::alarmParameterChanged,
            this, &MYDSCProject::onAlarmParamChanged);
}
```

### 21.3 信号发射时机与锁策略

| 信号                  | 发射位置                                     | 是否在锁内    | 说明                |
| ------------------- | ---------------------------------------- | -------- | ----------------- |
| `alarmTriggered`    | `triggerAlarm()` / `onOnDelayTimeout()`  | **锁外发射** | 避免死锁              |
| `alarmAcknowledged` | `acknowledgeAlarm()`                     | **锁外发射** | UI 可能触发回调         |
| `alarmCleared`      | `acknowledgeReturnToNormal()`            | **锁外发射** | 同上                |
| `alarmEscalated`    | `triggerAlarm()`                         | **锁外发射** | 升级时同时发射 triggered |
| `alarmShelved`      | `shelveAlarm()`                          | **锁外发射** | —                 |
| `alarmUnshelved`    | `unshelveAlarm()`                        | **锁外发射** | —                 |
| `alarmCountChanged` | 所有状态变更方法                                 | **锁外发射** | 多处调用              |
| `changeRecorded`    | `setAlarmLimit()` / `setAlarmPriority()` | **锁外发射** | ChangeLog 内部也发射   |

**铁律：所有信号都在 `lock.unlock()` 之后发射！** 这是因为 Qt 的信号槽机制可能在同一线程内直接调用槽函数（DirectConnection），如果槽函数又调用了 AlarmEngine 的其他加锁方法，就会导致**递归锁死**。

### 21.4 信号参数设计原则

- **`alarmTriggered` 传递 `const AlarmEvent&`**：UI 层需要完整的报警信息来渲染弹窗（位号名、描述、优先级等），传引用避免拷贝
- **`alarmAcknowledged` 只传 `QString alarmId`**：确认操作只需要 ID 来定位列表中的行
- **`alarmCountChanged` 传两个 int**：标题栏只需显示数字，不需要完整数据
- **`alarmEscalated` 传三个参数**：tagId + oldLimit + newLimit，UI 可以高亮显示"升级"动画

---

## 廿二、内部数据结构完整剖析

### 22.1 m_activeAlarms — 活跃报警存储

```cpp
QHash<quint32, AlarmEvent> m_activeAlarms;  // 商业化优化：QMap → QHash
```

| 属性       | 值                                          | 设计理由                    |
| -------- | ------------------------------------------ | ----------------------- |
| 容器类型     | `QHash<quint32, AlarmEvent>`                | 按 tagId 哈希存储，O(1) 均摊查找 |
| Key 类型   | `quint32 tagId`                            | 同一位号同一时刻只有一个活跃报警        |
| Value 类型 | `AlarmEvent`（值类型）                          | QHash 的值是拷贝语义，修改时直接替换    |
| 最大容量     | 无硬限制                                       | 受内存限制，实际 < 500 个        |
| 生命周期     | 从 triggerAlarm 到 acknowledgeReturnToNormal | 中间经历多次状态迁移              |

**为什么改用 QHash？** 商业化优化：报警触发/确认是高频操作（每秒可达数十次），O(1) 均摊查找比 QMap 的 O(logN) 更优。UI 遍历时的排序由 `QList<AlarmEvent>` 查询结果接管（先 collect 再 sort），不再依赖存储容器的顺序。

### 22.2 m_alarmHistory — 历史记录

```cpp
QList<AlarmEvent> m_alarmHistory;  // 上限 5000 条
```

```cpp
// 插入逻辑（triggerAlarm 中）：
m_alarmHistory.prepend(event);       // 最新在前（prepend = O(1)）
if (m_alarmHistory.size() > 5000) {
    m_alarmHistory.removeLast();     // 移除最旧的（removeLast = O(1)）
}
```

| 属性   | 值                   | 设计理由                         |
| ---- | ------------------- | ---------------------------- |
| 容器类型 | `QList<AlarmEvent>` | prepend/removeLast 都是 O(1)   |
| 存储顺序 | 新→旧                 | `alarmHistory(10)` 返回最近 10 条 |
| 上限   | 5000 条              | 防止无限增长占用内存                   |
| 用途   | 报警历史查询、趋势分析         | 不替代数据库，只是内存缓存                |

**为什么上限是 5000？** 化工厂正常工况下每天约 100-300 条报警，5000 条覆盖约 2-3 周的历史。超过的部分从数据库查询。

### 22.3 m_onDelayEntries — On-Delay 跟踪

```cpp
struct OnDelayEntry {
    AlarmLimit limit;                // 当前最高限值等级
    float      value;                 // 最新采样值
    float      threshold;             // 限值
    AlarmPriority priority;           // 优先级
    AlarmClassification classification; // 分类
    int        onDelayMs = 3000;      // 配置的延迟时长
    QElapsedTimer elapsed;            // 启动计时器
};

QHash<quint32, OnDelayEntry> m_onDelayEntries;  // 商业化优化：QMap → QHash
```

**关键行为——值更新但不重置计时器：**

```cpp
// triggerAlarm 中：如果已有 on-delay 等待中
if (delayIt != m_onDelayEntries.end()) {
    if (limit > delayIt->limit) {
        delayIt->limit = limit;        // 升级限值
        delayIt->priority = priority;
        delayIt->classification = classification;
    }
    delayIt->value = value;            // 更新最新值
    return;                            // 注意：不重启 elapsed！
}
```

**为什么不重启计时器？** ISA-18.2 要求的是"持续超限 N 毫秒"，如果在等待期间值短暂回落再超限，应该从第一次超限开始算起，而不是重新计时。这避免了噪声导致的计时器反复重置。

### 22.4 m_shelveDeadlines — 屏蔽到期跟踪

```cpp
QHash<quint32, qint64> m_shelveDeadlines;  // tagId → 到期时间戳(ms)，QHash 优化
```

```cpp
// shelveAlarm 中设置：
if (durationSec > 0) {
    m_shelveDeadlines[tagId] = QDateTime::currentMSecsSinceEpoch()
                               + static_cast<qint64>(durationSec) * 1000;
} else {
    m_shelveDeadlines.remove(tagId);  // durationSec=0 表示永久屏蔽
}

// 定时器检查（每 10 秒）：
for (auto it = m_shelveDeadlines.begin(); it != m_shelveDeadlines.end(); ++it) {
    if (it.value() > 0 && now >= it.value()) {
        toUnshelve.append(it.key());
    }
}
```

**注意**：`durationSec=0` 时从 map 中移除该条目，表示永久屏蔽不会自动恢复。

### 22.5 m_offDelayEntries — Off-Delay 跟踪（商业化增强）

```cpp
struct OffDelayEntry {
    float      returnValue;     // 恢复时的值
    int        offDelayMs = 0;  // 配置的恢复延迟时长
    QElapsedTimer elapsed;      // 启动计时器
};

QHash<quint32, OffDelayEntry> m_offDelayEntries;
QTimer* m_offDelayTimer = nullptr;
```

**与 On-Delay 对称**：值回正常后不立即恢复，需持续正常 offDelayMs 毫秒。防止信号在限值附近抖动导致频繁报警/恢复。

### 22.6 m_floodSuppressedAlarms — 洪水自动抑制跟踪（商业化增强 — 不足3修复）

```cpp
QHash<quint32, AlarmState> m_floodSuppressedAlarms;  // tagId → 原状态
static constexpr int FLOOD_THRESHOLD = 10;           // 10分钟内报警数阈值
bool m_inFlood = false;                              // 是否处于泛滥状态
```

**工作原理**：进入泛滥状态时，遍历所有活跃报警，将非 Critical 报警的状态保存到此表，然后 Shelve 它们。泛滥结束后从此表恢复原始状态。

### 22.7 m_suppressionRules — 条件抑制规则存储（商业化增强 — 不足1修复）

```cpp
QVector<SuppressionRule> m_suppressionRules;  // 条件抑制规则列表
mutable QMutex m_suppressionMutex;           // 规则评估专用锁（独立于主锁）
```

**独立锁设计**：`m_suppressionMutex` 独立于 `m_mutex`，因为 `evaluateSuppression()` 在 `triggerAlarm()` 持主锁期间被调用，使用独立锁避免死锁。

### 22.8 m_chatteringState — 重复报警保护跟踪

```cpp
struct ChatteringState {
    int       count = 0;           // 1分钟内重复次数
    qint64    windowStart = 0;     // 窗口起始时间
    bool      autoShelved = false; // 是否已自动屏蔽
};
QHash<quint32, ChatteringState> m_chatteringState;
```

---

## 廿三、构造函数与析构函数细节（商业化增强版）

### 23.1 构造函数完整流程

```cpp
AlarmEngine::AlarmEngine()
{
    // Step 1: 创建 On-Delay 定时器（不启动！）
    m_onDelayTimer = new QTimer(this);
    m_onDelayTimer->setInterval(500);
    connect(m_onDelayTimer, &QTimer::timeout, this, [this]() {
        // ... 检查 on-delay 超时条目 ...
        // 锁内收集到期的 tagId 列表
        // 锁外逐一调用 onOnDelayTimeout()
    });

    // Step 2: 创建 Off-Delay 定时器（商业化增强）
    m_offDelayTimer = new QTimer(this);
    m_offDelayTimer->setInterval(500);
    connect(m_offDelayTimer, &QTimer::timeout, this, [this]() {
        // ... 检查 off-delay 超时条目 ...
        // 锁内收集到期的 tagId 列表
        // 锁外逐一调用 onOffDelayTimeout()
    });

    // Step 3: 创建 Shelve 检查 + 洪水保护 + KPI 持久化定时器
    m_shelveCheckTimer = new QTimer(this);
    m_shelveCheckTimer->setInterval(10000);
    connect(m_shelveCheckTimer, &QTimer::timeout, this, [this]() {
        // 1. 统计 totalActive/stale/shelved
        // 2. 推送外部统计到 KPI Monitor
        // 3. checkFloodCondition() — 洪水检测+自动抑制/恢复
        // 4. KPI 持久化 — 每 5 分钟保存快照到 DB
        // 5. 检查屏蔽到期 → 锁外逐一调用 unshelveAlarm()
    });

    // 注意：所有定时器已创建但未启动！
    // 必须等到 initialize() 才启动，确保对象完全构造
}
```

**为什么定时器不在构造函数中启动？**

- C++ 对象构造过程中，虚函数表可能未完全建立
- 如果定时器在构造过程中就触发回调，回调可能访问未初始化的成员
- `initialize()` 在所有对象创建完成后才被调用，保证安全

### 23.2 initialize() 完整流程

```cpp
void AlarmEngine::initialize()
{
    // Step 1: 加载音效文件（容错：不存在则静默失败）
    m_soundCritical = new QSoundEffect(this);
    m_soundMajor    = new QSoundEffect(this);
    m_soundMinor    = new QSoundEffect(this);

    auto loadSound = [](QSoundEffect* effect, const QString& path, float vol) {
        if (QFile::exists(path)) {
            effect->setSource(QUrl::fromLocalFile(path));
            effect->setVolume(vol);
            return true;
        }
        return false;
    };

    loadSound(m_soundCritical, "./sounds/alarm_critical.wav", 1.0f);
    loadSound(m_soundMajor,    "./sounds/alarm_high.wav",     0.8f);
    loadSound(m_soundMinor,    "./sounds/alarm_low.wav",      0.5f);

    // Step 2: 启动所有定时器（商业化增强：含 Off-Delay）
    m_onDelayTimer->start();
    m_offDelayTimer->start();
    m_shelveCheckTimer->start();

    LOG_INFO("AlarmEngine", "ISA-18.2 报警引擎初始化完成 (Level 1-4 + 商业化增强)");
}
```

### 23.3 析构函数

```cpp
AlarmEngine::~AlarmEngine()
{
    m_onDelayTimer->stop();     // 先停 On-Delay 定时器
    m_offDelayTimer->stop();    // 停 Off-Delay 定时器（商业化增强）
    m_shelveCheckTimer->stop(); // 停 Shelve 检查定时器
    // QTimer/QSoundEffect 是 QObject 子对象，由父对象自动 delete
}
```

**析构顺序重要吗？** 因为 QTimer 和 QSoundEffect 都通过 `new Xxx(this)` 创建为子对象，Qt 的 QObject 机制会自动按逆序析构子对象。显式 stop() 只是为了防止析构过程中定时器还在跑。

### 23.4 单例 + 禁止拷贝

```cpp
// 单例模式
static AlarmEngine& instance()
{
    static AlarmEngine instance;  // C++11 Magic Statics（线程安全）
    return instance;
}

// 禁止拷贝（防止单例被复制）
AlarmEngine(const AlarmEngine&) = delete;
AlarmEngine& operator=(const AlarmEngine&) = delete;
```

**C++11 Magic Statics 保证线程安全**：编译器自动在首次进入 `instance()` 时加锁初始化，之后不再加锁。比手动 double-check locking 更高效。

---

## 廿四、辅助方法详解

### 24.1 limitToString — 限值转中文

```cpp
QString AlarmEngine::limitToString(AlarmLimit limit) const
{
    switch (limit) {
    case AlarmLimit::HighHigh:     return "高高报";
    case AlarmLimit::High:         return "高报";
    case AlarmLimit::Low:          return "低报";
    case AlarmLimit::LowLow:       return "低低报";
    case AlarmLimit::Deviation:    return "偏差报警";
    case AlarmLimit::RateOfChange: return "变化率报警";
    default:                       return "未知";
    }
}
```

**用途**：构建 `description` 字段时使用，如 `"高高报报警，当前值=165.0℃，限值=160.0℃"`。

### 24.2 soundPathForPriority — 优先级→声音路径

```cpp
QString AlarmEngine::soundPathForPriority(AlarmPriority priority) const
{
    switch (priority) {
    case AlarmPriority::Critical: return "./sounds/alarm_critical.wav";
    case AlarmPriority::Major:    return "./sounds/alarm_high.wav";
    case AlarmPriority::Minor:
    case AlarmPriority::Advisory: return "./sounds/alarm_low.wav";
    default:                      return "";
    }
}
```

**注意**：Advisory 和 Minor 共用同一个声音文件（或无声音）。实际项目中 Advisory 通常不播放声音，只在画面上显示提示。

### 24.3 generateAlarmId — ID 生成

已在第十四章详述，补充一点：`m_alarmCounter` 是普通 `int`，不是原子变量。因为在单线程（主线程）环境中使用，不需要原子操作。但如果未来 AlarmEngine 移动到独立线程，需要改为 `std::atomic<int>`。

---

## 廿五、AlarmRationalization 完整字段说明

### 25.1 结构体定义（TagDef.h）

```cpp
struct AlarmRationalization {
    QString consequence;            // 后果："超温可能导致反应釜压力超限爆炸"
    QString operatorAction;         // 操作："关闭加热阀 HV-101，打开冷却水"
    int    expectedResponseTimeSec = 300; // 预期响应时间（秒）
    QString designPhilosophy;       // "检测超温 → 限制温度 → 防止爆炸"
    QString approver;               // 工艺工程师审批人
    qint64 approvedDate = 0;        // 审批日期
};
```

### 25.2 各字段用途

| 字段                        | 填写时机               | 使用者         | 说明            |
| ------------------------- | ------------------ | ----------- | ------------- |
| `consequence`             | Rationalization 阶段 | 操作员         | 回答"如果不处理会怎样？" |
| `operatorAction`          | Rationalization 阶段 | 操作员         | 回答"我该做什么？"    |
| `expectedResponseTimeSec` | Rationalization 阶段 | KPI Monitor | 超过此时间未响应视为陈旧  |
| `designPhilosophy`        | 设计阶段               | 工程师         | 记录设计意图，方便后续审查 |
| `approver`                | 审批时                | 审计系统        | 谁批准了这个报警的设计   |
| `approvedDate`            | 审批时                | 审计系统        | 何时批准的         |

### 25.3 当前实现状态

`AlarmRationalization` 结构体已在 TagDef.h 中定义，并已集成到 `TagInfo` 中（`AlarmRationalization rationalization;` 字段）。商业化扩展字段（`correctiveAction`, `relatedDocuments`, `area`, `zone`, `reviewCycleMonths`, `lastReviewDate`, `reviewer`, `isValid`）已定义完整，部分字段需要 UI 层配合填写。

---

## 廿六、AlarmKpiSnapshot 完整字段（商业化增强版）

```cpp
struct AlarmKpiSnapshot {
    qint64  timestamp = 0;           // 快照时间戳（用于 DB 持久化）
    // EEMUA 191 核心指标
    int     alarmCount10min = 0;      // 最近 10 分钟报警数
    float   avgPerHour = 0.0f;        // 平均每小时报警率
    int     peakCount10min = 0;       // 10 分钟滑动窗口峰值
    int     staleCount = 0;           // 陈旧报警数（>30min 未确认）
    int     totalActive = 0;          // 当前活跃报警总数
    int     shelvedCount = 0;         // 被屏蔽数
    int     suppressedCount = 0;      // 被抑制数
    // 商业化增强指标
    int     floodEventCount = 0;      // 报警泛滥事件次数
    float   floodDurationMin = 0.0f;  // 泛滥持续时间
    float   avgAckTimeSec = 0.0f;     // 平均确认时间
    int     chatteringCount = 0;      // 震荡报警数
    // 分布统计
    int     criticalCount = 0;        // Critical 报警数
    int     majorCount = 0;           // Major 报警数
    // Top-N + 健康度
    QStringList top5Frequent;         // 最频繁 5 个位号名
    float   systemHealthScore = 100.0f;  // 系统健康度 0-100
    QString healthGrade;                  // A/B/C/D/F
};
```

**数据流**：`AlarmKpiMonitor::snapshot()` → shelveCheckTimer 回调每 5 分钟 → `DatabaseManager::insertKpiSnapshot()` 持久化到 MySQL。UI 层通过 `kpiSnapshot()` 方法实时获取。

---

## 廿七、friend class 的作用

### 27.1 AlarmChangeLog 中的 friend 声明

```cpp
class AlarmChangeLog : public QObject {
    // ...
    friend class AlarmEngine;  // 允许 AlarmEngine 访问私有成员
private:
    mutable QMutex m_mutex;
    QVector<AlarmChangeRecord> m_records;
};
```

**为什么用 friend？** AlarmEngine 需要直接访问 ChangeLog 的私有成员吗？

实际上当前代码中 AlarmEngine 并没有直接访问 ChangeLog 的私有成员，而是通过公共接口 `recordChange()` 交互。`friend class` 声明是为了**预留未来可能的私有访问需求**（如批量操作时跳过 mutex 直接写入），属于防御性编程。

---

## 廿八、工业实战经验与不足分析

### 28.1 商用 DCS 报警系统的典型架构对比

```
┌─────────────────────────────────────────────────────────────────┐
│                    本项目架构（原型）                              │
│                                                                  │
│  DataParseThread ──▶ AlarmEngine ──▶ UI (QTableWidget)          │
│                     │                                           │
│                     ├─ AlarmKpiMonitor (内存统计)                │
│                     ├─ AlarmChangeLog (内存+JSON文件)            │
│                     └─ DatabaseManager (MySQL 写入)              │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                  商用 DCS 报警系统（如 Honeywell Experion）       │
│                                                                  │
│  DataAcquisition ──▶ Alarm Server ──▶ HMI Client                │
│                       │                                         │
│                       ├─ Alarm Historian (专用时序数据库)          │
│                       ├─ Audit Trail Server (独立审计服务)        │
│                       ├─ Alarm Rationalization Tool (配置工具)   │
│                       ├─ Advanced Alarm Management (AAM)         │
│                       │   ├─ 抑制逻辑（Suppression）              │
│                       │   ├─ 动态报警区域（Dynamic Areas）        │
│                       │   └─ 状态推断（State Inference）          │
│                       └─ 冗余热备（Active-Standby）              │
└─────────────────────────────────────────────────────────────────┘
```

### 28.2 本项目已有的优势（值得保留的设计）

| #   | 优势                           | 说明                                                   |
| --- | ---------------------------- | ---------------------------------------------------- |
| 1   | **八状态机严格遵循 ISA-18.2**        | Normal→ActiveUnack→ActiveAck→RTNUnack→关闭 + Shelved/SuppressedByDesign/OutOfService |
| 2   | **On-Delay + Off-Delay + Deadband 三层防抖** | 时间维度（触发/恢复）+ 幅度维度的三重保护                                |
| 3   | **信号全部在锁外发射**                | 避免了 Qt 信号槽中最常见的死锁陷阱                                  |
| 4   | **三模块解耦**                    | Engine/KpiMonitor/ChangeLog 各自独立可测                   |
| 5   | **变更审计全记录**                  | 谁/何时/改什么/为什么/谁审批，五要素齐全                               |
| 6   | **音频容错设计**                   | 文件缺失不崩溃，DCS 可靠性第一                                    |
| 7   | **商业化增强完整**                  | 条件抑制、洪水保护、身份绑定、KPI持久化、PBKDF2安全哈希                     |

### 28.3 当前实现的不足（商用化必须解决）

#### 不足 1：缺少报警抑制（Suppression）

**现状**：✅ **已修复** — 已实现条件抑制规则引擎。
**实现**：`SuppressionRule` 结构体（含 targetTagId/conditionTagId/conditionExpr/reason/enabled/createdBy/approver）+ `addSuppressionRule()` / `evaluateSuppression()` 方法。在 `triggerAlarm()` Step 4 中集成，通过 DoubleBuffer 读取条件位号值并解析条件表达式（==0, ==1, >N, <N）。
**工业场景**：当泵 P101 停运时，P101 出口流量低报 FIC_102 自动抑制。

#### 不足 2：缺少动态报警区域（Dynamic Alarm Areas）

**现状**：所有报警平铺在一个列表里。
**工业场景**：大型装置有 50+ 个操作单元，操作员只关注自己负责的区域。
**影响**：操作员被无关报警淹没，真正重要的报警被忽略。
**解决方案**：按区域/单元分组过滤，每个操作站只显示本区域的报警。

#### 不足 3：缺少报警洪水保护（Flood Protection）✅ 已修复

**现状**：✅ **已修复** — 已实现洪水保护自动抑制。
**实现**：`checkFloodCondition()` — 10分钟内报警数 > FLOOD_THRESHOLD(10) 时自动 Shelve 所有非 Critical 报警，保存原状态到 `m_floodSuppressedAlarms`。泛滥结束后自动恢复原状态。仅 Critical 优先级报警在泛滥期间不受影响。

#### 不足 4：缺少报警状态持久化

**现状**：`m_activeAlarms` 只在内存中，程序崩溃后丢失。
**工业场景**：DCS 系统 7×24 运行，偶尔需要重启升级。
**影响**：重启后所有活跃报警消失，操作员不知道之前有哪些未处理的报警。
**解决方案**：定期将 `m_activeAlarms` 快照写入数据库，启动时恢复。

#### 不足 5：缺少多客户端同步

**现状**：单实例设计，只有一个 UI 窗口。
**工业场景**：大型工厂有 5-20 个操作站（Operator Station）。
**影响**：A 操作站确认了报警，B 操作站还显示未确认。
**解决方案**：报警状态集中管理，所有客户端订阅状态变化通知（发布-订阅模式）。

#### 不足 6：缺少报警确认身份绑定 ✅ 已修复

**现状**：✅ **已修复** — 已实现确认身份绑定 + 权限检查。
**实现**：`acknowledgeAlarm(alarmId, operatorName)` 重载 — 调用前通过 `AuthManager::canOperate()` 检查权限（Operator 以下拒绝），确认时记录 `acknowledgeUser = operatorName` 到 AlarmEvent 中。旧版方法委托到新版（自动获取当前登录用户）。

#### 不足 7：KPI 数据不持久化 ✅ 已修复

**现状**：✅ **已修复** — 已实现 KPI 每 5 分钟自动持久化。
**实现**：在 shelveCheckTimer 回调中，每 300000ms（5分钟）自动调用 `DatabaseManager::insertKpiSnapshot(kpiSnap)` 将 KPI 快照写入 MySQL。DB 写入在锁外执行，不阻塞报警触发。支持长期趋势查询和 ISA-18.2 合规审计。

#### 不足 8：缺少报警回归测试工具

**现状**：修改报警逻辑后只能手动测试。
**工业场景**：每次修改都需要验证不影响现有功能。
**影响**：引入回归 Bug 的风险高。
**解决方案**：开发报警回放工具，加载历史数据文件，自动验证报警触发/确认/恢复序列是否符合预期。

### 28.4 性能瓶颈预判

| 场景          | 当前容量       | 商用要求    | 瓶颈点                             |
| ----------- | ---------- | ------- | ------------------------------- |
| 同时活跃报警数     | ~500（QHash）✅ | 5000+   | 已改为 QHash O(1) 均摊查找             |
| 报警历史        | 内存 5000 条  | 数据库百万条  | 内存缓存 + 数据库查询                  |
| On-Delay 条目 | ~100       | 1000+   | 500ms 定时器遍历全部条目，O(N)            |
| KPI 计算      | 2 小时事件窗口   | 7 天+    | `pruneOldEvents` 每次 O(N)，大数据量时慢 |
| 变更日志        | 内存 10000 条 | 数据库永久保存 | 应实时写入库                          |

### 28.5 安全性不足

| 项目    | 当前状态          | 商用要求                        |
| ----- | ------------- | --------------------------- |
| 密码存储  | PBKDF2-SHA256（10000轮+随机盐）✅ | bcrypt/argon2id + 迭代万次    |
| 操作员认证 | 用户名+密码        | 支持 LDAP/AD 集成 + 二因素认证       |
| 网络通信  | Modbus TCP 明文 | TLS 加密 + 证书双向认证             |
| 审计日志  | 可被删除          | 写入只读介质 + 数字签名               |
| 权限粒度  | 4 级硬编码        | RBAC + 细粒度位掩码（如"只能看不能改 SP"） |

### 28.6 可靠性不足

| 项目    | 当前状态   | 商用要求                   |
| ----- | ------ | ---------------------- |
| 单点故障  | 单进程    | 主备热切换 + 心跳检测           |
| 数据持久化 | 内存为主   | WAL 日志 + 定期 checkpoint |
| 异常恢复  | 重启即丢状态 | 断点续传 + 状态快照恢复          |
| 看门狗   | 无      | 进程守护 + 自动重启            |
| 日志轮转  | 无      | 按大小/日期自动分割 + 归档压缩      |

---

## 廿九、面试加分项速查表

| 问题                   | 关键词                    | 答案要点                                               |
| -------------------- | ---------------------- | -------------------------------------------------- |
| 为什么拆成三个文件？           | SRP                    | 状态机/KPI/审计各司其职，300-500 行/文件                        |
| On-Delay 怎么做的？       | QElapsedTimer          | 入队→定时器检查→hasExpired(onDelayMs)                     |
| Off-Delay 怎么做的？      | QElapsedTimer          | 与 On-Delay 对称：值回正常后持续正常 N ms 才确认恢复               |
| hasExpired 的 bug？    | elapsed() vs onDelayMs | 错误：hasExpired(elapsed()) 永远 true；正确：hasExpired(ms) |
| 为什么信号在锁外发射？          | 死锁预防                   | DirectConnection 下槽函数可能回调加锁方法                      |
| Shelving vs Suppression？ | 临时 vs 永久/操作员 vs 工程师    | Shelve 到期自动恢复；Suppression 需工程师审批                  |
| 为什么改用 QHash？         | O(1) 查找                | 报警触发/确认是高频操作，QHash 均摊 O(1) 优于 QMap O(logN)       |
| 单例线程安全？              | C++11 Magic Statics    | 编译器自动加锁，比 DCL 高效                                   |
| 报警去重怎么做？             | tagId 去重               | m_activeAlarms.find(tagId) 已存在则忽略/升级               |
| 条件抑制怎么实现？            | SuppressionRule        | DoubleBuffer 读取条件位号值 + 解析条件表达式(==0, >N, <N)      |
| 洪水保护怎么实现？            | FLOOD_THRESHOLD=10     | 泛滥时自动 Shelve 非 Critical 报警，结束后恢复原状态              |
| KPI 解耦怎么做的？          | 外部注入 + DB 持久化         | setExternalStats() 注入 + 每5分钟 insertKpiSnapshot    |
| 变更审计五要素？             | 5W                     | Who/When/What/Why/WhomApproved                     |
| 确认身份绑定？              | AuthManager + operatorName | acknowledgeAlarm() 前检查 canOperate()，记录 acknowledgeUser |
| 密码安全？                | PBKDF2-SHA256          | 10000轮迭代 + 16字节随机盐，格式: iterations$salt$hash       |
| ISA-18.2 四级都实现了吗？    | L1-L4 + 商业化增强           | L1 八状态机+Off-Delay / L2 抑制+OOS+条件抑制 / L3 KPI+洪水 / L4 审计   |
