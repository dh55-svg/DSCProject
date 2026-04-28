#pragma once
#include <QObject>
#include <memory>
#include "pipeline/DataPipeline.h"
#include "domain/tag/TagManager.h"
#include "domain/alarm/AlarmEngine.h"

class DataController : public QObject {
    Q_OBJECT
public:
    DataController(DataPipeline& pipeline, TagManager& tagMgr, AlarmEngine& alarmEngine,
                   IFieldbus& fieldbus, ILogger* logger = nullptr);

    DataPipeline& pipeline() { return m_pipeline; }
    TagManager& tagManager() { return m_tagMgr; }
    AlarmEngine& alarmEngine() { return m_alarmEngine; }

    void connectAll();
    void disconnectAll();
    void writeSetPoint(quint32 tagId, float value);
    void writeOutput(quint32 tagId, float value);
    void toggleAutoMode(quint32 tagId);

signals:
    void dataUpdated();
    void deviceStatusChanged(int deviceId, bool connected);
    void commStatusChanged(bool ok);

private:
    DataPipeline& m_pipeline;
    TagManager& m_tagMgr;
    AlarmEngine& m_alarmEngine;
    IFieldbus& m_fieldbus;
    ILogger* m_logger;
};
