#pragma once
#include <qstring.h>
#include <qdatetime.h>
#include <qhash.h>
#include <qvector.h>
#include <qstringlist.h>

// ============================================================
// ISA-18.2 基础枚举（商业化完整版）
// ============================================================

/**
 * @brief 数据质量码（IEC 62541 标准）
 */
enum class DataQuality : qint8 {
    Good = 0,
    Bad = 1,
    Uncertain = 2
};

/**
 * @brief ISA-18.2 报警限值等级
 *
 * 四级限值从严重到轻微排列：
 * - HighHigh / LowLow: 严重偏离，可能引发安全事故
 * - High / Low: 偏离预警，操作员关注
 * - Deviation: 偏差报警（实际值与设定值偏差超限）
 * - RateOfChange: 变化率报警（值变化过快）
 */
enum class AlarmLimit : quint8 {
    Normal       = 0,
    LowLow       = 1,
    Low          = 2,
    High         = 3,
    HighHigh     = 4,
    Deviation    = 5,   // 偏差报警
    RateOfChange = 6    // 变化率报警
};

/**
 * @brief ISA-18.2 报警优先级（4级体系）
 *
 * 决定：声光强度、响应时间要求、上报层级、通知策略
 *
 * 商业DCS实践：
 * - Critical: ≤5%的报警，必须有独立安全系统(SIS)验证
 * - Major:    ≤15%的报警，需要操作员立即响应
 * - Minor:    ≤30%的报警，操作员在合理时间内响应
 * - Advisory: ≥50%的报警，仅提示，无声音
 */
enum class AlarmPriority : quint8 {
    Advisory  = 0,
    Minor     = 1,
    Major     = 2,
    Critical  = 3
};

/**
 * @brief ISA-18.2 报警分类
 *
 * 商业DCS扩展分类，覆盖化工/电力/制药等行业需求
 */
enum class AlarmClassification : quint8 {
    Process       = 0,   // 工艺过程偏离（如温度超限）
    Safety        = 1,   // 安全系统触发（如联锁动作）
    Environmental = 2,   // 环保排放超标
    Quality       = 3,   // 质量参数偏离
    Machinery     = 4,   // 设备故障（如泵振动超限）
    Electrical    = 5,   // 电气系统异常
    Instrument    = 6    // 仪表故障/信号异常
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
// ISA-18.2 报警状态机（7状态完整版）
// ============================================================

/**
 * @brief ISA-18.2 报警状态机（商业化7状态）
 *
 * 标准状态迁移图:
 *
 *                         超限触发
 * ┌────────┐ ──────────────────────▶ ┌──────────────────────┐
 * │ Normal │                         │ ActiveUnacknowledged │◀─┐
 * └───┬────┘ ◀──────────────────────└──────────┬───────────┘  │
 *     ▲    RTN Ack                      Ack │     │ 值回正常     │ 重新超限
 *     │                                  │     ▼              │ (重新触发)
 *     │                         ┌──────────────────────┐      │
 *     │                         │ ActiveAcknowledged   │──────┘
 *     │                         └──────────┬───────────┘
 *     │                                    │ 值回正常
 *     │                                    ▼
 *     │                          ┌──────────────────────────┐
 *     │        RTN Ack           │ ReturnToNormal           │
 *     └─────────────────────────│ Unacknowledged           │
 *                                └──────────┬───────────────┘
 *                                           │ Ack
 *                                           ▼
 *                                ┌──────────────────────────┐
 *                     RTN Ack    │ ReturnToNormal           │
 *                 ──────────────│ Acknowledged → Normal    │
 *                                └──────────────────────────┘
 *
 * 非活跃状态（ISA-18.2 商业标准要求）:
 * - Shelved:             操作员临时屏蔽（固定时长后自动恢复）
 * - SuppressedByDesign:  设计抑制（工程师配置，永久抑制已知无效报警）
 * - OutOfService:        停用（设备检修/维护期间，系统级抑制）
 *
 * 商业化要点：
 * - Shelved 报警到期自动恢复，必须有原因记录
 * - SuppressedByDesign 必须有工程师审批，记录在变更日志
 * - OutOfService 必须关联设备维护工单
 */
enum class AlarmState : quint8 {
    Normal                          = 0,
    ActiveUnacknowledged            = 1,
    ActiveAcknowledged              = 2,
    ReturnToNormalUnacknowledged    = 3,
    ReturnToNormalAcknowledged      = 4,
    Shelved                         = 5,
    SuppressedByDesign              = 6,   // ISA-18.2 设计抑制
    OutOfService                    = 7    // ISA-18.2 停用状态
};

/**
 * @brief 报警抑制类型（ISA-18.2 区分不同抑制原因）
 */
enum class AlarmSuppressionType : quint8 {
    None              = 0,   // 无抑制
    DesignSuppression = 1,   // 设计抑制：工程师在Rationalization阶段决定此报警无效
    OutOfService      = 2,   // 停用：设备检修期间
    Interlock         = 3,   // 联锁抑制：安全联锁动作导致相关报警无效
    Override          = 4    // 强制抑制：管理员临时抑制（需审批）
};

/**
 * @brief 报警通知方式
 */
enum class AlarmNotificationType : quint8 {
    None       = 0,   // 无通知
    Visual     = 1,   // 仅画面提示
    Audible    = 2,   // 声音+画面
    Page       = 3,   // 寻呼通知
    Email      = 4,   // 邮件通知
    Escalation = 5    // 升级通知（超时未处理自动升级到上级）
};

// ============================================================
// ISA-18.2 报警 Rationalization 记录
// ============================================================

/**
 * @brief ISA-18.2 报警 Rationalization 完整记录
 *
 * Rationalization 是 ISA-18.2 的核心概念：
 * 每个报警必须经过工艺工程师评审，确定其合理性。
 *
 * 商业化要求：
 * - 无 Rationalization 记录的报警不能投入运行
 * - 所有字段必须填写完整
 * - 定期复审（通常每年一次）
 */
struct AlarmRationalization {
    // === 核心字段（ISA-18.2 强制要求） ===
    QString consequence;                // 不处理的后果："超温可能导致反应釜压力超限爆炸"
    QString operatorAction;             // 操作员应该做什么："关闭加热阀 HV-101，打开冷却水"
    int    expectedResponseTimeSec = 300;// 预期响应时间（秒）
    QString designPhilosophy;           // "检测超温 → 限制温度 → 防止爆炸"
    QString approver;                   // 工艺工程师审批人
    qint64 approvedDate = 0;            // 审批日期

