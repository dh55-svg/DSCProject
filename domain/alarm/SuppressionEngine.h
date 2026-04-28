#pragma once
#include <QVector>
#include <QMutex>
#include "domain/alarm/AlarmEvent.h"

class SuppressionEngine {
public:
    bool addRule(const SuppressionRule& rule) {
        QMutexLocker lock(&m_mutex);
        for (auto& r : m_rules) {
            if (r.ruleId == rule.ruleId) return false;
        }
        m_rules.append(rule);
        return true;
    }

    void removeRule(quint32 ruleId) {
        QMutexLocker lock(&m_mutex);
        m_rules.erase(std::remove_if(m_rules.begin(), m_rules.end(),
            [ruleId](const SuppressionRule& r) { return r.ruleId == ruleId; }), m_rules.end());
    }

    void setEnabled(quint32 ruleId, bool enabled) {
        QMutexLocker lock(&m_mutex);
        for (auto& r : m_rules) {
            if (r.ruleId == ruleId) { r.enabled = enabled; return; }
        }
    }

    QVector<SuppressionRule> rules() const {
        QMutexLocker lock(&m_mutex);
        return m_rules;
    }

    bool evaluate(quint32 tagId) const {
        QMutexLocker lock(&m_mutex);
        for (const auto& r : m_rules) {
            if (r.enabled && r.targetTagId == tagId) return true;
        }
        return false;
    }

private:
    QVector<SuppressionRule> m_rules;
    mutable QMutex m_mutex;
};
