# MY DCS — 分布式工业控制系统

> **Distributed Control System for chemical plants & refineries**
>
> ISA-18.2 alarm management · Modbus RTU/TCP fieldbus · P&ID graphics · MySQL/SQLite historian

---

## 概述

MY DCS 是一套基于 Qt6 / C++17 的分布式工业控制系统，面向化工厂、炼油厂等流程工业场景，提供完整的实时数据采集、报警管理、历史归档和 P&ID 图形监控能力。

**核心标准遵循：**
- **ISA-18.2-2016** — 报警管理全生命周期（7 状态状态机、搁置、抑制、抖振保护、泛滥检测、KPI 自监控）
- **ISA-101** — 人机界面标准（Observer/Operator/Engineer/Admin 四级权限、自动登出、操作审计）
- **EEMUA 191** — 报警系统性能指标（10 分钟滑动窗口、陈旧报警检测、系统健康评分 A/B/C/D/F）
- **IEC 62541** — 数据质量标记（Good/Uncertain/Bad/Stale/Simulated）

---

## 主要功能

### 数据采集
- **Modbus RTU/TCP** 双协议支持，最多 12 台 PLC 并发通信
- 每设备独立线程，自动重连 + 心跳监测
- 可配置轮询间隔（100ms–1000ms）、超时、重试次数

### 5 级实时数据管道
```
ModbusManager → LockFreeRingBuffer → DataParseThread → DoubleBuffer → UI
                                                              ↓
                                                   HistoryArchiveThread
```
- RCU 无锁双缓冲，写入 ~0.5μs，读取 <10ns
- 数据解析线程支持限值报警（HH/H/L/LL）、偏差报警、变化率报警、跳变检测

### ISA-18.2 报警管理（全部 4 层）
| 层级 | 功能 | 实现状态 |
|------|------|----------|
| L1 | 报警触发：On-Delay / Off-Delay / Deadband / 7 种分类 / 4 级优先级 | ✅ |
| L2 | 报警处理：7 状态状态机 + 确认绑定操作员身份 | ✅ |
| L3 | 报警抑制：搁置（自动过期）/ 设计抑制 / 条件抑制 / 搁置抑制 / 泛滥抑制 | ✅ |
| L4 | 报警审计：变更日志（操作员 + 旧值 + 新值 + 原因 + 审批） | ✅ |

### EEMUA 191 KPI 监控
- 10 分钟滑动窗口报警率（Average / Peak）
- 陈旧报警检测（>30min 未确认，Top-5 排名）
- 泛滥事件计数 + 持续时长
- 抖振报警检测（>N 次/分钟自动搁置）
- 系统健康评分（A/B/C/D/F）及阈值告警

### P&ID 图形监控
- JSON 配置文件驱动（`config/scene.json`），新增画面无需重新编译
- 动画组件：储罐（液位波浪）、阀门（蝶阀开/关/中间）、泵（启/停/故障旋转）、管道（流向动画）
- 数据标签实时显示，报警变色

### 历史归档 & 趋势
- 历史采样 1s，30 分钟内存环形缓冲（快速趋势查询）
- 批量写入 MySQL，5 分钟归档间隔
- QCustomPlot 趋势曲线：1h / 8h / 24h 时间范围，最大 20,000 点/系列

### 权限管理
- 4 级用户：Observer → Operator → Engineer → Admin
- PBKDF2-SHA256 盐化密码哈希（10000 轮迭代 + 16 字节随机盐）
- 关键操作二次确认
- 15 分钟无操作自动登出
- 全操作审计日志

### 样式
- 深色工业 HMI 配色（深灰底 + 蓝色 #0078d4 强调）
- 适配 24/7 控制室环境，减少视觉疲劳

---

## 技术栈

| 分类 | 技术 |
|------|------|
| 语言 | C++17 |
| UI 框架 | Qt 6.5.3（widgets, sql, network, serialbus, serialport） |
| 构建系统 | qmake / MSBuild（Visual Studio 2019, v142 工具集） |
| 数据库 | MySQL 8.0（InnoDB, utf8mb4）+ SQLite3 回退 |
| 现场总线 | libmodbus 3.1.12 |
| 图表 | QCustomPlot 2.1 |
| 测试 | Qt Test + CMake + CTest |
| 平台 | Windows x64（Debug / Release） |

---

## 项目结构

