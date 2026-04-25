#pragma once
#include <qstring.h>
#include <qdatetime.h>
#include <qhash.h>
#include <qvector.h>

// ============================================================
// ISA-18.2 基础枚举
// ============================================================

/**
 * @brief 数据质量码（IEC 标准）
 */
enum class DataQuality : qint8 {
    Good = 0,
    Bad = 1,
    Uncertain = 2
};

/**
 * @brief 报警限值等级（ISA-18.2 限值定义）
 *
 * Normal 不视为报警，其余四级从严重到轻微排列：
 * - HighHigh / LowLow: 严重偏离，可能引发安全事故
 * - High / Low: 偏离预警，操作员关注
 */
enum class AlarmLimit : quint8 {
    Normal   = 0,
    LowLow   = 1,
    Low      = 2,
    High     = 3,
    HighHigh = 4
};

/**
 * @brief ISA-18.2 报警优先级
 *
 * Critical → Major → Minor → Advisory
 * 决定：声光强度、响应时间要求、上报层级
 */
enum class AlarmPriority : quint8 {
    Advisory  = 0,   // 通知性，无声音，仅画面提示
    Minor     = 1,   // 一般报警，低频声音
    Major     = 2,   // 重要报警，高频声音 + 确认后消音
    Critical  = 3    // 紧急报警，声光持续直到确认
};

/**
 * @brief ISA-18.2 报警分类
 *
 * Process:    工艺过程偏离（如温度超限）
 * Safety:     安全系统触发（如联锁动作）
 * Environmental: 环保排放超标
 * Quality:    质量参数偏离
 * Machinery:  设备故障（如泵振动超限）
 */
enum class AlarmClassification : quint8 {
    Process       = 0,
    Safety        = 1,
    Environmental = 2,
    Quality       = 3,
    Machinery     = 4
};

/**
 * @brief 位号数据类型
 */
enum class TagType : quint8 {
    AI   = 0,
    AO   = 1,
    DI   = 2,
    DO   = 3,
    PID  = 4
};

// ============================================================
// ISA-18.2 报警事件状态机
// ============================================================

/**
 * @brief ISA-18.2 报警状态机状态
 *
 * 标准状态迁移:
 * ┌────────┐  超限   ┌──────────────────┐  Ack  ┌─────────────────┐
 * │ Normal ├───────▶│ ActiveUnack'ed   │──────▶│ ActiveAck'ed    │
 * └────────┘        └────────┬─────────┘       └────────┬─────────┘
 *           ▲                │ 值回正常                  │ 值回正常
 *           │                ▼                           ▼
 *           │         ┌──────────────────┐  Ack  ┌─────────────────┐
 *           └─────────│ ReturnToNormal   │◀──────│ ReturnToNormal  │
 *                     │ Unack'ed         │       │ Ack'ed          │
 *                     └──────────────────┘       └─────────────────┘
 *
 * Shelved: 操作员临时屏蔽（固定时长后自动恢复）
 */
enum class AlarmState : quint8 {
    Normal                    = 0,
    ActiveUnacknowledged      = 1,
    ActiveAcknowledged        = 2,
    ReturnToNormalUnacknowledged = 3,
    ReturnToNormalAcknowledged   = 4,
    Shelved                   = 5
};

/**
 * @brief ISA-18.2 报警 Rationalization 记录
 *
 * 每个报警在 Rationalization 阶段确定的属性。
 * 这些信息不参与运行期逻辑，但用于审计和持续改进。
 */
struct AlarmRationalization {
    QString consequence;            // 如果不处理的后果： "超温可能导致反应釜压力超限爆炸"
    QString operatorAction;         // 操作员应该做什么： "关闭加热阀 HV-101，打开冷却水"
    int    expectedResponseTimeSec = 300; // 预期响应时间（秒）
    QString designPhilosophy;       // "检测超温 → 限制温度 → 防止爆炸"
    QString approver;               // 工艺工程师审批人
    qint64 approvedDate = 0;        // 审批日期
};

// ============================================================
// 报警变更记录（ISA-18.2 Level 4）
// ============================================================

/**
 * @brief 报警参数变更记录
 *
 * ISA-18.2 要求：任何报警参数（限值、优先级、死区等）修改，
 * 必须记录谁、什么时候、为什么改、改了什么、谁审批的。
 */
struct AlarmChangeRecord {
    qint64  changeTime = 0;
    QString operatorName;           // 修改人
    quint32 tagId = 0;              // 位号ID
    QString fieldName;              // 字段名： "highLimit"
    QString oldValue;               // 旧值： "80.0"
    QString newValue;               // 新值： "85.0"
    QString reason;                 // 修改原因： "避免频繁误报警，工艺评审通过"
    bool    approved = false;       // 是否已审批
    QString approver;               // 审批人
};

// ============================================================
// 报警 KPI 快照（ISA-18.2 Level 3）
// ============================================================

/**
 * @brief 报警 KPI 快照
 *
 * ISA-18.2 推荐标准：
 * - 10分钟报警率 ≤ 10
 * - 平均报警率 ≤ 2/操作员/小时
 * - 高峰报警率 ≤ 10
 * - 陈旧报警（未确认超30分钟）< 5
 */
