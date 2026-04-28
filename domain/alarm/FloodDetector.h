#pragma once
#include <QVector>
#include <QDateTime>
#include "domain/alarm/AlarmEvent.h"

class FloodDetector {
public:
    static constexpr int FLOOD_THRESHOLD = 10; // alarms in 10 minutes

    void recordAlarm(quint32 tagId, const QString& tagName) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        if (m_windowStart == 0 || (now - m_windowStart) > 600000) {
            if (m_inFlood) endFlood(now);
            m_windowStart = now;
            m_windowCount = 1;
            return;
        }

        m_windowCount++;
        if (m_windowCount >= FLOOD_THRESHOLD && !m_inFlood) {
            m_inFlood = true;
            AlarmFloodEvent fe;
            fe.startTime = m_windowStart;
            fe.alarmCount = m_windowCount;
            fe.peakRate = m_windowCount;
            m_currentFlood = fe;
        }

        if (m_inFlood) {
            m_currentFlood.alarmCount = m_windowCount;
            m_currentFlood.peakRate = qMax(m_currentFlood.peakRate, m_windowCount);
        }
    }

    bool isInFlood() const { return m_inFlood; }
    AlarmFloodEvent currentFlood() const { return m_currentFlood; }
    QVector<AlarmFloodEvent> pastFloods() const { return m_pastFloods; }

    void endFlood(qint64 now) {
        if (m_inFlood) {
            m_currentFlood.endTime = now;
            m_pastFloods.prepend(m_currentFlood);
            if (m_pastFloods.size() > 100) m_pastFloods.removeLast();
            m_inFlood = false;
        }
    }

private:
    qint64 m_windowStart = 0;
    int m_windowCount = 0;
    bool m_inFlood = false;
    AlarmFloodEvent m_currentFlood;
    QVector<AlarmFloodEvent> m_pastFloods;
};
