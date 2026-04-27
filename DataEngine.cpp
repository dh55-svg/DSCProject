#include "DataEngine.h"
#include "logger.h"
#include "AuthManager.h"

DataEngine::DataEngine(QObject* parent)
    : QObject(parent)
{
}

DataEngine::~DataEngine()
{
    stop();
}

bool DataEngine::initialize()
{
    if (m_initialized)
    {
        LOG_WARN("DataEngine", "数据引擎已经初始化");
        return true;
    }
    LOG_INFO("DataEngine", "========== 开始初始化数据引擎 ==========");
    m_modbusManager = new ModbusManager(this);
    m_modbusManager->setRingBuffer(&m_ringBuffer);

    // 连接设备状态信号
    connect(m_modbusManager, &ModbusManager::deviceStatusChanged,
        this, &DataEngine::deviceStatusChanged);
    connect(m_modbusManager, &ModbusManager::allDevicesOffline,
        this, &DataEngine::onAllDevicesOffline);

    LOG_INFO("DataEngine", "ModbusManager创建完成");

    m_dataParseThread = new DataParseThread(this);
    m_dataParseThread->setDoubleBuffer(&m_doubleBuffer);
    m_dataParseThread->setRingBuffer(&m_ringBuffer);

    // 连接数据更新信号
    connect(m_dataParseThread, &DataParseThread::dataUpdated,
        this, &DataEngine::onDataUpdated);

    // 连接报警信号
    connect(m_dataParseThread, &DataParseThread::alarmTriggered,
        this, &DataEngine::alarmTriggered);
    connect(m_dataParseThread, &DataParseThread::alarmCleared,
        this, &DataEngine::alarmCleared);

    LOG_INFO("DataEngine", "DataParseThread创建完成");

    m_initialized = true;
    LOG_INFO("DataEngine", "========== 数据引擎初始化完成 ==========");
    return true;
}

bool DataEngine::addModbusDevice(const ModbusDeviceConfig& config)
{
    if (!m_initialized || !m_modbusManager) {
        LOG_ERROR("DataEngine", "数据引擎未初始化，无法添加设备");
        return false;
    }

    return m_modbusManager->addDevice(config);
}

