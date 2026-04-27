#pragma once
#include "export.h"
#include "TagDef.h"
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSoundEffect>

#include "AlarmKpiMonitor.h"
#include "AlarmChangeLog.h"

class DoubleBuffer;

// ============================================================
// ISA-18.2 商业化报警引擎（完整版）
// ============================================================

/**
 * @brief ISA-18.2 商业化报警引擎
 *
 * 覆盖 ISA-18.2 成熟度模型 Level 1-4 + 商业化增强：
 *
 * Level 1 - 技术实现：
 *   - 7 状态机（Normal / ActiveUnack / ActiveAck / RTNUnack / RTNAck / Shelved / Suppressed / OOS）
 *   - On-Delay 触发延时（值持续超限才触发，防噪声尖峰）
 *   - Off-Delay 恢复延时（值回正常后延迟确认恢复，防抖动）
 *   - 死区滞环（Deadband，值回正常需越过死区）
 *   - 报警升级（Low → High 等）
 *   - 报警去重（已有活跃报警不重复触发）
 *   - 重复报警保护（Chattering Protection，自动屏蔽震荡报警）
 *
 * Level 2 - Rationalization：
 *   - 4 级优先级（Critical/Major/Minor/Advisory）
 *   - 7 种分类（Process/Safety/Enviro/Quality/Machinery/Electrical/Instrument）
 *   - Shelving（操作员临时屏蔽，自动恢复）
 *   - Suppression-by-Design（设计抑制，工程师审批）
 *   - Out-of-Service（设备停用，关联工单）
 *   - 操作员注释（Operator Annotation）
 *   - 后果记录+操作指南（AlarmRationalization）
 *   - 报警区域/分区管理
 *
 * Level 3 - KPI 监控（EEMUA 191）：
 *   - 滑动窗口 10 分钟报警率
 *   - 平均报警率/小时
 *   - 高峰报警率
 *   - 陈旧报警检测（>30min 未确认）
 *   - 报警泛滥事件检测与记录
 *   - 震荡报警检测
 *   - Top-N 最频繁报警
 *   - 系统健康度评分（A/B/C/D/F）
 *
 * Level 4 - 变更管理：
 *   - 参数变更全记录（谁/何时/改什么/为什么/谁审批）
 *   - 审批流程（待审批→已审批/已驳回）
 *   - 临时修改自动恢复
 *   - 审计报告导出
 *
 * 商业化增强：
 *   - 报警过滤（按优先级/分类/区域/状态/时间/关键字）
 *   - 报警通知策略（声光/寻呼/邮件/升级）
 *   - 报警泛滥保护（Flood Protection）
 *   - 偏差报警 / 变化率报警
 *   - 报警历史持久化到数据库
 *
 * 线程安全：所有 public 方法使用 QMutex 保护
 */
class BUSINESS_EXPORT AlarmEngine : public QObject {
    Q_OBJECT
public:
    static AlarmEngine& instance();

    /// 初始化报警引擎（加载音频、启动 KPI 定时器）
    void initialize();

    /// 设置 DoubleBuffer 引用（用于条件抑制规则评估）
    void setDoubleBuffer(DoubleBuffer* buffer) { m_doubleBuffer = buffer; }

    // ============================================================
    // Level 1: 报警触发与状态管理
    // ============================================================

    /**
     * @brief 触发报警（ISA-18.2 Level 1）
     *
     * 完整逻辑链：
     * 1. 检查报警是否启用（alarmEnabled + isLimitEnabled）
     * 2. 检查是否被抑制（SuppressedByDesign / OutOfService）
     * 3. 检查是否被屏蔽（Shelved）
     * 4. 检查重复报警保护（Chattering Protection）
     * 5. 检查是否已有同 tag 报警 → 升级/去重
     * 6. 进入 On-Delay 等待
     * 7. On-Delay 到期 → 创建报警事件
     */
    void triggerAlarm(quint32 tagId,
                      AlarmLimit limit,
                      float triggerValue,
                      float thresholdValue,
                      AlarmPriority priority = AlarmPriority::Major,
                      AlarmClassification classification = AlarmClassification::Process,
                      int onDelayMs = 3000);

    /**
     * @brief 值回正常（ISA-18.2 Return-to-Normal）
     *
     * 支持 Off-Delay：
     * - 如果位号配置了 offDelayMs > 0，值回正常后不立即恢复
     * - 需要值持续在正常范围内 offDelayMs 毫秒后才确认恢复
     * - 防止信号在限值附近抖动导致频繁报警/恢复
     */
    void clearAlarm(quint32 tagId, float returnValue);

    // ============================================================
    // Level 1: 确认操作（含身份绑定 — 不足6修复）
    // ============================================================

