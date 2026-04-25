#include "AlarmEngine.h"
#include "logger.h"
#include "DatabaseManager.h"
#include <QFile>

AlarmEngine& AlarmEngine::instance()
{
	static AlarmEngine instance;
	return instance;
}
void AlarmEngine::initialize()
{
	// 加载报警音效文件
	// 工业现场不同级别报警用不同声音：
	// - 高高报/低低报：急促的蜂鸣声
	// - 高报/低报：平缓的提示音
	m_alarmSoundHigh = new QSoundEffect(this);
	m_alarmSoundLow = new QSoundEffect(this);

	QString highSoundPath = "./sounds/alarm_high.wav";
	QString lowSoundPath = "./sounds/alarm_low.wav";

	if (QFile::exists(highSoundPath)) {
		m_alarmSoundHigh->setSource(QUrl::fromLocalFile(highSoundPath));
		m_alarmSoundHigh->setVolume(0.8f);
		LOG_INFO("AlarmEngine", QString("加载高优先级报警音效: %1").arg(highSoundPath));
	} else {
		LOG_WARN("AlarmEngine", QString("高优先级报警音效文件不存在: %1，报警时将静音").arg(highSoundPath));
	}

	if (QFile::exists(lowSoundPath)) {
		m_alarmSoundLow->setSource(QUrl::fromLocalFile(lowSoundPath));
		m_alarmSoundLow->setVolume(0.5f);
		LOG_INFO("AlarmEngine", QString("加载低优先级报警音效: %1").arg(lowSoundPath));
	} else {
		LOG_WARN("AlarmEngine", QString("低优先级报警音效文件不存在: %1，报警时将静音").arg(lowSoundPath));
	}

	LOG_INFO("AlarmEngine", "报警引擎初始化完成");
}

void AlarmEngine::triggerAlarm(quint32 tagId, AlarmState severity, float triggerValue, float thresholdValue)
{
	QMutexLocker lock(&m_mutex);

	auto it = m_activeAlarms.find(tagId);
	if (it != m_activeAlarms.end())
	{
		// 已有报警，检查是否需要升级
		// 例如：从高报升级到高高报
		if (severity > it->severity)
		{
			it->severity = severity;
			it->triggerValue = triggerValue;
			it->thresholdValue = thresholdValue;
			it->triggerTime = QDateTime::currentMSecsSinceEpoch();
			LOG_WARN("AlarmEngine", QString("报警升级: %1 -> %2, 位号ID=%3")
				.arg(static_cast<int>(it->severity))
				.arg(static_cast<int>(severity))
				.arg(tagId));
		}
		return; // 已有活跃报警，不重复触发
	}
	// 创建新报警事件
	AlarmEvent event;
	event.alarmId = generateAlarmId();
	event.tagId = tagId;
	event.severity = severity;
	event.triggerValue = triggerValue;
	event.thresholdValue = thresholdValue;
	event.triggerTime = QDateTime::currentMSecsSinceEpoch();
	event.active = true;
	event.acknowledged = false;
	// 生成描述
	QString severityStr;
	switch (severity) {
	case AlarmState::HighHigh: severityStr = "高高报"; break;
	case AlarmState::High:     severityStr = "高报"; break;
	case AlarmState::Low:      severityStr = "低报"; break;
	case AlarmState::LowLow:   severityStr = "低低报"; break;
	default: severityStr = "未知"; break;
	}
	event.description = QString("%1报警，当前值=%2，限值=%3")
		.arg(severityStr)
		.arg(triggerValue, 0, 'f', 1)
		.arg(thresholdValue, 0, 'f', 1);

	m_activeAlarms.insert(tagId, event);

	// 添加到历史记录
	m_alarmHistory.prepend(event);
	if (m_alarmHistory.size() > 1000) {
		m_alarmHistory.removeLast(); // 限制历史记录大小
	}
	// 写入数据库
	DatabaseManager::instance().insertAlarmRecord(
		tagId, static_cast<int>(severity), event.description,
		triggerValue, thresholdValue, event.triggerTime);

	// 播放报警音
	if (m_soundEnabled) {
		if (severity == AlarmState::HighHigh || severity == AlarmState::LowLow) {
			if (m_alarmSoundHigh) {
				m_alarmSoundHigh->play();
			}
		}
		else {
			if (m_alarmSoundLow) {
				m_alarmSoundLow->play();
			}
		}
	}
	// 在锁外发射信号
	lock.unlock();
	emit alarmTriggered(event);
	emit alarmCountChanged(m_activeAlarms.size());

	LOG_WARN("AlarmEngine", QString("报警触发: %1, 位号ID=%2, 值=%3")
		.arg(severityStr).arg(tagId).arg(triggerValue, 0, 'f', 1));
}

