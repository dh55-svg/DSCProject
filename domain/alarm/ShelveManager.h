#pragma once
#include <QHash>
#include <QDateTime>

class ShelveManager {
public:
    void shelve(quint32 tagId, int durationSec) {
        if (durationSec > 0) {
            m_deadlines[tagId] = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(durationSec) * 1000;
        } else {
            m_deadlines.remove(tagId);
        }
    }

    void unshelve(quint32 tagId) {
        m_deadlines.remove(tagId);
    }

    // Returns list of tagIds whose shelve period has expired
    QList<quint32> checkExpired() {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<quint32> expired;
        auto it = m_deadlines.begin();
        while (it != m_deadlines.end()) {
            if (it.value() > 0 && now >= it.value()) {
                expired.append(it.key());
                it = m_deadlines.erase(it);
            } else {
                ++it;
            }
        }
        return expired;
    }

    int count() const { return m_deadlines.size(); }

private:
    QHash<quint32, qint64> m_deadlines;
};
