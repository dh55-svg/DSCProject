#pragma once
#include <qtypes.h>

enum class AlarmLimit : quint8 {
    Normal       = 0,
    LowLow       = 1,
    Low          = 2,
    High         = 3,
    HighHigh     = 4,
    Deviation    = 5,
    RateOfChange = 6
};
