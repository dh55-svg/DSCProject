#include "DataController.h"

DataController::DataController(DataPipeline& pipeline, TagManager& tagMgr, AlarmEngine& alarmEngine,
                                IFieldbus& fieldbus, ILogger* logger)
    : m_pipeline(pipeline), m_tagMgr(tagMgr), m_alarmEngine(alarmEngine), m_fieldbus(fieldbus), m_logger(logger)
{
    connect(&m_pipeline, &DataPipeline::dataUpdated, this, &DataController::dataUpdated);
    connect(&m_pipeline, &DataPipeline::deviceStatusChanged, this, &DataController::deviceStatusChanged);
    connect(&m_pipeline, &DataPipeline::commStatusChanged, this, &DataController::commStatusChanged);
}

void DataController::connectAll() { m_pipeline.start(); }
void DataController::disconnectAll() { m_pipeline.stop(); }

void DataController::writeSetPoint(quint32 tagId, float value) {
    m_pipeline.writeSetPoint(tagId, value);
}

void DataController::writeOutput(quint32 tagId, float value) {
    m_pipeline.writeOutput(tagId, value);
}

void DataController::toggleAutoMode(quint32 tagId) {
    m_pipeline.setAutoMode(tagId, false);
}
