# MYDSCProject 重构详细报告

## 1. 项目概览对比

| 维度       | 原始项目 (MYDSCProject)            | 重构项目 (MYDSCProject_v2)             |
| -------- | ------------------------------ | ---------------------------------- |
| 源文件总数    | 53 个 (.h + .cpp)               | 57 个 (.h + .cpp)                   |
| 目录结构     | 单层扁平，所有文件在同一目录                 | 4 层树形结构，15 个子目录                    |
| 主要窗口文件行数 | MYDSCProject.cpp 2727 行        | MainWindow.cpp 214 行               |
| 数据库文件行数  | DatabaseManager.cpp 1586 行     | 拆为 6 个 Repo 实现，最大 179 行            |
| 标签定义行数   | TagDef.h 581 行                 | TagInfo.h 64 行 + alarm 拆分          |
| 构建系统     | Qt .pro 文件                     | CMake (CMakeLists.txt 190 行)       |
| 单例数量     | 5 个 (Meyer's singleton)        | 0 个                                |
| 对外接口抽象   | 无 (全部具体类)                      | 9 个纯虚接口                            |
| 第三方依赖    | qcustomplot, libmodbus, qtmqtt | qcustomplot, libmodbus (移除 qtmqtt) |
| C++ 标准   | 未指定 (隐式 C++11/14)              | C++17                              |
| 编译器支持    | MinGW / MSVC                   | MingGW / MSVC                      |

---

## 2. 架构分层对比

### 原始项目：扁平结构

```
MYDSCProject/
├── MYDSCProject.h/cpp       (2727 行主窗口 + 8 个单例调用)
├── DatabaseManager.h/cpp     (1586 行，直接 SQL)
├── TagDef.h                  (581 行 god struct)
├── TagConfigMgr.h/cpp        (单例)
├── AuthManager.h/cpp         (单例)
├── AlarmEngine.h/cpp         (单例)
├── DataEngine.h/cpp          (综合引擎)
├── DataParseThread.h/cpp     (解析线程)
├── DoubleBuffer.h            (RCU 双缓冲)
├── HistoryArchiveThread.h/cpp (归档线程)
├── ModbusComm.h/cpp          (Modbus 通信)
├── ModbusManager.h/cpp       (Modbus 管理，单例)
├── PerformanceMonitor.h/cpp   (性能监控)
├── SystemHealthMonitor.h/cpp  (空桩)
├── DataBackupManager.h/cpp    (空桩)
├── RealtimeDb.h/cpp           (6 行桩)
├── ConfigManager.h/cpp        (配置管理)
├── Logger.h/cpp               (日志单例)
├── AlarmChangeLog.h/cpp       (报警变更日志)
├── AlarmKpiMonitor.h/cpp      (KPI 监控)
├── PidScene.h/cpp             (P&ID 场景)
├── PidView.h/cpp              (P&ID 视图)
├── TagConfigDialog.h/cpp      (位号配置对话框)
├── TrendWidget.h/cpp          (趋势控件)
├── BaseGraphicsItem.h/cpp     (图元基类)
├── TankItem.h/cpp, ValveItem.h/cpp, PumpItem.h/cpp, PipeItem.h/cpp, DataLabelItem.h/cpp
├── lockFreeRingBuffer.h       (无锁环形缓冲)
├── export.h, core_global.h, comm_global.h, LogLevelSpec.h
└── main.cpp                   (159 行)
```

### 重构项目：4 层架构

```
MYDSCProject_v2/
├── app/                       # 启动组装层
│   ├── AppConfig.h            (配置结构体 + fromJson 工厂)
│   ├── AppContext.h           (DI 容器，持有 12 个 shared_ptr)
│   └── ApplicationBuilder.h   (Fluent Builder 构建依赖图)
├── application/               # 应用层 — 薄控制器 (3 个)
│   ├── DataController.h/cpp     (转发 DataPipeline)
│   ├── AlarmController.h/cpp    (转发 AlarmEngine)
│   └── AuthController.h/cpp     (转发 AuthManager)
├── domain/                    # 领域层 — 纯逻辑，无外部依赖
│   ├── common/                # 枚举类型
│   │   ├── AlarmLimit.h         (7 种报警限)
│   │   ├── AlarmState.h         (8 种 ISA-18.2 状态 + 5 个辅助枚举)
│   │   └── DataQuality.h        (IEC 62541 数据质量)
│   ├── tag/                   # 位号域
│   │   ├── TagInfo.h            (34 字段纯数据 struct)
│   │   ├── TagManager.h/cpp     (CRUD + JSON 导入导出)
│   │   ├── DeadbandFilter.h     (死区迟滞逻辑)
│   │   ├── DeviationChecker.h   (偏差检查)
│   │   └── RateOfChangeChecker.h(ROC 梯度检查)
│   ├── alarm/                 # 报警子系统 (ISA-18.2 + EEMUA 191)
│   │   ├── AlarmEngine.h/cpp    (35+ 方法, 18 信号)
│   │   ├── AlarmEvent.h         (7 个 struct/class)
│   │   ├── AlarmStateMachine.h  (状态转换验证表)
│   │   ├── AlarmKpiMonitor.h/cpp(EEMUA 191 KPI)
│   │   ├── AlarmChangeLog.h/cpp (变更审计日志)
│   │   ├── ChatteringGuard.h    (抖动保护)
│   │   ├── FloodDetector.h      (报警泛滥检测)
│   │   ├── ShelveManager.h      (搁置管理)
│   │   └── SuppressionEngine.h  (条件抑制)
│   └── auth/                  # 认证域
│       ├── User.h               (用户实体 struct)
│       ├── AuthManager.h/cpp    (登录/权限/操作日志)
│       ├── SessionManager.h     (会话 + 自动超时)
│       └── PasswordHasher.h     (PBKDF2-SHA256)
├── infrastructure/            # 基础设施层 — 全部接口抽象
│   ├── fieldbus/              # 现场总线
│   │   ├── IFieldbus.h          (9 方法纯虚接口)
│   │   ├── ModbusComm.h/cpp     (Modbus TCP/RTU 通信)
│   │   ├── ModbusImpl.h/cpp     (多线程 IFieldbus 实现)
│   │   ├── SimulatorImpl.h/cpp  (正弦波模拟器)
│   │   └── OpcUaImpl.h          (OPC UA 预留桩)
│   ├── messaging/             # 消息传递
│   │   ├── IMessageBus.h        (5 方法纯虚接口)
│   │   ├── LockFreeRingBuffer.h (MPSC 无锁环形缓冲 + RingBufMessageBus)
│   │   └── DoubleBuffer.h       (RCU 双缓冲, TagSnapshot)
│   ├── persistence/           # 持久化 — Repository 模式
│   │   ├── IAlarmRepo.h         (14 方法接口)
│   │   ├── IHistoryRepo.h       (3 方法接口)
│   │   ├── ITagRepo.h           (5 方法接口)
│   │   ├── IUserRepo.h          (3 方法接口)
│   │   ├── IOperationRepo.h     (1 方法接口)
│   │   ├── mysql/               # MySQL 实现
│   │   │   ├── ConnectionPool.h/cpp  (QSqlDatabase 连接池)
│   │   │   ├── AlarmMysqlRepo.h/cpp  (3 张表)
│   │   │   ├── HistoryMysqlRepo.h/cpp(1 张表)
│   │   │   ├── TagMysqlRepo.h/cpp    (1 张表)
│   │   │   ├── UserMysqlRepo.h/cpp   (1 张表)
│   │   │   └── OperationMysqlRepo.h/cpp (1 张表)
│   │   └── sqlite/
│   │       └── AlarmSqliteRepo.h  (SQLite 备选桩)
│   ├── logging/               # 日志
│   │   ├── ILogger.h            (6 方法纯虚接口)
│   │   └── SpdlogAdapter.h/cpp  (QFile 文件日志, 支持 logCallback)
│   ├── config/                # 配置
│   │   ├── IConfigRepo.h        (5 方法接口)
│   │   └── JsonConfigRepo.h/cpp (QJsonDocument 读写)
│   └── threading/             # 线程
│       └── ThreadGuard.h        (RAII QThread 包装)
├── pipeline/                  # 数据处理管道
│   ├── DataPipeline.h/cpp       (编排器: MessageBus + DoubleBuffer + 解析 + 归档)
│   ├── DataParseThread.h/cpp    (寄存器→工程值 + 死区 + 报警限检查)
│   └── HistorySampler.h/cpp     (1s 采样, 5min 批量归档, 环形缓存)
├── presentation/              # 表现层
│   ├── MainWindow.h/cpp         (主窗口, 4 标签页, 对接触控器)
│   ├── PidScene.h/cpp           (P&ID 场景, JSON 动态布局)
│   ├── TrendWidget.h/cpp        (趋势控件, QCustomPlot)
│   ├── TagConfigDialog.h/cpp    (位号编辑对话框)
│   └── widgets/               # 工业图元
│       ├── BaseGraphicsItem.h/cpp (基类, 位号绑定/报警颜色/质量码)
│       ├── TankItem.h/cpp         (储罐液位)
│       ├── ValveItem.h/cpp        (阀门开度)
│       ├── PumpItem.h/cpp         (泵运转)
│       ├── PipeItem.h/cpp         (管道流动)
│       └── DataLabelItem.h/cpp    (数据标签)
├── main.cpp                   # 入口 (59 行装配)
├── CMakeLists.txt             # CMake 构建 (190 行)
├── config/
│   ├── app.json               # 应用配置
│   ├── tags.json               # 16 个示例位号
│   └── scene.json              # P&ID 画面组态
└── 3pair/                     # 第三方库
    ├── qcustomplot/
    └── libmodbus-3.1.12/
```

---

## 3. 单例消除详情

原始项目通过 Meyer's 单例模式全局获取服务，任何文件都可以直接调用 `XXX::instance()`。重构后全部改为构造函数依赖注入。

### 3.1 TagConfigMgr::instance()

**原始 (TagConfigMgr.h/cpp, 449 行):**

```cpp
class TagConfigMgr {
public:
    static TagConfigMgr& instance();  // Meyer's singleton
    TagInfo getTag(quint32 tagId) const;
    TagInfo getTagByName(const QString& name) const;
    QList<TagInfo> getAllTags() const;
    // ...
};
// 调用方式：TagConfigMgr::instance().getTag(id)
```

**重构后 (TagManager.h/cpp, 57 + 180 行):**

```cpp
class TagManager : public QObject {
    Q_OBJECT
public:
    explicit TagManager(ITagRepo& repo, ILogger* logger = nullptr);
    // 相同的方法签名，无 static instance()
    // 增加了 QReadWriteLock 线程安全保护
};
// 调用方式：构造函数注入，通过 AppContext 或直接传递引用
```

**变化：**

- 移除了 Meyer's singleton
- 构造函数接受 `ITagRepo&` 和 `ILogger*`
- 新增 `QReadWriteLock` 读写锁保护所有数据访问
- 新增信号 `tagAdded`, `tagRemoved`, `configChanged`
- 职责不变：位号 CRUD、JSON 导入导出、Modbus 地址索引、设备分组

### 3.2 AuthManager::instance()

**原始 (AuthManager.h/cpp, 346 行):**

```cpp
class AuthManager : public QObject {
public:
    static AuthManager& instance();
    bool login(const QString& user, const QString& pass);
    void logout();
    // ...
};
// 内部直接调用 DatabaseManager::instance() 读写用户表
```

**重构后 (AuthManager.h/cpp, 47 + 101 行):**

```cpp
class AuthManager : public QObject {
    Q_OBJECT
public:
    explicit AuthManager(IUserRepo& userRepo, IOperationRepo& opRepo,
                         ILogger* logger = nullptr);
    void initialize();  // 新增：显式初始化，加载用户
    void shutdown();    // 新增：显式清理
    // 相同的登录/登出/权限接口
};
```

**变化：**

- 移除了 Meyer's singleton
- 注入 `IUserRepo&` + `IOperationRepo&` 替代直接 SQL
- 新增 `initialize()` / `shutdown()` 显式生命周期
- 分离出 `SessionManager` 类（会话超时独立逻辑）
- 分离出 `PasswordHasher` 类（PBKDF2-SHA256 独立逻辑）
- 新增 `confirmCriticalAction()` 关键操作确认
- 信号连接：`session.timeout → logout`

### 3.3 AlarmEngine::instance()

**原始 (AlarmEngine.h/cpp, 523 + 715 行):**

```cpp
class AlarmEngine : public QObject {
public:
    static AlarmEngine& instance();
    void triggerAlarm(quint32 tagId, AlarmLimit limit, float value, float threshold);
    // ...
};
// 内部调用：TagConfigMgr::instance().getTag(tagId)
// 内部调用：DatabaseManager::instance().insertAlarmRecord(...)
```

**重构后 (AlarmEngine.h/cpp, 169 + 644 行):**

```cpp
class AlarmEngine : public QObject {
    Q_OBJECT
public:
    explicit AlarmEngine(IAlarmRepo& alarmRepo, TagManager* tagManager,
                         ILogger* logger = nullptr);
    void initialize();  // 新增
    // 35+ 方法，18 信号
};
```

**变化：**

- 移除了 Meyer's singleton
- 注入 `IAlarmRepo&` + `TagManager*` 替代直接单例调用
- 新增 `initialize()` 加载现有活跃报警
- 组件化：`ShelveManager`、`SuppressionEngine`、`FloodDetector`、`ChatteringGuard` 各自独立类
- `AlarmKpiMonitor` 和 `AlarmChangeLog` 作为成员子对象
- 信号数量从 10 增加到 18 个
- 新增 `setDoubleBuffer()` 绑定数据源

### 3.4 DatabaseManager::instance()

**原始 (DatabaseManager.h/cpp, 277 + 1586 行):**

```cpp
class DatabaseManager : public QObject {
public:
    static DatabaseManager& instance();
    // 用户管理
    bool addUser(const QString& user, const QString& hash, int level);
    User loadUser(const QString& username);
    // 报警记录
    bool insertAlarmRecord(const AlarmEvent& event);
    bool updateAlarmAck(const QString& alarmId, ...);
    // 操作日志
    bool insertOperationLog(...);
    // 历史数据
    bool insertHistoryRecord(const HistoryRecord& rec);
    // 位号存储
    bool insertTag(const TagInfo& tag);
    // 共 30+ 个方法，涵盖 7 张表
};
```

**重构后（6 个 Repository）：**

| Repo 接口        | 方法数 | MySQL 实现                  | 管理表                                                 |
| -------------- | --- | ------------------------- | --------------------------------------------------- |
| IAlarmRepo     | 14  | AlarmMysqlRepo (179 行)    | alarm_events, alarm_change_log, alarm_kpi_snapshots |
| IHistoryRepo   | 3   | HistoryMysqlRepo (66 行)   | history_data                                        |
| ITagRepo       | 5   | TagMysqlRepo (50 行)       | tags                                                |
| IUserRepo      | 3   | UserMysqlRepo (60 行)      | users                                               |
| IOperationRepo | 1   | OperationMysqlRepo (26 行) | operation_log                                       |

**变化：**

- 1586 行巨类拆为 5 个接口 + 5 个实现 + 1 个连接池
- 每个 Repo 职责单一，独立可测
- 通过 `ConnectionPool` 共享数据库连接
- SQLite 备选：`AlarmSqliteRepo` 桩（28 行），可替换 MySQL 实现
- 纯虚接口支持 mock 测试

### 3.5 Logger::instance()

**原始 (Logger.h/cpp, 60 + 129 行):**

```cpp
class Logger {
public:
    static Logger& instance();
    void debug(const QString& msg);
    void info(const QString& msg);
    // ...
};
```

**重构后 (ILogger.h 24 行 + SpdlogAdapter.h/cpp 40 + 87 行):**

```cpp
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const QString& msg) = 0;
    virtual void debug/info/warn/error(const QString& msg) = 0;
    virtual void setLevel(LogLevel level) = 0;
    virtual void setLogCallback(std::function<void(const QString&)>) = 0;
};
```

**变化：**

- 提取 `ILogger` 纯虚接口，支持多实现
- `SpdlogAdapter` 为 QFile 文件日志实现（虽名 Spdlog，实际不依赖 spdlog 库）
- 新增 `setLogCallback()` 支持 UI 日志回显
- 枚举 `LogLevel` 独立定义

---

## 4. DataEngine 拆分对比

原始 `DataEngine`（428 行）是一个综合类，重构后拆为 3 个独立模块：

| 功能          | 原始                           | 重构后                             |
| ----------- | ---------------------------- | ------------------------------- |
| 数据管道编排      | DataEngine (428 行)           | DataPipeline (83 行 .cpp)        |
| 协议解析 + 报警检查 | 内嵌于 DataEngine               | DataParseThread (148 行 .cpp)    |
| 历史采样 + 归档   | HistoryArchiveThread (244 行) | HistorySampler (131 行 .cpp)     |
| Modbus 设备管理 | ModbusManager (325 行，单例)     | ModbusImpl (113 行，实现 IFieldbus) |

**关键变化：**

- `DataPipeline` 作为编排器通过 `QObject::connect` 连接信号，不再硬编码调用
- `DataParseThread` 注入 `TagManager*` 和 `AlarmEngine*` 替代单例
- `ModbusImpl` 实现 `IFieldbus` 接口，支持替换为 `SimulatorImpl` 或 `OpcUaImpl`
- 原始 `HistoryArchiveThread` 拆为 `HistorySampler`，新增环形缓存查询
- 新增 `RingBufMessageBus` 作为数据总线适配器

---

## 5. TagDef.h 拆分对比

原始 `TagDef.h`（581 行）包含所有数据结构，重构后按领域拆分为 4 个文件：

| 原始内容                                                | 重构后位置                                                                  | 行数                   |
| --------------------------------------------------- | ---------------------------------------------------------------------- | -------------------- |
| 枚举 (AlarmState, AlarmLimit, DataQuality, TagType 等) | domain/common/AlarmState.h, AlarmLimit.h, DataQuality.h                | 77 行合计               |
| TagInfo struct (运行时值 + 组态)                          | domain/tag/TagInfo.h (纯组态)                                             | 64 行                 |
| AlarmEvent struct                                   | domain/alarm/AlarmEvent.h                                              | 164 行 (含 7 个 struct) |
| User struct                                         | domain/auth/User.h                                                     | 15 行                 |
| 辅助逻辑 (deadband, deviation, ROC)                     | domain/tag/DeadbandFilter.h, DeviationChecker.h, RateOfChangeChecker.h | 79 行合计               |

**关键变化：**

- 原始 `TagDef.h` 中 `TagInfo` 混合了配置字段和运行时字段，重构后 `TagInfo` 仅保留配置字段，运行时值存在 `DoubleBuffer::TagSnapshot`
- 报警相关枚举和 struct 独立到 `alarm/` 域
- 辅助算法（死区、偏差、ROC）各自独立类，方便单元测试

---

## 6. 功能完整性对比

### 6.1 保留并完善的功能

| 功能                 | 原始状态                            | 重构后状态                              |
| ------------------ | ------------------------------- | ---------------------------------- |
| ISA-18.2 7 状态报警状态机 | 完整                              | 完整 + 独立 AlarmStateMachine 类        |
| EEMUA 191 KPI 监控   | 完整 (AlarmKpiMonitor)            | 完整 + 独立阈值配置                        |
| 报警抖动保护             | 内嵌于 AlarmEngine                 | 独立 ChatteringGuard 类               |
| 报警泛滥检测             | 内嵌于 AlarmEngine                 | 独立 FloodDetector 类 (10条/10分钟)      |
| 报警搁置管理             | 内嵌于 AlarmEngine                 | 独立 ShelveManager 类                 |
| 报警条件抑制             | 内嵌于 AlarmEngine                 | 独立 SuppressionEngine 类             |
| 报警变更审计             | 完整 (AlarmChangeLog)             | 完整 + approval workflow             |
| Modbus TCP/RTU 通信  | 完整 (ModbusComm + ModbusManager) | 完整 + IFieldbus 接口抽象                |
| 无锁环形缓冲             | 完整 (8192 槽)                     | 完整 + IMessageBus 适配器               |
| RCU 双缓冲            | 完整                              | 完整 + TagSnapshot 独立定义              |
| P&ID 画面            | 完整 (PidScene, 6 种图元)            | 完整 + JSON 动态布局                     |
| 趋势控件               | 完整 (QCustomPlot)                | 完整 (1H/8H/24H)                     |
| 位号配置对话框            | 完整                              | 完整 + TagManager DI                 |
| 用户认证               | 完整 (4 用户等级)                     | 完整 + SessionManager 超时             |
| 密码哈希               | PBKDF2-SHA256                   | PBKDF2-SHA256 + 向后兼容旧格式            |
| 日志系统               | Logger 单例，写文件                   | ILogger 接口 + SpdlogAdapter + UI 回调 |
| MySQL 双后端          | MySQL + SQLite                  | MySQL + SQLite + Repo 接口解耦         |
| 模拟器模式              | 无                               | SimulatorImpl 正弦波数据生成              |
| 数据导出               | 菜单项 (未实现)                       | 菜单项 (未实现)                          |
| 性能监控面板             | PerformanceMonitor.cpp          | 菜单项 (未实现)                          |

### 6.2 新增功能

| 功能                | 说明                                 |
| ----------------- | ---------------------------------- |
| 依赖注入容器            | AppContext 统一管理所有服务生命周期            |
| Fluent Builder 模式 | ApplicationBuilder 链式组装依赖图         |
| RAII 线程管理         | ThreadGuard 自动停止线程、释放资源            |
| 会话超时              | SessionManager 15 分钟自动登出           |
| 关键操作确认            | AuthManager::confirmCriticalAction |
| 报警 On-delay 定时器   | AlarmEngine 延迟触发抖动报警               |
| OPC UA 桩          | OpcUaImpl 预留接口，替换 Modbus 即可切换协议    |
| JSON 可配置启动        | config/app.json 控制数据库后端、总线类型       |

### 6.3 暂未实现/简化的功能

| 功能                          | 原始状态         | 重构后状态                             |
| --------------------------- | ------------ | --------------------------------- |
| SystemHealthMonitor         | 空桩 .h + .cpp | 移除（未包含）                           |
| DataBackupManager           | 空桩 .h + .cpp | 移除（未包含）                           |
| RealtimeDb                  | 6 行桩 .cpp    | 移除（未包含）                           |
| QtMqtt 集成                   | 引用了 qtmqtt 库 | 移除（未使用）                           |
| AlarmMysqlRepo::queryEvents | 未实现（返回空列表）   | 仅实现 insert + update，查询待完善         |
| AlarmSqliteRepo             | 未存在          | 桩实现（空方法体）                         |
| 历史趋势多 tag 查询                | 完整           | 完整 (queryTrend + queryMultiTrend) |
| P&ID 编辑器                    | 无            | 无（依赖 JSON 外部编辑）                   |
| OPC UA 真实实现                 | 无            | 桩 (OpcUaImpl 空方法)                 |

---

## 7. 接口抽象一览

```
IFieldbus ─── ModbusImpl (多线程)
         ├── SimulatorImpl (正弦波)
         └── OpcUaImpl (桩)

IMessageBus ─── RingBufMessageBus (LockFreeRingBuffer 8192 槽)

IAlarmRepo ─── AlarmMysqlRepo (MySQL, 3 表)
          └── AlarmSqliteRepo (SQLite 桩)

IHistoryRepo ─── HistoryMysqlRepo

ITagRepo ─── TagMysqlRepo

IUserRepo ─── UserMysqlRepo

IOperationRepo ─── OperationMysqlRepo

ILogger ─── SpdlogAdapter (QFile 文件日志)

IConfigRepo ─── JsonConfigRepo (QJsonDocument)
```

所有接口均可通过 `ApplicationBuilder` 替换实现（如切换数据库后端、切换总线类型），无需修改业务代码。

---

## 8. main.cpp 入口对比

### 原始 main.cpp (159 行)

```cpp
int main() {
    // 大量硬编码配置
    // 逐个创建并注册设备
    // 直接调用 Singleton::instance()
    // 手动管理线程生命周期
    // 内联样式表
}
```

### 重构后 main.cpp (59 行)

```cpp
int main() {
    QApplication app(argc, argv);
    app.setStyleSheet("...");  // 集中样式

    // 1. 加载配置
    auto appCfg = AppConfig::fromJson("config/app.json", *configRepo);

    // 2. 构建依赖图 (Fluent API)
    auto ctx = ApplicationBuilder()
        .withConfig(appCfg)
        .withLogger()
        .withFieldbus()       // 根据 config 自动选择 Modbus/Simulator/OPC
        .withDatabase()       // 根据 config 自动选择 MySQL/SQLite
        .withDomain()
        .withPipeline()
        .build();

    // 3. 加载数据
    ctx->tagManager->loadFromJson("config/tags.json");

    // 4. 创建控制器 (薄封装)
    DataController dataCtrl(...);
    AlarmController alarmCtrl(...);
    AuthController authCtrl(...);

    // 5. 显示窗口
    MainWindow window(dataCtrl, alarmCtrl, authCtrl, logger);
    window.show();
    return app.exec();
}
```

---

## 9. 编译方式

### MinGW

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
mingw32-make -j8
```

### MSVC (Visual Studio 2019)

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.5.3/msvc2019_64
cmake --build . --config Debug
```

### 前置依赖

- CMake ≥ 3.22
- Qt 6.5.3 (Widgets, Sql, Network, Multimedia, PrintSupport)
- C++17 编译器 (GCC 11+ / MSVC 2019+)
- 无外部 spdlog 依赖（SpdlogAdapter 为 QFile 自实现）

---

## 10. 总结

**核心变化：**

1. **零单例** — 5 个 Meyer's singleton 全部消除，改为构造函数依赖注入
2. **接口分离** — 9 个纯虚接口（IFieldbus, IMessageBus, IAlarmRepo, IHistoryRepo, ITagRepo, IUserRepo, IOperationRepo, ILogger, IConfigRepo），基础设施全部可替换
3. **分层架构** — Presentation → Application → Domain → Infrastructure，依赖方向自上而下
4. **巨类拆分** — DatabaseManager (1586 行) → 6 个 Repo，DataEngine (428 行) → 3 个 Pipeline 模块，TagDef.h (581 行) → 4 个领域文件
5. **组件化** — AlarmEngine 拆出 7 个子组件（StateMachine, ChatteringGuard, FloodDetector, ShelveManager, SuppressionEngine, KpiMonitor, ChangeLog）
6. **Assembly 模式** — ApplicationBuilder + AppContext 在 main.cpp 组装，业务代码无创建依赖
7. **线程安全** — QReadWriteLock (TagManager), QMutex (AlarmEngine, SuppressionEngine), 原子操作 (DoubleBuffer, LockFreeRingBuffer), RAII (ThreadGuard)
8. **构建系统** — Qt .pro → CMake，支持 MinGW/MSVC 双编译器
