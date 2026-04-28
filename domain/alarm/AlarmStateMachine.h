#pragma once
#include "domain/alarm/AlarmEvent.h"

class AlarmStateMachine {
public:
    static bool canTransition(AlarmState from, AlarmState to) {
        // Valid ISA-18.2 state transitions
        switch (from) {
        case AlarmState::Normal:
            return to == AlarmState::ActiveUnacknowledged || to == AlarmState::OutOfService;
        case AlarmState::ActiveUnacknowledged:
            return to == AlarmState::ActiveAcknowledged || to == AlarmState::ReturnToNormalUnacknowledged
                || to == AlarmState::Shelved || to == AlarmState::SuppressedByDesign;
        case AlarmState::ActiveAcknowledged:
            return to == AlarmState::ReturnToNormalUnacknowledged || to == AlarmState::Shelved
                || to == AlarmState::ActiveUnacknowledged || to == AlarmState::SuppressedByDesign;
        case AlarmState::ReturnToNormalUnacknowledged:
            return to == AlarmState::ReturnToNormalAcknowledged || to == AlarmState::ActiveUnacknowledged;
        case AlarmState::ReturnToNormalAcknowledged:
            return to == AlarmState::Normal || to == AlarmState::ActiveUnacknowledged;
        case AlarmState::Shelved:
            return to == AlarmState::ActiveUnacknowledged || to == AlarmState::ReturnToNormalUnacknowledged;
        case AlarmState::SuppressedByDesign:
            return to == AlarmState::ActiveUnacknowledged || to == AlarmState::ReturnToNormalUnacknowledged
                || to == AlarmState::OutOfService;
        case AlarmState::OutOfService:
            return to == AlarmState::ActiveUnacknowledged || to == AlarmState::ReturnToNormalUnacknowledged;
        default: return false;
        }
    }
};
