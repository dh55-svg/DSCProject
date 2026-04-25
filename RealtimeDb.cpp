#include "RealtimeDb.h"

RealtimeDb& RealtimeDb::instance() {
    static RealtimeDb instance;
    return instance;
}