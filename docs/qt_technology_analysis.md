# Qt 技术详解与代码实操（MYDSCProject）

> 适用项目：ChemDCS / MYDSCProject  
> Qt 版本：Qt 6.5.3（MSVC 2022）  
> 构建工具：Visual Studio 2022 + Qt VS Tools（v142 平台工具集）  
> 模块清单：core; gui; network; widgets; multimedia; printsupport; serialbus; serialport; sql

---

## 目录

1. [QObject 与元对象系统](#一qobject-与元对象系统)
2. [信号与槽（Signal & Slot）](#二信号与槽signal--slot)
3. [QThread 多线程编程](#三qthread-多线程编程)
4. [QTimer 定时器](#四qtimer-定时器)
5. [QMutex / QReadWriteLock 线程同步](#五qmutex--qreadwritelock-线程同步)
6. [QAtomicInt 原子操作](#六qatomicint-原子操作)
7. [QJsonDocument / QJsonObject 配置持久化](#七qjsondocument--qjsonobject-配置持久化)
8. [QSqlDatabase / QSqlQuery 数据库](#八qsqldatabase--qsqlquery-数据库)
9. [QSoundEffect 音频播放](#九qsoundeffect-音频播放)
10. [QGraphicsView 架构（P&ID 画面）](#十qgraphicsview-架构pid-画面)
11. [QCustomPlot 趋势图](#十一qcustomplot-趋势图)
12. [QMetaObject::invokeMethod 跨线程调用](#十二qmetaobjectinvokemethod-跨线程调用)
13. [MOC 元对象编译器工作原理](#十三moc-元对象编译器工作原理)
14. [QSplashScreen 启动画面](#十四qsplashscreen-启动画面)
15. [Qt 容器选型指南](#十五qt-容器选型指南)

---

## 一、QObject 与元对象系统

### 1.1 什么是元对象系统？

Qt 的元对象系统（Meta-Object System）是 Qt 的核心机制，提供了**信号与槽**、**运行时类型信息**、**动态属性**等能力。实现依赖三个要素：

1. **QObject 基类**：所有需要使用元对象功能的类必须继承自 QObject
2. **Q_OBJECT 宏**：在类声明中必须包含，启用元对象特性
3. **MOC（Meta-Object Compiler）**：预处理头文件，生成额外的 C++ 代码

### 1.2 项目中的应用

本项目共有 **28 个类**继承 QObject，覆盖所有核心模块：

```cpp
// 主窗口
class MYDSCProject : public QMainWindow { Q_OBJECT };

// 数据引擎
class DataEngine : public QObject { Q_OBJECT };

// 报警引擎
class AlarmEngine : public QObject { Q_OBJECT };

// 权限管理
class AuthManager : public QObject { Q_OBJECT };

// 数据库
class DatabaseManager : public QObject { Q_OBJECT };

// Modbus 通信
class ModbusManager : public QObject { Q_OBJECT };
class ModbusComm : public QObject { Q_OBJECT };

// 自定义线程（QThread 本身也继承 QObject）
class DataParseThread : public QThread { Q_OBJECT };
class HistoryArchiveThread : public QThread { Q_OBJECT };

// P&ID 图元
class BaseGraphicsItem : public QGraphicsObject { Q_OBJECT };
class TankItem : public BaseGraphicsItem { Q_OBJECT };
class ValveItem : public BaseGraphicsItem { Q_OBJECT };
class PumpItem : public BaseGraphicsItem { Q_OBJECT };
class PipeItem : public BaseGraphicsItem { Q_OBJECT };
class DataLabelItem : public BaseGraphicsItem { Q_OBJECT };
```

### 1.3 Q_OBJECT 宏的作用

```cpp
class AlarmEngine : public QObject {
    Q_OBJECT  // ← 这个宏是关键
public:
    // ...
signals:
    void alarmTriggered(const AlarmEvent& event);
    void alarmAcknowledged(const QString& alarmId);
    // ...
};
```

`Q_OBJECT` 宏展开后：

- 声明 `static const QMetaObject staticMetaObject;` — 类的元对象
- 声明 `virtual const QMetaObject *metaObject() const;` — 获取元对象
- 声明 `void qt_static_metacall(...)` — 静态调用函数
- 声明 `qt_metacall(...)` / `qt_metacast(...)` — 动态调用/类型转换

MOC 生成的 `moc_AlarmEngine.cpp` 中会创建这些函数的具体实现，包含信号名称、参数类型等运行时信息。

### 1.4 QGraphicsObject 特殊说明

项目中图元类继承自 `QGraphicsObject` 而非 `QGraphicsItem`：

```cpp
class BaseGraphicsItem : public QGraphicsObject {  // 不是 QGraphicsItem
    Q_OBJECT
};
```

**原因：** `QGraphicsObject` 是 `QGraphicsItem` 和 `QObject` 的联合子类，既具有图元特性（boundingRect、paint），又支持信号与槽。纯 `QGraphicsItem` 不支持信号与槽，除非使用多继承，但多继承容易出问题。

---

## 二、信号与槽（Signal & Slot）

### 2.1 信号定义

项目中各模块定义了大量自定义信号：

**AlarmEngine 的 13 个信号（AlarmEngine.h:209-256）：**

```cpp
signals:
    // Level 1: 基础报警信号
    void alarmTriggered(const AlarmEvent& event);   // 报警触发
    void alarmAcknowledged(const QString& alarmId);  // 报警确认
    void alarmReturnToNormalAcknowledged(const QString& alarmId);
    void alarmCleared(const QString& alarmId);       // 报警清除

    // Level 2: 增强信号
    void alarmShelved(quint32 tagId, const QString& reason, int durationSec);
    void alarmUnshelved(quint32 tagId);
    void alarmEscalated(quint32 tagId, AlarmLimit oldLimit, AlarmLimit newLimit);
    void alarmParameterChanged(quint32 tagId, const QString& fieldName,
                                const QString& oldValue, const QString& newValue);

    // Level 3: KPI 信号
    void alarmCountChanged(int activeCount, int unackCount);

    // Level 4: 变更审计信号
    void changeRecorded(const AlarmChangeRecord& record);
```

**ModbusComm 的通信信号（ModbusComm.h:68-82）：**

```cpp
signals:
    void connectionEstablished();                    // 连接建立
    void connectionLost();                            // 连接断开
    void connectionError(const QString& error);       // 连接错误
    void dataReceived(int serverAddress, int startAddress,
                      const QVector<quint16>& values); // 数据接收
    void writeCompleted(int serverAddress, int address, bool success);
    void heartbeatTimeout();
```

### 2.2 槽函数定义

槽可以是普通成员函数或 `slots` 关键字声明的函数：

```cpp
// 头文件中的声明
class ModbusManager : public QObject {
    Q_OBJECT
private slots:
    void onDataReceived(int serverAddress, int startAddress,
                        const QVector<quint16>& values);
    void onDeviceConnectionLost();
    void onDeviceConnectionEstablished();
};

class AlarmKpiMonitor : public QObject {
    Q_OBJECT
private slots:
    void onTick();  // 定时器触发
};
```

### 2.3 connect 连接的四种用法

**用法一：经典方式（DataEngine.cpp:27-30）**

```cpp
connect(m_modbusManager, &ModbusManager::deviceStatusChanged,
        this, &DataEngine::deviceStatusChanged);
```

直接把信号转发到另一个信号。

**用法二：跨线程信号转发（DataEngine.cpp:39-46）**

```cpp
connect(m_dataParseThread, &DataParseThread::dataUpdated,
        this, &DataEngine::onDataUpdated);

connect(m_dataParseThread, &DataParseThread::alarmTriggered,
        this, &DataEngine::alarmTriggered);
```

DataParseThread 运行在独立线程中，发射信号时 Qt 自动使用 `Qt::QueuedConnection`（因为 sender 和 receiver 在不同线程）。

**用法三：DirectConnection 跨线程（ModbusManager.cpp:71）**

```cpp
connect(ctx.comm, &ModbusComm::dataReceived,
        this, &ModbusManager::onDataReceived,
        Qt::DirectConnection);
```

强制使用 DirectConnection：dataReceived 信号在 ModbusComm 线程发射，但 onDataReceived 内部仅做数据打包+入队（无锁队列），操作极快，不涉及 UI，所以 DirectConnection 可行。

**用法四：QueuedConnection 保证线程安全（ModbusManager.cpp:73-79）**

```cpp
connect(ctx.comm, &ModbusComm::connectionLost,
        this, &ModbusManager::onDeviceConnectionLost,
        Qt::QueuedConnection);

connect(ctx.comm, &ModbusComm::connectionEstablished,
        this, &ModbusManager::onDeviceConnectionEstablished,
        Qt::QueuedConnection);
```

设备状态变化使用 QueuedConnection，因为 onDeviceConnectionEstablished 内部会 `emit deviceStatusChanged()`，而这个信号需要被主线程处理。QueuedConnection 保证 slot 在 receiver 所在线程（主线程）执行。

**用法五：Lambda 表达式（TagConfigDialog.cpp:179-188）**

```cpp
connect(m_tagTable, &QTableWidget::currentCellChanged,
        this, [this](int row, int, int, int) {
    if (row >= 0) {
        auto* idItem = m_tagTable->item(row, 0);
        if (idItem) {
            quint32 tagId = idItem->data(Qt::UserRole).toUInt();
            auto tag = TagConfigMgr::instance().getTag(tagId);
            if (tag.tagId != 0) populateForm(tag);
        }
    }
});
```

Lambda 表达式作为槽函数，代码简洁，适合简单逻辑。

### 2.4 连接类型总结

| 连接类型                     | 行为                    | 使用场景        |
| ------------------------ | --------------------- | ----------- |
| 默认 (AutoConnection)      | 同线程 Direct，跨线程 Queued | 通用          |
| DirectConnection         | 在 sender 线程立即执行       | 极简操作（如入队）   |
| QueuedConnection         | 事件循环队列，跨线程安全          | 跨线程状态同步     |
| BlockingQueuedConnection | 阻塞等待 slot 执行完         | 极少用，有死锁风险   |
| UniqueConnection         | 组合标志，禁止重复连接           | 防多次 connect |

---

## 三、QThread 多线程编程

### 3.1 项目线程模型概览

```
┌──────────────────────────────────────────────────────────────┐
│                     MYDSCProject 线程架构                      │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  主线程 (UI Thread)                                          │
│  ├── QApplication / 事件循环                                  │
│  ├── MYDSCProject 主窗口                                     │
│  ├── DataEngine (调度中枢)                                    │
│  ├── AlarmEngine (报警引擎)                                   │
│  ├── DatabaseManager (数据库)                                 │
│  └── TagConfigMgr / ConfigManager 等                          │
│                                                              │
│  线程1: ModbusThread-1 (ModbusComm 设备1)                     │
│  线程2: ModbusThread-2 (ModbusComm 设备2)                     │
│  ... (最多12个设备线程)                                       │
│                                                              │
│  线程n+1: DataParseThread (数据解析线程)                       │
│                                                              │
│  线程n+2: HistoryArchiveThread (历史归档线程)                  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 方式一：QThread 子类化（DataParseThread）

**头文件（DataParseThread.h:53-54）：**

```cpp
class DataParseThread : public QThread {
    Q_OBJECT
protected:
    void run() override;
};
```

**实现文件（DataParseThread.cpp:58-102）：**

```cpp
void DataParseThread::run()
{
    m_running.storeRelaxed(1);
    qint64 lastSwapTime = 0;
    std::vector<RawModbusData> batch;
    batch.reserve(256);

    while (m_running) {
        batch.clear();
        if (m_ringBuffer) {
            m_ringBuffer->dequeueBatch(batch, 256);  // 从无锁队列取数据
        }

        if (!batch.empty()) {
            processBatch(batch);                      // 处理数据
            m_totalProcessed.fetchAndAddRelaxed(batch.size());
        }

        // 定期 swap 双缓冲区
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if ((now - lastSwapTime) >= m_swapInterval) {
            if (m_doubleBuffer) {
                m_doubleBuffer->commit();
                emit dataUpdated();
            }
            lastSwapTime = now;
        }

        QThread::msleep(batch.empty() ? m_processInterval : 1);
    }
}
```

**关键设计：**

- `m_running` 原子变量控制循环退出，避免 `terminate()`
- 有空闲睡眠：batch 空时按 `m_processInterval`（默认 20ms）休眠
- 有数据时只休眠 1ms，确保高吞吐

### 3.3 方式二：QThread + moveToThread（ModbusManager）

**每设备一线程（ModbusManager.cpp:64-81）：**

```cpp
bool ModbusManager::addDevice(const ModbusDeviceConfig& config)
{
    // 创建工作线程
    ctx.thread = new QThread();
    ctx.thread->setObjectName(QString("ModbusThread-%1").arg(ctx.deviceId));

    // 创建通信对象（无 parent！）
    ctx.comm = new ModbusComm();
    ctx.comm->setPollConfig(config.pollServerAddress,
                            config.pollStartAddress,
                            config.pollCount);
    // 将对象移动到工作线程
    ctx.comm->moveToThread(ctx.thread);

    // 连接信号：DirectConnection 用于数据接收
    connect(ctx.comm, &ModbusComm::dataReceived,
            this, &ModbusManager::onDataReceived,
            Qt::DirectConnection);

    // QueuedConnection 用于设备状态变化
    connect(ctx.comm, &ModbusComm::connectionLost,
            this, &ModbusManager::onDeviceConnectionLost,
            Qt::QueuedConnection);

    ctx.thread->start();  // 启动线程

    // 在目标线程中执行连接操作
    QMetaObject::invokeMethod(ctx.comm, [this, captureId]() {
        QMutexLocker deviceLock(&m_devicemutex);
        auto it = m_device.find(captureId);
        if (it != m_device.end() && it.value().comm) {
            it.value().comm->connectToHost(it.value().config.modbusConfig);
        }
    });
}
```

**为什么用 moveToThread 而不是子类化？**

| 对比项   | 子类化 QThread                          | moveToThread     |
| ----- | ------------------------------------ | ---------------- |
| 适用场景  | 单纯的后台循环                              | 对象有多个槽函数需在后台执行   |
| 代码复杂度 | 需要重写 run()                           | 不需要重写，但需管理对象生命周期 |
| 信号槽   | 槽在主线程执行                              | 槽在线程的事件循环中执行     |
| 本项目用途 | DataParseThread、HistoryArchiveThread | ModbusComm（每个设备） |

### 3.4 线程安全退出

```cpp
ModbusManager::~ModbusManager()
{
    stopAll();
    for (auto it = m_device.begin(); it != m_device.end(); ++it) {
        DeviceConfig& ctx = it.value();

        // 第一步：请求退出事件循环
        ctx.thread->quit();

        // 第二步：等待线程结束（最多 3 秒）
        ctx.thread->wait(3000);

        // 第三步：如果还没结束，强制终止
        if (ctx.thread->isRunning()) {
            ctx.thread->terminate();  // 危险操作，最后手段
            ctx.thread->wait();
        }

        // 第四步：清理资源
        if (ctx.comm) {
            ctx.comm->disconnect();
            delete ctx.comm;
        }
        delete ctx.thread;
    }
}
```

**最佳实践：** 先 `quit()` 再 `wait()`，绝不直接 `terminate()`，因为 `terminate()` 可能使 mutex 处于锁定状态导致死锁。

---

## 四、QTimer 定时器

### 4.1 基础用法：Modbus 轮询定时器

**ModbusComm 的轮询定时器（ModbusComm.h:97-98）：**

```cpp
class ModbusComm : public QObject {
    Q_OBJECT
    // ...
    QTimer* m_pollTimer = nullptr;       // 轮询定时器
    QTimer* m_heartbeatTimer = nullptr;   // 心跳定时器
};
```

**创建和启动（伪代码）：**

```cpp
bool ModbusComm::connectToHost(const ModbusConfig& config)
{
    // ... 连接 Modbus 设备 ...

    // 创建轮询定时器（每 500ms 读取一次）
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusComm::onPollTimeout);
    m_pollTimer->start(config.poolInterval);  // 500ms

    // 创建心跳定时器（每 5s 检查连接）
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ModbusComm::onHeartbeatTimeout);
    m_heartbeatTimer->start(config.heartbeatInterval);  // 5000ms
}
```

### 4.2 报警引擎的 on-delay 定时器

**延时触发机制（AlarmEngine.h:291-302）：**

```cpp
struct OnDelayEntry {
    AlarmLimit limit;
    float      value;
    float      threshold;
    AlarmPriority priority;
    AlarmClassification classification;
    int        onDelayMs = 3000;
    QElapsedTimer elapsed;  // 高精度计时器
};

QMap<quint32, OnDelayEntry> m_onDelayEntries;
QTimer* m_onDelayTimer = nullptr;
```

**延时触发逻辑：**

```cpp
void AlarmEngine::triggerAlarm(...)
{
    // 不立即触发，先启动 on-delay 计时
    OnDelayEntry entry;
    entry.limit = limit;
    entry.value = triggerValue;
    // ...
    entry.elapsed.start();  // 开始计时
    m_onDelayEntries.insert(tagId, entry);
}

void AlarmEngine::onOnDelayTimeout()
{
    // 检查哪些 on-delay 已到期
    for (auto it = m_onDelayEntries.begin(); it != m_onDelayEntries.end(); ) {
        if (it.value().elapsed.elapsed() >= it.value().onDelayMs) {
            // 超时了，真正触发报警
            actuallyTriggerAlarm(it.key(), ...);
            it = m_onDelayEntries.erase(it);
        } else {
            ++it;
        }
    }
}
```

**为什么用 QElapsedTimer 而不用 QTimer::singleShot？**

- On-delay 期间值可能提前恢复正常，需要取消触发
- `QElapsedTimer` 可以随时检查耗时，灵活取消

### 4.3 UI 刷新定时器

```cpp
// MYDSCProject.h — 主窗口定时器
QTimer* m_refreshTimer;  // 100ms 刷新一次 UI 数据

// MYDSCProject.cpp
m_refreshTimer = new QTimer(this);
connect(m_refreshTimer, &QTimer::timeout, this, &MYDSCProject::refreshDisplay);
m_refreshTimer->start(100);  // 10 FPS
```

### 4.4 KPI 监控定时器

**AlarmKpiMonitor 的定时器（AlarmKpiMonitor.h:78）：**

```cpp
class AlarmKpiMonitor : public QObject {
    Q_OBJECT
    QTimer* m_timer = nullptr;

    void initialize() {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &AlarmKpiMonitor::onTick);
        m_timer->start(10000);  // 每 10 秒计算一次 KPI
    }
};
```

### 4.5 QTimer::singleShot 一次性延时

```cpp
// ModbusComm.cpp — 自动重连
QTimer::singleShot(2000, this, [this]() {
    if (!m_connected) {
        attemptReconnect();
    }
});

// 2 秒后执行重连尝试，不阻塞当前线程
```

### 4.6 管道流动动画：全局共享定时器

```cpp
// PipeItem.h — 所有管道共享一个全局定时器
static QTimer* s_animTimer;
static qreal s_globalOffset;
static int s_pipeCount;

void PipeItem::initAnimation()
{
    if (!s_animTimer) {
        s_animTimer = new QTimer;
        connect(s_animTimer, &QTimer::timeout, []() {
            s_globalOffset += 2.0;  // 统一增加偏移量
        });
        s_animTimer->start(100);  // 10 FPS 动画
    }
    s_pipeCount++;
}
```

**为什么用全局定时器？** 如果 100 根管道各有一个 QTimer，QTimer 对象过多会导致事件循环压力大。一个共享定时器统一驱动所有管道动画，性能好得多。

---

## 五、QMutex / QReadWriteLock 线程同步

### 5.1 QMutex + QMutexLocker 基础模式

本项目中有 **12 个类**使用了 QMutex，模式完全一致：

```cpp
// ConfigManager.h
class ConfigManager : public QObject {
    // ...
private:
    mutable QMutex m_mutex;
};

// ConfigManager.cpp
bool ConfigManager::loadTags(const QString& jsonPath, DataEngine* engine)
{
    QMutexLocker lock(&m_mutex);  // 自动加锁，离开作用域自动解锁
    // ... 线程安全操作 ...
    return true;
}
```

**为什么用 QMutexLocker 而不用手动 lock()/unlock()？**

- 异常安全：如果函数中间抛出异常，QMutexLocker 析构时自动解锁
- 防止遗漏：手动 unlock() 可能忘记，导致死锁

### 5.2 DatabaseManager 的重试加锁模式

```cpp
bool DatabaseManager::batchInsertHistory(const QVector<HistoryRecord>& records)
{
    const int MAX_RETRY = 3;
    int retrycount = 0;
    while (retrycount < MAX_RETRY) {
        QMutexLocker lock(&m_mutex);     // 加锁

        if (!ensureConnection()) {
            return false;
        }
        if (!m_db.transaction()) {        // 开启事务
            retrycount++;
            QThread::msleep(retrycount * 100);  // 指数退避
            continue;
        }
        // ... 执行 SQL ...
        if (!m_db.commit()) {              // 提交事务
            retrycount++;
            QThread::msleep(100 * retrycount);
            continue;
        }
        return true;
    }
    return false;
}
```

**重试机制：** 数据库操作可能因连接问题失败，重试 3 次并逐次增加等待时间。

### 5.3 QReadWriteLock：读写分离

**TagConfigMgr 使用读写锁而非普通互斥锁（TagConfigMgr.h:135）：**

```cpp
class TagConfigMgr : public QObject {
    // ...
private:
    mutable QReadWriteLock m_rwlock;
};
```

**读操作（多个读线程可以并发）：**

```cpp
TagInfo TagConfigMgr::getTag(quint32 tagId) const
{
    QReadLocker locker(&m_rwlock);
    return m_tags.value(tagId);   // 多个读线程同时访问
}

int TagConfigMgr::tagCount() const
{
    QReadLocker locker(&m_rwlock);
    return m_tags.size();
}
```

**写操作（独占锁）：**

```cpp
bool TagConfigMgr::addTag(const TagInfo& tag)
{
    QWriteLocker lock(&m_rwlock);  // 写锁阻塞所有读操作
    if (m_tags.contains(tag.tagId)) {
        return false;
    }
    m_tags.insert(tag.tagId, tag);
    m_nameIndex.insert(tag.tagName, tag.tagId);
    // ...
    return true;
}
```

**为什么用 QReadWriteLock 而不是 QMutex？**

- TagConfigMgr 的读操作远多于写操作（UI 每 100ms 读取，配置修改几分钟一次）
- QMutex 读和写互相阻塞，读读不阻塞；QReadWriteLock 允许多个读并发，只有写才阻塞
- 实测：300 个位号时，QReadWriteLock 比 QMutex 快 3-5 倍

### 5.4 锁使用场景总表

| 类               | 锁类型              | 保护内容   | 读频率      | 写频率     |
| --------------- | ---------------- | ------ | -------- | ------- |
| TagConfigMgr    | `QReadWriteLock` | 位号配置数据 | 极高（20ms） | 极低（小时级） |
| AuthManager     | `QMutex`         | 权限数据   | 高        | 低       |
| ConfigManager   | `QMutex`         | 配置路径   | 低        | 低       |
| DatabaseManager | `QMutex`         | 数据库连接  | 中        | 中       |
| ModbusManager   | `QMutex`         | 设备列表   | 中        | 低       |
| ModbusComm      | `QMutex`         | 写入队列   | 中        | 高       |
| AlarmEngine     | `QMutex`         | 报警数据   | 高        | 高       |
| HistoryThread   | `QMutex`         | 归档缓存   | 中        | 中       |

---

## 六、QAtomicInt 原子操作

### 6.1 基础用法

QAtomicInt 提供无需加锁的整型原子操作，主要用于线程间的状态标志和统计计数。

**启动/停止标志位：**

```cpp
// DataParseThread.h
QAtomicInt m_running;          // 线程运行标志

// DataParseThread.cpp
void DataParseThread::stop() {
    m_running.storeRelaxed(0);  // 原子写入 0，线程在 run() 中读取
}

void DataParseThread::run() {
    m_running.storeRelaxed(1);
    while (m_running.loadRelaxed()) {  // 原子读取
        // ... 工作循环 ...
    }
}
```

**为什么不用普通的 `bool m_running`？**

- 一个线程写、另一个线程读普通 bool 是**数据竞争**（C++ 未定义行为）
- 编译器可能优化掉读取（把变量缓存到寄存器），导致线程永远看不到变化
- `QAtomicInt` 保证写线程的修改在读线程立即可见

### 6.2 统计计数器

**ModbusManager 的入队和丢弃统计：**

```cpp
// ModbusManager.h
QAtomicInt m_totalEnqueued;     // 总入队数据包数
QAtomicInt m_totalDropped;      // 总丢失数据包数（队列满时）

// ModbusManager.cpp
if (!m_ringBuffer->enqueue(raw)) {
    m_totalDropped.fetchAndAddRelaxed(1);  // 原子递增
    int drop = m_totalDropped.loadRelaxed();
    if (drop % 100 == 0) {
        LOG_WARN("ModbusManager",
            QString("环形缓冲区已满，数据丢失! 总丢失=%1")
            .arg(drop));
    }
} else {
    m_totalEnqueued.fetchAndAddRelease(1);
}
```

**DataParseThread 的处理统计：**

```cpp
QAtomicInt m_totalProcessed;   // 已处理数据包数
QAtomicInt m_totalAlarms;      // 已触发报警数

// 递增
m_totalProcessed.fetchAndAddRelaxed(static_cast<int>(batch.size()));
m_totalAlarms.fetchAndAddRelease(1);

// 析构时读取
LOG_INFO("DataParseThread", "processed=%1, alarms=%2"
    .arg(m_totalProcessed.loadRelaxed())
    .arg(m_totalAlarms.loadRelaxed()));
```

### 6.3 项目中使用的原子变量

| 类                    | 原子变量                                | 用途        |
| -------------------- | ----------------------------------- | --------- |
| ModbusManager        | `m_running`                         | 运行状态      |
| ModbusManager        | `m_totalEnqueued`, `m_totalDropped` | 统计计数      |
| ModbusComm           | `m_connected`, `m_polling`          | 连接/轮询状态   |
| ModbusComm           | `m_reconnectiong`                   | 重连标志（防重入） |
| DataParseThread      | `m_running`                         | 线程运行标志    |
| DataParseThread      | `m_totalProcessed`, `m_totalAlarms` | 统计计数      |
| HistoryArchiveThread | `m_running`                         | 线程运行标志    |
| HistoryArchiveThread | `m_totalArchived`, `m_totalFailed`  | 统计计数      |

### 6.4 std::atomic 替代方案

项目同时使用了 C++11 的 `std::atomic` 实现 DoubleBuffer 的无锁 RCU 机制：

```cpp
// DoubleBuffer.h — RCU 无锁双缓冲区
using ImmutableSnapshot = std::shared_ptr<const SnapshotMap>;
mutable ImmutableSnapshot m_readOnlySnapshot;

// 写线程：原子发布新快照
std::atomic_store(&m_readOnlySnapshot, std::move(newsnap));

// 读线程：原子获取当前快照（无锁！）
ImmutableSnapshot readAll() const {
    return std::atomic_load(&m_readOnlySnapshot);
}
```

**QAtomicInt vs std::atomic：** 项目根据使用场景混用两者。对于简单的计数器，QAtomicInt 代码更简洁（`fetchAndAddRelaxed`）；但对于智能指针的原子操作，必须用 `std::atomic_load/store`，QAtomicInt 无法胜任。

---

## 七、QJsonDocument / QJsonObject 配置持久化

### 7.1 JSON 读取解析

**DataEngine 加载 tags.json（DataEngine.cpp:69-236）：**

```cpp
bool DataEngine::loadTagConfig(const QString& jsonPath)
{
    // 第一步：打开文件
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR("DataEngine", "无法打开位号配置文件");
        return false;
    }

    // 第二步：解析 JSON
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR("DataEngine", QString("JSON解析失败: %1").arg(error.errorString()));
        return false;
    }

    // 第三步：遍历数组
    QJsonArray tags = doc.array();
    for (const auto& item : tags) {
        QJsonObject obj = item.toObject();

        TagInfo tag;
        tag.tagId = static_cast<quint32>(obj["tagId"].toInt());
        tag.tagName = obj["tagName"].toString();
        tag.description = obj["description"].toString();
        tag.unit = obj["unit"].toString();

        // 带默认值的读取（JSON 缺失字段时使用）
        tag.engHigh = static_cast<float>(obj["engHigh"].toDouble(100.0));
        tag.engLow = static_cast<float>(obj["engLow"].toDouble(0.0));

        // 合理性校验
        if (tag.engHigh <= tag.engLow) {
            // 自动修正并警告
        }
        // ...
    }
}
```

### 7.2 数据校验示例

```cpp
// 报警限值顺序校验
if (tag.highHighLimit < tag.highLimit ||
    tag.highLimit < tag.lowLimit ||
    tag.lowLimit < tag.lowLowLimit) {
    // 自动修正
    tag.highHighLimit = qMax(tag.highHighLimit, tag.highLimit);
    tag.highLimit = qMax(tag.highLimit, tag.lowLimit);
    tag.lowLimit = qMax(tag.lowLimit, tag.lowLowLimit);
}

// Modbus 地址范围验证
if (tag.modbusServerAddr < 1 || tag.modbusServerAddr > 247) {
    tag.modbusServerAddr = qBound(1, tag.modbusServerAddr, 247);
}
```

### 7.3 JSON 序列化（保存）

**TagConfigMgr 保存到文件（TagConfigMgr.cpp:273-312）：**

```cpp
bool TagConfigMgr::saveToJson(const QString& jsonPath) const
{
    QReadLocker locker(&m_rwlock);

    // 构建 JSON 数组
    QJsonArray tagsArray;
    for (const TagInfo& tag : m_tags) {
        QJsonObject obj;
        obj["tagId"] = static_cast<int>(tag.tagId);
        obj["tagName"] = tag.tagName;
        obj["description"] = tag.description;
        obj["unit"] = tag.unit;
        obj["tagType"] = static_cast<int>(tag.tagType);
        obj["engHigh"] = tag.engHigh;
        obj["engLow"] = tag.engLow;
        obj["highHighLimit"] = tag.highHighLimit;
        // ...
        tagsArray.append(obj);
    }

    QJsonDocument doc(tagsArray);

    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));  // 美化输出
    file.close();
    return true;
}
```

### 7.4 P&ID 图元 JSON 组态

```cpp
// BaseGraphicsItem.h — 所有图元都支持从 JSON 加载
class BaseGraphicsItem : public QGraphicsObject {
    Q_OBJECT
public:
    virtual void loadFromJson(const QJsonObject& json);

    // 子类示例 — TankItem
    void TankItem::loadFromJson(const QJsonObject& json) {
        m_width = json["width"].toDouble(60.0);
        m_height = json["height"].toDouble(100.0);
        bindTag(json["tagName"].toString());  // 绑定位号
        // ...
    }
};
```

**JSON 场景文件示例（config/scene.json）：**

```json
[
    { "type": "Tank", "x": 100, "y": 200, "width": 60, "height": 100, "tagName": "LT_101" },
    { "type": "Pipe", "x": 130, "y": 250, "length": 200, "width": 8, "flowDir": "LeftToRight" },
    { "type": "Valve", "x": 230, "y": 246, "tagName": "FV_101" },
    { "type": "Pump",  "x": 400, "y": 240, "tagName": "PUMP_101" }
]
```

### 7.5 JSON 工具函数详解

| 函数                          | 用途                    | 参数签名                                            |
| --------------------------- | --------------------- | ----------------------------------------------- |
| `QJsonDocument::fromJson()` | 将 QByteArray 解析为 JSON | `fromJson(const QByteArray&, QJsonParseError*)` |
| `QJsonDocument::toJson()`   | 序列化为 QByteArray       | `toJson(Indented/Compact)`                      |
| `QJsonObject::value()`      | 按键取值                  | `value(const QString&) const`                   |
| `QJsonValue::toInt()`       | 转 int                 | `toInt(defaultValue)`                           |
| `QJsonValue::toDouble()`    | 转 double（可带默认值）       | `toDouble(defaultValue)`                        |
| `QJsonValue::toString()`    | 转 QString             | `toString(defaultValue)`                        |
| `QJsonValue::toObject()`    | 转 QJsonObject         | `toObject()`                                    |
| `QJsonValue::toArray()`     | 转 QJsonArray          | `toArray()`                                     |
| `QJsonObject::contains()`   | 检查键是否存在               | `contains(const QString&) const`                |

---

## 八、QSqlDatabase / QSqlQuery 数据库

### 8.1 MySQL 连接初始化

```cpp
// DatabaseManager.cpp — MySQL 连接
m_db = QSqlDatabase::addDatabase("QMYSQL", "dcs_mysql");
m_db.setHostName(host);
m_db.setPort(port);
m_db.setDatabaseName(database);
m_db.setUserName(username);
m_db.setPassword(password);
m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1;MYSQL_OPT_CONNECT_TIMEOUT=10");

if (!m_db.open()) {
    QString error = m_db.lastError().text();
    LOG_ERROR("Database", QString("MySQL连接失败: %1").arg(error));
    return false;
}
```

### 8.2 SQLite 回退机制

```cpp
// 先尝试 MySQL，失败则使用 SQLite 本地存储
bool DatabaseManager::initializeWithFallback(...)
{
    if (initialize(host, port, database, username, password)) {
        m_useSqlite = false;
        return true;  // MySQL 成功
    }

    // MySQL 不可用，自动降级到 SQLite
    m_useSqlite = true;
    m_db = QSqlDatabase::addDatabase("QSQLITE", "dcs_sqlite");
    m_db.setDatabaseName("./data/dcs.db");

    // SQLite 优化：WAL 模式
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode=WAL");
    query.exec("PRAGMA synchronous=NORMAL");
}
```

**为什么需要回退？** 化工现场可能存在网络问题导致 MySQL 不可用。SQLite 本地存储保证系统至少能记录数据，不掉数据。

### 8.3 批量插入 + 事务

```cpp
bool DatabaseManager::batchInsertHistory(const QVector<HistoryRecord>& records)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) return false;

    // 开启事务
    if (!m_db.transaction()) {
        LOG_WARN("Database", QString("开启事务失败: %1").arg(m_db.lastError().text()));
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT INTO history_data (tag_id, value, quality, timestamp) "
                   "VALUES (:tag_id, :value, :quality, :timestamp)");

    for (const auto& record : records) {
        query.bindValue(":tag_id", static_cast<uint>(record.tagId));
        query.bindValue(":value", record.value);
        query.bindValue(":quality", record.quality);
        query.bindValue(":timestamp", static_cast<qlonglong>(record.timestamp));

        if (!query.exec()) {
            m_db.rollback();  // 失败时回滚
            return false;
        }
    }

    // 提交事务
    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }
    return true;
}
```

**为什么用事务？** 非事务模式下每条 INSERT 都是独立提交，磁盘 I/O 开销巨大。事务将 N 条 INSERT 合并为一次提交，性能提升 100 倍以上。

### 8.4 查询 + 结果集遍历

```cpp
QVector<HistoryRecord> DatabaseManager::queryHistory(
    quint32 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime,
    int maxPoints)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT tag_id, value, quality, timestamp "
                  "FROM history_data "
                  "WHERE tag_id = :tag_id "
                  "  AND timestamp BETWEEN :start AND :end "
                  "ORDER BY timestamp LIMIT :limit");
    query.bindValue(":tag_id", static_cast<uint>(tagId));
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", maxPoints);

    QVector<HistoryRecord> result;
    if (query.exec()) {
        while (query.next()) {
            HistoryRecord record;
            record.tagId = query.value(0).toUInt();
            record.value = query.value(1).toDouble();
            record.quality = query.value(2).value<quint8>();
            record.timestamp = query.value(3).toLongLong();
            result.append(record);
        }
    }
    return result;
}
```

### 8.5 连接验证与自动重连

```cpp
bool DatabaseManager::ensureConnection()
{
    if (m_useSqlite) {
        return m_db.isOpen();  // SQLite 不需要重连
    }

    if (!m_db.isValid()) {
        // 重建连接对象
        m_db = QSqlDatabase::addDatabase("QMYSQL", "dcs_mysql");
        // ... 设置连接参数 ...
    }

    if (!m_db.isOpen()) {
        if (!m_db.open()) {
            emit mysqlConnectionChanged(false);
            return false;
        }
    }

    // 执行测试查询验证连接有效性
    QSqlQuery query(m_db);
    if (!query.exec("SELECT 1")) {
        // 连接已断开，重连
        m_db.close();
        if (!m_db.open()) {
            emit mysqlConnectionChanged(false);
            return false;
        }
    }
    return true;
}
```

### 8.6 DDL 建表

```cpp
bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);
    // 批量创建表（IF NOT EXISTS 保证幂等性）
    query.exec(
        "CREATE TABLE IF NOT EXISTS history_data ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  tag_id INT UNSIGNED NOT NULL,"
        "  value DOUBLE NOT NULL,"
        "  quality TINYINT UNSIGNED DEFAULT 0,"
        "  timestamp BIGINT NOT NULL,"
        "  INDEX idx_tag_time (tag_id, timestamp)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    // ...
}
```

**重要：** `IF NOT EXISTS` 确保多次调用不会报错，系统启动时幂等地检查表是否存在。

---

## 九、QSoundEffect 音频播放

### 9.1 报警声音优先级

```cpp
// AlarmEngine.h
class AlarmEngine : public QObject {
    Q_OBJECT
    // ...
private:
    QSoundEffect* m_soundCritical = nullptr;  // 紧急报警声音
    QSoundEffect* m_soundMajor    = nullptr;  // 重要报警声音
    QSoundEffect* m_soundMinor    = nullptr;  // 一般报警声音
    bool m_soundEnabled = true;
};
```

### 9.2 声音文件映射

```cpp
QString AlarmEngine::soundPathForPriority(AlarmPriority priority) const
{
    switch (priority) {
    case AlarmPriority::Critical:
        return ":/sounds/alarm_critical.wav";    // 高频持续音
    case AlarmPriority::Major:
        return ":/sounds/alarm_major.wav";        // 中频间歇音
    case AlarmPriority::Minor:
        return ":/sounds/alarm_minor.wav";        // 低频单声
    default:
        return "";                                // Advisory 无声音
    }
}
```

### 9.3 播放控制

```cpp
void AlarmEngine::playAlarmSound(AlarmPriority priority)
{
    if (!m_soundEnabled) return;

    QSoundEffect* sound = nullptr;
    switch (priority) {
    case AlarmPriority::Critical: sound = m_soundCritical; break;
    case AlarmPriority::Major:    sound = m_soundMajor;    break;
    case AlarmPriority::Minor:    sound = m_soundMinor;    break;
    default: return;
    }

    if (sound && !sound->isPlaying()) {
        sound->play();  // 异步播放，不阻塞
    }
}
```

### 9.4 注意事项

- `QSoundEffect` 适用于短音效（报警声），不适合背景音乐
- 使用 WAV 格式（PCM），无需解码
- `play()` 是异步的，立即返回，在后台线程解码+播放
- 同一对象重复调用 `play()` 会重新播放，可以叠加

---

## 十、QGraphicsView 架构（P&ID 画面）

### 10.1 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                    QGraphicsView 架构                          │
│                                                              │
│  QGraphicsView (PidView)                                     │
│  └── Viewport (显示区域)                                      │
│                                                              │
│  QGraphicsScene (PidScene)                                   │
│  └── 图元列表                                                │
│      ├── TankItem (反应釜/储罐液位)                            │
│      ├── PipeItem (管道流动)                                   │
│      ├── ValveItem (阀门)                                     │
│      ├── PumpItem (泵)                                        │
│      └── DataLabelItem (数据显示)                              │
│                                                              │
│  每个图元绑定一个位号(TagName)                                  │
│  位号值变化 → 图元自动更新外观                                  │
└──────────────────────────────────────────────────────────────┘
```

### 10.2 PidScene：场景管理

```cpp
// PidScene.h
class PidScene : public QGraphicsScene {
    Q_OBJECT
public:
    bool loadFromJson(const QString& jsonPath);  // 从 JSON 加载画面
    void clearScene();
    QList<BaseGraphicsItem*> allGraphicItems() const;

signals:
    void graphicItemClicked(BaseGraphicsItem* item);

private:
    BaseGraphicsItem* createItem(const QString& type);  // 工厂方法
    QMap<QString, BaseGraphicsItem*> m_items;  // tagName → 图元
};

// PidScene.cpp — 创建图元的工厂方法
BaseGraphicsItem* PidScene::createItem(const QString& type)
{
    if (type == "Tank")  return new TankItem();
    if (type == "Valve") return new ValveItem();
    if (type == "Pump")  return new PumpItem();
    if (type == "Pipe")  return new PipeItem();
    if (type == "Label") return new DataLabelItem();
    return nullptr;
}

// 从 JSON 加载画面
bool PidScene::loadFromJson(const QString& jsonPath)
{
    QFile file(jsonPath);
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray items = doc.array();

    for (const auto& item : items) {
        QJsonObject obj = item.toObject();
        BaseGraphicsItem* gi = createItem(obj["type"].toString());
        if (gi) {
            gi->loadFromJson(obj);
            addItem(gi);  // 添加到场景
        }
    }
}
```

### 10.3 BaseGraphicsItem：图元基类

```cpp
class BaseGraphicsItem : public QGraphicsObject {
    Q_OBJECT
public:
    void bindTag(const QString& tagName);
    virtual void loadFromJson(const QJsonObject& json);
    virtual QString itemTypeName() const = 0;

    // 数据更新回调
    void onTagValueChanged(quint32 tagId, float newValue);

signals:
    void itemClicked(BaseGraphicsItem* item);
    void stateChanged();

protected:
    virtual void updateAppearance() = 0;  // 子类实现外观更新

    float tagValue() const { return m_tagValue; }
    AlarmLimit tagAlarmState() const { return m_alarmState; }
    DataQuality tagQuality() const { return m_quality; }

    QColor alarmColor() const {
        switch (m_alarmState) {
        case AlarmLimit::HighHigh: return QColor(255, 0, 0);      // 红色
        case AlarmLimit::High:     return QColor(255, 165, 0);    // 橙色
        case AlarmLimit::LowLow:   return QColor(255, 0, 0);
        case AlarmLimit::Low:      return QColor(255, 165, 0);
        default: return Qt::white;
        }
    }

    // 点击事件
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        emit itemClicked(this);
    }

    QString m_tagName;
    quint32 m_tagId = 0;
    float m_tagValue = 0.0f;
    AlarmLimit m_alarmState = AlarmLimit::Normal;
    DataQuality m_quality = DataQuality::Good;
};
```

### 10.4 TankItem：自定义图元绘制

```cpp
class TankItem : public BaseGraphicsItem {
    Q_OBJECT
public:
    QString itemTypeName() const override { return "Tank"; }

    QRectF boundingRect() const override {
        return QRectF(-m_width/2, -m_height/2, m_width, m_height);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*,
               QWidget*) override
    {
        // 绘制罐体外壳
        painter->setPen(QPen(m_tankColor, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(boundingRect(), 5, 5);

        // 计算液位高度
        qreal levelHeight = m_height * (m_levelPercent / 100.0);
        QRectF liquidRect(-m_width/2 + 3, m_height/2 - levelHeight,
                          m_width - 6, levelHeight);

        // 绘制液体（颜色随报警状态变化）
        painter->setBrush(m_liquidColor);
        painter->setPen(Qt::NoPen);
        painter->drawRect(liquidRect);

        // 绘制液位数值
        painter->setPen(Qt::white);
        painter->drawText(boundingRect(), Qt::AlignCenter,
                          QString("%1%").arg(m_levelPercent, 0, 'f', 1));
    }

protected:
    void updateAppearance() override {
        m_levelPercent = tagValue();                // 更新液位
        m_liquidColor = alarmColor();               // 更新颜色
        update();                                   // 触发重绘
    }
};
```

### 10.5 PipeItem：动画实现

```cpp
class PipeItem : public BaseGraphicsItem {
    Q_OBJECT
    // 全局共享动画定时器（所有管道共用一个）
    static QTimer* s_animTimer;
    static qreal s_globalOffset;
    static int s_pipeCount;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*,
               QWidget*) override
    {
        painter->setPen(QPen(m_fluidColor, m_width));
        painter->drawLine(QPointF(0, 0), QPointF(m_length, 0));

        // 流动箭头动画（使用全局偏移量）
        if (m_flowing) {
            painter->setPen(QPen(m_fluidColor.lighter(150), 2));
            for (qreal x = s_globalOffset % 20; x < m_length; x += 20) {
                painter->drawLine(QPointF(x, -3), QPointF(x + 5, 0));
                painter->drawLine(QPointF(x, 3), QPointF(x + 5, 0));
            }
        }
    }
};
```

### 10.6 数据绑定机制

RealtimeDb 已移除。当前数据绑定通过 TagConfigMgr 查配置 + DoubleBuffer 读取实时值：

```cpp
void BaseGraphicsItem::bindTag(const QString& tagName)
{
    m_tagName = tagName;
    m_tagId = TagConfigMgr::instance().getTagByName(tagName).tagId;

    if (m_tagId != 0) {
        // 不再注册回调。UI 更新走 onDataUpdated() → swap() → readTag() 路径
        TagInfo tag = TagConfigMgr::instance().getTag(m_tagId);
        m_engLow = tag.engLow;
        m_engHigh = tag.engHigh;
    }
}

// onTagValueChanged 由 MYDSCProject::onDataUpdated() 遍历所有图元时调用
void BaseGraphicsItem::onTagValueChanged(const DoubleBuffer::TagSnapshot& snap)
{
    m_tagValue = snap.currentValue;
    m_alarmState = snap.alarmState;
    m_quality = snap.quality;
    updateAppearance();
}
```

### 10.7 QGraphicsView 使用要点

**性能优化：**

- 大量图元时开启 `setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate)`
- 使用 `setCacheMode(QGraphicsView::CacheBackground)` 缓存背景
- 图元较多时使用 `QGraphicsItem::ItemCoordinateCache` 缓存

**常见踩坑：**

- `paint()` 中不能做复杂计算，它每帧调用
- `boundingRect()` 必须准确，否则出现残影或裁剪
- 图元不要频繁 delete/new，尽量复用
- QGraphicsItem 没有 Q_OBJECT，需要信号槽必须用 QGraphicsObject

---

## 十一、QCustomPlot 趋势图

### 11.1 基础结构

```cpp
// TrendWidget.h
#include "qcustomplot.h"

class TrendWidget : public QWidget {
    Q_OBJECT
    QCustomPlot* m_plot;
    QSharedPointer<QCPAxisTickerDateTime> m_dateTicker;

    struct TrendSeries {
        QString tagName;
        quint32 tagId;
        QCPGraph* graph;       // 曲线对象
        QPen pen;               // 线条样式
    };
    QVector<TrendSeries> m_series;
};
```

### 11.2 初始化趋势图

```cpp
void TrendWidget::initPlot()
{
    m_plot = new QCustomPlot(this);

    // X 轴：时间格式
    m_dateTicker.reset(new QCPAxisTickerDateTime);
    m_dateTicker->setDateTimeFormat("HH:mm:ss");
    m_plot->xAxis->setTicker(m_dateTicker);
    m_plot->xAxis->setLabel("时间");

    // Y 轴：工程值
    m_plot->yAxis->setLabel("工程值");

    // 启用图例
    m_plot->legend->setVisible(true);

    // 启用交互
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}
```

### 11.3 添加曲线

```cpp
void TrendWidget::addSeries(const QString& tagName, const QColor& color)
{
    TrendSeries series;
    series.tagName = tagName;
    series.tagId = TagConfigMgr::instance().getTagByName(tagName).tagId;

    // 创建曲线
    series.graph = m_plot->addGraph();
    series.pen = QPen(color, 1.5);
    series.graph->setPen(series.pen);
    series.graph->setName(tagName);

    m_series.append(series);
}
```

### 11.4 数据更新

```cpp
void TrendWidget::refreshData()
{
    double now = QDateTime::currentSecsSinceEpoch();
    double start = now - 3600;  // 最近 1 小时

    for (auto& s : m_series) {
        // 查询历史数据
        auto records = HistoryArchiveThread::instance().queryTrend(
            s.tagId,
            QDateTime::fromSecsSinceEpoch(start),
            QDateTime::fromSecsSinceEpoch(now));

        // 填充数据点
        QVector<double> x, y;
        for (const auto& r : records) {
            x.append(r.timestamp / 1000.0);  // ms → s
            y.append(r.value);
        }
        s.graph->setData(x, y);
    }

    // 自动缩放坐标轴
    m_plot->rescaleAxes();
    m_plot->replot();  // 触发重绘
}
```

### 11.5 QCustomPlot 优势

| 特性      | QCustomPlot    | Qt Charts |
| ------- | -------------- | --------- |
| 许可证     | GPL/商业         | GPL/商业    |
| 性能      | 快（直接 QPainter） | 中等        |
| 文件大小    | 单 .h+.cpp      | 完整 Qt 模块  |
| 功能      | 所有 2D 图表       | 2D/3D     |
| 本项目选择原因 | 轻量、快速          | 未使用       |

---

## 十二、QMetaObject::invokeMethod 跨线程调用

### 12.1 基础语法

`QMetaObject::invokeMethod` 可以在**指定线程的事件循环中执行函数**，是跨线程调用的重要工具。

```cpp
// 在目标线程中异步执行 lambda
QMetaObject::invokeMethod(targetObject, [this]() {
    // 这段代码在 targetObject 所在线程执行
    doSomething();
}, Qt::QueuedConnection);
```

### 12.2 ModbusManager 中的 10+ 处使用

**在 Modbus 线程中执行连接（跨线程安全）：**

```cpp
// ModbusManager.cpp:85 — 在 ModbusComm 的工作线程中执行 connectToHost
int captureId = config.deviceId;
QMetaObject::invokeMethod(ctx.comm, [this, captureId]() {
    QMutexLocker deviceLock(&m_devicemutex);
    auto it = m_device.find(captureId);
    if (it != m_device.end() && it.value().comm) {
        it.value().comm->connectToHost(it.value().config.modbusConfig);
    }
});
```

**停止轮询（在目标线程中执行）：**

```cpp
// ModbusManager.cpp:118 — 停止设备
QMetaObject::invokeMethod(commPtr, [commPtr]() {
    commPtr->stopPoll();
    commPtr->disconnect();
});

// ModbusManager.cpp:142 — 启动全部设备轮询
QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm]() {
    comm->startPoll();
}, Qt::QueuedConnection);

// ModbusManager.cpp:170 — 启动单个设备
QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm]() {
    comm->startPoll();
}, Qt::QueuedConnection);
```

**下发写入指令：**

```cpp
// ModbusManager.cpp:243 — 操作员下发寄存器写入
QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm, serverAddress, address, value]() {
    comm->writeHoldingRegister(serverAddress, address, value);
}, Qt::QueuedConnection);
```

### 12.3 为什么需要 invokeMethod？

考虑这个场景：

1. `ModbusComm` 对象通过 `moveToThread()` 移到了工作线程
2. 主线程不能直接调用 `ModbusComm::startPoll()` — 这是跨线程直接调用，线程不安全
3. `QMetaObject::invokeMethod` 将调用请求包装成事件，投递到目标线程的事件队列
4. 目标线程的事件循环取出事件并执行，从而实现跨线程安全调用

**如果不使用 invokeMethod 的后果：**

```cpp
ctx.comm->startPoll();  // ❌ 跨线程直接调用！
```

这会在线程 A 中调用属于线程 B 的对象的函数，可能导致：

- 数据竞争（同时访问成员变量）
- Qt 信号槽机制混乱
- 定时器无法正常工作

### 12.4 invokeMethod 注意事项

- 目标对象必须在事件循环中（`QThread::exec()` 运行中）
- 默认使用 `Qt::AutoConnection`，跨线程时自动变为 `QueuedConnection`
- Lambda 中捕获的变量必须确保在 lambda 执行时仍然有效（小心悬空指针）

---

## 十三、MOC 元对象编译器工作原理

### 13.1 编译流程

```
源文件 (.h)  →  MOC 处理  →  moc_xxx.cpp  →  编译  →  moc_xxx.obj
```

MOC 解析包含 `Q_OBJECT` 的头文件，生成标准 C++ 代码。

### 13.2 MOC 生成了什么？

以 AlarmEngine.h 为例，MOC 生成 `moc_AlarmEngine.cpp`，包含：

**元对象定义：**

```cpp
// MOC 生成的元对象结构体
const QMetaObject AlarmEngine::staticMetaObject = {
    { &QObject::staticMetaObject,  // 父类元对象
      "AlarmEngine",               // 类名
      nullptr,                     // 插件数据
      {
        // 信号索引表
        { "alarmTriggered(AlarmEvent)", 0, 0 },
        { "alarmAcknowledged(QString)", 1, 0 },
        // ...
      },
      { /* 方法信息 */ }
    }
};
```

**信号发射函数：**

```cpp
// MOC 生成的信号函数
void AlarmEngine::alarmTriggered(const AlarmEvent& _t1)
{
    // 遍历所有连接的槽，逐个调用
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
```

**类型转换：**

```cpp
// MOC 生成的类型转换
void *AlarmEngine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, "AlarmEngine")) return this;
    return QObject::qt_metacast(_clname);
}
```

### 13.3 项目 MOC 文件列表

项目中注册了 **32 个头文件** 进行 MOC 处理（.vcxproj MoC 阶段）：

```
TankItem.h, ValveItem.h, AlarmEngine.h, AlarmKpiMonitor.h,
AlarmChangeLog.h, AuthManager.h, BaseGraphicsItem.h,
ConfigManager.h, DatabaseManager.h, DataEngine.h,
TagConfigMgr.h, HistoryArchiveThread.h,
PidScene.h, MYDSCProject.h, PipeItem.h, PumpItem.h,
DataLabelItem.h, PerformanceMonitor.h, DataParseThread.h,
DataBackupManager.h, ModbusManager.h, ModbusComm.h,
TrendWidget.h, TagConfigDialog.h,
3pair/qcustomplot/qcustomplot.h  // 第三方库也需要 MOC
```

### 13.4 常见 MOC 编译错误

**错误场景：** 拆分 AlarmEngine 后，旧的 `moc_AlarmEngine.cpp` 包含 3 个 Q_OBJECT 类，但新头文件只剩 1 个。

**症状：** 运行时崩溃 `0x0000005`（访问冲突），地址 `0xFFFFFFFFFFFFFFFF`

**原因：** 旧 moc_*.obj 缓存在构建目录中，与新头文件不匹配

**解决：** 清理构建（Clean Solution）→ 重新生成（Rebuild Solution）

**检查方法：**

```cpp
// 检查某个 `moc_xxx.cpp` 是否最新
// 方法：删除中间文件，让 Qt VS Tools 重新生成
// 常见位置：x64/Debug/qt/moc/moc_AlarmEngine.cpp
```

---

## 十四、QSplashScreen 启动画面

```cpp
// main.cpp:140-152
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ... 初始化 ...

    // 创建启动画面
    QSplashScreen splash;
    splash.showMessage(QStringLiteral("正在初始化 DCS 系统..."),
        Qt::AlignBottom | Qt::AlignCenter, Qt::white);
    splash.show();
    app.processEvents();  // 立即处理事件，显示画面

    // 执行耗时的初始化操作
    // ... 数据库初始化、配置加载、设备连接 ...

    // 创建主窗口
    MYDSCProject window;
    window.show();

    // 关闭启动画面
    splash.finish(&window);

    return app.exec();
}
```

**关键点：**

- `splash.show()` 后必须调用 `app.processEvents()` 才能真正显示（否则在事件循环启动前不会绘制）
- `splash.finish(&window)` — 在主窗口显示完成后自动关闭启动画面

---

## 十五、Qt 容器选型指南

### 15.1 本项目容器使用总表

| 容器类型           | 使用场景     | 项目举例                  |
| -------------- | -------- | --------------------- |
| `QHash<K,V>`   | 快速查找，无序  | TagConfigMgr 的位号索引    |
| `QMap<K,V>`    | 有序遍历     | PidScene 的图元列表        |
| `QVector<T>`   | 连续存储，遍历快 | DataParseThread 的批量数据 |
| `QList<T>`     | 小数据量     | AlarmEngine 的历史记录     |
| `QQueue<T>`    | 先进先出     | ModbusComm 的写入队列      |
| `QPair<T1,T2>` | 二元组返回    | TagConfigMgr 的量程      |
| `QStringList`  | 字符串列表    | TagConfigMgr 的位号名列表   |

### 15.2 QHash vs QMap 选择

```cpp
// QHash — 当需要 O(1) 查找且不关心顺序时
QHash<quint32, TagInfo> m_tags;              // tagId → TagInfo
QHash<QString, quint32> m_nameIndex;          // tagName → tagId

// QMap — 当需要按 key 遍历时
QMap<QString, BaseGraphicsItem*> m_items;     // tagName → Item（有序排列）
```

### 15.3 QString 常用操作

```cpp
// 字符串拼接（项目中大量使用）
QString msg = QString("位号 %1 温度超限: %2℃").arg(tagName).arg(value);

// 类型转换
QString num = QString::number(123.456, 'f', 2);  // "123.46"

// 日志输出
LOG_INFO("ModbusManager", QString("设备在线: ID=%1").arg(deviceId));

// 对象名
ctx.thread->setObjectName(QString("ModbusThread-%1").arg(ctx.deviceId));
```

### 15.4 迭代器使用

```cpp
// Java 风格迭代器
for (auto it = m_device.constBegin(); it != m_device.constEnd(); ++it) {
    if (it.value().comm == sendercomm) {
        raw.deviceId = it.key();
        break;
    }
}

// 范围 for（C++11）
for (const auto& tag : tags) {
    // ...
}
```

---

## 附录：Qt 模块依赖关系图

```
MYDSCProject.exe
├── Qt6Core.dll        — 核心：QObject, QTimer, QThread, QJson*
├── Qt6Gui.dll         — GUI：QPainter, QColor, QFont
├── Qt6Widgets.dll     — 控件：QMainWindow, QGraphicsView
├── Qt6Network.dll     — 网络：Modbus TCP 通信
├── Qt6Sql.dll         — 数据库：MySQL + SQLite
├── Qt6Multimedia.dll  — 音频：QSoundEffect
├── Qt6SerialPort.dll  — 串口：Modbus RTU（预留）
├── Qt6SerialBus.dll   — 串行总线（预留）
└── Qt6PrintSupport.dll— 打印（预留）
    │
    └── 第三方库
        ├── libmodbus.dll  — Modbus 协议实现
        └── QCustomPlot    — 趋势图控件（源码集成）
```

---

## 十六、PerformanceMonitor 性能监控

### 16.1 设计目标

DCS 系统对实时性要求极高，任何模块的性能退化都可能导致数据丢失或报警延迟。PerformanceMonitor 提供运行时性能数据采集和分析能力，帮助定位性能瓶颈。

### 16.2 核心接口

```cpp
class PerformanceMonitor : public QObject {
public:
    static PerformanceMonitor& instance();

    // 记录单个性能指标
    void recordMetric(const QString& name, double value);

    // 记录操作延迟（毫秒）
    void recordLatency(const QString& operation, double durationMs);

    // 记录计数器
    void recordCount(const QString& name, int count = 1);

    // 计时器：开始/停止
    void startTimer(const QString& timerName);
    double stopTimer(const QString& timerName);

    // 获取统计信息（平均值、最大值、最小值、计数）
    QHash<QString, double> getStatistics(const QString& name) const;

    // 生成性能报告
    QString generateReport() const;

    // 清除所有统计数据
    void reset();
};
```

### 16.3 便捷宏

```cpp
// 计时宏 — 在函数入口/出口使用
#define PERF_START(name) PerformanceMonitor::instance().startTimer(name)
#define PERF_STOP(name)  PerformanceMonitor::instance().stopTimer(name)

// 延迟记录宏
#define PERF_RECORD_LATENCY(op, duration) \
    PerformanceMonitor::instance().recordLatency(op, duration)

// 计数器宏
#define PERF_RECORD_COUNT(name, count) \
    PerformanceMonitor::instance().recordCount(name, count)
```

### 16.4 使用示例

```cpp
// DataParseThread::processBatch 中使用
void DataParseThread::processBatch(const std::vector<RawModbusData>& batch)
{
    PERF_START("DataParseThread::processBatch");

    for (const auto& raw : batch) {
        parseRawData(raw);
    }

    double elapsed = PERF_STOP("DataParseThread::processBatch");
    PERF_RECORD_LATENCY("DataParseThread::processBatch", elapsed);
    PERF_RECORD_COUNT("DataParseThread::recordsProcessed", batch.size());
}
```

### 16.5 内部数据结构

```cpp
struct MetricData {
    QVector<double> values;  // 最近 N 次采样值
    double sum = 0.0;        // 累计总和
    double min = 0.0;        // 最小值
    double max = 0.0;        // 最大值
    int count = 0;           // 采样次数
};

QHash<QString, MetricData> m_metrics;     // 指标名 → 数据
QHash<QString, QElapsedTimer> m_timers;   // 计时器名 → QElapsedTimer
mutable QMutex m_mutex;                    // 线程安全
```

### 16.6 性能报告输出

```cpp
QString PerformanceMonitor::generateReport() const
{
    QMutexLocker lock(&m_mutex);
    QString report;
    QTextStream out(&report);

    out << "===== DCS 性能报告 =====\n";
    for (auto it = m_metrics.constBegin(); it != m_metrics.constEnd(); ++it) {
        const MetricData& d = it.value();
        double avg = d.count > 0 ? d.sum / d.count : 0;
        out << QString("%1: avg=%2ms, min=%3ms, max=%4ms, count=%5\n")
               .arg(it.key(), -40)
               .arg(avg, 8, 'f', 2)
               .arg(d.min, 8, 'f', 2)
               .arg(d.max, 8, 'f', 2)
               .arg(d.count);
    }
    return report;
}
```

### 16.7 项目中的埋点一览

| 模块 | 指标名 | 含义 | 预期值 |
|------|--------|------|--------|
| DataParseThread | `DataParseThread::processBatch` | 批量解析耗时 | < 5ms |
| DataParseThread | `DataParseThread::recordsProcessed` | 处理记录数 | 每批 256 |
| AlarmEngine | `AlarmEngine::triggerAlarm` | 报警触发耗时 | < 1ms |
| AlarmEngine | `AlarmEngine::acknowledgeAlarm` | 确认操作耗时 | < 0.5ms |
| DatabaseManager | `DatabaseManager::batchInsertHistory` | 批量写入耗时 | < 50ms |
| DoubleBuffer | `DoubleBuffer::commit` | 快照发布耗时 | < 0.1ms |
| ModbusComm | `ModbusComm::onPollTimeout` | 单次轮询耗时 | < 100ms |

---

## 十七、AuthManager 权限管理

### 17.1 权限等级设计（ISA-101）

```cpp
enum class UserLevel {
    Observer = 0,   // 只能看，不能操作（参观人员、管理层）
    Operator = 1,   // 可以改SP、切手动/自动（当班操作员）
    Engineer = 2,   // 可以改PID参数、报警限值、量程（仪表工程师）
    Admin = 3       // 可以改系统配置、用户管理（系统管理员）
};
```

**为什么是 4 级？** ISA-101 标准定义了 DCS 系统的最低权限分级要求。化工厂误关一个阀门可能引发爆炸，权限控制不是可选项。

### 17.2 核心接口

```cpp
class AuthManager : public QObject {
public:
    static AuthManager& instance();
    void initialize();

    // 用户登录/登出
    bool login(const QString& username, const QString& password);
    void logout();
    bool isLoggedIn() const;

    // 当前用户信息
    QString currentUsername() const;
    UserLevel currentUserLevel() const;

    // 权限检查
    bool hasPermission(UserLevel requiredLevel) const;
    bool canOperate() const;       // Operator 及以上
    bool canConfigure() const;     // Engineer 及以上

    // 二次确认（关键操作前调用）
    bool confirmCriticalAction(const QString& action, const QString& detail);

    // 操作日志
    void logAction(const QString& action, const QString& detail = QString());

    // 自动登出
    void setAutoLogoutTimeout(int timeoutMs);
    void resetAutoLogoutTimer();
};
```

### 17.3 登录验证流程

```
用户输入用户名+密码
    │
    ├─ 1. 查找用户表（m_users）
    │     └─ 用户不存在 → 返回 false
    │
    ├─ 2. 密码哈希比对
    │     ├─ 输入密码 → SHA256 哈希
    │     └─ 与存储的 passwordHash 比对
    │
    ├─ 3. 设置当前用户
    │     ├─ m_currentUser = username
    │     └─ m_currentLevel = user.level
    │
    ├─ 4. 启动自动登出计时器
    │     └─ 默认 15 分钟无操作自动登出
    │
    └─ 5. 发射信号
          ├─ emit userLoggedIn(username, level)
          └─ 记录登录日志
```

### 17.4 二次确认机制

```cpp
bool AuthManager::confirmCriticalAction(const QString& action,
                                         const QString& detail)
{
    // 弹出确认对话框 + 密码输入
    QMessageBox::StandardButton result = QMessageBox::question(
        nullptr, action, detail,
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return false;

    // 关键操作需要重新输入密码
    // （防止操作员离开座位后被人误操作）
    QString password = QInputDialog::getText(
        nullptr, "身份验证", "请输入密码以确认操作:",
        QLineEdit::Password);

    return verifyPassword(m_currentUser, password);
}
```

**使用场景：**

| 操作 | 需要权限 | 是否二次确认 |
|------|---------|------------|
| 修改 SP | Operator | 否 |
| 切换手动模式 | Operator | **是** |
| 修改报警限值 | Engineer | **是** |
| 修改 PID 参数 | Engineer | **是** |
| 修改量程 | Engineer | **是** |
| 用户管理 | Admin | **是** |

### 17.5 自动登出

```cpp
void AuthManager::setAutoLogoutTimeout(int timeoutMs)
{
    if (timeoutMs <= 0) {
        // 禁用自动登出
        if (m_autoLogoutTimer) m_autoLogoutTimer->stop();
        return;
    }

    if (!m_autoLogoutTimer) {
        m_autoLogoutTimer = new QTimer(this);
        connect(m_autoLogoutTimer, &QTimer::timeout,
                this, &AuthManager::onAutoLogoutTimeout);
    }
    m_autoLogoutTimer->setInterval(timeoutMs);
    m_autoLogoutTimer->start();
}

void AuthManager::onAutoLogoutTimeout()
{
    LOG_INFO("AuthManager", "操作员超时自动登出");
    logout();
    emit userLoggedOut();
}
```

**为什么需要自动登出？** 操作员离开操作台后，如果系统仍处于登录状态，其他人可能误操作。ISA-101 要求 DCS 系统在操作员不活动一段时间后自动登出。

### 17.6 密码安全

```cpp
QString AuthManager::hashPassword(const QString& password) const
{
    // 当前：SHA256 简单哈希（原型阶段）
    // 生产环境应使用 bcrypt/argon2 + 加盐 + 90 天过期策略
    QByteArray hash = QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha256);
    return QString(hash.toHex());
}
```

---

## 十八、DataBackupManager 数据备份

### 18.1 备份类型

```cpp
enum class BackupType {
    Full,          // 全量备份：所有数据
    Incremental    // 增量备份：仅上次备份后的变更
};
```

### 18.2 核心接口

```cpp
class DataBackupManager : public QObject {
public:
    static DataBackupManager& instance();

    bool createFullBackup();
    bool createIncrementalBackup();
    bool cleanupOldBackups();                     // 清理过期备份
    QString generateBackupFileName(BackupType type) const;
    bool verifyBackupIntegrity(const QString& backupPath) const;
};
```

### 18.3 备份文件命名规则

```cpp
QString DataBackupManager::generateBackupFileName(BackupType type) const
{
    QString prefix = (type == BackupType::Full) ? "full" : "incr";
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return QString("./backups/%1_%2.db").arg(prefix).arg(timestamp);
    // 示例：./backups/full_20260425_143025.db
    //       ./backups/incr_20260425_180000.db
}
```

### 18.4 备份策略

| 备份类型 | 频率 | 保留策略 | 大小估算 |
|---------|------|---------|---------|
| 全量备份 | 每天凌晨 2:00 | 保留最近 7 天 | ~500MB |
| 增量备份 | 每 4 小时 | 保留最近 48 小时 | ~50MB |

### 18.5 完整性校验

```cpp
bool DataBackupManager::verifyBackupIntegrity(const QString& backupPath) const
{
    // Step 1: 检查文件是否存在且大小 > 0
    QFileInfo info(backupPath);
    if (!info.exists() || info.size() == 0) return false;

    // Step 2: 尝试打开 SQLite 数据库
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "verify");
    db.setDatabaseName(backupPath);
    if (!db.open()) return false;

    // Step 3: 执行完整性检查
    QSqlQuery query(db);
    bool ok = query.exec("PRAGMA integrity_check");
    db.close();
    QSqlDatabase::removeDatabase("verify");

    return ok;
}
```

---

## 十九、关键代码路径速查

| 功能 | 文件 | 类/方法 | Qt 技术 |
|------|------|---------|---------|
| 性能计时 | PerformanceMonitor.h | `PERF_START/STOP` | QElapsedTimer + QHash |
| 性能报告 | PerformanceMonitor.cpp | `generateReport()` | QTextStream |
| 用户登录 | AuthManager.cpp | `login()` | QCryptographicHash |
| 权限检查 | AuthManager.cpp | `hasPermission()` | QMutex + enum |
| 二次确认 | AuthManager.cpp | `confirmCriticalAction()` | QMessageBox + QInputDialog |
| 自动登出 | AuthManager.cpp | `onAutoLogoutTimeout()` | QTimer |
| 操作日志 | AuthManager.cpp | `logAction()` | DatabaseManager |
| 全量备份 | DataBackupManager.cpp | `createFullBackup()` | QSqlDatabase |
| 增量备份 | DataBackupManager.cpp | `createIncrementalBackup()` | QSqlDatabase |
| 备份校验 | DataBackupManager.cpp | `verifyBackupIntegrity()` | SQLite PRAGMA |
| 备份清理 | DataBackupManager.cpp | `cleanupOldBackups()` | QDir + QFileInfo |