struct AlarmKpiSnapshot {
    qint64  timestamp = 0;
    int     alarmCount10min = 0;     // 最近10分钟报警数
    float   avgPerHour = 0.0f;       // 平均报警率/小时
    int     peakCount10min = 0;      // 10分钟峰值
    int     staleCount = 0;          // 未确认超30分钟
    int     totalActive = 0;         // 当前活跃报警数
    int     shelvedCount = 0;        // 被屏蔽数
    QStringList top5Frequent;        // 最频繁5个报警位号名
};

// ============================================================
// 报警事件结构体
// ============================================================

/**
 * @brief 完整报警事件（ISA-18.2 全字段）
 */
struct AlarmEvent {
    // === 标识字段 ===
    QString   alarmId;               // 唯一ID： "ALM_20260425143025_0001"
    quint32   tagId = 0;             // 关联位号ID
    QString   tagName;               // 位号名： "TIC_101"

    // === ISA-18.2 报警属性（Rationalization 阶段确定） ===
    AlarmLimit     limit      = AlarmLimit::Normal;        // 限值等级 HH/H/L/LL
    AlarmPriority  priority   = AlarmPriority::Major;      // 优先级
    AlarmClassification classification = AlarmClassification::Process; // 分类

    // === 触发信息 ===
    QString   description;           // "高高报报警，当前值=165.0℃，限值=160.0℃"
    float     triggerValue = 0.0f;   // 触发时的实际值
    float     thresholdValue = 0.0f; // 报警限值
    qint64    triggerTime = 0;       // 触发时间戳

    // === ISA-18.2 状态机 ===
    AlarmState state = AlarmState::ActiveUnacknowledged; // 当前状态

    // === 确认信息 ===
    bool      acknowledged = false;
    qint64    acknowledgeTime = 0;   // 确认时间

    // === 恢复信息 ===
    bool      active = true;
    qint64    returnToNormalTime = 0; // 值回正常时间
    qint64    returnAckTime = 0;      // 恢复确认时间
    float     returnValue = 0.0f;     // 恢复时的值

    // === Shelving ===
    bool      shelved = false;
    qint64    shelvedTime = 0;
    QString   shelveReason;
    int       shelveDurationSec = 0;  // 屏蔽时长，0=永久

    // === 便捷判断 ===
    bool isActive() const {
        return state == AlarmState::ActiveUnacknowledged
            || state == AlarmState::ActiveAcknowledged;
    }
    bool needsAttention() const {
        return state == AlarmState::ActiveUnacknowledged
            || state == AlarmState::ReturnToNormalUnacknowledged;
    }
};

// ============================================================
// 位号信息结构体
// ============================================================

/**
 * @brief 位号信息结构体（ISA-18.2 增强版）
 *
 * 这是整个DCS系统最核心的数据结构，每个测点对应一个TagInfo。
 *
 * 设计要点：
 * 1. 使用普通类型，线程安全由外部保证
 * 2. 静态属性（量程、报警限值）在组态时确定，运行期不变
 * 3. 报警死区(deadband)防止信号在报警限附近来回波动
 * 4. onDelayMs 防止噪声尖峰误报（值超限必须持续 X ms 才触发）
 */
struct TagInfo {
    quint32 tagId = 0;              // 唯一ID
    QString tagName;                // 位号名： "TIC_101"
    QString description;            // 描述： "1号反应釜温度"
    QString unit;                   // 工程单位： "℃"
    TagType tagType = TagType::AI;

    // === 实时数据 ===
    float       currentValue = 0.0f;
    float       setPoint = 0.0f;
    float       outputValue = 0.0f;
    AlarmLimit  alarmLimit = AlarmLimit::Normal;  // 当前超限等级
    DataQuality quality = DataQuality::Good;
    qint64      timestamp = 0;

    // === 量程 ===
    float engHigh = 100.0f;
    float engLow  = 0.0f;

    // === ISA-18.2 报警限值 ===
    float highHighLimit = 90.0f;
    float highLimit     = 80.0f;
    float lowLimit      = 10.0f;
    float lowLowLimit   = 5.0f;

    // === ISA-18.2 报警参数（Rationalization） ===
    float        deadband     = 1.0f;   // 滞环死区（防抖动）
    int          onDelayMs    = 3000;   // 确认时间，默认3秒（值超限持续3秒才触发报警）
    AlarmPriority priority    = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;

    // === Modbus 映射 ===
    int modbusServerAddr = 1;
    int modbusRegAddr    = 0;
    int modbusRegCount   = 1;

    // === PID 参数 ===
    float kp       = 1.0f;
    float ki       = 0.1f;
    float kd       = 0.0f;
    bool  autoMode = true;

    // === Rationalization 记录 ===
    AlarmRationalization rationalization;

    // === 便捷访问 ===
    float pv()      const { return currentValue; }
    float sp()      const { return setPoint; }
    float out()     const { return outputValue; }
    DataQuality qual() const { return quality; }
    AlarmLimit alarm()   const { return alarmLimit; }
};