bool DataEngine::loadTagConfig(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        LOG_ERROR("DataEngine", QString("无法打开位号配置文件: %1").arg(jsonPath));
        return false;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        LOG_ERROR("DataEngine", QString("JSON解析失败: %1").arg(error.errorString()));
        return false;
    }
    QJsonArray tags = doc.array();
    QVector<TagInfo> tagList;
    tagList.reserve(tags.size());
    for (const auto& item : tags)
    {
        QJsonObject obj = item.toObject();  // 修复：使用 item.toObject() 而非 doc.object()
        TagInfo tag;
        if (!obj.contains("tagId"))
        {
            LOG_ERROR("DataEngine", "缺少必填字段: tagId");
            continue;
        }
        tag.tagId = static_cast<quint32>(obj["tagId"].toInt());
        tag.tagName = obj["tagName"].toString();
        tag.description = obj["description"].toString();
        tag.unit = obj["unit"].toString();

        if (tag.tagName.isEmpty())
        {
            LOG_ERROR("DataEngine",
                QString("位号ID %1 缺少名称").arg(tag.tagId));
            continue;
        }
        // 检查重复ID
        bool duplicate = false;
        for (const auto& existing : tagList)
        {
            if (existing.tagId == tag.tagId)
            {
                LOG_ERROR("DataEngine",
                    QString("重复的位号ID: %1 (%2)").arg(tag.tagId).arg(tag.tagName));
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        QString typeStr = obj["tagType"].toString("AI");
        if (typeStr == "AI") tag.tagType = TagType::AI;
        else if (typeStr == "AO") tag.tagType = TagType::AO;
        else if (typeStr == "DI") tag.tagType = TagType::DI;
        else if (typeStr == "DO") tag.tagType = TagType::DO;
        else if (typeStr == "PID") tag.tagType = TagType::PID;
        else {
            LOG_WARN("DataEngine",
                QString("未知信号类型 '%1'，使用默认值AI").arg(typeStr));
            tag.tagType = TagType::AI;
        }
        // 量程和报警限值（带合理性检查）
        tag.engHigh = static_cast<float>(obj["engHigh"].toDouble(100.0));
        tag.engLow = static_cast<float>(obj["engLow"].toDouble(0.0));

        // 量程合理性验证
        if (tag.engHigh <= tag.engLow) {
            LOG_WARN("DataEngine",
                QString("位号 %1 量程设置不合理: engHigh=%2 <= engLow=%3，自动修正")
                .arg(tag.tagName).arg(tag.engHigh).arg(tag.engLow));

            float temp = tag.engHigh;
            tag.engHigh = tag.engLow + 100.0f;
            tag.engLow = temp;
        }
        tag.highHighLimit = static_cast<float>(obj["highHighLimit"].toDouble(90.0));
        tag.highLimit = static_cast<float>(obj["highLimit"].toDouble(80.0));
        tag.lowLimit = static_cast<float>(obj["lowLimit"].toDouble(10.0));
        tag.lowLowLimit = static_cast<float>(obj["lowLowLimit"].toDouble(5.0));
        tag.deadband = static_cast<float>(obj["deadband"].toDouble(1.0));
        // 报警限值合理性验证
        if (tag.highHighLimit > tag.engHigh ||
            tag.lowLowLimit < tag.engLow) {
            LOG_WARN("DataEngine",
                QString("位号 %1 报警限值超出量程，自动限制在量程范围内")
                .arg(tag.tagName));

            tag.highHighLimit = qMin(tag.highHighLimit, tag.engHigh);
            tag.lowLowLimit = qMax(tag.lowLowLimit, tag.engLow);
        }
        // 报警限值顺序验证
        if (tag.highHighLimit < tag.highLimit ||
            tag.highLimit < tag.lowLimit ||
            tag.lowLimit < tag.lowLowLimit) {
            LOG_WARN("DataEngine",
                QString("位号 %1 报警限值顺序异常，自动修正")
                .arg(tag.tagName));

            tag.highHighLimit = qMax(tag.highHighLimit, tag.highLimit);
            tag.highLimit = qMax(tag.highLimit, tag.lowLimit);
            tag.lowLimit = qMax(tag.lowLimit, tag.lowLowLimit);
        }
        // Modbus映射验证
        tag.modbusDeviceId   = obj["modbusDeviceId"].toInt(0);
        tag.modbusServerAddr = obj["modbusServerAddr"].toInt(1);
        tag.modbusRegAddr = obj["modbusRegAddr"].toInt(0);
        tag.modbusRegCount = obj["modbusRegCount"].toInt(1);

        // Modbus地址范围验证
        if (tag.modbusServerAddr < 1 || tag.modbusServerAddr > 247) {
            LOG_WARN("DataEngine",
                QString("位号 %1 Modbus从站地址超出范围(1-247): %2")
                .arg(tag.tagName).arg(tag.modbusServerAddr));
            tag.modbusServerAddr = qBound(1, tag.modbusServerAddr, 247);
        }

        if (tag.modbusRegAddr < 0 || tag.modbusRegAddr > 65535) {
            LOG_WARN("DataEngine",
                QString("位号 %1 寄存器地址超出范围: %2")
                .arg(tag.tagName).arg(tag.modbusRegAddr));
            tag.modbusRegAddr = qBound(0, tag.modbusRegAddr, 65535);
        }

        if (tag.modbusRegCount < 1 || tag.modbusRegCount > 125) {
            LOG_WARN("DataEngine",
                QString("位号 %1 寄存器数量超出范围(1-125): %2")
                .arg(tag.tagName).arg(tag.modbusRegCount));
            tag.modbusRegCount = qBound(1, tag.modbusRegCount, 125);
        }

        // PID参数验证
        if (tag.tagType == TagType::PID) {
            tag.kp = static_cast<float>(obj["kp"].toDouble(1.0));
            tag.ki = static_cast<float>(obj["ki"].toDouble(0.1));
            tag.kd = static_cast<float>(obj["kd"].toDouble(0.0));

            // PID参数合理性检查
            if (tag.kp < 0 || tag.ki < 0 || tag.kd < 0) {
                LOG_WARN("DataEngine",
                    QString("位号 %1 PID参数不能为负数，强制为默认值")
                    .arg(tag.tagName));
                tag.kp = qMax(0.0f, tag.kp);
                tag.ki = qMax(0.0f, tag.ki);
                tag.kd = qMax(0.0f, tag.kd);
            }
        }
        // 添加到TagConfigMgr
        TagConfigMgr::instance().addTag(tag);

        tagList.append(tag);
    }
    int count = tagList.size();

    if (count == 0) {
        LOG_ERROR("DataEngine", "没有加载到任何有效位号配置");
        return false;
    }

    LOG_INFO("DataEngine",
        QString("位号配置加载完成，共 %1 个有效位号").arg(count));

    if (m_dataParseThread)
    {
        m_dataParseThread->setTagConfig(tagList);
    }
    return true;
}

void DataEngine::start()
{
    if (m_running) return;
    if (!m_initialized)
    {
        LOG_ERROR("DataEngine", "数据引擎未初始化，无法启动");
        return;
    }
    m_running = true;

    m_dataParseThread->start();
    m_modbusManager->startAll();
    LOG_INFO("DataEngine", "数据引擎已启动（纯内存存储：环形缓冲区+双缓冲）");
}

void DataEngine::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    // 停止Modbus轮询
    if (m_modbusManager) {
        m_modbusManager->stopAll();
    }

    // 停止DataParseThread
    if (m_dataParseThread) {
        m_dataParseThread->stop();
        m_dataParseThread->wait(3000);
    }
    LOG_INFO("DataEngine", "数据引擎已停止");
}

