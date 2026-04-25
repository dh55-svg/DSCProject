#include "DataParseThread.h"
#include "logger.h"
#include "AlarmEngine.h"
DataParseThread::DataParseThread(QObject* parent):QThread(parent)
{
	m_running.storeRelaxed(0);
	m_totalProcessed.storeRelaxed(0);
	m_totalAlarms.storeRelaxed(0);
}
DataParseThread::~DataParseThread()
{
	stop();
	wait(3000);
}

void DataParseThread::setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue)
{
	m_ringBuffer = queue;
}

void DataParseThread::setDoubleBuffer(DoubleBuffer* buffer)
{
	m_doubleBuffer = buffer;
}

void DataParseThread::setTagConfig(const QVector<TagInfo>& tags)
{
	m_tagByRegAddr.clear();
	m_tagsByDevice.clear();

	for (const auto& tag : tags)
	{
		quint64 key = (static_cast<quint64>(tag.modbusServerAddr) << 32) | static_cast<quint64>(tag.modbusRegAddr);
		m_tagByRegAddr.insert(key, tag);


		m_tagsByDevice[tag.modbusServerAddr].append(tag.tagId);
	}
	LOG_INFO("DataParseThread", QString("Tag config loaded: %1 tags, %2 devices")
		.arg(tags.size()).arg(m_tagsByDevice.size()));
}

void DataParseThread::setProcessInterval(int ms)
{
	m_processInterval = qBound(5,ms,1000);
}

void DataParseThread::setSwapInterval(int ms)
{
	m_swapInterval = qBound(10, ms, 1000);
}

void DataParseThread::stop()
{
	m_running.storeRelaxed(0);
}

void DataParseThread::run()
{
	m_running.storeRelaxed(1);
	LOG_INFO("DataParseThread", QString("Data thread started: interval=%1ms, swap=%2ms")
		.arg(m_processInterval).arg(m_swapInterval));

	qint64 lastSwapTime = 0;
	std::vector<RawModbusData> batch;
	batch.reserve(256);

	while (m_running) {
		batch.clear();
		if (m_ringBuffer)
		{
			m_ringBuffer->dequeueBatch(batch,256);

		}
		if (!batch.empty())
		{
			processBatch(batch);
			m_totalProcessed.fetchAndAddRelaxed(static_cast<int>(batch.size()));
		}

		quint64 now = QDateTime::currentMSecsSinceEpoch();
		if ((now - lastSwapTime) >= m_swapInterval)
		{
			if (m_doubleBuffer)
			{
				m_doubleBuffer->commit();
				emit dataUpdated();
			}
			lastSwapTime = now;
		}
		if (batch.empty())
		{
			QThread::msleep(m_processInterval);
		}
		else {
			QThread::msleep(1);
		}
	}
	LOG_INFO("DataParseThread", QString("Data thread exited: processed=%1, alarms=%2")
		.arg(m_totalProcessed.loadRelaxed())
		.arg(m_totalAlarms.loadRelaxed()));
}

void DataParseThread::processBatch(const std::vector<RawModbusData>& batch)
{
	int processedCount = 0;
	int alarmCheckCount = 0;
	for (const auto& raw : batch) {
		for (int i = 0; i < raw.valueCount; ++i)
		{
			int regAddr = raw.startAddress + i;
			quint64 key = (static_cast<quint64>(raw.serverAddress) << 32) | static_cast<quint64>(regAddr);
			auto it = m_tagByRegAddr.find(key);
			if (it == m_tagByRegAddr.end()) continue;
			const TagInfo& tag = it.value();
			float pv = registerToValue(raw.values[i], tag);
			if (m_doubleBuffer)
			{
				DoubleBuffer::TagSnapshot snapshot;
				snapshot.tagId = tag.tagId;
				snapshot.currentValue = pv;
				snapshot.setPoint = tag.setPoint;
				snapshot.outputValue = tag.outputValue;
				snapshot.alarmstate = tag.alarmLimit;
				snapshot.quality = DataQuality::Good;
				snapshot.timestamp = raw.timestamp;
				m_doubleBuffer->write(tag.tagId,snapshot);

			}


			checkAlarmOptimized(tag, pv);
			alarmCheckCount++;
			processedCount++;
		}
	}
}