    /// 确认报警（ActiveUnack → ActiveAck）— 旧版兼容
    void acknowledgeAlarm(const QString& alarmId);

    /// 确认报警（带操作员身份 — ISA-18.2 要求身份绑定）
    bool acknowledgeAlarm(const QString& alarmId, const QString& operatorName);

    /// 按位号确认（旧版兼容）
    void acknowledgeAlarmByTagId(quint32 tagId);

    /// 按位号确认（带操作员身份）
    bool acknowledgeAlarmByTagId(quint32 tagId, const QString& operatorName);

    /// 确认所有活跃报警
    void acknowledgeAll();

    /// 确认所有活跃报警（带操作员身份）
    void acknowledgeAll(const QString& operatorName);

    /// 确认恢复（RTNUnack → Normal）
    void acknowledgeReturnToNormal(const QString& alarmId);
    void acknowledgeReturnToNormalByTagId(quint32 tagId);
    void acknowledgeAllReturnToNormal();

    // ============================================================
    // Level 2: Shelving（操作员临时屏蔽）
    // ============================================================

    /**
     * @brief 屏蔽报警（ISA-18.2 Shelve）
     *
     * 操作员临时屏蔽报警，通常用于已知故障等待维修。
     * 屏蔽到期自动恢复，或操作员手动取消屏蔽。
     *
     * @param tagId      位号ID
     * @param reason     屏蔽原因（必须填写）
     * @param durationSec 0=永久  >0=自动恢复秒数
     * @param user       操作员名称
     */
    void shelveAlarm(quint32 tagId, const QString& reason,
                     int durationSec = 3600, const QString& user = QString());

    /// 便利方法：屏蔽报警（分钟为单位，自动获取当前用户）
    void shelveAlarm(quint32 tagId, int durationMin);

    /// 取消屏蔽
    void unshelveAlarm(quint32 tagId);

    /// 获取所有屏蔽中的报警
    QList<AlarmEvent> shelvedAlarms() const;

    // ============================================================
    // Level 2: Suppression-by-Design（设计抑制）
    // ============================================================

    /**
     * @brief 设计抑制（ISA-18.2 Suppression-by-Design）
     *
     * 工程师在 Rationalization 阶段决定此报警无效。
     * 必须有审批记录，记录在变更日志中。
     *
     * @param tagId        位号ID
     * @param reason       抑制原因（必须填写）
     * @param user         工程师名称
     * @param approver     审批人
     */
    void suppressByDesign(quint32 tagId, const QString& reason,
                          const QString& user, const QString& approver);

    /// 便利方法：设计抑制（自动获取当前用户）
    void suppressAlarm(quint32 tagId, const QString& reason);

    /// 取消设计抑制
    void unsuppressByDesign(quint32 tagId);

    /// 便利方法：取消设计抑制
    void unsuppressAlarm(quint32 tagId);

    // ============================================================
    // Level 2: Suppression-by-Condition 条件抑制（商业化增强 — 不足1修复）
    // ============================================================

    /// 添加条件抑制规则
    bool addSuppressionRule(const SuppressionRule& rule);

    /// 移除条件抑制规则
    void removeSuppressionRule(quint32 ruleId);

    /// 启用/禁用规则
    void setSuppressionRuleEnabled(quint32 ruleId, bool enabled);

    /// 获取所有抑制规则
    QVector<SuppressionRule> suppressionRules() const;

    /// 评估抑制：检查指定位号是否应被条件抑制
    bool evaluateSuppression(quint32 tagId) const;

    // ============================================================
    // Level 2: Out-of-Service（设备停用）
    // ============================================================

    /**
     * @brief 设备停用（ISA-18.2 Out-of-Service）
     *
     * 设备检修/维护期间，系统级抑制所有相关报警。
     * 必须关联维护工单号。
     *
     * @param tagId        位号ID
     * @param reason       停用原因
     * @param user         操作员名称
     * @param workOrderNo  关联维护工单号
     */
    void setOutOfService(quint32 tagId, const QString& reason,
                         const QString& user, const QString& workOrderNo);

    /// 便利方法：设备停用（自动获取当前用户）
    void setOutOfService(quint32 tagId, const QString& reason);

    /// 恢复服务
    void returnToService(quint32 tagId);

    // ============================================================
    // Level 2: 操作员注释
    // ============================================================

    /**
     * @brief 添加操作员注释（ISA-18.2 Operator Annotation）
     *
     * 操作员可以对报警添加注释，记录处理过程。
     * 所有注释记录在审计日志中。
     */
    void annotateAlarm(const QString& alarmId, const QString& annotation,
                       const QString& user);

    /// 便利方法：添加操作员注释（自动获取当前用户）
    void annotateAlarm(const QString& alarmId, const QString& annotation);

