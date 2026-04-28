#pragma once
#include <qtypes.h>

enum class AlarmState : quint8 {
    Normal                          = 0,
    ActiveUnacknowledged            = 1,
    ActiveAcknowledged              = 2,
    ReturnToNormalUnacknowledged    = 3,
    ReturnToNormalAcknowledged      = 4,
    Shelved                         = 5,
    SuppressedByDesign              = 6,
    OutOfService                    = 7
};

enum class AlarmSuppressionType : quint8 {
    None              = 0,
    DesignSuppression = 1,
    OutOfService      = 2,
    Interlock         = 3,
    Override          = 4
};

enum class AlarmPriority : quint8 {
    Advisory  = 0,
    Minor     = 1,
    Major     = 2,
    Critical  = 3
};

enum class AlarmClassification : quint8 {
    Process       = 0,
    Safety        = 1,
    Environmental = 2,
    Quality       = 3,
    Machinery     = 4,
    Electrical    = 5,
    Instrument    = 6
};

enum class AlarmNotificationType : quint8 {
    None       = 0,
    Visual     = 1,
    Audible    = 2,
    Page       = 3,
    Email      = 4,
    Escalation = 5
};

enum class TagType : quint8 {
    AI   = 0,
    AO   = 1,
    DI   = 2,
    DO   = 3,
    PID  = 4
};
