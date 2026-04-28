#pragma once
#include "app/AppContext.h"
#include "app/AppConfig.h"
#include "infrastructure/fieldbus/ModbusImpl.h"
#include "infrastructure/fieldbus/SimulatorImpl.h"
#include "infrastructure/fieldbus/OpcUaImpl.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include "infrastructure/persistence/mysql/AlarmMysqlRepo.h"
#include "infrastructure/persistence/mysql/HistoryMysqlRepo.h"
#include "infrastructure/persistence/mysql/TagMysqlRepo.h"
#include "infrastructure/persistence/mysql/UserMysqlRepo.h"
#include "infrastructure/persistence/mysql/OperationMysqlRepo.h"
#include "infrastructure/persistence/sqlite/AlarmSqliteRepo.h"
#include "infrastructure/logging/SpdlogAdapter.h"
#include "infrastructure/config/JsonConfigRepo.h"

class ApplicationBuilder {
public:
    ApplicationBuilder() {
        m_ctx = std::make_shared<AppContext>();
    }

    ApplicationBuilder& withConfig(const AppConfig& cfg) {
        m_cfg = cfg;
        return *this;
    }

    ApplicationBuilder& withFieldbus() {
        if (m_cfg.fieldbusType == "simulator") {
            m_ctx->fieldbus = std::make_shared<SimulatorImpl>();
        } else if (m_cfg.fieldbusType == "opcua") {
            m_ctx->fieldbus = std::make_shared<OpcUaImpl>();
        } else {
            m_ctx->fieldbus = std::make_shared<ModbusImpl>();
        }
        return *this;
    }

    ApplicationBuilder& withLogger() {
        auto logger = std::make_shared<SpdlogAdapter>();
        logger->setLogDir("./logs");
        m_ctx->logger = logger;
        return *this;
    }

    ApplicationBuilder& withConfigRepo() {
        m_ctx->configRepo = std::make_shared<JsonConfigRepo>();
        return *this;
    }

    ApplicationBuilder& withDatabase() {
        std::shared_ptr<ConnectionPool> pool;
        if (m_cfg.dbBackend == "mysql") {
            pool.reset(ConnectionPool::mysql(m_cfg.mysql.host, m_cfg.mysql.port,
                m_cfg.mysql.database, m_cfg.mysql.user, m_cfg.mysql.password));
        } else {
            pool.reset(ConnectionPool::sqlite(m_cfg.sqlite.path));
        }
        m_ctx->alarmRepo = std::make_shared<AlarmMysqlRepo>(pool);
        m_ctx->historyRepo = std::make_shared<HistoryMysqlRepo>(pool);
        m_ctx->tagRepo = std::make_shared<TagMysqlRepo>(pool);
        m_ctx->userRepo = std::make_shared<UserMysqlRepo>(pool);
        m_ctx->operationRepo = std::make_shared<OperationMysqlRepo>(pool);
        return *this;
    }

    ApplicationBuilder& withDomain() {
        auto* logger = m_ctx->logger.get();
        m_ctx->tagManager = std::make_shared<TagManager>(*m_ctx->tagRepo, logger);
        m_ctx->authManager = std::make_shared<AuthManager>(*m_ctx->userRepo, *m_ctx->operationRepo, logger);
        m_ctx->authManager->initialize();
        m_ctx->alarmEngine = std::make_shared<AlarmEngine>(*m_ctx->alarmRepo, m_ctx->tagManager.get(), logger);
        m_ctx->alarmEngine->initialize();
        return *this;
    }

    ApplicationBuilder& withPipeline() {
        m_ctx->dataPipeline = std::make_shared<DataPipeline>();
        m_ctx->dataPipeline->setTagManager(m_ctx->tagManager.get());
        m_ctx->dataPipeline->setAlarmEngine(m_ctx->alarmEngine.get());
        m_ctx->dataPipeline->setFieldbus(m_ctx->fieldbus.get());
        m_ctx->dataPipeline->setHistoryRepo(m_ctx->historyRepo.get());
        return *this;
    }

    std::shared_ptr<AppContext> build() {
        return m_ctx;
    }

private:
    std::shared_ptr<AppContext> m_ctx;
    AppConfig m_cfg;
};