    // ============================================================
    // Level 2: 报警参数动态修改（含变更记录）
    // ============================================================

    /// 修改报警限值（含变更记录，ISA-18.2 Level 4）
    bool setAlarmLimit(quint32 tagId, const QString& fieldName,
                       float newValue, const QString& operatorName,
                       const QString& reason);

    /// 修改报警优先级（含变更记录）
    bool setAlarmPriority(quint32 tagId, AlarmPriority newPriority,
                          const QString& operatorName, const QString& reason);

    // ============================================================
    // Level 2: 报警过滤与查询
    // ============================================================

    /// 所有活跃报警（Active + RTN Unack/Ack）
    QList<AlarmEvent> activeAlarms() const;

    /// 未确认报警（需要操作员立即关注的）
    QList<AlarmEvent> unacknowledgedAlarms() const;

    /// 按位号查报警
    AlarmEvent alarmByTagId(quint32 tagId) const;

    /// 报警历史（最多 limit 条）
    QList<AlarmEvent> alarmHistory(int limit = 1000) const;

    /// 按过滤条件查询报警（ISA-18.2 报警汇总）
    QList<AlarmEvent> filteredAlarms(const AlarmFilter& filter) const;

    /// 按严重程度分类计数
    int activeAlarmCount() const;
    int activeAlarmCount(AlarmLimit limit) const;
    int activeAlarmCount(AlarmPriority priority) const;

    /// 未确认报警数
    int unacknowledgedCount() const;

    /// 被抑制报警数
    int suppressedCount() const;

    /// 被停用报警数
    int outOfServiceCount() const;

    /// 获取所有区域列表
    QStringList areas() const;

    /// 按区域获取报警
    QList<AlarmEvent> alarmsByArea(const QString& area) const;

    // ============================================================
    // Level 3: KPI 访问
    // ============================================================

    AlarmKpiMonitor* kpiMonitor() { return &m_kpiMonitor; }

    /// 便利方法：获取当前 KPI 快照
    AlarmKpiSnapshot kpiSnapshot() const;

    /// 便利方法：获取 Top-N 频发报警（返回 tagId → 触发次数）
    QVector<QPair<quint32, int>> topFrequentAlarms(int topN = 5) const;

    /// 获取报警泛滥事件列表
    QVector<AlarmFloodEvent> floodEvents() const;

    // ============================================================
    // Level 4: 变更日志访问
    // ============================================================

    AlarmChangeLog* changeLog() { return &m_changeLog; }

    // ============================================================
    // 音频控制
    // ============================================================

    void setSoundEnabled(bool enabled);
    bool soundEnabled() const { return m_soundEnabled; }

signals:
    // ============================================================
    // Level 1: 基础信号
    // ============================================================

    /// 报警触发（UI 层连接此信号做闪烁、弹窗、声音）
    void alarmTriggered(const AlarmEvent& event);

    /// 报警确认
    void alarmAcknowledged(const QString& alarmId);

    /// 报警恢复确认
    void alarmReturnToNormalAcknowledged(const QString& alarmId);

    /// 报警清除（从活跃列表移除，不再显示）
    void alarmCleared(const QString& alarmId);

    // ============================================================
    // Level 2: 增强信号
    // ============================================================

    /// 报警屏蔽
    void alarmShelved(quint32 tagId, const QString& reason, int durationSec);

    /// 报警取消屏蔽
    void alarmUnshelved(quint32 tagId);

    /// 报警设计抑制
    void alarmSuppressed(quint32 tagId, const QString& reason);

    /// 报警取消抑制
    void alarmUnsuppressed(quint32 tagId);

    /// 报警停用
    void alarmOutOfService(quint32 tagId, const QString& reason);

    /// 报警恢复服务
    void alarmReturnedToService(quint32 tagId);

    /// 报警升级（从低限值升到高限值）
    void alarmEscalated(quint32 tagId, AlarmLimit oldLimit, AlarmLimit newLimit);

    /// 报警参数变更
    void alarmParameterChanged(quint32 tagId, const QString& fieldName,
                               const QString& oldValue, const QString& newValue);

    /// 操作员注释添加
    void alarmAnnotated(const QString& alarmId, const QString& annotation);

    /// 重复报警保护触发（自动屏蔽震荡报警）
    void chatteringAlarmDetected(quint32 tagId, int repeatCount);

    // ============================================================
    // Level 3: KPI 信号
    // ============================================================

    /// 活跃报警数变化（标题栏/状态栏更新用）
    void alarmCountChanged(int activeCount, int unackCount);

    /// 报警泛滥事件
    void alarmFloodDetected(const AlarmFloodEvent& floodEvent);

    // ============================================================
    // Level 4: 变更审计信号
    // ============================================================

