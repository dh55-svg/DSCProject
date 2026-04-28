#pragma once
#include <memory>
#include "domain/alarm/AlarmEngine.h"
#include "domain/auth/AuthManager.h"
#include "domain/tag/TagManager.h"
#include "pipeline/DataPipeline.h"
#include "infrastructure/fieldbus/IFieldbus.h"
#include "infrastructure/persistence/IAlarmRepo.h"
#include "infrastructure/persistence/IHistoryRepo.h"
#include "infrastructure/persistence/ITagRepo.h"
#include "infrastructure/persistence/IUserRepo.h"
#include "infrastructure/persistence/IOperationRepo.h"
#include "infrastructure/logging/ILogger.h"
#include "infrastructure/config/IConfigRepo.h"

struct AppContext {
    // Domain services
    std::shared_ptr<AlarmEngine> alarmEngine;
    std::shared_ptr<AuthManager> authManager;
    std::shared_ptr<TagManager> tagManager;
    std::shared_ptr<DataPipeline> dataPipeline;

    // Infrastructure (interfaces)
    std::shared_ptr<IFieldbus> fieldbus;
    std::shared_ptr<IAlarmRepo> alarmRepo;
    std::shared_ptr<IHistoryRepo> historyRepo;
    std::shared_ptr<ITagRepo> tagRepo;
    std::shared_ptr<IUserRepo> userRepo;
    std::shared_ptr<IOperationRepo> operationRepo;
    std::shared_ptr<ILogger> logger;
    std::shared_ptr<IConfigRepo> configRepo;
};
