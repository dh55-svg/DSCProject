#pragma once
#include <QHash>
#include <qdatetime.h>
#include "domain/tag/TagInfo.h"

struct RateOfChangeData {
    float lastValue = 0.0f;
    qint64 lastTime = 0;
};

class RateOfChangeChecker {
public:
    bool exceedsLimit(quint32 tagId, float currentValue, const TagInfo& cfg) {
        auto& d = m_data[tagId];
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        if (d.lastTime == 0 || cfg.rateOfChangeLimit <= 0.0f) {
            d.lastValue = currentValue;
            d.lastTime = now;
            return false;
        }

        float dt = (now - d.lastTime) / 1000.0f;
        if (dt <= 0.0f) return false;

        float rate = qAbs(currentValue - d.lastValue) / dt;
        d.lastValue = currentValue;
        d.lastTime = now;

        return rate > cfg.rateOfChangeLimit;
    }

    void reset(quint32 tagId) { m_data.remove(tagId); }

private:
    QHash<quint32, RateOfChangeData> m_data;
};