    // === 商业化扩展字段 ===
    QString correctiveAction;           // 纠正措施："检查冷却水流量，确认FCV-101开度"
    QString relatedDocuments;           // 关联文档："SOP-2024-015 第3.2节"
    QString area;                       // 报警区域："反应工段" / "罐区"
    QString zone;                       // 报警区域细分："R-101区域"
    int    reviewCycleMonths = 12;      // 复审周期（月），默认12个月
    qint64 lastReviewDate = 0;          // 上次复审日期
    QString reviewer;                   // 复审人
    bool   isValid = true;              // Rationalization 是否有效（过期后为false）
};

// ============================================================
// 报警变更记录（ISA-18.2 Level 4 - 商业化增强版）
// ============================================================

/**
 * @brief 报警参数变更记录（含审批流程）
 *
 * ISA-18.2 要求：任何报警参数修改必须记录：
 * - 谁（操作人）
 * - 什么时候（时间戳）
 * - 为什么（原因）
 * - 改了什么（字段、旧值、新值）
 * - 谁审批的（审批人）
 *
 * 商业化增强：
 * - 审批状态机（待审批→已审批/已驳回）
 * - 关联工单号
 * - 有效期（临时修改自动恢复）
 */
struct AlarmChangeRecord {
    qint64  changeTime = 0;
    QString operatorName;           // 修改人
    quint32 tagId = 0;              // 位号ID
    QString fieldName;              // 字段名："highLimit"
    QString oldValue;               // 旧值："80.0"
    QString newValue;               // 新值："85.0"
    QString reason;                 // 修改原因："避免频繁误报警，工艺评审通过"

    // === 审批流程 ===
    bool    approved = false;       // 是否已审批
    QString approver;               // 审批人
    qint64  approveTime = 0;        // 审批时间
    bool    rejected = false;       // 是否已驳回
    QString rejectReason;           // 驳回原因

    // === 商业化扩展 ===
    QString workOrderNo;            // 关联工单号："WO-2026-0135"
    qint64  validUntil = 0;         // 有效期截止（0=永久），临时修改自动恢复
    bool    autoReverted = false;   // 是否已自动恢复

