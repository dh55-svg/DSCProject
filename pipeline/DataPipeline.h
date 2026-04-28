#pragma once
#include <QObject>
#include <QVector>
#include <memory>
#include "pipeline/DataParseThread.h"
#include "pipeline/HistorySampler.h"
#include "infrastructure/messaging/LockFreeRingBuffer.h"
#include "infrastructure/fieldbus/IFieldbus.h"

class TagManager;
class AlarmEngine;

class DataPipeline : public QObject {
    Q_OBJECT
public:
    explicit DataPipeline(QObject* parent = nullptr);
    ~DataPipeline();

    void setTagManager(TagManager* mgr);
    void setAlarmEngine(AlarmEngine* engine);
    void setFieldbus(IFieldbus* bus);
    void setHistoryRepo(IHistoryRepo* repo);
    void injectTagConfig(const QVector<TagInfo>& tags);
    void injectSource(IMessageBus* source);

    void start();
    void stop();
    DoubleBuffer* doubleBuffer() { return &m_doubleBuffer; }
    IFieldbus* fieldbus() { return m_fieldbus; }

    void writeSetPoint(quint32 tagId, float value);
    void writeOutput(quint32 tagId, float value);
    void setAutoMode(quint32 tagId, bool autoMode);

    HistorySampler* historySampler() { return &m_historySampler; }

signals:
    void dataUpdated();
    void alarmTriggered(quint32 tagId, AlarmLimit limit, float value, float threshold);
    void alarmCleared(quint32 tagId);
    void deviceStatusChanged(int deviceId, bool connected);
    void commStatusChanged(bool ok);

private:
    RingBufMessageBus m_messageBus;
    DoubleBuffer m_doubleBuffer;
    DataParseThread m_parseThread;
    HistorySampler m_historySampler;
    IFieldbus* m_fieldbus = nullptr;
    TagManager* m_tagManager = nullptr;
    AlarmEngine* m_alarmEngine = nullptr;
};
