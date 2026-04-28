#pragma once
#include <qmath.h>
#include "domain/tag/TagInfo.h"

struct DeviationChecker {
    static bool exceedsDeviation(float pv, float sp, float deviationLimit) {
        return qAbs(pv - sp) > deviationLimit;
    }
};
