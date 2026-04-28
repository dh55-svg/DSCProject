#include "DataPipeline.h"
#include "domain/tag/TagManager.h"
#include "domain/alarm/AlarmEngine.h"
#include <QDateTime>

DataPipeline::DataPipeline(QObject* parent) : QObject(parent) {
    m_parseThread.setRingBuffer(m_messageBus.ringBuffer());
    m_parseThread.setDoubleBuffer(&m_doubleBuffer);
    m_historySampler.setDoubleBuffer(&m_doubleBuffer);

    connect(&m_parseThread, &DataParseThread::dataUpdated, this, &DataPipeline::dataUpdated);
    connect(&m_parseThread, &DataParseThread::alarmTriggered, this, &DataPipeline::alarmTriggered);
}

DataPipeline::~DataPipeline() { stop(); }

void DataPipeline::setTagManager(TagManager* mgr) {
    m_tagManager = mgr;
    m_parseThread.setTagManager(mgr);
}

void DataPipeline::setAlarmEngine(AlarmEngine* engine) {
    m_alarmEngine = engine;
    m_parseThread.setAlarmEngine(engine);
}

void DataPipeline::setFieldbus(IFieldbus* bus) {
    m_fieldbus = bus;
    if (bus) bus->setDataSink(&m_messageBus);
}

void DataPipeline::setHistoryRepo(IHistoryRepo* repo) {
    m_historySampler.setHistoryRepo(repo);
}

void DataPipeline::injectTagConfig(const QVector<TagInfo>& tags) {
    m_parseThread.setTagConfig(tags);
}

void DataPipeline::injectSource(IMessageBus* source) {
    Q_UNUSED(source);
    // Source is the fieldbus which writes to m_messageBus
}

void DataPipeline::start() {
    m_parseThread.start();
    m_historySampler.start();
    if (m_fieldbus) m_fieldbus->startAll();
}

void DataPipeline::stop() {
    m_parseThread.stop();
    m_historySampler.stop();
    if (m_fieldbus) m_fieldbus->stopAll();
}

void DataPipeline::writeSetPoint(quint32 tagId, float value) {
    DoubleBuffer::TagSnapshot snap;
    snap.tagId = tagId;
    snap.setPoint = value;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_doubleBuffer.write(tagId, snap);

    if (m_tagManager && m_fieldbus) {
        auto tag = m_tagManager->getTag(tagId);
        quint16 rawVal = static_cast<quint16>(((value - tag.engLow) / (tag.engHigh - tag.engLow)) * 65535.0f);
        m_fieldbus->writeRegister(tag.modbusDeviceId, tag.modbusRegAddr, rawVal);
    }
}

void DataPipeline::writeOutput(quint32 tagId, float value) {
    DoubleBuffer::TagSnapshot snap;
    snap.tagId = tagId;
    snap.outputValue = value;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_doubleBuffer.write(tagId, snap);
}

void DataPipeline::setAutoMode(quint32 tagId, bool autoMode) {
    Q_UNUSED(tagId);
    Q_UNUSED(autoMode);
}
