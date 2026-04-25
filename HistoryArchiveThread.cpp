#include "HistoryArchiveThread.h"
#include "logger.h"
HistoryArchiveThread& HistoryArchiveThread::instance()
{
	static HistoryArchiveThread instance;
	return instance;
}
HistoryArchiveThread::HistoryArchiveThread(QObject* parent):QThread(parent)
{
	m_running.storeRelaxed(0);
	m_totalArchived.storeRelaxed(0);
	m_totalFailed.storeRelaxed(0);
	m_cache.reserve(100000);
}
HistoryArchiveThread::~HistoryArchiveThread()
{
	stop();
	wait(3000);
	// 线程退出前，如果还有缓存数据，尝试写入MySQL
	if (!m_cache.isEmpty()) {
		LOG_WARN("HistoryArchiveThread",
			QString("线程退出时还有 %1 条缓存数据未归档，尝试写入...")
			.arg(m_cache.size()));
		doArchive();
	}
}

void HistoryArchiveThread::setDoubleBuffer(DoubleBuffer* buffer)
{
	m_doubleBuffer = buffer;
}

void HistoryArchiveThread::setArchiveInterval(int seconds)
{
	m_archiveIntervalSec = qBound(60, seconds, 3600);
	LOG_INFO("HistoryArchiveThread", QString("归档间隔设置为 %1 秒").arg(m_archiveIntervalSec));
}

void HistoryArchiveThread::setSampleInterval(int ms)
{
	m_sampleIntervalMs = qBound(100, ms, 60000);
}

void HistoryArchiveThread::stop()
{
	m_running.storeRelaxed(0);
}

QVector<HistoryRecord> HistoryArchiveThread::queryTrend(quint32 tagId, const QDateTime& startTime, const QDateTime& endTime, int maxPoints)
{
	// 转发给DatabaseManager
	return DatabaseManager::instance().queryHistory(tagId, startTime, endTime, maxPoints);
}

QMap<quint32, QVector<HistoryRecord>> HistoryArchiveThread::queryMultiTrend(const QVector<quint32>& tagIds, const QDateTime& startTime, const QDateTime& endTime, int maxPoints)
{
	QMap<quint32, QVector<HistoryRecord>> result;

	for (quint32 tagId : tagIds) {
		result[tagId] = DatabaseManager::instance().queryHistory(tagId, startTime, endTime, maxPoints);
	}

	return result;
}

void HistoryArchiveThread::run()
{
	m_running.storeRelaxed(1);
	LOG_INFO("HistoryArchiveThread",
		QString("历史归档线程启动，采样间隔=%1ms，归档间隔=%2秒")
		.arg(m_sampleIntervalMs).arg(m_archiveIntervalSec));

	while (m_running.loadRelaxed()) {
		// 1. 从双缓冲区采样数据
		sampleData();
		// 2. 检查是否需要归档
		{
			QMutexLocker locker(&m_cacheMutex);
			if (!m_cache.isEmpty() && m_firstRecordTime > 0) {
				qint64 now = QDateTime::currentMSecsSinceEpoch();
				qint64 elapsedSec = (now - m_firstRecordTime) / 1000;

				if (elapsedSec >= m_archiveIntervalSec) {
					locker.unlock();
					doArchive();
				}
			}
		}
		QThread::msleep(m_sampleIntervalMs);
	}
	LOG_INFO("HistoryArchiveThread", "历史归档线程退出");
}

void HistoryArchiveThread::sampleData()
{
	if (!m_doubleBuffer) {
		return;
	}

	auto snapshot = m_doubleBuffer->readAll();
	if (snapshot->empty())
	{
		return;
	}
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	QMutexLocker locker(&m_cacheMutex);
	// 记录第一条数据的时间戳
	if (m_cache.isEmpty()) {
		m_firstRecordTime = now;
	}
	// 遍历所有位号，采样当前值
	for (const auto& [tagId, tag] : *snapshot) {
		ArchiveRecord record;
		record.tagId = tagId;
		record.value = tag.currentValue;
		record.timestamp = now;
		record.quality = static_cast<quint8>(tag.quality);
		m_cache.append(record);
	}


}

bool HistoryArchiveThread::doArchive()
{
	QMutexLocker locker(&m_cacheMutex);

	if (m_cache.isEmpty()) {
		return true;
	}
	// 转换为HistoryRecord格式
	QVector<HistoryRecord> records;
	records.reserve(m_cache.size());
	for (const auto& rec : m_cache) {
		HistoryRecord hr;
		hr.tagId = rec.tagId;
		hr.value = rec.value;
		hr.timestamp = rec.timestamp;
		hr.quality = rec.quality;
		records.append(hr);
	}
	int recordCount = records.size();
	locker.unlock();
	// 调用DatabaseManager写入MySQL
	qint64 startTime = QDateTime::currentMSecsSinceEpoch();
	bool success = DatabaseManager::instance().batchInsertHistory(records);
	qint64 durationMs = QDateTime::currentMSecsSinceEpoch() - startTime;

	if (success) {
		m_totalArchived.fetchAndAddRelaxed(recordCount);

		// 清空缓存
		QMutexLocker clearLocker(&m_cacheMutex);
		m_cache.clear();
		m_firstRecordTime = 0;

		LOG_INFO("HistoryArchiveThread",
			QString("归档完成: %1 条记录, 耗时 %2ms")
			.arg(recordCount).arg(durationMs));
		emit archiveCompleted(recordCount, durationMs);
		return true;
	}
	else {
		m_totalFailed.fetchAndAddRelaxed(recordCount);
		LOG_ERROR("HistoryArchiveThread",
			QString("归档失败: %1 条记录").arg(recordCount));
		emit archiveFailed("数据库写入失败");
		return false;
	}

}
