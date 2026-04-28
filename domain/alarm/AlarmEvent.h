#pragma once
#include <qstring.h>
#include <qdatetime.h>
#include "domain/common/DataQuality.h"
#include "domain/common/AlarmLimit.h"
#include "domain/common/AlarmState.h"

struct AlarmRationalization {
    QString consequence;
    QString operatorAction;
    int    expectedResponseTimeSec = 300;
    QString designPhilosophy;
    QString approver;
    qint64 approvedDate = 0;
    QString correctiveAction;
    QString relatedDocuments;
    QString area;
    QString zone;
    int    reviewCycleMonths = 12;
    qint64 lastReviewDate = 0;
    QString reviewer;
    bool   isValid = true;
};

struct AlarmChangeRecord {
    qint64  changeTime = 0;
    QString operatorName;
    quint32 tagId = 0;
    QString fieldName;
    QString oldValue;
    QString newValue;
    QString reason;
    bool    approved = false;
    QString approver;
    qint64  approveTime = 0;
    bool    rejected = false;
    QString rejectReason;
    QString workOrderNo;
    qint64  validUntil = 0;
    bool    autoReverted = false;
    QString sessionId;
    QString workstation;
};

struct AlarmKpiSnapshot {
    qint64  timestamp = 0;
    int     alarmCount10min = 0;
    float   avgPerHour = 0.0f;
    int     peakCount10min = 0;
    int     staleCount = 0;
    int     totalActive = 0;
    int     shelvedCount = 0;
    int     suppressedCount = 0;
    int     floodEventCount = 0;
    float   floodDurationMin = 0.0f;
    float   avgAckTimeSec = 0.0f;
    int     chatteringCount = 0;
    int     staleAlarmPercent = 0;
    int     criticalCount = 0;
    int     majorCount = 0;
    int     minorCount = 0;
    int     advisoryCount = 0;
    QStringList top5Frequent;
    QStringList top5Stale;
    float   systemHealthScore = 100.0f;
    QString healthGrade;
};

struct AlarmEvent {
    QString   alarmId;
    quint32   tagId = 0;
    QString   tagName;
    AlarmLimit     limit      = AlarmLimit::Normal;
    AlarmPriority  priority   = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;
    QString   description;
    float     triggerValue = 0.0f;
    float     thresholdValue = 0.0f;
    qint64    triggerTime = 0;
    AlarmState state = AlarmState::ActiveUnacknowledged;
    bool      acknowledged = false;
    qint64    acknowledgeTime = 0;
    QString   acknowledgeUser;
    bool      active = true;
    qint64    returnToNormalTime = 0;
    qint64    returnAckTime = 0;
    float     returnValue = 0.0f;
    bool      shelved = false;
    qint64    shelvedTime = 0;
    QString   shelveReason;
    int       shelveDurationSec = 0;
    QString   shelveUser;
    AlarmSuppressionType suppressionType = AlarmSuppressionType::None;
    QString   suppressionReason;
    QString   suppressionUser;
    qint64    suppressionTime = 0;
    bool      outOfService = false;
    QString   outOfServiceReason;
    QString   outOfServiceUser;
    QString   workOrderNo;
    QString   operatorAnnotation;
    qint64    annotationTime = 0;
    QString   annotationUser;
    QString   area;
    QString   zone;
    AlarmNotificationType notificationType = AlarmNotificationType::Audible;
    qint64    lastNotificationTime = 0;
    int       notificationCount = 0;
    int       repeatCount = 0;
    qint64    firstTriggerTime = 0;

    bool isActive() const {
        return state == AlarmState::ActiveUnacknowledged
            || state == AlarmState::ActiveAcknowledged;
    }
    bool needsAttention() const {
        return state == AlarmState::ActiveUnacknowledged
            || state == AlarmState::ReturnToNormalUnacknowledged;
    }
    bool isSuppressed() const {
        return state == AlarmState::SuppressedByDesign
            || state == AlarmState::OutOfService;
    }
    bool isShelved() const {
        return state == AlarmState::Shelved;
    }
};

struct AlarmFilter {
    QList<AlarmPriority> priorities;
    QList<AlarmClassification> classifications;
    QList<AlarmState> states;
    QStringList areas;
    qint64     fromTime = 0;
    qint64     toTime = 0;
    QString    keyword;
    bool       includeShelved = false;
    bool       includeSuppressed = false;
    bool       includeOutOfService = false;
};

struct SuppressionRule {
    quint32     ruleId = 0;
    quint32     targetTagId;
    quint32     conditionTagId;
    QString     conditionExpr;
    QString     reason;
    bool        enabled = true;
    QString     createdBy;
    QString     approver;
    qint64      createdTime = 0;
};

struct AlarmFloodEvent {
    qint64     startTime = 0;
    qint64     endTime = 0;
    int        alarmCount = 0;
    int        peakRate = 0;
    QStringList topContributors;
    QString    triggerCause;
    QString    analyst;
    qint64     analysisTime = 0;
};
