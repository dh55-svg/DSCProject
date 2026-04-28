#pragma once
#include <qobject.h>
#include <QHash>
#include <qreadwritelock.h>
#include <qvector.h>
#include "domain/tag/TagInfo.h"
#include "infrastructure/persistence/ITagRepo.h"
#include "infrastructure/logging/ILogger.h"

class TagManager : public QObject {
    Q_OBJECT
public:
    explicit TagManager(ITagRepo& repo, ILogger* logger = nullptr);

    bool addTag(const TagInfo& tag);
    bool removeTag(quint32 tagId);
    TagInfo getTag(quint32 tagId) const;
    TagInfo getTagByName(const QString& tagName) const;
    QList<TagInfo> getAllTags() const;
    QStringList getAllTagNames() const;
    int tagCount() const;

    QPair<float, float> getRange(quint32 tagId) const;
    QString getUnit(quint32 tagId) const;

    struct AlarmLimits { float highHigh=90, high=80, low=10, lowLow=5, deadband=1; };
    AlarmLimits getAlarmLimits(quint32 tagId) const;

    struct ModbusMapping { int serverAddr=1, regAddr=0, regCount=1; };
    ModbusMapping getModbusMapping(quint32 tagId) const;
    quint32 findTagByModbusAddr(int serverAddr, int regAddr) const;

    bool updateTag(quint32 tagId, const TagInfo& tag);
    bool updateAlarmLimits(quint32 tagId, const AlarmLimits& limits);
    bool updateRange(quint32 tagId, float engLow, float engHigh);

    void addTags(const QVector<TagInfo>& tags);
    void clear();
    QVector<quint32> getTagsByDevice(int deviceId) const;

    bool loadFromJson(const QString& jsonPath);
    bool saveToJson(const QString& jsonPath) const;

signals:
    void tagAdded(quint32 tagId);
    void tagRemoved(quint32 tagId);
    void configChanged(quint32 tagId);

private:
    ITagRepo& m_repo;
    ILogger* m_logger;
    QHash<quint32, TagInfo> m_tags;
    QHash<QString, quint32> m_nameIndex;
    QHash<quint32, quint32> m_modbusAddrIndex;
    QHash<int, QVector<quint32>> m_deviceIndex;
    mutable QReadWriteLock m_rwlock;
};