    // === 审计追踪 ===
    QString sessionId;              // 操作会话ID（防抵赖）
    QString workstation;            // 操作站名称
};

// ============================================================
// 报警 KPI 快照（ISA-18.2 Level 3 - 商业化增强版）
// ============================================================

/**
 * @brief 报警 KPI 快照（EEMUA 191 / ISA-18.2 商业指标）
 *
 * EEMUA 191 推荐标准（全球DCS行业基准）：
 * - 10分钟报警率 ≤ 10（可管理）
 * - 平均报警率 ≤ 2/操作员/小时（可接受）
 * - 高峰报警率 ≤ 10
 * - 陈旧报警（未确认超30分钟）< 5%
 * - 泛滥报警（flood）占比 < 3%
 *
 * 商业化增强指标：
 * - 报警泛滥事件次数/持续时间
 * - 报警确认时间分布
 * - 报警重复率（同一报警反复触发）
 * - 各优先级/分类分布
 */
struct AlarmKpiSnapshot {
    qint64  timestamp = 0;

    // === EEMUA 191 核心指标 ===
    int     alarmCount10min = 0;     // 最近10分钟报警数
    float   avgPerHour = 0.0f;       // 平均报警率/小时
    int     peakCount10min = 0;      // 10分钟峰值
    int     staleCount = 0;          // 未确认超30分钟
    int     totalActive = 0;         // 当前活跃报警数
    int     shelvedCount = 0;        // 被屏蔽数
    int     suppressedCount = 0;     // 被抑制数

    // === 商业化增强指标 ===
    int     floodEventCount = 0;     // 报警泛滥事件次数（10分钟内>10个报警算一次泛滥）
    float   floodDurationMin = 0.0f; // 泛滥持续时间（分钟）
    float   avgAckTimeSec = 0.0f;    // 平均确认时间（秒）
    int     chatteringCount = 0;     // 震荡报警数（同一报警1分钟内重复触发≥3次）
    int     staleAlarmPercent = 0;   // 陈旧报警百分比

    // === 分布统计 ===
    int     criticalCount = 0;       // Critical 优先级报警数
    int     majorCount = 0;          // Major 优先级报警数
    int     minorCount = 0;          // Minor 优先级报警数
    int     advisoryCount = 0;       // Advisory 优先级报警数

    // === Top-N 分析 ===
    QStringList top5Frequent;        // 最频繁5个报警位号名
    QStringList top5Stale;           // 最陈旧5个报警位号名

    // === 系统健康度 ===
    float   systemHealthScore = 100.0f;  // 报警系统健康度评分（0-100）
    QString healthGrade;                  // 健康等级：A/B/C/D/F
};

// ============================================================
// 报警事件结构体（ISA-18.2 商业化完整版）
// ============================================================

/**
 * @brief 完整报警事件（ISA-18.2 全字段 + 商业化扩展）
 *
 * 商业化增强：
 * - 操作员注释（Operator Annotation）
 * - 报警区域/分区
 * - 通知策略
 * - 关联报警组
 * - 重复计数
 */
struct AlarmEvent {
    // === 标识字段 ===
    QString   alarmId;               // 唯一ID："ALM_20260425143025_0001"
    quint32   tagId = 0;             // 关联位号ID
    QString   tagName;               // 位号名："TIC_101"

    // === ISA-18.2 报警属性（Rationalization 阶段确定） ===
    AlarmLimit     limit      = AlarmLimit::Normal;
    AlarmPriority  priority   = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;

    // === 触发信息 ===
    QString   description;           // "高高报报警，当前值=165.0℃，限值=160.0℃"
    float     triggerValue = 0.0f;   // 触发时的实际值
    float     thresholdValue = 0.0f; // 报警限值
    qint64    triggerTime = 0;       // 触发时间戳

    // === ISA-18.2 状态机 ===
    AlarmState state = AlarmState::ActiveUnacknowledged;

    // === 确认信息 ===
    bool      acknowledged = false;
    qint64    acknowledgeTime = 0;   // 确认时间
    QString   acknowledgeUser;       // 确认操作员

    // === 恢复信息 ===
    bool      active = true;
    qint64    returnToNormalTime = 0; // 值回正常时间
    qint64    returnAckTime = 0;      // 恢复确认时间
    float     returnValue = 0.0f;     // 恢复时的值