    /// 变更记录已生成
    void changeRecorded(const AlarmChangeRecord& record);

private:
    AlarmEngine();
    ~AlarmEngine() override;
    AlarmEngine(const AlarmEngine&) = delete;
    AlarmEngine& operator=(const AlarmEngine&) = delete;

    /// 生成唯一报警ID
    QString generateAlarmId();

    /// 根据 AlarmLimit 获取限值描述
    QString limitToString(AlarmLimit limit) const;

    /// 根据优先级获取声音文件路径
    QString soundPathForPriority(AlarmPriority priority) const;

    /// 检查重复报警保护（Chattering Protection）
    /// @return true=应屏蔽此报警（重复过多）
    bool checkChattering(quint32 tagId);

    /// 启动屏蔽到期检查定时器
    void startShelveTimer();

    /// on-delay 定时器超时 → 实际触发报警
    void onOnDelayTimeout(quint32 tagId, AlarmLimit limit,
                          float value, float threshold,
                          AlarmPriority priority,
                          AlarmClassification classification);

    /// off-delay 定时器超时 → 确认恢复
    void onOffDelayTimeout(quint32 tagId, float returnValue);

    /// 屏蔽到期
    void onShelveTimeout(quint32 tagId);

    /// 发送报警声音
    void playAlarmSound(AlarmPriority priority);

    /// 检测报警泛滥事件
    void checkFloodCondition();

    // === 存储 ===
    QHash<quint32, AlarmEvent> m_activeAlarms;     // 活跃报警（含 RTN/Suppressed/OOS 状态）
    QList<AlarmEvent>         m_alarmHistory;      // 历史记录（上限 5000）

    // === On-Delay 定时器跟踪 ===
    struct OnDelayEntry {
        AlarmLimit limit;
        float      value;
        float      threshold;
        AlarmPriority priority;
        AlarmClassification classification;
        int        onDelayMs = 3000;
        QElapsedTimer elapsed;
    };
    QHash<quint32, OnDelayEntry> m_onDelayEntries;
    QTimer* m_onDelayTimer = nullptr;

    // === Off-Delay 定时器跟踪 ===
    struct OffDelayEntry {
        float      returnValue;
        int        offDelayMs = 0;
        QElapsedTimer elapsed;
    };
    QHash<quint32, OffDelayEntry> m_offDelayEntries;
    QTimer* m_offDelayTimer = nullptr;

    // === Shelve 定时器跟踪 ===
    QHash<quint32, qint64> m_shelveDeadlines;
    QTimer* m_shelveCheckTimer = nullptr;

    // === 重复报警保护跟踪 ===
    struct ChatteringState {
        int       count = 0;           // 1分钟内重复次数
        qint64    windowStart = 0;     // 窗口起始时间
        bool      autoShelved = false; // 是否已自动屏蔽
    };
    QHash<quint32, ChatteringState> m_chatteringState;

    // === 报警泛滥事件跟踪 ===
    QVector<AlarmFloodEvent> m_floodEvents;
    qint64 m_floodWindowStart = 0;     // 当前泛滥窗口起始时间
    int    m_floodWindowCount = 0;      // 当前10分钟窗口内报警数
    bool   m_inFlood = false;           // 是否处于泛滥状态

    // === 泛滥期间自动抑制跟踪（商业化增强 — 不足3修复） ===
    QHash<quint32, AlarmState> m_floodSuppressedAlarms;  // 泛滥期间被抑制的报警（tagId → 原状态）
    static constexpr int FLOOD_THRESHOLD = 10;           // 洪水阈值（10分钟内报警数）

    // === 条件抑制规则存储（商业化增强 — 不足1修复） ===
    QVector<SuppressionRule> m_suppressionRules;
    mutable QMutex m_suppressionMutex;

    // === 音频 ===
    QSoundEffect* m_soundCritical = nullptr;
    QSoundEffect* m_soundMajor    = nullptr;
    QSoundEffect* m_soundMinor    = nullptr;
    bool m_soundEnabled = true;

    // === 子系统 ===
    AlarmKpiMonitor m_kpiMonitor;
    AlarmChangeLog  m_changeLog;

    // === DoubleBuffer 引用（条件抑制规则评估用） ===
    DoubleBuffer* m_doubleBuffer = nullptr;

    // === 外部统计值（由 checkFloodCondition 等方法使用） ===
    int m_externalTotalActive = 0;
    int m_externalStaleCount = 0;
    int m_externalShelvedCount = 0;

    // === ID 生成 ===
    int m_alarmCounter = 0;

    // === KPI 持久化（不足7修复） ===
    qint64 m_lastKpiSaveTime = 0;          // 上次保存KPI快照的时间戳

    // === 互斥 ===
    mutable QMutex m_mutex;
};
