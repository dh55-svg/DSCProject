#pragma once
#include <qstring.h>
#include "domain/common/DataQuality.h"
#include "domain/common/AlarmLimit.h"
#include "domain/common/AlarmState.h"
#include "domain/alarm/AlarmEvent.h"

struct TagInfo {
    quint32 tagId = 0;
    QString tagName;
    QString description;
    QString unit;
    TagType tagType = TagType::AI;
    float engHigh = 100.0f;
    float engLow  = 0.0f;
    float highHighLimit = 90.0f;
    float highLimit     = 80.0f;
    float lowLimit      = 10.0f;
    float lowLowLimit   = 5.0f;
    float deviationLimit = 10.0f;
    bool  deviationEnabled = false;
    float rateOfChangeLimit = 0.0f;
    int   rateOfChangePeriodMs = 60000;
    bool  rateOfChangeEnabled = false;
    float        deadband     = 1.0f;
    int          onDelayMs    = 3000;
    int          offDelayMs   = 0;
    AlarmPriority priority    = AlarmPriority::Major;
    AlarmClassification classification = AlarmClassification::Process;
    bool  alarmEnabled = true;
    bool  highHighEnabled = true;
    bool  highEnabled     = true;
    bool  lowEnabled      = true;
    bool  lowLowEnabled   = true;
    int   maxRepeatsPerMin = 3;
    int   repeatCount = 0;
    qint64 repeatWindowStart = 0;
    QString area;
    QString zone;
    AlarmNotificationType notificationType = AlarmNotificationType::Audible;
    int   escalationTimeoutSec = 0;
    int modbusDeviceId   = 0;
    int modbusServerAddr = 1;
    int modbusRegAddr    = 0;
    int modbusRegCount   = 1;
    float kp       = 1.0f;
    float ki       = 0.1f;
    float kd       = 0.0f;
    bool  autoMode = true;
    AlarmRationalization rationalization;

    bool isLimitEnabled(AlarmLimit lim) const {
        switch (lim) {
        case AlarmLimit::HighHigh: return highHighEnabled;
        case AlarmLimit::High:     return highEnabled;
        case AlarmLimit::Low:      return lowEnabled;
        case AlarmLimit::LowLow:   return lowLowEnabled;
        case AlarmLimit::Deviation: return deviationEnabled;
        case AlarmLimit::RateOfChange: return rateOfChangeEnabled;
        default: return false;
        }
    }
};