    // === Shelving（操作员临时屏蔽） ===
    bool      shelved = false;
    qint64    shelvedTime = 0;
    QString   shelveReason;
    int       shelveDurationSec = 0;  // 屏蔽时长，0=永久
    QString   shelveUser;             // 屏蔽操作员

    // === Suppression（设计抑制） ===
    AlarmSuppressionType suppressionType = AlarmSuppressionType::None;
    QString   suppressionReason;       // 抑制原因
    QString   suppressionUser;         // 抑制操作员
    qint64    suppressionTime = 0;     // 抑制时间

    // === Out-of-Service（停用） ===
    bool      outOfService = false;
    QString   outOfServiceReason;      // 停用原因
    QString   outOfServiceUser;        // 停用操作员
    QString   workOrderNo;             // 关联维护工单号

    // === 操作员注释（ISA-18.2 Operator Annotation） ===
    QString   operatorAnnotation;      // 操作员添加的注释
    qint64    annotationTime = 0;      // 注释时间
    QString   annotationUser;          // 注释操作员

    // === 区域/分区 ===
    QString   area;                    // 报警区域："反应工段"
    QString   zone;                    // 报警分区："R-101区域"

    // === 通知策略 ===
    AlarmNotificationType notificationType = AlarmNotificationType::Audible;
    qint64    lastNotificationTime = 0;    // 上次通知时间
    int       notificationCount = 0;       // 通知次数

    // === 重复计数（Flood Protection 用） ===
    int       repeatCount = 0;            // 重复触发次数
    qint64    firstTriggerTime = 0;       // 首次触发时间（用于计算重复率）

    // === 便捷判断 ===
    bool isActive() const {
        return state == AlarmState::ActiveUnacknowledged
            || state == AlarmState::ActiveAcknowledged;
    }
    bool needsAttention() const {
        return state == AlarmState::ActiveUnacknowledged
            || state == AlarmState::ReturnToNormalUnacknowledged;
    }
    bool isSuppressed() const {
        return state == AlarmState::SuppressedByDesign
            || state == AlarmState::OutOfService;
    }
    bool isShelved() const {
        return state == AlarmState::Shelved;
    }
};

// ============================================================
// 报警过滤条件（ISA-18.2 报警汇总查询用）
// ============================================================

/**
 * @brief 报警过滤条件
 *
 * 用于报警汇总列表的多条件过滤，支持：
 * - 按优先级过滤
 * - 按分类过滤
 * - 按区域过滤
 * - 按状态过滤
 * - 按时间范围过滤
 * - 按关键字搜索
 */
struct AlarmFilter {
    QList<AlarmPriority> priorities;       // 优先级过滤（空=全部）
    QList<AlarmClassification> classifications; // 分类过滤（空=全部）
    QList<AlarmState> states;              // 状态过滤（空=全部）
    QStringList areas;                     // 区域过滤（空=全部）
    qint64     fromTime = 0;              // 起始时间（0=不限）
    qint64     toTime = 0;                // 截止时间（0=不限）
    QString    keyword;                    // 关键字搜索（位号名/描述）
    bool       includeShelved = false;     // 是否包含已屏蔽
    bool       includeSuppressed = false;  // 是否包含已抑制
    bool       includeOutOfService = false;// 是否包含已停用
};

// ============================================================
// ISA-18.2 条件抑制规则（商业化增强 — 不足1修复）
// ============================================================

/**
 * @brief 报警条件抑制规则（ISA-18.2 Suppression-by-Condition）
 *
 * 根据其他位号的状态动态抑制报警，防止报警风暴。
 *
 * 典型工业场景：
 * - 泵 P101 停运(DI=0) → 抑制泵出口流量低报 FIC_102
 * - 安全联锁触发 DI_SIS_01=1 → 抑制所有关联工艺报警
 * - 设备停用 → 抑制该设备所有报警
 */
struct SuppressionRule {
    quint32     ruleId = 0;           // 规则唯一ID
    quint32     targetTagId;          // 被抑制的报警位号
    quint32     conditionTagId;       // 条件位号（判据）
    QString     conditionExpr;        // 条件表达式："==0"、"==1"、">100"、"<0"
    QString     reason;               // 抑制原因说明
    bool        enabled = true;       // 规则启用/禁用
    QString     createdBy;            // 创建人
    QString     approver;             // 审批人
    qint64      createdTime = 0;      // 创建时间
};

// ============================================================
// 报警泛滥事件记录
// ============================================================

/**
 * @brief 报警泛滥事件（Alarm Flood）
 *
 * ISA-18.2 定义：10分钟内报警数超过10个即为泛滥事件。
 * 商业DCS必须记录每次泛滥的详细信息，用于事后分析。
 */
struct AlarmFloodEvent {
    qint64     startTime = 0;            // 泛滥开始时间
    qint64     endTime = 0;              // 泛滥结束时间（0=进行中）
    int        alarmCount = 0;           // 泛滥期间报警总数
    int        peakRate = 0;             // 峰值报警率
    QStringList topContributors;         // 贡献最多的位号（Top5）
    QString    triggerCause;             // 触发原因（事后分析填写）
    QString    analyst;                  // 分析人
    qint64     analysisTime = 0;         // 分析时间
};

// ============================================================
// 位号信息结构体（ISA-18.2 商业化增强版）
// ============================================================

/**
 * @brief 位号信息结构体（ISA-18.2 商业化完整版）
 *
 * 商业化增强：
 * - Off-Delay 恢复延时（值回正常后延迟确认恢复，防止抖动）
 * - 报警区域/分区
 * - 偏差报警限值
 * - 变化率报警限值
 * - 报警启用/禁用开关
 * - 重复报警保护参数
 */
struct TagInfo {
    quint32 tagId = 0;              // 唯一ID
    QString tagName;                // 位号名："TIC_101"
    QString description;            // 描述："1号反应釜温度"
    QString unit;                   // 工程单位："℃"
    TagType tagType = TagType::AI;

