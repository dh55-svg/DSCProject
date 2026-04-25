#pragma once
#include "export.h"
#include "TagDef.h"
#include <QObject>
#include <QTimer>
#include <QMap>
#include <QList>
#include <QMutex>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSoundEffect>

#include "AlarmKpiMonitor.h"
#include "AlarmChangeLog.h"

// ============================================================
// ISA-18.2 核心报警引擎
// ============================================================

/**
 * @brief ISA-18.2 报警引擎（完整实现）
 *
 * 覆盖 ISA-18.2 成熟度模型 Level 1-4：
 *
 * Level 1 - 技术实现：
 *   - 5 状态机（Normal / ActiveUnack / ActiveAck / RTNUnack / RTNAck）
 *   - On-Delay 定时器（值持续超限才触发，防噪声尖峰）
 *   - 死区滞环（Deadband，值回正常需越过死区）
 *   - 报警升级（Low → High 等）
 *   - 报警去重（已有活跃报警不重复触发）
 *
 * Level 2 - Rationalization：
 *   - 4 级优先级（Critical/Major/Minor/Advisory）
 *   - 5 种分类（Process/Safety/Enviro/Quality/Machinery）
 *   - Shelving（操作员临时屏蔽，自动恢复）
 *   - 后果记录+操作指南（AlarmRationalization）
 *
 * Level 3 - KPI 监控：
 *   - 滑动窗口 10 分钟报警率
 *   - 平均报警率/小时
 *   - 高峰报警率
 *   - 陈旧报警检测（>30min 未确认）
 *   - Top-N 最频繁报警
 *
 * Level 4 - 变更管理：
 *   - 参数变更全记录（谁/何时/改什么/为什么/谁审批）
 *   - 审计报告导出
 *   - 审批流程
 *
 * 线程安全：所有 public 方法使用 QMutex 保护
 */
class BUSINESS_EXPORT AlarmEngine : public QObject {
    Q_OBJECT
public:
    static AlarmEngine& instance();

    /// 初始化报警引擎（加载音频、启动 KPI 定时器）
    void initialize();

    // ============================================================
    // Level 1: 报警触发与状态管理
    // ============================================================

    /**
     * @brief 触发报警（ISA-18.2 Level 1）
     *
     * @param tagId      位号ID
     * @param limit      超限等级 HH/H/L/LL
     * @param triggerValue 当前值
     * @param thresholdValue 限值
     * @param priority   报警优先级
     * @param classification 报警分类
     *
     * 状态机逻辑：
     * - 如果已有同 tagId 报警且 severity 更高 → 升级
     * - 如果已有同 tagId 报警且 severity 相同 → 忽略（去重）
     * - 如果已有同 tagId 报警且 severity 更低 → 忽略
     * - 如果无报警 → 创建新事件，状态=ActiveUnacknowledged
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
     * 状态迁移：
     * - ActiveUnacknowledged → ReturnToNormalUnacknowledged
     * - ActiveAcknowledged   → ReturnToNormalUnacknowledged
     *
     * 不回 Normal！操作员必须确认恢复后才彻底关闭。
     */
    void clearAlarm(quint32 tagId, float returnValue);

    // ============================================================
    // Level 1: 确认操作
    // ============================================================

    /**
     * @brief 确认报警（ISA-18.2 Acknowledge）
     *
     * ActiveUnacknowledged → ActiveAcknowledged
     * 报警声音停止，灯从闪烁变常亮。
     */
    void acknowledgeAlarm(const QString& alarmId);

    /// 按位号确认
    void acknowledgeAlarmByTagId(quint32 tagId);

    /// 确认所有活跃报警
    void acknowledgeAll();

    /**
     * @brief 确认恢复（ISA-18.2 Return-to-Normal Acknowledge）
     *
     * ReturnToNormalUnacknowledged → Normal（从活跃列表移除）
     * 操作员确认"我已看到恢复，问题已解决"。
     */
    void acknowledgeReturnToNormal(const QString& alarmId);
    void acknowledgeReturnToNormalByTagId(quint32 tagId);
    void acknowledgeAllReturnToNormal();

    // ============================================================
    // Level 2: Shelving（屏蔽/暂停）
    // ============================================================