```
MYDSCProject/
├── main.cpp                    # 程序入口，深色样式，启动画面
├── MYDSCProject.h/cpp          # 主窗口：菜单栏/工具栏/停靠窗/状态栏/报警面板
├── TagDef.h                    # 所有 ISA-18.2 数据结构定义
│
├── DataEngine.h/cpp            # 核心调度引擎 —— 5 级管道组装
├── ModbusManager.h/cpp         # Modbus 设备管理器（最多 12 台）
├── ModbusComm.h/cpp            # Modbus 通信封装（基于 libmodbus）
├── DataParseThread.h/cpp       # 数据解析线程（限值/偏差/变化率报警检查）
├── HistoryArchiveThread.h/cpp  # 历史归档线程（1s 采样，5min 归档）
├── DoubleBuffer.h              # RCU 无锁双缓冲
├── lockFreeRingBuffer.h        # MPSC 无锁环形缓冲
│
├── AlarmEngine.h/cpp           # ISA-18.2 报警引擎
├── AlarmKpiMonitor.h/cpp       # EEMUA 191 KPI 监控器
├── AlarmChangeLog.h/cpp        # 报警变更审计日志
├── AuthManager.h/cpp           # 用户认证 & 权限管理
│
├── DatabaseManager.h/cpp       # MySQL/SQLite 数据库管理器（12 表）
├── ConfigManager.h/cpp         # JSON 配置管理
├── TagConfigMgr.h/cpp          # 位号配置管理
├── RealtimeDb.h                # 实时内存数据库
│
├── BaseGraphicsItem.h/cpp      # P&ID 图形基类
├── TankItem.h/cpp              # 储罐图形项
├── ValveItem.h/cpp             # 阀门图形项
├── PumpItem.h/cpp              # 泵图形项
├── PipeItem.h/cpp              # 管道图形项
├── DataLabelItem.h/cpp         # 数据标签图形项
├── PidScene.h/cpp              # P&ID 场景
├── PidView.h/cpp               # P&ID 视图
│
├── TrendWidget.h/cpp           # 趋势曲线控件
├── TagConfigDialog.h/cpp       # 位号配置对话框
├── PerformanceMonitor.h/cpp    # 性能监控
├── logger.h/cpp                # 日志系统（单例，日志轮转）
│
├── config/
│   ├── tags.json               # 位号/测点配置
│   └── scene.json              # P&ID 画面布局
├── docs/
│   ├── alarm_module_analysis.md
│   ├── database_schema.md
│   ├── data_engine_analysis.md
│   ├── qt_technology_analysis.md
│   └── tag_config_analysis.md
├── tests/
│   ├── TestDoubleBuffer.cpp
│   ├── TestAlarmEngine.cpp
│   ├── TestAuthManager.cpp
│   ├── CMakeLists.txt
│   └── _run_tests.bat
├── 3pair/
│   ├── qcustomplot/            # QCustomPlot 图表库
│   ├── libmodbus-3.1.12/       # libmodbus 通信库
│   └── qtmqtt/                 # Qt MQTT 模块（预留）
├── MYDSCProject.vcxproj        # VS2019 项目文件
├── MYDSCProject.qrc            # Qt 资源文件
├── MYDSCProject.ui             # Qt Designer 主窗口 UI
└── README.md
```

---

## 构建 & 运行

### 环境要求

| 依赖 | 版本 |
|------|------|
| Qt | 6.5.3（MSVC 2019 64-bit） |
| Visual Studio | 2019（v142 工具集） |
| MySQL | 8.0+（或仅使用 SQLite 回退） |
| libmodbus | 3.1.12（已内置在 `3pair/`） |
| CMake | 3.16+（仅测试用） |

### 构建步骤

1. 安装 **Qt 6.5.3**（MSVC 2019 64-bit）和 **Qt VS Tools** 扩展
2. 安装 **MySQL 8.0**（可选，无 MySQL 时自动回退 SQLite）
3. 编译 libmodbus：
   ```
   cd 3pair/libmodbus-3.1.12/src/win32
   MSBuild modbus.vcxproj /p:Configuration=Release /p:Platform=x64
   ```
4. 在 Visual Studio 2019 中打开 `MYDSCProject.vcxproj`
5. 选择 **Release x64**，生成解决方案
6. 确保 `config/tags.json` 和 `config/scene.json` 复制到输出目录

### 测试

```bash
cd tests
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=<Qt6安装路径>/msvc2019_64
cmake --build .
ctest
# 或直接
..\build_test.bat && ..\run_tests.bat
```

### 运行

```bash
MYDSCProject.exe
# 日志输出到 ./logs/
# 数据目录 ./data/（SQLite 自动创建）
```

数据库配置编辑 `config/tags.json` 中的 `database` 节，或首次启动自动使用 SQLite。

---

## 数据库

12 表 Schema（MySQL InnoDB / SQLite3）：

| 表 | 说明 |
|----|------|
| `tags` | 位号/测点配置（含报警限值、死区、On/Off-Delay、RoC 等） |
| `history_data` | 历史时序数据（按天分区） |
| `alarm_events` | ISA-18.2 完整报警记录（7 状态、搁置/抑制/OOS、确认用户、注释等） |
| `alarm_change_log` | 报警参数变更审计（变更前后值、原因、审批人、工作令号） |
| `alarm_kpi` | 报警 KPI 快照（报警率、泛滥数、抖振数、健康评分等） |
| `operation_log` | 操作审计日志 |
| `users` / `user_roles` / `user_sessions` | 用户 & 角色 & 会话管理 |
| `system_events` | 系统事件记录 |
| `shift_logs` | 交接班日志 |
| `backup_log` | 备份记录 |

详见 [docs/database_schema.md](docs/database_schema.md)

---

## 默认账户

| 用户名 | 密码 | 权限等级 |
|--------|------|----------|
| admin | admin123 | Admin（系统管理） |
| engineer | eng123 | Engineer（仪表工程师） |
| operator | op123 | Operator（操作员） |
| observer | obs123 | Observer（只读） |

密码使用 PBKDF2-SHA256 存储（10000 轮迭代 + 16 字节随机盐），格式 `iterations$salt$hash`。

---

## 文档

- [报警模块详细分析](docs/alarm_module_analysis.md)
- [数据库 Schema 设计](docs/database_schema.md)
- [数据引擎分析](docs/data_engine_analysis.md)
- [位号配置分析](docs/tag_config_analysis.md)
- [Qt 技术分析](docs/qt_technology_analysis.md)

---

## License

MIT