    // === 实时数据 ===
    float       currentValue = 0.0f;
    float       setPoint = 0.0f;
    float       outputValue = 0.0f;
    AlarmLimit  alarmLimit = AlarmLimit::Normal;
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

    // === 偏差报警限值 ===
    float deviationLimit = 10.0f;       // 偏差限值（|PV-SP| > 此值触发）
    bool  deviationEnabled = false;     // 是否启用偏差报警

    // === 变化率报警限值 ===
    float rateOfChangeLimit = 0.0f;     // 变化率限值（单位/秒）
    int   rateOfChangePeriodMs = 60000; // 变化率计算周期（毫秒）
    bool  rateOfChangeEnabled = false;  // 是否启用变化率报警

    // === ISA-18.2 报警参数（Rationalization） ===
    float        deadband     = 1.0f;   // 滞环死区（防抖动）
    int          onDelayMs    = 3000;   // 触发延时，默认3秒
    int          offDelayMs   = 0;      // 恢复延时（ISA-18.2 Off-Delay），0=立即恢复
    AlarmPriority priority    = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;

    // === 报警启用开关 ===
    bool  alarmEnabled = true;          // 报警总开关
    bool  highHighEnabled = true;       // HH报警启用
    bool  highEnabled     = true;       // H报警启用
    bool  lowEnabled      = true;       // L报警启用
    bool  lowLowEnabled   = true;       // LL报警启用

    // === 重复报警保护（Chattering/Flapping Protection） ===
    int   maxRepeatsPerMin = 3;         // 每分钟最大重复次数（超过则自动屏蔽）
    int   repeatCount = 0;              // 当前重复计数
    qint64 repeatWindowStart = 0;       // 重复计数窗口起始时间

    // === 区域/分区 ===
    QString area;                       // 报警区域："反应工段"
    QString zone;                       // 报警分区："R-101区域"

    // === 通知策略 ===
    AlarmNotificationType notificationType = AlarmNotificationType::Audible;
    int   escalationTimeoutSec = 0;      // 升级超时（秒），0=不升级

    // === Modbus 映射 ===
    int modbusDeviceId   = 0;
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

    /**
     * @brief 检查指定限值等级是否启用
     */
    bool isLimitEnabled(AlarmLimit lim) const {
        switch (lim) {
        case AlarmLimit::HighHigh: return highHighEnabled;
        case AlarmLimit::High:     return highEnabled;
        case AlarmLimit::Low:      return lowEnabled;
        case AlarmLimit::LowLow:   return lowLowEnabled;
        case AlarmLimit::Deviation: return deviationEnabled;
        case AlarmLimit::RateOfChange: return rateOfChangeEnabled;
        default: return false;
        }
    }
};