    /**
     * @brief 屏蔽报警（ISA-18.2 Shelve）
     *
     * 操作员临时屏蔽报警，通常用于已知故障等待维修。
     * 屏蔽到期自动恢复，或操作员手动取消屏蔽。
     *
     * @param tagId      位号ID
     * @param reason     屏蔽原因
     * @param durationSec 0=永久  >0=自动恢复秒数
     */
    void shelveAlarm(quint32 tagId, const QString& reason, int durationSec = 3600);

    /// 取消屏蔽
    void unshelveAlarm(quint32 tagId);

    /// 获取所有屏蔽中的报警
    QList<AlarmEvent> shelvedAlarms() const;

    // ============================================================
    // Level 2: 报警参数动态修改（含变更记录）
    // ============================================================

    /**
     * @brief 修改报警限值（含变更记录，ISA-18.2 Level 4）
     */
    bool setAlarmLimit(quint32 tagId, const QString& fieldName,
                       float newValue, const QString& operatorName,
                       const QString& reason);

    /**
     * @brief 修改报警优先级（含变更记录）
     */
    bool setAlarmPriority(quint32 tagId, AlarmPriority newPriority,
                          const QString& operatorName, const QString& reason);

    // ============================================================
    // Level 1-2: 查询接口
    // ============================================================

    /// 所有活跃报警（Active + RTN Unack/Ack）
    QList<AlarmEvent> activeAlarms() const;

    /// 未确认报警（需要操作员立即关注的）
    QList<AlarmEvent> unacknowledgedAlarms() const;

    /// 按位号查报警
    AlarmEvent alarmByTagId(quint32 tagId) const;

    /// 报警历史（最多 limit 条）
    QList<AlarmEvent> alarmHistory(int limit = 1000) const;

    /// 按严重程度分类计数
    int activeAlarmCount() const;
    int activeAlarmCount(AlarmLimit limit) const;
    int activeAlarmCount(AlarmPriority priority) const;

    /// 未确认报警数
    int unacknowledgedCount() const;

    // ============================================================
    // Level 3: KPI 访问
    // ============================================================

    AlarmKpiMonitor* kpiMonitor() { return &m_kpiMonitor; }

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

    /// 报警升级（从低限值升到高限值）
    void alarmEscalated(quint32 tagId, AlarmLimit oldLimit, AlarmLimit newLimit);

    /// 报警参数变更
    void alarmParameterChanged(quint32 tagId, const QString& fieldName,
                               const QString& oldValue, const QString& newValue);

    // ============================================================
    // Level 3: KPI 信号
    // ============================================================

    /// 活跃报警数变化（标题栏/状态栏更新用）
    void alarmCountChanged(int activeCount, int unackCount);

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

    // 启动屏蔽到期检查定时器
    void startShelveTimer();

    /// on-delay 定时器超时 → 实际触发报警
    void onOnDelayTimeout(quint32 tagId, AlarmLimit limit,
                          float value, float threshold,
                          AlarmPriority priority,
                          AlarmClassification classification);

    /// 屏蔽到期
    void onShelveTimeout(quint32 tagId);

    /// 发送报警声音
    void playAlarmSound(AlarmPriority priority);

    // === 存储 ===
    QMap<quint32, AlarmEvent> m_activeAlarms;     // 活跃报警（含 RTN 状态）
    QList<AlarmEvent>         m_alarmHistory;      // 历史记录（上限 2000）

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
    QMap<quint32, OnDelayEntry> m_onDelayEntries;
    QTimer* m_onDelayTimer = nullptr;

    // === Shelve 定时器跟踪 ===
    QMap<quint32, qint64> m_shelveDeadlines;
    QTimer* m_shelveCheckTimer = nullptr;

    // === 音频 ===
    QSoundEffect* m_soundCritical = nullptr;
    QSoundEffect* m_soundMajor    = nullptr;
    QSoundEffect* m_soundMinor    = nullptr;
    bool m_soundEnabled = true;

    // === 子系统 ===
    AlarmKpiMonitor m_kpiMonitor;
    AlarmChangeLog  m_changeLog;

    // === ID 生成 ===
    int m_alarmCounter = 0;

    // === 互斥 ===
    mutable QMutex m_mutex;
};