float DataParseThread::registerToValue(quint16 rawValue, const TagInfo& tag) const
{


	float range = tag.engHigh - tag.engLow;
	float value = tag.engLow + (static_cast<float>(rawValue) / 65535.0f) * range;
	return value;
}

void DataParseThread::checkAlarm(quint32 tagId, float value)
{
	TagInfo tag;
	for (auto it = m_tagByRegAddr.constBegin(); it != m_tagByRegAddr.constEnd(); ++it)
	{
		if (it.value().tagId == tagId)
		{
			tag = it.value();
			break;
		}
	}
	if (tag.tagId == 0) return;
	checkAlarmOptimized(tag, value);
}

void DataParseThread::checkAlarmOptimized(const TagInfo& tag, float value)
{
	quint32 tagId = tag.tagId;
	AlarmLimit newLimit = AlarmLimit::Normal;

	if (value >= tag.highHighLimit) {
		newLimit = AlarmLimit::HighHigh;
	} else if (value >= tag.highLimit) {
		newLimit = AlarmLimit::High;
	} else if (value <= tag.lowLowLimit) {
		newLimit = AlarmLimit::LowLow;
	} else if (value <= tag.lowLimit) {
		newLimit = AlarmLimit::Low;
	}

	AlarmLimit oldLimit = tag.alarmLimit;

	// 死区滞环：值必须越过死区才认为恢复正常
	if (oldLimit != AlarmLimit::Normal && newLimit == AlarmLimit::Normal) {
		bool canClear = false;
		if (oldLimit == AlarmLimit::High || oldLimit == AlarmLimit::HighHigh) {
			canClear = (value < tag.highLimit - tag.deadband);
		} else if (oldLimit == AlarmLimit::Low || oldLimit == AlarmLimit::LowLow) {
			canClear = (value > tag.lowLimit + tag.deadband);
		}
		if (!canClear) {
			m_inDeadband[tagId] = true;
			return;
		}
		m_inDeadband.remove(tagId);
	}

	if (newLimit != oldLimit) {
		m_totalAlarms.fetchAndAddRelease(1);

		// 更新双缓冲区报警状态 (用于UI显示)
		if (m_doubleBuffer) {
			DoubleBuffer::TagSnapshot snapshot = m_doubleBuffer->readTag(tagId);
			snapshot.alarmstate = newLimit;
			m_doubleBuffer->write(tagId, snapshot);
		}

		// 通过ISA-18.2 报警引擎处理完整状态机
		if (newLimit != AlarmLimit::Normal) {
			float threshold = 0.0f;
			switch (newLimit) {
			case AlarmLimit::HighHigh: threshold = tag.highHighLimit; break;
			case AlarmLimit::High:     threshold = tag.highLimit; break;
			case AlarmLimit::LowLow:   threshold = tag.lowLowLimit; break;
			case AlarmLimit::Low:      threshold = tag.lowLimit; break;
			default: break;
			}
			AlarmEngine::instance().triggerAlarm(
			    tagId, newLimit, value, threshold,
			    tag.priority, tag.classification, tag.onDelayMs);
			emit alarmTriggered(tagId, newLimit, value, threshold);
		} else {
			AlarmEngine::instance().clearAlarm(tagId, value);
			emit alarmCleared(tagId);
		}
	}
}

void DataParseThread::markDeviceBad(int deviceId)
{
	auto it = m_tagsByDevice.find(deviceId);
	if (it == m_tagsByDevice.end()) {
		return;
	}
	for (quint32 tagId : it.value()) {
		if (m_doubleBuffer) {
			DoubleBuffer::TagSnapshot snapshot = m_doubleBuffer->readTag(tagId);
			snapshot.quality = DataQuality::Bad;
			m_doubleBuffer->write(tagId, snapshot);
		}
	}

	LOG_WARN("DataParseThread", QString("Device %1 tags marked as Bad").arg(deviceId));
}
