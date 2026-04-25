#pragma once
#include "export.h"
#include <qobject.h>
#include <QHash>
#include <qreadwritelock.h>
#include <qvector.h>
#include <qstringlist.h>
#include "TagDef.h"
/**
 * @brief 位号配置管理器（精简版）
 *
 * 职责明确化：
 * - 只管理位号的静态配置（量程、报警限值、单位、Modbus映射等）
 * - 实时数据由DoubleBuffer负责存储和更新
 * - 不再存储实时值，避免数据重复
 *
 * 架构分工：
 * ┌──────────────────┐     ┌──────────────────┐
 * │   DoubleBuffer   │     │					  TagConfigMgr   │
 * │  实时数据存储     │     │					  静态配置管理    │
 * │  - value/sp/out  │     │					 - 量程/单位     │
 * │  - quality       │     │					 - 报警限值      │
 * │  - 无锁读取      │     │					 - Modbus映射    │
 * └──────────────────┘     └──────────────────┘
 *        高频路径										 低频路径
 *
 * 使用场景：
 * - UI显示：从DoubleBuffer读取实时值，从TagConfigMgr读取单位/量程
 * - 报警判断：从DoubleBuffer读取当前值，从TagConfigMgr读取报警限值
 * - 历史归档：从DoubleBuffer读取实时值，从TagConfigMgr读取位号信息
 */
class DATAENGINE_EXPORT TagConfigMgr :public QObject {
	Q_OBJECT
public:
	static TagConfigMgr& instance();
    // ========== 位号配置管理 ==========

    // 添加位号配置
    bool addTag(const TagInfo& tag);

    // 移除位号配置
    bool removeTag(quint32 tagId);

    // 获取位号完整配置
    TagInfo getTag(quint32 tagId) const;
    TagInfo getTagByName(const QString& tagName) const;

    // 获取所有位号配置
    QList<TagInfo> getAllTags() const;
    QStringList getAllTagNames() const;

    // 位号数量
    int tagCount() const;

    // ========== 静态配置查询（高频使用） ==========

    // 获取量程
    QPair<float, float> getRange(quint32 tagId) const;

    // 获取单位
    QString getUxnit(quint32 tagId) const;

    // 获取报警限值
    struct AlarmLimits {
        float highHigh = 90.0f;
        float high = 80.0f;
        float low = 10.0f;
        float lowLow = 5.0f;
        float deadband = 1.0f;
    };
    AlarmLimits getAlarmLimits(quint32 tagId) const;

    // 获取Modbus映射
    struct ModbusMapping {
        int serverAddr = 1;
        int regAddr = 0;
        int regCount = 1;
    };
    ModbusMapping getModbusMapping(quint32 tagId) const;

    // 按Modbus地址查找位号（解析线程用）
    quint32 findTagByModbusAddr(int serverAddr, int regAddr) const;

    // ========== 配置修改（低频使用） ==========

    // 更新报警限值
    bool updateAlarmLimits(quint32 tagId, const AlarmLimits& limits);

    // 更新量程
    bool updateRange(quint32 tagId, float engLow, float engHigh);

    // ========== 批量操作 ==========

    // 批量添加位号
    void addTags(const QVector<TagInfo>& tags);

    // 清空所有位号
    void clear();

    // 按设备ID获取位号列表
    QVector<quint32> getTagsByDevice(int deviceId) const;

    // ========== 配置导入导出 ==========

    // 从JSON加载配置
    bool loadFromJson(const QString& jsonPath);

    // 保存配置到JSON
    bool saveToJson(const QString& jsonPath) const;
signals:
    // 配置变更信号
    void tagAdded(quint32 tagId);
    void tagRemoved(quint32 tagId);
    void configChanged(quint32 tagId);
private:
    TagConfigMgr() = default;
    ~TagConfigMgr() override = default;
    TagConfigMgr(const TagConfigMgr&) = delete;
    TagConfigMgr& operator=(const TagConfigMgr&) = delete;

    // 位号配置表
    QHash<quint32, TagInfo> m_tags;

    // 名称索引（加速按名称查找）
    QHash<QString, quint32> m_nameIndex;

    // Modbus地址索引（加速解析线程查找）
    // key = (serverAddr << 16) | regAddr
    QHash<quint32, quint32> m_modbusAddrIndex;

    // 设备ID索引（按设备分组）
    QHash<int, QVector<quint32>> m_deviceIndex;

    // 读写锁
    mutable QReadWriteLock m_rwlock;
};