bool DataEngine::isRunning() const
{
    return m_running;
}

void DataEngine::setSetPoint(quint32 tagId, float sp)
{
    if (!AuthManager::instance().canOperate()) {
        LOG_WARN("DataEngine", "权限不足: 需要操作员权限才能修改设定值");
        return;
    }
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0)
    {
        LOG_WARN("DataEngine", QString("无效的位号ID: %1").arg(tagId));
        return;
    }
    sp = qBound(tag.engLow, sp, tag.engHigh);

    // 二次确认（关键操作）
    float currentSp = tag.setPoint;
    if (!AuthManager::instance().confirmCriticalAction("SET_SP",
            QString("位号 %1 设定值 %2 → %3 %4")
                .arg(tag.tagName).arg(currentSp, 0, 'f', 1)
                .arg(sp, 0, 'f', 1).arg(tag.unit))) {
        return;
    }

    // 直接写入 DoubleBuffer
    auto snapshot = m_doubleBuffer.readTag(tagId);
    snapshot.setPoint = sp;
    m_doubleBuffer.write(tagId, snapshot);

    if (m_modbusManager)
    {
        float range = tag.engHigh - tag.engLow;
        float normalized = (sp - tag.engLow) / range;
        normalized = qBound(0.0f, normalized, 1.0f);
        quint16 regValue = static_cast<quint16>(normalized * 65535.0f);
        m_modbusManager->writeRegister(tag.modbusDeviceId,
            tag.modbusServerAddr,
            tag.modbusRegAddr + 1,
            regValue);

        AuthManager::instance().logAction("SET_SP",
            QString("tagId=%1, %2: %3→%4 %5")
                .arg(tagId).arg(tag.tagName)
                .arg(currentSp, 0, 'f', 1).arg(sp, 0, 'f', 1)
                .arg(tag.unit));

        LOG_INFO("DataEngine", QString("下发SP: %1 = %2 %3")
            .arg(tag.tagName).arg(sp).arg(tag.unit));
    }
}