void AlarmEngine::acknowledgeAlarm(const QString& alarmId)
{
	QMutexLocker locker(&m_mutex);

	for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
		if (it->alarmId == alarmId && !it->acknowledged) {
			it->acknowledged = true;
			it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();

			QString alarmIdCopy = alarmId;
			locker.unlock();
			emit alarmAcknowledged(alarmIdCopy);
			LOG_INFO("AlarmEngine", QString("报警已确认: %1").arg(alarmIdCopy));
			return;
		}
	}
}

void AlarmEngine::acknowledgeAlarmByTagId(quint32 tagId)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it != m_activeAlarms.end() && !it->acknowledged) {
        it->acknowledged = true;
        it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
        QString alarmId = it->alarmId;
        locker.unlock();
        emit alarmAcknowledged(alarmId);
        LOG_INFO("AlarmEngine", QString("报警已确认(按位号): %1, tagId=%2").arg(alarmId).arg(tagId));
    }
}

void AlarmEngine::acknowledgeAll()
{
	QMutexLocker locker(&m_mutex);

	for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
		if (!it->acknowledged) {
			it->acknowledged = true;
			it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
			emit alarmAcknowledged(it->alarmId);
		}
	}

	LOG_INFO("AlarmEngine", "所有报警已确认");

}

void AlarmEngine::clearAlarm(quint32 tagId)
{
	QMutexLocker locker(&m_mutex);

	auto it = m_activeAlarms.find(tagId);
	if (it == m_activeAlarms.end()) {
		return; // 没有活跃报警，无需清除
	}
	it->active = false;
	it->clearTime = QDateTime::currentMSecsSinceEpoch();

	QString alarmId = it->alarmId;
	m_activeAlarms.erase(it);

	locker.unlock();
	emit alarmCleared(alarmId);
	emit alarmCountChanged(m_activeAlarms.size());

	LOG_INFO("AlarmEngine", QString("报警恢复: 位号ID=%1").arg(tagId));
}

QList<AlarmEvent> AlarmEngine::activeAlarms() const
{
	QMutexLocker locker(&m_mutex);
	return m_activeAlarms.values();
}

QList<AlarmEvent> AlarmEngine::allAlarms(int limit) const
{
	QMutexLocker locker(&m_mutex);
	return m_alarmHistory.mid(0, qMin(limit, m_alarmHistory.size()));
}

int AlarmEngine::activeAlarmCount() const
{
	QMutexLocker locker(&m_mutex);
	return m_activeAlarms.size();
}

int AlarmEngine::activeAlarmCount(AlarmState severity) const
{
	QMutexLocker locker(&m_mutex);
	int count = 0;
	for (const auto& event : m_activeAlarms) {
		if (event.severity == severity) {
			count++;
		}
	}
	return count;
}

void AlarmEngine::setSoundEnabled(bool enabled)
{
	m_soundEnabled = enabled;
	if (!enabled) {
		if (m_alarmSoundHigh) m_alarmSoundHigh->stop();
		if (m_alarmSoundLow) m_alarmSoundLow->stop();
	}
}

QString AlarmEngine::generateAlarmId()
{
	// 格式：ALM_20260417143025_0001
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	return QString("ALM_%1_%2")
		.arg(QDateTime::fromMSecsSinceEpoch(now).toString("yyyyMMddHHmmss"))
		.arg(++m_alarmCounter, 4, 10, QChar('0'));
}