void DataEngine::setOutput(quint32 tagId, float out)
{
    if (!AuthManager::instance().canOperate()) {
        LOG_WARN("DataEngine", "权限不足: 需要操作员权限才能修改输出值");
        return;
    }
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) {
        return;
    }

    out = qBound(0.0f, out, 100.0f);

    float currentOut = tag.outputValue;
    if (!AuthManager::instance().confirmCriticalAction("SET_OUT",
            QString("位号 %1 输出值 %2% → %3%")
                .arg(tag.tagName).arg(currentOut, 0, 'f', 1)
                .arg(out, 0, 'f', 1))) {
        return;
    }

    // 直接写入 DoubleBuffer
    auto snapshot = m_doubleBuffer.readTag(tagId);
    snapshot.outputValue = out;
    m_doubleBuffer.write(tagId, snapshot);

    if (m_modbusManager) {
        float range = tag.engHigh - tag.engLow;
        float normalized = (out - tag.engLow) / range;
        normalized = qBound(0.0f, normalized, 1.0f);
        quint16 regValue = static_cast<quint16>(normalized * 65535.0f);

        m_modbusManager->writeRegister(tag.modbusDeviceId,
            tag.modbusServerAddr,
            tag.modbusRegAddr + 2,
            regValue);

        AuthManager::instance().logAction("SET_OUT",
            QString("tagId=%1, %2: OUT=%3%")
                .arg(tagId).arg(tag.tagName).arg(out, 0, 'f', 1));

        LOG_INFO("DataEngine", QString("下发OUT: %1 = %2%")
            .arg(tag.tagName).arg(out));
    }
}

void DataEngine::setAutoMode(quint32 tagId, bool autoMode)
{
    if (!AuthManager::instance().canOperate()) {
        LOG_WARN("DataEngine", "权限不足: 需要操作员权限才能切换自动/手动模式");
        return;
    }
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) {
        return;
    }

    // 切到手动模式 = 危险操作，ISA-101 要求二次确认
    if (!autoMode) {
        if (!AuthManager::instance().confirmCriticalAction("SET_MANUAL_MODE",
                QString("位号 %1 即将切换到手动模式，操作员可直接控制阀门开度，请确认")
                    .arg(tag.tagName))) {
            return;
        }
    }

    if (m_modbusManager)
    {
        quint16 modeValue = autoMode ? 1 : 0;
        m_modbusManager->writeRegister(tag.modbusDeviceId,
            tag.modbusServerAddr,
            tag.modbusRegAddr + 3,
            modeValue);

        AuthManager::instance().logAction(
            autoMode ? "SET_AUTO_MODE" : "SET_MANUAL_MODE",
            QString("tagId=%1, %2")
                .arg(tagId).arg(tag.tagName));

        LOG_INFO("DataEngine", QString("切换模式: %1 -> %2")
            .arg(tag.tagName).arg(autoMode ? "AUTO" : "MAN"));
    }
}

void DataEngine::onDataUpdated()
{
    // DataParseThread交换双缓冲区后发射信号
    // 转发到UI层
    emit dataUpdated();
}

void DataEngine::onAllDevicesOffline()
{
    // 所有设备离线，将所有位号质量码标记为Bad
    auto allTags = TagConfigMgr::instance().getAllTags();
    for (const auto& tag : allTags) {
        auto snapshot = m_doubleBuffer.readTag(tag.tagId);
        snapshot.quality = DataQuality::Bad;
        m_doubleBuffer.write(tag.tagId, snapshot);
    }
    emit commStatusChanged(false);
    LOG_ERROR("DataEngine", "所有通讯设备离线，所有位号质量码已标记为Bad");
}
